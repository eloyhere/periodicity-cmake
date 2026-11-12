// Unit tests for the ChordDiagram component.
//
// The suite renders offscreen into a transparent QImage and delivers pointer
// events with QApplication::sendEvent. It pins the contract the header
// documents: node arcs exist, one ribbon per positive pair, ribbon thickness is
// proportional to the pair weight (the core geometry invariant), the node arcs
// are hit-testable, hovering and clicking reach the same node index, reduced
// motion snaps the ribbon sweep, and an empty matrix paints the shared state.

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QRegion>
#include <QSignalSpy>
#include <QSize>
#include <QSizeF>
#include <QTest>
#include <QWidget>
#include <Qt>

#include <md/components/chord_diagram.h>
#include <md/core/measure_spec.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/motion.h>

#include <cmath>

namespace {

void sendMouse(QWidget& widget, QEvent::Type type, const QPointF& position,
               Qt::MouseButton button, Qt::MouseButtons buttons)
{
    const QPoint global = widget.mapToGlobal(position.toPoint());
    QMouseEvent event(type, position, QPointF(global), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

void clickAt(QWidget& widget, const QPointF& position)
{
    sendMouse(widget, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
}

QImage renderWidget(QWidget& widget)
{
    const qreal ratio = widget.devicePixelRatioF();
    QImage image(widget.size() * ratio, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    widget.render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();

    return image;
}

QColor pixelAt(const QImage& image, const QPointF& logical)
{
    const qreal ratio = image.devicePixelRatio();
    const int x = qBound(0, qRound(logical.x() * ratio), image.width() - 1);
    const int y = qBound(0, qRound(logical.y() * ratio), image.height() - 1);
    return image.pixelColor(x, y);
}

int pixelsDifferingFrom(const QImage& image, const QColor& background)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            const int delta = qAbs(pixel.red() - background.red())
                + qAbs(pixel.green() - background.green())
                + qAbs(pixel.blue() - background.blue())
                + qAbs(pixel.alpha() - background.alpha());
            if (delta > 24)
                ++count;
        }
    }
    return count;
}

} // namespace

class TestChordDiagram : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructionAndMeasurement();
    void matrixNormalization();
    void ribbonWidthTracksValue();
    void nodeHitTest();
    void hoverHighlightsNode();
    void clickActivatesNode();
    void paintedPixels();
    void emptyStatePaints();
    void reducedMotionSnaps();
    void revealReachesOne();
};

void TestChordDiagram::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestChordDiagram::constructionAndMeasurement()
{
    md::ChordDiagram chart;
    QCOMPARE(chart.nodeCount(), 0);
    QCOMPARE(chart.ribbonCount(), 0);
    QVERIFY(chart.names().isEmpty());
    QVERIFY(chart.isSymmetric());

    chart.measure(md::MeasureSpec::exactly(300.0), md::MeasureSpec::exactly(280.0));
    QCOMPARE(chart.measuredSize(), QSizeF(300.0, 280.0));

    chart.resize(380, 360);
    QVERIFY(chart.plotRect().isValid());
    QCOMPARE(chart.nodeRadius(), 0.0);
    QCOMPARE(chart.nodeAt(QPointF(190.0, 180.0)), -1);
}

void TestChordDiagram::matrixNormalization()
{
    md::ChordDiagram chart;
    chart.setMatrix({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")},
                    {{0.0, 2.0}, {2.0, 0.0, 1.0}});

    QCOMPARE(chart.nodeCount(), 3);
    QCOMPARE(chart.names().size(), 3);
    QCOMPARE(chart.value(1, 0), 2.0);
    QVERIFY(std::isnan(chart.value(9, 9)));
    QCOMPARE(chart.ribbonCount(), 2); // A-B and B-C; A-C is missing

    // max(v(i,j), v(j,i)) is the undirected strength, so a one-sided matrix
    // still paints its ribbons.
    chart.setMatrix({QStringLiteral("A"), QStringLiteral("B")},
                    {{0.0, 0.0}, {3.0, 0.0}});
    QCOMPARE(chart.ribbonCount(), 1);
    QVERIFY(chart.ribbonWidth(0, 1) > 0.0);
    QCOMPARE(chart.ribbonWidth(0, 1), chart.ribbonWidth(1, 0));

    chart.setUnit(QStringLiteral("GB"));
    QCOMPARE(chart.unit(), QStringLiteral("GB"));
}

void TestChordDiagram::ribbonWidthTracksValue()
{
    md::ChordDiagram chart;
    chart.setMatrix({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")},
                    {{0.0, 2.0, 6.0}, {2.0, 0.0, 1.0}, {6.0, 1.0, 0.0}});
    chart.resize(420, 400);

    QCOMPARE(chart.ribbonCount(), 3);
    QVERIFY(chart.nodeRadius() > 30.0);

    const qreal small = chart.ribbonWidth(0, 1);
    const qreal large = chart.ribbonWidth(0, 2);
    const qreal medium = chart.ribbonWidth(1, 2);

    QVERIFY(small > 0.0);
    QVERIFY(large > small);
    QVERIFY(medium > 0.0);
    QVERIFY2(qAbs(large / small - 3.0) < 1e-6,
             "a pair with three times the weight has three times the width");
    QVERIFY2(qAbs(medium / small - 0.5) < 1e-6,
             "the width is a single global scale times the value");
    QCOMPARE(chart.ribbonWidth(0, 1), chart.ribbonWidth(1, 0));
    QCOMPARE(chart.ribbonWidth(0, 0), 0.0);
}

void TestChordDiagram::nodeHitTest()
{
    md::ChordDiagram chart;
    chart.setMatrix({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
                     QStringLiteral("D")},
                    {{0.0, 3.0, 1.0, 0.0},
                     {3.0, 0.0, 2.0, 1.0},
                     {1.0, 2.0, 0.0, 4.0},
                     {0.0, 1.0, 4.0, 0.0}});
    chart.resize(400, 380);

    const QPointF centre = chart.plotRect().center();
    const qreal radius = chart.nodeRadius();
    QVERIFY(radius > 20.0);

    for (int node = 0; node < 4; ++node) {
        const QPointF point = chart.nodeCenter(node);
        QVERIFY2(std::hypot(point.x() - centre.x(), point.y() - centre.y()) > radius * 0.8,
                 "a node centre sits on the node circle");
        QCOMPARE(chart.nodeAt(point), node);
    }

    QCOMPARE(chart.nodeAt(centre), -1);
    QCOMPARE(chart.nodeAt(QPointF(2.0, 2.0)), -1);
    QCOMPARE(chart.nodeAt(QPointF(chart.plotRect().right() + 40.0, centre.y())), -1);
}

void TestChordDiagram::hoverHighlightsNode()
{
    md::ChordDiagram chart;
    chart.setMatrix({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")},
                    {{0.0, 2.0, 5.0}, {2.0, 0.0, 1.0}, {5.0, 1.0, 0.0}});
    chart.resize(360, 340);

    const QImage before = renderWidget(chart);
    sendMouse(chart, QEvent::MouseMove, chart.nodeCenter(0), Qt::NoButton, Qt::NoButton);

    QCOMPARE(chart.hoveredNode(), 0);
    const QImage after = renderWidget(chart);
    QVERIFY2(before != after, "highlighting a node must repaint its ribbons");

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&chart, &leave);
    QCOMPARE(chart.hoveredNode(), -1);
}

void TestChordDiagram::clickActivatesNode()
{
    md::ChordDiagram chart;
    chart.setMatrix({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")},
                    {{0.0, 2.0, 5.0}, {2.0, 0.0, 1.0}, {5.0, 1.0, 0.0}});
    chart.resize(360, 340);

    QSignalSpy spy(&chart, &md::ChordDiagram::nodeActivated);

    clickAt(chart, chart.nodeCenter(2));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 2);
    QCOMPARE(chart.selectedNode(), 2);

    // An empty click clears the selection and emits nothing.
    clickAt(chart, chart.plotRect().center());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(chart.selectedNode(), -1);
}

void TestChordDiagram::paintedPixels()
{
    md::ChordDiagram chart;
    chart.setMatrix({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")},
                    {{0.0, 2.0, 6.0}, {2.0, 0.0, 1.0}, {6.0, 1.0, 0.0}});
    chart.resize(400, 380);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());
    QVERIFY(pixelsDifferingFrom(image, Qt::transparent) > 500);

    // The node ring is painted over the ribbons, so its middle pixel is the
    // node's palette colour.
    const QVector<QColor> palette = chart.chartPalette();
    QVERIFY(!palette.isEmpty());
    QCOMPARE(pixelAt(image, chart.nodeCenter(0)).rgb(), palette.at(0).rgb());
}

void TestChordDiagram::emptyStatePaints()
{
    md::ChordDiagram chart;
    chart.resize(320, 260);

    QCOMPARE(chart.nodeCount(), 0);
    QCOMPARE(chart.ribbonCount(), 0);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());
    QVERIFY2(pixelsDifferingFrom(image, Qt::transparent) > 0,
             "the empty state must paint its label");
}

void TestChordDiagram::reducedMotionSnaps()
{
    md::ChordDiagram chart;

    md::MotionScheme motion;
    motion.reducedMotion = true;
    md::ThemeScope* scope = md::ThemeScope::install(
        &chart, md::Theme::baselineLight().withMotion(motion));
    QVERIFY(scope != nullptr);

    chart.setMatrix({QStringLiteral("A"), QStringLiteral("B")},
                    {{0.0, 4.0}, {4.0, 0.0}});
    chart.resize(300, 300);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());

    QVERIFY(pixelsDifferingFrom(renderWidget(chart), Qt::transparent) > 100);
}

void TestChordDiagram::revealReachesOne()
{
    md::ChordDiagram chart;
    chart.setMatrix({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")},
                    {{0.0, 2.0, 5.0}, {2.0, 0.0, 1.0}, {5.0, 1.0, 0.0}});
    chart.resize(340, 340);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();

    QTRY_VERIFY_WITH_TIMEOUT(chart.isRevealing(), 2000);
    QVERIFY2(chart.revealProgress() < 1.0, "the default theme sweeps the ribbons out");
    QTRY_VERIFY_WITH_TIMEOUT(!chart.isRevealing(), 5000);
    QCOMPARE(chart.revealProgress(), 1.0);
}

QTEST_MAIN(TestChordDiagram)
#include "tst_chord_diagram.moc"
