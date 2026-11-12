// Unit tests for the EpicycleChart component.
//
// The suite pins the Fourier contract rather than a fixed picture: a sampled
// unit circle must decompose to a radius sum of one, the trace must stay inside
// the plot, the target hit test must return the sample that was painted, and the
// reveal must actually run (and snap under reduced motion). Rendering happens
// offscreen into a transparent QImage, like the other chart suites.

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QRegion>
#include <QSizeF>
#include <QTest>
#include <QWidget>
#include <Qt>

#include <md/components/epicycle_chart.h>
#include <md/core/measure_spec.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/motion.h>

#include <cmath>
#include <vector>

namespace {

constexpr qreal kPi = 3.14159265358979323846;

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

std::vector<QPointF> circleTarget(int count)
{
    std::vector<QPointF> points;
    points.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        const qreal angle = 2.0 * kPi * index / count;
        points.push_back(QPointF(std::cos(angle), std::sin(angle)));
    }
    return points;
}

} // namespace

class TestEpicycleChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructionAndMeasurement();
    void circleDecomposesToUnitRadius();
    void traceStaysInsidePlot();
    void targetPointHitTest();
    void hoverMarksSample();
    void speedAndCircleSwitch();
    void paintedPixels();
    void emptyStatePaints();
    void reducedMotionSnaps();
    void revealReachesOne();
};

void TestEpicycleChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestEpicycleChart::constructionAndMeasurement()
{
    md::EpicycleChart chart;
    QCOMPARE(chart.sampleCount(), 0);
    QVERIFY(chart.target().empty());
    QCOMPARE(chart.harmonics(), 8);
    QVERIFY(chart.showsCircles());
    QCOMPARE(chart.speed(), 1.0);
    QCOMPARE(chart.epicycleRadiusSum(), 0.0);
    QCOMPARE(chart.targetPointAt(QPointF(100.0, 100.0)), -1);

    chart.measure(md::MeasureSpec::exactly(320.0), md::MeasureSpec::exactly(300.0));
    QCOMPARE(chart.measuredSize(), QSizeF(320.0, 300.0));

    chart.resize(360, 340);
    QVERIFY(chart.plotRect().isValid());
}

void TestEpicycleChart::circleDecomposesToUnitRadius()
{
    md::EpicycleChart chart;
    chart.setHarmonics(4);
    chart.setTarget(circleTarget(64));
    chart.resize(360, 340);

    QCOMPARE(chart.sampleCount(), 64);
    QVERIFY(!chart.epicycleRadii().isEmpty());

    const qreal sum = chart.epicycleRadiusSum();
    QVERIFY2(qAbs(sum - 1.0) < 1e-6,
             "the unit circle's first harmonic carries the whole radius");

    const QVector<qreal> radii = chart.epicycleRadii();
    qreal stored = 0.0;
    for (qreal radius : radii)
        stored += radius;
    QCOMPARE(stored, sum);

    // A single vector of radius one produces a trace on the unit circle. Its
    // pixel distance from the projection centre must equal the grid radius
    // scaled by the value, i.e. the full half-extent of the drawing.
    const QPolygonF trace = chart.tracePath();
    QVERIFY(trace.size() > 40);

    const QRectF plot = chart.plotRect();
    qreal maximumDistance = 0.0;
    for (const QPointF& point : trace) {
        maximumDistance = std::max(maximumDistance,
                                   std::hypot(point.x() - plot.center().x(),
                                              point.y() - plot.center().y()));
    }
    QVERIFY(maximumDistance > plot.height() * 0.2);
    QVERIFY(maximumDistance <= std::min(plot.width(), plot.height()) / 2.0 + 1.0);
}

void TestEpicycleChart::traceStaysInsidePlot()
{
    md::EpicycleChart chart;
    chart.setTarget(circleTarget(48));
    chart.setHarmonics(8);
    chart.resize(320, 300);

    const QPolygonF trace = chart.tracePath();
    QVERIFY(trace.size() > 100);

    const QRectF bounds = chart.plotRect().adjusted(-1.0, -1.0, 1.0, 1.0);
    for (const QPointF& point : trace)
        QVERIFY2(bounds.contains(point), "the projected trace must stay in the plot");
}

void TestEpicycleChart::targetPointHitTest()
{
    md::EpicycleChart chart;
    chart.setTarget(circleTarget(32));
    chart.resize(340, 320);

    const QPointF painted = chart.targetPoint(10);
    QVERIFY(!painted.isNull());
    QCOMPARE(chart.targetPointAt(painted), 10);

    QCOMPARE(chart.targetPointAt(QPointF(2.0, 2.0)), -1);
    QCOMPARE(chart.targetPointAt(QPointF(chart.plotRect().right() + 20.0,
                                         chart.plotRect().bottom() + 20.0)),
             -1);
    QCOMPARE(chart.targetPoint(999), QPointF());
}

void TestEpicycleChart::hoverMarksSample()
{
    md::EpicycleChart chart;
    chart.setTarget(circleTarget(32));
    chart.resize(340, 320);

    const QImage before = renderWidget(chart);
    sendMouseMove(chart, chart.targetPoint(12));

    QCOMPARE(chart.hoveredTargetPoint(), 12);
    const QImage after = renderWidget(chart);
    QVERIFY2(before != after, "hovering a sample must paint a marker and readout");

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&chart, &leave);
    QCOMPARE(chart.hoveredTargetPoint(), -1);
}

void TestEpicycleChart::speedAndCircleSwitch()
{
    md::EpicycleChart chart;
    chart.setTarget(circleTarget(48));
    chart.resize(340, 320);

    chart.setSpeed(2.5);
    QCOMPARE(chart.speed(), 2.5);

    const QImage withCircles = renderWidget(chart);
    chart.setShowCircles(false);
    QVERIFY(!chart.showsCircles());
    const QImage withoutCircles = renderWidget(chart);
    QVERIFY2(withCircles != withoutCircles, "the circle switch must change painted pixels");

    chart.setHarmonics(4);
    QCOMPARE(chart.harmonics(), 4);
}

void TestEpicycleChart::paintedPixels()
{
    md::EpicycleChart chart;
    chart.setTarget(circleTarget(64));
    chart.resize(360, 340);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());
    QVERIFY(pixelsDifferingFrom(image, Qt::transparent) > 500);
}

void TestEpicycleChart::emptyStatePaints()
{
    md::EpicycleChart chart;
    chart.resize(320, 280);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());
    QVERIFY2(pixelsDifferingFrom(image, Qt::transparent) > 0,
             "the empty state must paint its label");
    QCOMPARE(chart.tracePath().size(), 0);
}

void TestEpicycleChart::reducedMotionSnaps()
{
    md::EpicycleChart chart;

    md::MotionScheme motion;
    motion.reducedMotion = true;
    md::ThemeScope* scope = md::ThemeScope::install(
        &chart, md::Theme::baselineLight().withMotion(motion));
    QVERIFY(scope != nullptr);

    chart.setTarget(circleTarget(32));
    chart.resize(300, 300);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());
    QVERIFY(pixelsDifferingFrom(renderWidget(chart), Qt::transparent) > 100);
}

void TestEpicycleChart::revealReachesOne()
{
    md::EpicycleChart chart;
    chart.setTarget(circleTarget(48));
    chart.resize(320, 320);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();

    QTRY_VERIFY_WITH_TIMEOUT(chart.isRevealing(), 2000);
    QVERIFY2(chart.revealProgress() < 1.0, "the default theme traces the curve on");
    QTRY_VERIFY_WITH_TIMEOUT(!chart.isRevealing(), 5000);
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(chart.tracePath().size() > 100);
}

QTEST_MAIN(TestEpicycleChart)
#include "tst_epicycle_chart.moc"
