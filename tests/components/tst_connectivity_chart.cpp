// Unit tests for the ConnectivityChart component.
//
// The widget is exercised offscreen like the other chart suites: the application
// theme is pinned to baseline light and the chart is rendered into a transparent
// QImage, so a pixel assertion sees exactly what the component painted. Pointer
// input is delivered with QApplication::sendEvent.
//
// The tests pin the contract the header documents: the matrix is normalized to a
// square with padded names, the diverging scale is neutral at zero and reaches
// the primary/error roles at the ends, cell geometry and hit-testing agree, the
// hover readout carries the indices and the value, reduced motion snaps the
// reveal synchronously, and an empty matrix paints the shared empty state.

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QSize>
#include <QSizeF>
#include <QTest>
#include <QWidget>
#include <Qt>

#include <md/components/connectivity_chart.h>
#include <md/core/measure_spec.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/motion.h>

#include <cmath>

namespace {

void sendMouseMove(QWidget& widget, const QPointF& position)
{
    const QPoint global = widget.mapToGlobal(position.toPoint());
    QMouseEvent event(QEvent::MouseMove, position, QPointF(global), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
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

class TestConnectivityChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructionAndMeasurement();
    void squareNormalizationAndSymmetry();
    void cellGeometryAndHitTest();
    void divergingScalePaints();
    void hoverReadout();
    void emptyStatePaints();
    void reducedMotionSnaps();
    void revealReachesOne();
};

void TestConnectivityChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestConnectivityChart::constructionAndMeasurement()
{
    md::ConnectivityChart chart;
    QCOMPARE(chart.size(), 0);
    QVERIFY(!chart.isSquare());
    QVERIFY(chart.names().isEmpty());
    QVERIFY(chart.matrix().isEmpty());
    QVERIFY(chart.isSymmetric());

    chart.measure(md::MeasureSpec::exactly(320.0), md::MeasureSpec::exactly(240.0));
    QCOMPARE(chart.measuredSize(), QSizeF(320.0, 240.0));

    chart.resize(360, 300);
    const QRectF plot = chart.plotRect();
    QVERIFY2(plot.isValid() && plot.width() > 40.0 && plot.height() > 40.0,
             "a resized chart must expose a usable plot rectangle");
}

void TestConnectivityChart::squareNormalizationAndSymmetry()
{
    md::ConnectivityChart chart;
    chart.setMatrix({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")},
                    {{0.0, 2.0}, {2.0, 0.0, -1.0}, {1.0}});

    QCOMPARE(chart.size(), 3);
    QVERIFY(chart.isSquare());
    QCOMPARE(chart.names().size(), 3);
    QCOMPARE(chart.value(0, 1), 2.0);
    QCOMPARE(chart.value(1, 1), 0.0);
    QVERIFY2(std::isnan(chart.value(0, 2)), "a missing cell is unobserved, not zero");
    QVERIFY2(std::isnan(chart.value(9, 9)), "an out-of-range index reports NaN");

    // The padded matrix is [[0,2,NaN],[2,0,-1],[1,NaN,0]], so it is not
    // symmetric: (0,2) is NaN while (2,0) is 1.
    QVERIFY(!chart.isSymmetric());

    chart.setMatrix({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")},
                    {{0.0, 2.0, -1.0}, {2.0, 0.0, 0.5}, {-1.0, 0.5, 0.0}});
    QVERIFY(chart.isSymmetric());
    QVERIFY(chart.isSymmetric(1e-6));
    QVERIFY(chart.isSymmetric(1e-12));

    chart.setRange(-2.0, 6.0);
    QVERIFY(chart.hasExplicitRange());
    QCOMPARE(chart.rangeMinimum(), -2.0);
    QCOMPARE(chart.rangeMaximum(), 6.0);

    chart.clearRange();
    QVERIFY(!chart.hasExplicitRange());
    QCOMPARE(chart.rangeMinimum(), -1.0);
    QCOMPARE(chart.rangeMaximum(), 2.0);

    chart.setUnit(QStringLiteral("nA"));
    QCOMPARE(chart.unit(), QStringLiteral("nA"));
}

void TestConnectivityChart::cellGeometryAndHitTest()
{
    md::ConnectivityChart chart;
    chart.setMatrix({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
                     QStringLiteral("D")},
                    {{0.0, 1.0, 2.0, 3.0},
                     {1.0, 0.0, 4.0, 5.0},
                     {2.0, 4.0, 0.0, 6.0},
                     {3.0, 5.0, 6.0, 0.0}});
    chart.resize(360, 320);

    const QRectF first = chart.cellRect(0, 0);
    const QRectF second = chart.cellRect(1, 2);
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());
    QVERIFY2(qAbs(first.width() - first.height()) < 0.5, "cells must stay square");
    QVERIFY2(qAbs(first.width() - second.width()) < 0.5, "every cell has one size");

    QCOMPARE(chart.cellAt(first.center()), QPoint(0, 0));
    QCOMPARE(chart.cellAt(second.center()), QPoint(2, 1));
    QCOMPARE(chart.cellAt(QPointF(1.0, 1.0)), QPoint(-1, -1));
    QCOMPARE(chart.cellAt(QPointF(359.0, 319.0)), QPoint(-1, -1));

    // Outside the matrix but inside the colour-bar gutter is not a cell.
    const QRectF plot = chart.plotRect();
    QCOMPARE(chart.cellAt(QPointF(plot.right() - 2.0, plot.center().y())), QPoint(-1, -1));
}

void TestConnectivityChart::divergingScalePaints()
{
    md::ConnectivityChart chart;
    chart.setMatrix({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")},
                    {{-1.0, 0.0, 1.0}, {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}});
    chart.setRange(-1.0, 1.0);
    chart.resize(360, 300);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const md::ColorScheme colours = md::Theme::baselineLight().colours();
    QCOMPARE(pixelAt(image, chart.cellRect(0, 0).center()).rgb(), colours.primary.rgb());
    QCOMPARE(pixelAt(image, chart.cellRect(0, 1).center()).rgb(),
             colours.surfaceContainerHighest.rgb());
    QCOMPARE(pixelAt(image, chart.cellRect(0, 2).center()).rgb(), colours.error.rgb());

    QVERIFY(pixelsDifferingFrom(image, Qt::transparent) > 500);
}

void TestConnectivityChart::hoverReadout()
{
    md::ConnectivityChart chart;
    chart.setMatrix({QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral("Gamma")},
                    {{0.0, 2.0, -3.0}, {2.0, 0.0, 7.25}, {-3.0, 7.25, 0.0}});
    chart.resize(360, 300);

    const QPointF target = chart.cellRect(2, 1).center();
    const QImage before = renderWidget(chart);

    sendMouseMove(chart, target);
    QCOMPARE(chart.hoveredCell(), QPoint(1, 2));
    QCOMPARE(chart.hoveredValue(), 7.25);
    QVERIFY(!chart.hoverLabel().isEmpty());
    QVERIFY2(chart.hoverLabel().contains(QStringLiteral("7.25")),
             "the readout carries the value");
    QVERIFY2(chart.hoverLabel().contains(QStringLiteral("(2, 1)")),
             "the readout carries the row and column");

    const QImage after = renderWidget(chart);
    QVERIFY2(before != after, "hovering must paint an outline and a readout");

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&chart, &leave);
    QCOMPARE(chart.hoveredCell(), QPoint(-1, -1));
}

void TestConnectivityChart::emptyStatePaints()
{
    md::ConnectivityChart chart;
    chart.resize(320, 220);

    QCOMPARE(chart.size(), 0);
    QCOMPARE(chart.cellAt(QPointF(160.0, 110.0)), QPoint(-1, -1));

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());
    QVERIFY2(pixelsDifferingFrom(image, Qt::transparent) > 0,
             "the empty state must paint its label");
}

void TestConnectivityChart::reducedMotionSnaps()
{
    md::ConnectivityChart chart;

    md::MotionScheme motion;
    motion.reducedMotion = true;
    md::ThemeScope* scope = md::ThemeScope::install(
        &chart, md::Theme::baselineLight().withMotion(motion));
    QVERIFY(scope != nullptr);

    chart.setMatrix({QStringLiteral("A"), QStringLiteral("B")},
                    {{0.0, 1.0}, {1.0, 0.0}});
    chart.resize(280, 260);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());

    const QImage image = renderWidget(chart);
    QCOMPARE(pixelAt(image, chart.cellRect(0, 1).center()).rgb(),
             md::Theme::baselineLight().colours().error.rgb());
}

void TestConnectivityChart::revealReachesOne()
{
    md::ConnectivityChart chart;
    chart.setMatrix({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")},
                    {{0.0, 1.0, 2.0}, {1.0, 0.0, 3.0}, {2.0, 3.0, 0.0}});
    chart.resize(300, 280);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();

    QTRY_VERIFY_WITH_TIMEOUT(chart.isRevealing(), 2000);
    QVERIFY2(chart.revealProgress() < 1.0, "the default theme animates the rows in");
    QTRY_VERIFY_WITH_TIMEOUT(!chart.isRevealing(), 5000);
    QCOMPARE(chart.revealProgress(), 1.0);
}

QTEST_MAIN(TestConnectivityChart)
#include "tst_connectivity_chart.moc"
