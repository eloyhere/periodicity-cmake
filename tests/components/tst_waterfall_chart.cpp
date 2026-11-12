// Tests for the WaterfallChart component.
//
// The suite pins a reduced-motion application theme so the first render is the
// settled stack. Geometry comes from tracePoint(), and the hit-test case feeds
// that exact pixel back to traceAt(), so the paint pass and the hit test are
// checked against one shared mapping. The reveal cases install a normal-motion
// ThemeScope explicitly.

#include <md/components/waterfall_chart.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QTest>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int kChartWidth = 520;
constexpr int kChartHeight = 380;

md::Theme reducedMotionTheme()
{
    md::Theme theme = md::Theme::baselineLight();
    md::MotionScheme motion = theme.motion();
    motion.reducedMotion = true;
    return theme.withMotion(motion);
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

void prepare(QWidget& widget)
{
    widget.resize(kChartWidth, kChartHeight);
    widget.show();
    QCoreApplication::processEvents();
}

QColor pixelAt(const QImage& image, QPointF logical)
{
    const qreal ratio = image.devicePixelRatio();
    const int x = std::clamp(static_cast<int>(std::lround(logical.x() * ratio)),
                             0, image.width() - 1);
    const int y = std::clamp(static_cast<int>(std::lround(logical.y() * ratio)),
                             0, image.height() - 1);
    return image.pixelColor(x, y);
}

int paintedPixels(const QImage& image, const QRectF& logicalRect, const QColor& background)
{
    const qreal ratio = image.devicePixelRatio();
    const int left = std::clamp(static_cast<int>(std::floor(logicalRect.left() * ratio)),
                                0, image.width() - 1);
    const int top = std::clamp(static_cast<int>(std::floor(logicalRect.top() * ratio)),
                               0, image.height() - 1);
    const int right = std::clamp(static_cast<int>(std::ceil(logicalRect.right() * ratio)),
                                 0, image.width() - 1);
    const int bottom = std::clamp(static_cast<int>(std::ceil(logicalRect.bottom() * ratio)),
                                  0, image.height() - 1);

    int count = 0;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (image.pixelColor(x, y) != background)
                ++count;
        }
    }

    return count;
}

std::vector<std::vector<double>> testTraces()
{
    return {
        {-70.0, -70.0, -70.0, -70.0, -70.0, -70.0, -70.0, -70.0},
        {-70.0, -60.0, -40.0, -20.0, -45.0, -62.0, -72.0, -75.0},
        {-75.0, -75.0, -75.0, -75.0, -75.0, -75.0, -75.0, -75.0},
    };
}

} // namespace

class TestWaterfallChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void constructsWithDefaults();
    void settersClampAndNormalise();
    void axisTicksUseHertzAndDecibels();
    void tracesPaintAtTracePoint();
    void depthOffsetsOlderTracesUpward();
    void traceAtResolvesTheNearestSpectrum();
    void emptyStackPaints();
    void revealAnimationReachesOne();
    void reducedMotionSnaps();
};

void TestWaterfallChart::initTestCase()
{
    const md::Theme theme = reducedMotionTheme();
    md::Theme::setApplicationTheme(theme);
    QApplication::setPalette(theme.toPalette());
}

void TestWaterfallChart::cleanupTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestWaterfallChart::constructsWithDefaults()
{
    md::WaterfallChart chart;

    QCOMPARE(chart.traceCount(), 0);
    QCOMPARE(chart.binCount(), 0);
    QCOMPARE(chart.sampleRate(), 48000.0);
    QCOMPARE(chart.nyquistFrequency(), 24000.0);
    QCOMPARE(chart.minimumDb(), -100.0);
    QCOMPARE(chart.maximumDb(), 0.0);
    QCOMPARE(chart.depth(), 0.32);
    QVERIFY(!chart.isLegendVisible());
    QVERIFY(chart.isAxesVisible());
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());
    QVERIFY(chart.sizeHint().width() > 0);
    QVERIFY(chart.sizeHint().height() > 0);

    chart.setTraces(testTraces());
    QCOMPARE(chart.traceCount(), 3);
    QCOMPARE(chart.binCount(), 8);
    QCOMPARE(chart.traces().at(1).at(3), -20.0);

    chart.setSampleRate(8000.0);
    chart.setDbRange(-80.0, -10.0);
    QCOMPARE(chart.sampleRate(), 8000.0);
    QCOMPARE(chart.nyquistFrequency(), 4000.0);
    QCOMPARE(chart.minimumDb(), -80.0);
    QCOMPARE(chart.maximumDb(), -10.0);
}

void TestWaterfallChart::settersClampAndNormalise()
{
    md::WaterfallChart chart;

    chart.setDbRange(-10.0, -80.0);
    QCOMPARE(chart.minimumDb(), -80.0);
    QCOMPARE(chart.maximumDb(), -10.0);

    chart.setDbRange(3.0, 3.0);
    QVERIFY(chart.maximumDb() > chart.minimumDb());

    chart.setDepth(0.5);
    QCOMPARE(chart.depth(), 0.5);
    chart.setDepth(2.0);
    QCOMPARE(chart.depth(), 0.9);
    chart.setDepth(-1.0);
    QCOMPARE(chart.depth(), 0.0);

    chart.setSampleRate(0.0);
    QCOMPARE(chart.sampleRate(), 48000.0);
    chart.setSampleRate(44100.0);
    QCOMPARE(chart.sampleRate(), 44100.0);
}

void TestWaterfallChart::axisTicksUseHertzAndDecibels()
{
    md::WaterfallChart chart;
    chart.setSampleRate(8000.0);
    chart.setDbRange(-80.0, -10.0);
    chart.setTraces(testTraces());
    prepare(chart);

    const QVector<qreal> frequencyTicks = chart.frequencyTicks();
    QVERIFY(frequencyTicks.size() >= 2);
    for (const qreal tick : frequencyTicks) {
        QVERIFY(tick >= 0.0);
        QVERIFY(tick <= chart.nyquistFrequency() + 1e-9);
    }

    const QVector<qreal> levelTicks = chart.levelTicks();
    QVERIFY(levelTicks.size() >= 2);
    for (const qreal tick : levelTicks) {
        QVERIFY(tick >= chart.minimumDb() - 1e-9);
        QVERIFY(tick <= chart.maximumDb() + 1e-9);
    }

    QVERIFY(chart.math()->xAxisLabel().contains(QStringLiteral("Hz")));
    QVERIFY(chart.math()->yAxisLabel().contains(QStringLiteral("dB")));
}

void TestWaterfallChart::tracesPaintAtTracePoint()
{
    md::WaterfallChart chart;
    chart.setSampleRate(8000.0);
    chart.setDbRange(-80.0, -10.0);
    chart.setTraces(testTraces());
    prepare(chart);

    const QPointF point = chart.tracePoint(1, 3);
    QVERIFY(!point.isNull());
    QVERIFY(chart.plotRect().contains(point));

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    const QRectF around(point.x() - 3.0, point.y() - 3.0, 6.0, 6.0);
    QVERIFY2(paintedPixels(image, around, background) > 0,
             "the spectrum stroke or its fill must paint at tracePoint()");
}

void TestWaterfallChart::depthOffsetsOlderTracesUpward()
{
    md::WaterfallChart chart;
    chart.setSampleRate(8000.0);
    chart.setDbRange(-80.0, -10.0);
    // Equal levels isolate the depth offset from the amplitude mapping.
    chart.setTraces(std::vector<std::vector<double>>(3, std::vector<double>(8, -50.0)));
    chart.setDepth(0.5);
    prepare(chart);

    const QPointF oldest = chart.tracePoint(0, 3);
    const QPointF middle = chart.tracePoint(1, 3);
    const QPointF newest = chart.tracePoint(2, 3);

    QVERIFY(!oldest.isNull());
    QVERIFY(!middle.isNull());
    QVERIFY(!newest.isNull());

    QVERIFY2(oldest.y() < middle.y(), "the oldest trace must recede upward");
    QVERIFY2(middle.y() < newest.y(), "the newest trace must sit lowest");
    QVERIFY(newest.y() < chart.plotRect().bottom());

    chart.setDepth(0.0);
    const QPointF flatOldest = chart.tracePoint(0, 3);
    const QPointF flatNewest = chart.tracePoint(2, 3);
    QVERIFY(std::abs(flatOldest.y() - flatNewest.y())
            < std::abs(oldest.y() - newest.y()));
}

void TestWaterfallChart::traceAtResolvesTheNearestSpectrum()
{
    md::WaterfallChart chart;
    chart.setSampleRate(8000.0);
    chart.setDbRange(-80.0, -10.0);
    chart.setTraces(testTraces());
    prepare(chart);

    int traceIndex = -1;
    double frequency = 0.0;
    double level = 0.0;

    QVERIFY(chart.traceAt(chart.tracePoint(1, 3), &traceIndex, &frequency, &level));
    QCOMPARE(traceIndex, 1);
    QVERIFY(std::abs(frequency - 4000.0 * 3.0 / 7.0)
            < 4000.0 / 7.0 + 1e-9);
    QCOMPARE(level, -20.0);

    QVERIFY(!chart.traceAt(QPointF(1.0, 1.0), &traceIndex, &frequency, &level));
    QCOMPARE(traceIndex, -1);
}

void TestWaterfallChart::emptyStackPaints()
{
    md::WaterfallChart chart;
    prepare(chart);

    QVERIFY(chart.traces().empty());
    QVERIFY(!renderWidget(chart).isNull());

    chart.setTraces({});
    QVERIFY(!renderWidget(chart).isNull());

    // Traces made entirely of non-finite levels are still an empty state.
    chart.setTraces({{std::nan(""), std::nan("")}});
    QVERIFY(!renderWidget(chart).isNull());
}

void TestWaterfallChart::revealAnimationReachesOne()
{
    md::WaterfallChart chart;
    chart.setTraces(testTraces());

    md::ThemeScope* scope = md::ThemeScope::install(&chart, md::Theme::baselineLight());
    QVERIFY(scope != nullptr);

    chart.resize(kChartWidth, kChartHeight);
    chart.show();
    renderWidget(chart); // the first visible paint starts the shared reveal

    QVERIFY2(chart.revealProgress() < 1.0, "a normal-motion show must animate the cascade");
    QVERIFY(chart.isRevealing());

    QTRY_VERIFY_WITH_TIMEOUT(!chart.isRevealing(), 5000);
    QCOMPARE(chart.revealProgress(), 1.0);
}

void TestWaterfallChart::reducedMotionSnaps()
{
    md::WaterfallChart chart;

    md::MotionScheme motion;
    motion.reducedMotion = true;
    md::ThemeScope* scope = md::ThemeScope::install(
        &chart, md::Theme::baselineLight().withMotion(motion));
    QVERIFY(scope != nullptr);

    chart.setSampleRate(8000.0);
    chart.setDbRange(-80.0, -10.0);
    chart.setTraces(testTraces());

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.resize(kChartWidth, kChartHeight);
    chart.show();
    renderWidget(chart); // a reduced-motion paint settles synchronously

    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());

    const QImage image = renderWidget(chart);
    const QPointF point = chart.tracePoint(1, 3);
    QVERIFY(!point.isNull());
    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    QVERIFY(paintedPixels(image, QRectF(point.x() - 3.0, point.y() - 3.0, 6.0, 6.0),
                          background) > 0);
}

QTEST_MAIN(TestWaterfallChart)
#include "tst_waterfall_chart.moc"
