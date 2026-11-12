// Tests for the PolarPlotChart component.
//
// The pattern is rendered offscreen at a fixed geometry. The cases pin the
// angle convention (0° at the top, clockwise), the linear and dB radial axes,
// the mirror option, the complex-response convenience overload, hit-testing,
// the empty state, and both reveal branches.

#include <md/components/polar_plot_chart.h>
#include <md/core/measure_spec.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/motion.h>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QTest>
#include <QWidget>

#include <cmath>
#include <complex>
#include <vector>

namespace {

constexpr int kChartWidth = 560;
constexpr int kChartHeight = 560;

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

int colourDistance(const QColor& left, const QColor& right)
{
    return qAbs(left.red() - right.red()) + qAbs(left.green() - right.green())
        + qAbs(left.blue() - right.blue()) + qAbs(left.alpha() - right.alpha());
}

bool hasColourNear(const QImage& image, const QPointF& logical, const QColor& colour,
                   int radius = 3)
{
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (colourDistance(pixelAt(image, logical + QPointF(dx, dy)), colour) <= 60)
                return true;
        }
    }
    return false;
}

int pixelsDifferingFrom(const QImage& image, const QColor& background)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (colourDistance(image.pixelColor(x, y), background) > 24)
                ++count;
        }
    }
    return count;
}

void prepare(QWidget& widget)
{
    widget.resize(kChartWidth, kChartHeight);
}

void setDefaultPattern(md::PolarPlotChart& chart)
{
    chart.setAngles({0.0, 45.0, 90.0, 135.0, 180.0, 225.0, 270.0, 315.0},
                    {1.0, 0.8, 0.6, 0.4, 0.2, 0.4, 0.6, 0.8});
}

} // namespace

class TestPolarPlotChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructionAndMeasurement();
    void anglesAreNormalisedAndSorted();
    void linearRadialTicks();
    void decibelRadialTicks();
    void angleConventionAndGeometry();
    void paintedPatternUsesSeriesColour();
    void mirrorChangesThePaintedPixels();
    void complexResponseBuildsFullCircle();
    void hitTestMapsPixels();
    void emptyStatePaintsLabel();
    void revealReachesOne();
    void reducedMotionSnaps();
};

void TestPolarPlotChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestPolarPlotChart::constructionAndMeasurement()
{
    md::PolarPlotChart chart;
    setDefaultPattern(chart);
    prepare(chart);

    QCOMPARE(chart.pointCount(), 8);
    QCOMPARE(chart.radialScale(), md::PolarPlotChart::RadialScale::Linear);
    QCOMPARE(chart.radialFloorDb(), -40.0);
    QVERIFY(!chart.isMirrored());
    QCOMPARE(chart.revealProgress(), 1.0);
    QCOMPARE(chart.radialMaximum(), 1.0);
    QVERIFY(chart.radialAxisTitle().contains(QStringLiteral("Magnitude")));
    QVERIFY(chart.angleAxisTitle().contains(QStringLiteral("°")));

    chart.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());
    QVERIFY(chart.measuredSize().width() > 0.0);
    QVERIFY(chart.measuredSize().height() > 0.0);

    // The dB floor is clamped, and the mirror flag is a plain switch.
    chart.setRadialFloorDb(-500.0);
    QCOMPARE(chart.radialFloorDb(), -200.0);
    chart.setMirror(true);
    QVERIFY(chart.isMirrored());

    QCOMPARE(chart.angleAt(-1), 0.0);
    QCOMPARE(chart.valueAt(100), 0.0);
    QCOMPARE(chart.radialFraction(-1), 0.0);
}

void TestPolarPlotChart::anglesAreNormalisedAndSorted()
{
    md::PolarPlotChart chart;
    chart.setAngles({270.0, -90.0, 0.0, 90.0, 450.0}, {0.5, -0.5, 1.0, 0.25, 0.0});

    // -90 normalizes to 270, 450 to 90; the sequence is sorted by angle and a
    // negative value becomes its magnitude.
    QCOMPARE(chart.pointCount(), 5);
    QCOMPARE(chart.angleAt(0), 0.0);
    QCOMPARE(chart.angleAt(1), 90.0);
    QCOMPARE(chart.angleAt(2), 90.0);
    QCOMPARE(chart.angleAt(3), 270.0);
    QCOMPARE(chart.angleAt(4), 270.0);
    QCOMPARE(chart.valueAt(0), 1.0);
    QVERIFY(std::abs(chart.valueAt(1) - 0.25) < 1.0e-12);
    QVERIFY(std::abs(chart.valueAt(2) - 0.0) < 1.0e-12);
    QVERIFY(std::abs(chart.valueAt(3) - 0.5) < 1.0e-12);
    QVERIFY(std::abs(chart.valueAt(4) - 0.5) < 1.0e-12);

    // A non-finite pair is dropped.
    chart.setAngles({0.0, std::nan("")}, {1.0, 2.0});
    QCOMPARE(chart.pointCount(), 1);
}

void TestPolarPlotChart::linearRadialTicks()
{
    md::PolarPlotChart chart;
    setDefaultPattern(chart);
    prepare(chart);

    const QVector<double> ticks = chart.radialTicks();
    QVERIFY(ticks.size() >= 2);
    QCOMPARE(ticks.first(), 0.0);
    QCOMPARE(ticks.last(), 1.0);

    const QStringList labels = chart.radialTickLabels();
    QCOMPARE(labels.size(), ticks.size());
    QVERIFY(labels.contains(QStringLiteral("0.0")));
    QVERIFY(labels.contains(QStringLiteral("1.0")));
}

void TestPolarPlotChart::decibelRadialTicks()
{
    md::PolarPlotChart chart;
    setDefaultPattern(chart);
    chart.setRadialScale(md::PolarPlotChart::RadialScale::Db);
    prepare(chart);

    QCOMPARE(chart.radialScale(), md::PolarPlotChart::RadialScale::Db);
    QVERIFY(chart.radialAxisTitle().contains(QStringLiteral("dB")));

    const QVector<double> ticks = chart.radialTicks();
    QCOMPARE(ticks.first(), -40.0);
    QCOMPARE(ticks.last(), 0.0);
    for (int i = 1; i < ticks.size(); ++i)
        QVERIFY(std::abs((ticks.at(i) - ticks.at(i - 1)) - 10.0) < 1.0e-9);

    const QStringList labels = chart.radialTickLabels();
    QVERIFY(labels.contains(QStringLiteral("-40")));
    QVERIFY(labels.contains(QStringLiteral("0")));

    // A sample 20 dB below the maximum sits at half the radius.
    QCOMPARE(chart.valueAt(0), 1.0);
    chart.setAngles({0.0, 90.0}, {1.0, 0.1});
    QVERIFY(std::abs(chart.radialFraction(1) - 0.5) < 1.0e-9);
}

void TestPolarPlotChart::angleConventionAndGeometry()
{
    md::PolarPlotChart chart;
    setDefaultPattern(chart);
    prepare(chart);

    const QRectF area = chart.polarRect();
    QVERIFY(!area.isEmpty());
    QVERIFY2(std::abs(area.width() - area.height()) < 0.5,
             "the polar area must be a square");
    QVERIFY2(area.center() == chart.plotRect().center(),
             "the polar area must be centred in the plot rectangle");

    const QPointF middle = chart.centre();
    const qreal radius = chart.radius();
    QVERIFY(radius > 0.0);

    // 0° is at twelve o'clock, 90° at three o'clock, 180° at six o'clock.
    const QPointF top = chart.pointForIndex(0);
    QVERIFY(std::abs(top.x() - middle.x()) < 0.5);
    QVERIFY(std::abs(top.y() - (middle.y() - radius)) < 0.5);

    const QPointF right = chart.pointForIndex(2);
    QVERIFY(std::abs(right.x() - (middle.x() + radius * 0.6)) < 0.5);
    QVERIFY(std::abs(right.y() - middle.y()) < 0.5);

    const QPointF bottom = chart.pointForIndex(4);
    QVERIFY(std::abs(bottom.y() - (middle.y() + radius * 0.2)) < 0.5);
}

void TestPolarPlotChart::paintedPatternUsesSeriesColour()
{
    md::PolarPlotChart chart;
    setDefaultPattern(chart);
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QColor colour = chart.seriesColour(0);
    QVERIFY(colour.isValid());
    for (int index = 0; index < chart.pointCount(); ++index)
        QVERIFY(hasColourNear(image, chart.pointForIndex(index), colour));

    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    QVERIFY2(pixelsDifferingFrom(image, background) > 100,
             "the polar grid and pattern must paint visible pixels");
}

void TestPolarPlotChart::mirrorChangesThePaintedPixels()
{
    md::PolarPlotChart chart;
    // A deliberately asymmetric half pattern, where the mirror produces a
    // second, different lobe.
    chart.setAngles({0.0, 30.0, 60.0, 90.0, 120.0, 150.0, 180.0},
                    {1.0, 0.9, 0.7, 0.5, 0.4, 0.3, 0.2});
    prepare(chart);

    const QImage plain = renderWidget(chart);
    QVERIFY(!plain.isNull());

    chart.setMirror(true);
    QVERIFY(chart.isMirrored());
    const QImage mirrored = renderWidget(chart);
    QVERIFY(!mirrored.isNull());

    QVERIFY2(plain != mirrored, "setMirror(true) must draw the reflected lobe");
}

void TestPolarPlotChart::complexResponseBuildsFullCircle()
{
    md::PolarPlotChart chart;
    chart.setComplexResponse({std::complex<double>(1.0, 0.0),
                              std::complex<double>(0.0, 1.0),
                              std::complex<double>(-1.0, 0.0),
                              std::complex<double>(0.0, -1.0)});
    prepare(chart);

    QCOMPARE(chart.pointCount(), 4);
    for (int index = 0; index < chart.pointCount(); ++index) {
        QCOMPARE(chart.angleAt(index), static_cast<double>(index) * 90.0);
        QCOMPARE(chart.valueAt(index), 1.0);
    }

    // The start angle moves the whole set.
    chart.setComplexResponse({std::complex<double>(1.0, 0.0),
                              std::complex<double>(0.0, 1.0)},
                             45.0);
    QCOMPARE(chart.pointCount(), 2);
    QCOMPARE(chart.angleAt(0), 45.0);
    QCOMPARE(chart.angleAt(1), 225.0);

    // An empty response clears the chart.
    chart.setComplexResponse({});
    QCOMPARE(chart.pointCount(), 0);
}

void TestPolarPlotChart::hitTestMapsPixels()
{
    md::PolarPlotChart chart;
    setDefaultPattern(chart);
    prepare(chart);

    for (int index = 0; index < chart.pointCount(); ++index) {
        const QPointF point = chart.pointForIndex(index);
        QVERIFY(!point.isNull());
        QCOMPARE(chart.nearestIndex(point), index);
    }

    QCOMPARE(chart.nearestIndex(QPointF(-50.0, -50.0)), -1);
    QCOMPARE(chart.nearestIndex(chart.centre() + QPointF(chart.radius() * 1.4, 0.0)), -1);
}

void TestPolarPlotChart::emptyStatePaintsLabel()
{
    md::PolarPlotChart chart;
    prepare(chart);

    QCOMPARE(chart.pointCount(), 0);
    QVERIFY(chart.pointForIndex(0).isNull());
    QCOMPARE(chart.nearestIndex(QPointF(100.0, 100.0)), -1);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());
    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    QVERIFY2(pixelsDifferingFrom(image, background) > 0,
             "the empty state must paint its themed label");
}

void TestPolarPlotChart::revealReachesOne()
{
    md::PolarPlotChart chart;
    setDefaultPattern(chart);
    prepare(chart);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();
    chart.repaint();

    // The reveal starts on the first visible paint, so wait for that paint
    // rather than for the show() call itself.
    QTRY_VERIFY_WITH_TIMEOUT(chart.revealProgress() < 1.0, 2000);
    QVERIFY(chart.isRevealing());

    QTRY_VERIFY_WITH_TIMEOUT(!chart.isRevealing(), 5000);
    QCOMPARE(chart.revealProgress(), 1.0);
}

void TestPolarPlotChart::reducedMotionSnaps()
{
    md::PolarPlotChart chart;
    md::MotionScheme motion;
    motion.reducedMotion = true;
    md::ThemeScope* scope = md::ThemeScope::install(
        &chart, md::Theme::baselineLight().withMotion(motion));
    QVERIFY(scope != nullptr);

    setDefaultPattern(chart);
    prepare(chart);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();

    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());

    const QImage image = renderWidget(chart);
    QVERIFY(hasColourNear(image, chart.pointForIndex(0), chart.seriesColour(0)));
}

QTEST_MAIN(TestPolarPlotChart)
#include "tst_polar_plot_chart.moc"
