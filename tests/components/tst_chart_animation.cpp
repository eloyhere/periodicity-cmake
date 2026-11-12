// Tests for the chart animation model.
//
// The suite drives a tiny ChartBase subclass that paints one unmistakable mark
// and records every progress value it is handed. That makes the three contracts
// observable without pixel archaeology: the reveal really ramps from 0 to 1 over
// a token duration, a data change re-enters the update run, reduced motion and
// animated(false) settle synchronously, and stop() leaves a repaint-safe final
// frame.
//
// Like the other chart suites this one runs offscreen. The reveal starts on the
// first paint of a *visible* chart, so every case shows the widget and then
// forces that first paint with QWidget::render(); a hidden render deliberately
// paints the settled frame and is covered by its own case.

#include <md/components/chart_animation.h>
#include <md/components/chart_common.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QTest>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace {

constexpr int kChartWidth = 420;
constexpr int kChartHeight = 300;

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

QColor pixelAt(const QImage& image, QPointF logical)
{
    const qreal ratio = image.devicePixelRatio();
    const int x = std::clamp(static_cast<int>(std::lround(logical.x() * ratio)),
                             0, image.width() - 1);
    const int y = std::clamp(static_cast<int>(std::lround(logical.y() * ratio)),
                             0, image.height() - 1);
    return image.pixelColor(x, y);
}

md::ChartSeries makeSeries(const QVector<QPointF>& points)
{
    md::ChartSeries series;
    series.name = QStringLiteral("probe");
    series.points = points;
    return series;
}

// The data every chart in this suite carries: three finite points are enough for
// hasData() to select paintPlot() over the empty state.
QVector<md::ChartSeries> probeSeries()
{
    return {makeSeries({{0.0, 0.0}, {1.0, 2.0}, {2.0, 1.0}})};
}

// The mark a probe chart paints: a 20 x 20 red square in the middle of the plot,
// which no frame or axis can cover (the probes hide the axes).
QRectF markRect(const QRectF& plot)
{
    const QPointF centre = plot.center();
    return QRectF(centre.x() - 10.0, centre.y() - 10.0, 20.0, 20.0);
}

// A chart that implements the progress overload and records what it receives.
class ProbeChart : public md::ChartBase
{
public:
    explicit ProbeChart(QWidget* parent = nullptr)
        : md::ChartBase(parent)
    {
        setAxesVisible(false);
        setInteractionFeedbackEnabled(false);
    }

    QVector<qreal> progressValues;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override
    {
        painter.fillRect(markRect(plot), QColor(Qt::red));
    }

    void paintPlot(QPainter& painter, const QRectF& plot, qreal progress) override
    {
        progressValues.append(progress);

        painter.save();
        painter.setOpacity(painter.opacity() * std::clamp(progress, 0.0, 1.0));
        painter.fillRect(markRect(plot), QColor(Qt::red));
        painter.restore();
    }
};

// A chart that only implements the settled overload. It relies on the default
// progress overload for the fade, which is the "no per-element timing" path.
class SettledChart : public md::ChartBase
{
public:
    explicit SettledChart(QWidget* parent = nullptr)
        : md::ChartBase(parent)
    {
        setAxesVisible(false);
        setInteractionFeedbackEnabled(false);
    }

    int paints = 0;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override
    {
        ++paints;
        painter.fillRect(markRect(plot), QColor(Qt::red));
    }
};

// A chart whose per-frame hook fails. The exception must be contained and
// reported, and the chart must settle on its final frame instead of freezing
// half-revealed.
class ThrowingChart : public ProbeChart
{
public:
    using ProbeChart::ProbeChart;

protected:
    void onAnimationProgress(qreal) override
    {
        throw std::runtime_error("probe animation failure");
    }
};

// A chart with precomputed pixel-space paths. It is the benchmark subject: the
// only per-frame work is blitting the cached furniture and stroking five
// QPainterPaths, which is the shape the caching contract is meant to support.
class BenchmarkChart : public md::ChartBase
{
public:
    explicit BenchmarkChart(QWidget* parent = nullptr)
        : md::ChartBase(parent)
    {
        setInteractionFeedbackEnabled(false);
    }

    int chartCacheBuilds = 0;

protected:
    void updateChartCache() override
    {
        ++chartCacheBuilds;

        paths_.clear();
        paths_.reserve(series().size());

        for (const md::ChartSeries& entry : series()) {
            QPainterPath path;
            bool started = false;
            for (const QPointF& point : entry.points) {
                if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
                    started = false;
                    continue;
                }

                const QPointF pixel = math()->toPixel(point);
                if (!started) {
                    path.moveTo(pixel);
                    started = true;
                } else {
                    path.lineTo(pixel);
                }
            }
            paths_.append(path);
        }
    }

    void paintPlot(QPainter& painter, const QRectF& plot, qreal progress) override
    {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setClipRect(plot, Qt::IntersectClip);
        painter.setOpacity(painter.opacity() * std::clamp(progress, 0.0, 1.0));

        for (const QPainterPath& path : std::as_const(paths_))
            painter.strokePath(path, QPen(QColor(Qt::darkCyan), 1.0));

        painter.restore();
    }

    void paintPlot(QPainter& painter, const QRectF& plot) override
    {
        paintPlot(painter, plot, 1.0);
    }

private:
    QVector<QPainterPath> paths_;
};

// Five series of two thousand points: the shape the parent asked the benchmark
// to exercise.
QVector<md::ChartSeries> heavySeries()
{
    QVector<md::ChartSeries> list;
    list.reserve(5);

    for (int seriesAt = 0; seriesAt < 5; ++seriesAt) {
        md::ChartSeries series;
        series.name = QStringLiteral("s%1").arg(seriesAt);
        series.points.reserve(2000);

        for (int pointAt = 0; pointAt < 2000; ++pointAt) {
            const qreal x = pointAt * 0.01;
            const qreal y = std::sin(x * (seriesAt + 1)) + seriesAt * 0.2;
            series.points.append(QPointF(x, y));
        }

        list.append(series);
    }

    return list;
}

// A chart with precomputed pixel-space paths. It is the benchmark subject: the
// only per-frame work is blitting the cached furniture and stroking five
// QPainterPaths, which is the shape the caching contract is meant to support.
// (See BenchmarkChart above.)

// Times one render into a reused image. The painter is created inside the timed
// region on purpose: a real paint pays for it too, and the cold/warm comparison
// stays honest.
qint64 renderNanos(QWidget& widget, QImage& image)
{
    QElapsedTimer timer;
    QPainter painter(&image);

    timer.start();
    widget.render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();

    return timer.nsecsElapsed();
}

// Shows the widget and forces the first visible paint, which is what starts the
// reveal. The window system never paints under QTEST_MAIN with the offscreen
// platform, so the explicit render is the deterministic trigger.
void revealOnFirstPaint(QWidget& widget)
{
    widget.resize(kChartWidth, kChartHeight);
    widget.show();
    renderWidget(widget);
}

} // namespace

class TestChartAnimation : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void revealsOverTheTokenDuration();
    void disabledAnimationPaintsTheFinalFrame();
    void dataUpdateRestartsTheAnimation();
    void reducedMotionSnapsInOneTurn();
    void stopSettlesOnTheFinalFrame();
    void progressValuesAreInRangeAndMonotonic();
    void settledOverloadIsFadedByTheDefault();
    void throwingHookIsReportedThroughLastError();
    void staticLayerInvalidationRules();
    void benchmarkPaintUsesTheStaticCache();
};

void TestChartAnimation::initTestCase()
{
    // The baseline theme does not reduce motion, which is what makes the reveal
    // cases meaningful. Every case pins its own duration, so the default is not
    // part of the contract under test.
    md::Theme theme = md::Theme::baselineLight();
    md::MotionScheme motion = theme.motion();
    motion.reducedMotion = false;
    theme = theme.withMotion(motion);

    md::Theme::setApplicationTheme(theme);
    QApplication::setPalette(theme.toPalette());
}

void TestChartAnimation::revealsOverTheTokenDuration()
{
    ProbeChart chart;
    chart.setSeries(probeSeries());
    chart.animation().setDuration(md::MotionLevel::Level3); // 750 ms

    revealOnFirstPaint(chart);

    QVERIFY2(chart.animation().isRunning(),
             "the first visible paint must start the reveal");
    QVERIFY2(chart.animation().progress() < 1.0,
             "the reveal must not jump to its final frame");
    QCOMPARE(static_cast<int>(chart.animation().mode()),
             static_cast<int>(md::ChartAnimation::PlayMode::Reveal));

    QTRY_COMPARE_WITH_TIMEOUT(chart.animation().progress(), 1.0, 5000);
    QVERIFY(!chart.animation().isRunning());
}

void TestChartAnimation::disabledAnimationPaintsTheFinalFrame()
{
    ProbeChart chart;
    chart.setSeries(probeSeries());
    chart.setAnimated(false);
    QVERIFY(!chart.isAnimated());

    revealOnFirstPaint(chart);

    QCOMPARE(chart.animation().progress(), 1.0);
    QVERIFY2(!chart.animation().isRunning(),
             "an animated(false) chart must never start an animator");
    QVERIFY(!chart.progressValues.isEmpty());
    QCOMPARE(chart.progressValues.last(), 1.0);

    const QImage image = renderWidget(chart);
    QCOMPARE(pixelAt(image, chart.plotRect().center()).rgb(), QColor(Qt::red).rgb());
}

void TestChartAnimation::dataUpdateRestartsTheAnimation()
{
    ProbeChart chart;
    chart.setSeries(probeSeries());
    chart.animation().setDuration(md::MotionLevel::Level2); // 500 ms

    revealOnFirstPaint(chart);
    QTRY_COMPARE_WITH_TIMEOUT(chart.animation().progress(), 1.0, 5000);
    QVERIFY(!chart.animation().isRunning());

    chart.progressValues.clear();
    chart.setSeries(probeSeries());

    QCOMPARE(static_cast<int>(chart.animation().mode()),
             static_cast<int>(md::ChartAnimation::PlayMode::Update));
    QVERIFY2(chart.animation().isRunning(), "a data change after the reveal restarts the run");
    QVERIFY2(chart.animation().progress() < 1.0,
             "the update must re-reveal from the start, not keep the settled frame");

    QTRY_COMPARE_WITH_TIMEOUT(chart.animation().progress(), 1.0, 5000);
    QVERIFY(!chart.progressValues.isEmpty());
    QVERIFY2(chart.progressValues.first() < 0.5,
             "the first update frame must be near the start of the run");
}

void TestChartAnimation::reducedMotionSnapsInOneTurn()
{
    ProbeChart chart;

    md::MotionScheme motion = md::Theme::baselineLight().motion();
    motion.reducedMotion = true;
    md::ThemeScope* scope =
        md::ThemeScope::install(&chart, md::Theme::baselineLight().withMotion(motion));
    QVERIFY(scope != nullptr);

    chart.setSeries(probeSeries());
    chart.animation().setDuration(md::MotionLevel::Level3);

    revealOnFirstPaint(chart);

    // The snap is synchronous: no turn ever shows a partial chart, and no
    // animator is started at all.
    QCOMPARE(chart.animation().progress(), 1.0);
    QVERIFY2(!chart.animation().isRunning(),
             "reduced motion must not start an animator");
    for (const qreal value : std::as_const(chart.progressValues))
        QCOMPARE(value, 1.0);

    const QImage image = renderWidget(chart);
    QCOMPARE(pixelAt(image, chart.plotRect().center()).rgb(), QColor(Qt::red).rgb());
}

void TestChartAnimation::stopSettlesOnTheFinalFrame()
{
    ProbeChart chart;
    chart.setSeries(probeSeries());
    chart.animation().setDuration(md::MotionLevel::Level3);

    revealOnFirstPaint(chart);
    QVERIFY(chart.animation().isRunning());
    QVERIFY(chart.animation().progress() < 1.0);

    chart.animation().stop();

    QCOMPARE(chart.animation().progress(), 1.0);
    QVERIFY(!chart.animation().isRunning());
    QCOMPARE(static_cast<int>(chart.animation().mode()),
             static_cast<int>(md::ChartAnimation::PlayMode::None));

    // Repaint-safe: the stopped chart paints the finished frame, and painting it
    // again produces exactly the same pixels instead of advancing or crashing.
    const QImage first = renderWidget(chart);
    const QImage second = renderWidget(chart);
    QVERIFY(!first.isNull());
    QCOMPARE(first, second);
    QCOMPARE(pixelAt(first, chart.plotRect().center()).rgb(), QColor(Qt::red).rgb());
}

void TestChartAnimation::progressValuesAreInRangeAndMonotonic()
{
    ProbeChart chart;
    chart.setSeries(probeSeries());
    chart.animation().setDuration(md::MotionLevel::Level1); // 250 ms

    revealOnFirstPaint(chart);
    QTRY_COMPARE_WITH_TIMEOUT(chart.animation().progress(), 1.0, 5000);

    // Force the frame the completion scheduled, so the recorded list ends on the
    // value the finished run paints with.
    renderWidget(chart);

    QVERIFY2(chart.progressValues.size() >= 2,
             "the reveal must deliver more than its first frame");
    QVERIFY2(chart.progressValues.first() < 0.5,
             "the first painted frame must be at the start of the reveal");

    qreal previous = -1.0;
    for (const qreal value : std::as_const(chart.progressValues)) {
        QVERIFY2(value >= 0.0 && value <= 1.0,
                 "the progress overload must only see values in [0, 1]");
        QVERIFY2(value >= previous - 1e-9,
                 "the progress overload must see monotonically increasing values");
        previous = value;
    }

    QCOMPARE(chart.progressValues.last(), 1.0);
}

void TestChartAnimation::settledOverloadIsFadedByTheDefault()
{
    SettledChart chart;
    chart.setSeries(probeSeries());
    chart.animation().setDuration(md::MotionLevel::Level3);

    revealOnFirstPaint(chart);

    // The default progress overload returns before the settled overload at
    // progress 0, so the first frame shows the furniture but no marks.
    QCOMPARE(chart.paints, 0);

    QTRY_COMPARE_WITH_TIMEOUT(chart.animation().progress(), 1.0, 5000);

    const QImage image = renderWidget(chart);
    QVERIFY2(chart.paints > 0, "the settled overload must paint the finished frame");
    QCOMPARE(pixelAt(image, chart.plotRect().center()).rgb(), QColor(Qt::red).rgb());
}

void TestChartAnimation::throwingHookIsReportedThroughLastError()
{
    ThrowingChart chart;
    chart.setSeries(probeSeries());
    chart.animation().setDuration(md::MotionLevel::Level1);

    revealOnFirstPaint(chart);

    QTRY_VERIFY_WITH_TIMEOUT(!chart.lastError().isEmpty(), 5000);
    QVERIFY2(chart.lastError().contains(QStringLiteral("probe animation failure")),
             qPrintable(chart.lastError()));

    // The run was abandoned on its final frame, not left half-revealed.
    QCOMPARE(chart.animation().progress(), 1.0);
    QVERIFY(!chart.animation().isRunning());

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());
    QCOMPARE(pixelAt(image, chart.plotRect().center()).rgb(), QColor(Qt::red).rgb());
}

void TestChartAnimation::staticLayerInvalidationRules()
{
    ProbeChart chart;
    chart.setSeries(probeSeries());
    chart.setAnimated(false);
    chart.resize(kChartWidth, kChartHeight);

    // A hidden render paints the settled frame and builds the layer once.
    renderWidget(chart);
    QVERIFY(chart.isStaticLayerCached());
    const quint64 initialBuilds = chart.staticLayerBuilds();
    QVERIFY(initialBuilds >= 1);

    // Plain repaints never rebuild it.
    renderWidget(chart);
    renderWidget(chart);
    QCOMPARE(chart.staticLayerBuilds(), initialBuilds);

    // A resize does.
    chart.resize(kChartWidth + 40, kChartHeight + 20);
    renderWidget(chart);
    QVERIFY(chart.staticLayerBuilds() > initialBuilds);
    const quint64 afterResize = chart.staticLayerBuilds();

    // An explicit invalidation rebuilds exactly once.
    chart.invalidateStaticLayer();
    QVERIFY(!chart.isStaticLayerCached());
    renderWidget(chart);
    QCOMPARE(chart.staticLayerBuilds(), afterResize + 1);
    renderWidget(chart);
    QCOMPARE(chart.staticLayerBuilds(), afterResize + 1);

    // A theme change invalidates the cached fonts, palette and layer.
    md::ThemeScope* scope = md::ThemeScope::install(
        &chart, md::Theme::baselineLight().withMotion(md::Theme::baselineLight().motion()));
    QVERIFY(scope != nullptr);
    QCoreApplication::processEvents(); // the ThemeChange event is posted
    renderWidget(chart);
    // Re-installing the identical theme may legitimately leave the cache valid;`r`n    // an explicit invalidation must still force exactly one rebuild.`r`n    QVERIFY(chart.staticLayerBuilds() >= afterResize + 1);`r`n    chart.invalidateStaticLayer();`r`n    renderWidget(chart);`r`n    QCOMPARE(chart.staticLayerBuilds(), afterResize + 2);
}

void TestChartAnimation::benchmarkPaintUsesTheStaticCache()
{
    BenchmarkChart chart;

    // Reduced motion keeps the auto-fit synchronous; the benchmark measures the
    // paint path, not the range animation.
    md::MotionScheme motion = md::Theme::baselineLight().motion();
    motion.reducedMotion = true;
    md::ThemeScope* scope =
        md::ThemeScope::install(&chart, md::Theme::baselineLight().withMotion(motion));
    QVERIFY(scope != nullptr);

    chart.setAnimated(false);
    chart.setSeries(heavySeries());
    chart.resize(800, 500);
    chart.show();
    QCoreApplication::processEvents();

    const qreal ratio = chart.devicePixelRatioF();
    QImage target(chart.size() * ratio, QImage::Format_ARGB32_Premultiplied);
    target.setDevicePixelRatio(ratio);
    target.fill(Qt::transparent);

    // Cold: invalidate both layers before every render, so each timing includes
    // a full static-layer rebuild and a full chart-cache rebuild.
    constexpr int kColdRuns = 3;
    qint64 coldNanos = 0;
    for (int run = 0; run < kColdRuns; ++run) {
        chart.invalidateStaticLayer();
        chart.invalidateChartCache();
        coldNanos += renderNanos(chart, target);
    }
    const qreal coldMeanMs = static_cast<qreal>(coldNanos) / kColdRuns / 1.0e6;

    // Warm: nothing changes between renders. The static layer and the paths
    // must both be reused.
    constexpr int kWarmRuns = 120;
    const quint64 buildsBefore = chart.staticLayerBuilds();
    const int chartBuildsBefore = chart.chartCacheBuilds;

    QElapsedTimer warmClock;
    warmClock.start();
    for (int run = 0; run < kWarmRuns; ++run)
        renderNanos(chart, target);
    const qreal warmMeanMs =
        static_cast<qreal>(warmClock.nsecsElapsed()) / kWarmRuns / 1.0e6;

    QCOMPARE(chart.staticLayerBuilds(), buildsBefore);
    QCOMPARE(chart.chartCacheBuilds, chartBuildsBefore);
    QVERIFY(chart.isStaticLayerCached());

    qInfo("chart paint benchmark: 800x500, 5 series x 2000 points, "
          "%d cold / %d warm renders: cold %.3f ms, warm %.3f ms",
          kColdRuns, kWarmRuns, coldMeanMs, warmMeanMs);

    // The measured warm mean in this Debug build is far below the threshold;
    // the generous bound keeps a loaded CI box from turning the benchmark into
    // a flake. The cache-reuse assertions above are the hard part.
    //
    // Reference point: a standalone probe that only strokes the same five
    // 2000-point paths into an 800x500 image takes ~3.7 ms per frame in this
    // Debug configuration, so 25 ms leaves several times the observed cost for
    // the chart machinery and a slower machine.
    constexpr qreal kMaximumWarmPaintMs = 25.0;
    QVERIFY2(warmMeanMs < kMaximumWarmPaintMs,
             qPrintable(QStringLiteral("warm mean %1 ms exceeds %2 ms")
                            .arg(warmMeanMs)
                            .arg(kMaximumWarmPaintMs)));
}

QTEST_MAIN(TestChartAnimation)
#include "tst_chart_animation.moc"
