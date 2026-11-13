// Benchmarks for the retained-layer (L2) work.
//
// Three scenarios, one per acceptance shape named in
// docs/rendering-architecture.md ("Performance workstream" and "Target
// architecture"):
//
//   windowOfComponents  74 components in one window; an uncached full paint,
//                       a cached clean frame, and a cached frame with one
//                       component's 1-rect damage;
//   pageOfCharts        25 chart widgets on one page; the same three states
//                       (the third changes one chart's data);
//   revealAnimation     one chart's reveal driven by a manual animation clock
//                       (no wall-clock wait), uncached versus cached, total
//                       time to settle.
//
// Measurement rules, because a benchmark that lies is worse than none:
//
//   * samples use std::chrono::steady_clock, 30 iterations per configuration
//     after one discarded warm-up pass, and report mean, median and p95;
//   * every frame is forced through QWidget::render() (the explicit paint path,
//     the same call the component tests use) and the produced image is checked
//     for full opacity and colour variation, so an empty paint fails instead of
//     producing a fast number;
//   * the cached configurations assert their premise: every layer-backed view
//     holds a layer, liveLayerCount() does not move between frames, and the
//     clean frame runs zero paintContent hooks. A silent fallback to the direct
//     path would run the hooks and fail the assertion. The process-wide
//     LayerCacheStats is private to render/ and to view/'s private edge, so the
//     hit is asserted through these public consequences rather than through the
//     cache's own counters;
//   * the cached clean frame must be at least 2x cheaper than the uncached full
//     paint (median), because that is the entire justification for the layer
//     work. Every other comparison is reported, never asserted.
//
// The table is printed to stdout as PERF_ROW lines and, when
// PERIODICITY_BENCH_OUT names a file, the same table is written there. The
// results are recorded in docs/perf/layer-benchmarks.md; this file does not
// invent numbers.

#include <md/animation/animator.h>
#include <md/components/button.h>
#include <md/components/chart_common.h>
#include <md/components/line_chart.h>
#include <md/components/text.h>
#include <md/theme/theme.h>
#include <md/view/layer_debug.h>
#include <md/view/view.h>

#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QRegion>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>
#include <QWidget>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>
#include <utility>
#include <vector>

namespace {

// 30 iterations is the documented minimum and enough for a stable median: the
// paint path is synchronous, so the samples are not interleaved with event-loop
// work.
constexpr int kIterations = 30;

// windowOfComponents: 10 x 8 cells, 110 x 72 each. 74 widgets fill 70 cells of
// the first seven rows and four of the last row.
constexpr int kComponentColumns = 10;
constexpr int kComponentRows = 8;
constexpr int kComponentCellWidth = 110;
constexpr int kComponentCellHeight = 72;
constexpr int kComponentGap = 6;
constexpr int kComponentMargin = 8;
constexpr int kComponentCount = 74;
constexpr int kComponentWindowWidth =
    kComponentMargin * 2 + kComponentColumns * kComponentCellWidth
    + (kComponentColumns - 1) * kComponentGap;
constexpr int kComponentWindowHeight =
    kComponentMargin * 2 + kComponentRows * kComponentCellHeight
    + (kComponentRows - 1) * kComponentGap;

// pageOfCharts: 5 x 5 charts of 300 x 220.
constexpr int kChartColumns = 5;
constexpr int kChartRows = 5;
constexpr int kChartWidth = 300;
constexpr int kChartHeight = 220;
constexpr int kChartGap = 8;
constexpr int kChartMargin = 8;
constexpr int kChartCount = kChartColumns * kChartRows;
constexpr int kChartWindowWidth =
    kChartMargin * 2 + kChartColumns * kChartWidth
    + (kChartColumns - 1) * kChartGap;
constexpr int kChartWindowHeight =
    kChartMargin * 2 + kChartRows * kChartHeight
    + (kChartRows - 1) * kChartGap;

// revealAnimation: one 640 x 420 chart, 16 ms of animation time per frame. The
// step is the animation-clock step, not a wall-clock sleep; a 750 ms Level3
// reveal therefore takes 47 advances and 48 painted frames every run.
constexpr int kRevealWidth = 640;
constexpr int kRevealHeight = 420;
constexpr int kRevealStepMs = 16;
constexpr int kRevealSafetyFrameCap = 4096;
constexpr int kRevealSeries = 4;
constexpr int kRevealPoints = 120;

// The probe mark a component paints when it is the damaged subject. It is
// deliberately small: the layer's clip for that pass must be the 1-rect damage,
// not the whole surface.
const QRect kProbeRect(8, 8, 10, 10);

QRect gridRect(int index, int columns, int cellWidth, int cellHeight, int gap, int margin)
{
    const int column = index % columns;
    const int row = index / columns;
    return QRect(margin + column * (cellWidth + gap),
                 margin + row * (cellHeight + gap), cellWidth, cellHeight);
}

QSize pixelSizeFor(const QWidget& widget)
{
    const qreal ratio = widget.devicePixelRatioF();
    return QSize(static_cast<int>(std::lround(widget.width() * ratio)),
                 static_cast<int>(std::lround(widget.height() * ratio)));
}

QImage makeTarget(const QWidget& widget)
{
    QImage image(pixelSizeFor(widget), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(widget.devicePixelRatioF());
    image.fill(Qt::transparent);
    return image;
}

// A top-level host with a defined background. QWidget::render() with
// DrawChildren alone does not fill the window; this paintEvent does, so a frame
// that painted nothing stays transparent instead of looking like a frame.
class HostWindow final : public QWidget
{
public:
    explicit HostWindow(QWidget* parent = nullptr)
        : QWidget(parent)
    {
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0xf2, 0xf2, 0xf2));
    }
};

// The common face of the instrumented components. The benchmark needs to ask
// every widget the same three questions (did the content hook run, what was its
// clip, does it hold a layer) without knowing the concrete component type.
class CountedProbe
{
public:
    virtual ~CountedProbe() = default;

    [[nodiscard]] virtual md::View* view() noexcept = 0;
    [[nodiscard]] virtual int contentPaintCount() const noexcept = 0;
    virtual void resetContentPaintCount() noexcept = 0;
    [[nodiscard]] virtual QRect lastContentClip() const noexcept = 0;
    virtual void setObserveClip(bool observe) = 0;
    virtual void setProbeColour(const QColor& colour) = 0;
    [[nodiscard]] virtual QRect probeRect() const noexcept = 0;
};

// Wraps a real component and forwards its paintContent(), so the benchmark
// measures the component the library ships, not a stand-in. The optional probe
// mark is used only by the damaged component in windowOfComponents; it is
// painted after the component's own content and needs no repaint by itself
// (the benchmark records the damage explicitly, exactly as a component would).
template <class Base>
class CountingView final : public Base, public CountedProbe
{
public:
    explicit CountingView(QWidget* parent = nullptr)
        : Base(parent)
    {
    }

    [[nodiscard]] md::View* view() noexcept override { return this; }
    [[nodiscard]] int contentPaintCount() const noexcept override { return contentPaints_; }
    void resetContentPaintCount() noexcept override
    {
        contentPaints_ = 0;
        lastClip_ = QRect();
    }
    [[nodiscard]] QRect lastContentClip() const noexcept override { return lastClip_; }
    void setObserveClip(bool observe) override { observeClip_ = observe; }
    void setProbeColour(const QColor& colour) override { probeColour_ = colour; }
    [[nodiscard]] QRect probeRect() const noexcept override { return kProbeRect; }

protected:
    void paintContent(QPainter& painter, const QRectF& content) override
    {
        ++contentPaints_;
        if (observeClip_) {
            lastClip_ = painter.clipBoundingRect().toAlignedRect();
        }

        Base::paintContent(painter, content);

        if (probeColour_.isValid())
            painter.fillRect(QRectF(kProbeRect), probeColour_);
    }

private:
    int contentPaints_ = 0;
    bool observeClip_ = false;
    QRect lastClip_;
    QColor probeColour_;
};

// The reveal subject. ChartBase owns a wall-clock Animator for its reveal and
// exposes no manual clock, so this subclass suppresses the chart's own run and
// plays the same fade through the documented paintPlot(..., progress) hook with
// an Animator on the manual clock. That keeps the benchmark deterministic
// without adding a library API: the paint path, the reveal spec and the
// per-frame invalidation are the chart's own; only the clock owner differs.
class RevealLineChart : public md::LineChartView
{
public:
    explicit RevealLineChart(QWidget* parent = nullptr)
        : md::LineChartView(parent)
    {
        setAnimated(false); // suppress ChartBase's wall-clock reveal
        setInteractionFeedbackEnabled(false);
        revealAnimator_.setManualClock(true);
    }

    void startBenchmarkReveal()
    {
        revealProgress_ = 0.0;

        const md::MotionSpec spec =
            animation().specFor(md::ChartAnimation::PlayMode::Reveal);
        revealAnimator_.start(spec, [this](qreal value) {
            revealProgress_ = value;
            // A reveal frame repaints the whole chart, exactly as the chart's
            // own onAnimatorValue() does.
            requestUpdate(QRectF(rect()));
        });

        // The first frame is progress 0; mark it dirty even if the retained
        // layer was clean after the warm-up frame.
        requestUpdate(QRectF(rect()));
    }

    void advanceBenchmarkReveal(std::chrono::milliseconds delta)
    {
        revealAnimator_.advanceTimeline(delta);
    }

    [[nodiscard]] bool isBenchmarkRevealing() const noexcept
    {
        return revealAnimator_.isRunning();
    }

    [[nodiscard]] qreal benchmarkRevealProgress() const noexcept
    {
        return revealProgress_;
    }

protected:
    void paintPlot(QPainter& painter, const QRectF& plot, qreal) override
    {
        // The same fade ChartBase's default progress overload applies, with the
        // manual clock's value instead of the chart's own animation value.
        if (revealProgress_ <= 0.0)
            return;
        if (revealProgress_ >= 1.0) {
            md::LineChartView::paintPlot(painter, plot);
            return;
        }

        painter.save();
        painter.setOpacity(painter.opacity() * revealProgress_);
        painter.setClipRect(plot, Qt::IntersectClip);
        md::LineChartView::paintPlot(painter, plot);
        painter.restore();
    }

private:
    md::animation::Animator revealAnimator_;
    qreal revealProgress_ = 1.0;
};

QVector<md::ChartSeries> lineSeries(double phase)
{
    QVector<md::ChartSeries> list;
    list.reserve(kRevealSeries);

    for (int seriesAt = 0; seriesAt < kRevealSeries; ++seriesAt) {
        md::ChartSeries series;
        series.name = QStringLiteral("series %1").arg(seriesAt);
        series.points.reserve(kRevealPoints);

        for (int pointAt = 0; pointAt < kRevealPoints; ++pointAt) {
            const double x = pointAt * 0.1;
            const double y =
                std::sin(x * (seriesAt + 1) + phase) + seriesAt * 0.25;
            series.points.append(QPointF(x, y));
        }

        list.append(series);
    }

    return list;
}

struct Stats
{
    double meanMs = 0.0;
    double medianMs = 0.0;
    double p95Ms = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
};

Stats statsOf(std::vector<double> samples)
{
    Stats stats;
    if (samples.empty())
        return stats;

    std::sort(samples.begin(), samples.end());

    double sum = 0.0;
    for (const double sample : samples)
        sum += sample;

    const std::size_t count = samples.size();
    stats.meanMs = sum / static_cast<double>(count);
    stats.medianMs = (count % 2 == 1)
        ? samples[count / 2]
        : 0.5 * (samples[count / 2 - 1] + samples[count / 2]);

    // Nearest-rank p95: the smallest sample that at least 95 % of the samples
    // do not exceed. For 30 samples that is the 29th value.
    const std::size_t rank =
        static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(count)));
    stats.p95Ms = samples[std::min(count - 1, rank == 0 ? 0 : rank - 1)];
    stats.minMs = samples.front();
    stats.maxMs = samples.back();
    return stats;
}

// One forced frame into `target`. The fill and the target painter are outside
// the timed span: the subject is the widget paint path, not the benchmark
// harness. `clearFirst` is false only for the reveal, where a per-frame fill
// would be charged to the animation total for no reason (the chart is opaque).
bool renderFrame(QWidget& widget, QImage& target, qint64* nanos, bool clearFirst = true)
{
    if (target.isNull() || target.size() != pixelSizeFor(widget))
        return false;

    if (clearFirst)
        target.fill(Qt::transparent);

    QPainter painter(&target);
    if (!painter.isActive())
        return false;

    const auto started = std::chrono::steady_clock::now();
    widget.render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    const auto finished = std::chrono::steady_clock::now();
    painter.end();

    if (nanos != nullptr) {
        *nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                     finished - started)
                     .count();
    }

    return true;
}

// "The frame was actually produced": every pixel is opaque (the host filled the
// window) and the sampled frame has real variation (the components painted
// something on top). This is what catches an empty paint, which would otherwise
// be the fastest possible sample.
bool frameWasProduced(const QImage& image, QString* why)
{
    if (image.isNull()) {
        *why = QStringLiteral("the frame is null");
        return false;
    }

    const int pixelsWide = image.width();
    const int pixelsHigh = image.height();
    if (pixelsWide < 2 || pixelsHigh < 2) {
        *why = QStringLiteral("the frame is empty (%1x%2)")
                   .arg(pixelsWide)
                   .arg(pixelsHigh);
        return false;
    }

    for (int y = 0; y < pixelsHigh; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < pixelsWide; ++x) {
            if (qAlpha(line[x]) != 255) {
                *why = QStringLiteral("the frame has an unpainted pixel at %1,%2")
                           .arg(x)
                           .arg(y);
                return false;
            }
        }
    }

    std::set<QRgb> colours;
    const int stepX = std::max(1, pixelsWide / 64);
    const int stepY = std::max(1, pixelsHigh / 64);
    for (int y = 0; y < pixelsHigh; y += stepY) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < pixelsWide; x += stepX)
            colours.insert(line[x]);
    }

    if (colours.size() < 8) {
        *why = QStringLiteral("the frame has only %1 sampled colours; the paint "
                              "path appears to be empty")
                   .arg(colours.size());
        return false;
    }

    return true;
}

bool renderFrameChecked(QWidget& widget, QImage& target, QString* why)
{
    if (!renderFrame(widget, target, nullptr)) {
        *why = QStringLiteral("the frame painter could not be started");
        return false;
    }

    return frameWasProduced(target, why);
}

void resetCounts(const std::vector<CountedProbe*>& probes)
{
    for (CountedProbe* probe : probes)
        probe->resetContentPaintCount();
}

int totalPaints(const std::vector<CountedProbe*>& probes)
{
    int total = 0;
    for (CountedProbe* probe : probes)
        total += probe->contentPaintCount();
    return total;
}

bool allHoldLayers(const std::vector<CountedProbe*>& probes)
{
    for (CountedProbe* probe : probes) {
        if (!probe->view()->isLayerBacked() || !probe->view()->hasLayerSurface())
            return false;
    }
    return true;
}

} // namespace

class TestLayerBenchmarks : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void windowOfComponents();
    void pageOfCharts();
    void revealAnimation();
    void cleanupTestCase();

private:
    void recordRow(const QString& scenario, const QString& config, int iterations,
                   int framesPerIteration, const Stats& stats, double ratio,
                   const QString& verdict);
    bool writeOutputFile();
    [[nodiscard]] QString tableText() const;
    void recordTwoTimesBudget(const QString& scenario, const Stats& uncached,
                              const Stats& clean);

    bool runRevealOnce(CountingView<RevealLineChart>& chart, QImage& target,
                       int* frames, double* totalMs, QString* error);
    bool measureReveal(bool cached, std::vector<double>* samples, int* frames,
                       QString* error);

    QStringList preambleLines_;
    QStringList rowLines_;
    QString outputError_;
};

void TestLayerBenchmarks::initTestCase()
{
    md::view::setLayerDebugUncached(false);

    // Pin the theme and motion so the charts do not depend on a host palette.
    // Motion is *not* reduced: the reveal scenario needs a real run.
    md::Theme theme = md::Theme::baselineLight();
    md::MotionScheme motion = theme.motion();
    motion.reducedMotion = false;
    theme = theme.withMotion(motion);
    md::Theme::setApplicationTheme(theme);
    QApplication::setPalette(theme.toPalette());

    preambleLines_ << QStringLiteral("PERF_META clock=steady_clock iterations=%1 "
                                     "warmup=1 platform=%2")
                          .arg(kIterations)
                          .arg(QGuiApplication::platformName());
    preambleLines_ << QStringLiteral(
        "PERF_SHAPE scenario=windowOfComponents widgets=%1 mix=Button*37,Text*37 "
        "layout=%2x%3 cell=%4x%5 window=%6x%7")
                          .arg(kComponentCount)
                          .arg(kComponentColumns)
                          .arg(kComponentRows)
                          .arg(kComponentCellWidth)
                          .arg(kComponentCellHeight)
                          .arg(kComponentWindowWidth)
                          .arg(kComponentWindowHeight);
    preambleLines_ << QStringLiteral(
        "PERF_SHAPE scenario=pageOfCharts widgets=%1 type=LineChart series=%2 "
        "points=%3 layout=%4x%5 cell=%6x%7 window=%8x%9")
                          .arg(kChartCount)
                          .arg(kRevealSeries)
                          .arg(kRevealPoints)
                          .arg(kChartColumns)
                          .arg(kChartRows)
                          .arg(kChartWidth)
                          .arg(kChartHeight)
                          .arg(kChartWindowWidth)
                          .arg(kChartWindowHeight);
    preambleLines_ << QStringLiteral(
        "PERF_SHAPE scenario=revealAnimation widgets=1 type=LineChartSubclass "
        "series=%1 points=%2 size=%3x%4 duration=Level3 step_ms=%5")
                          .arg(kRevealSeries)
                          .arg(kRevealPoints)
                          .arg(kRevealWidth)
                          .arg(kRevealHeight)
                          .arg(kRevealStepMs);

    std::puts("PERF_TABLE layer-benchmarks v1");
    for (const QString& line : std::as_const(preambleLines_))
        std::puts(line.toUtf8().constData());
    std::fflush(stdout);

    QVERIFY2(md::view::liveLayerCount() == 0,
             "the benchmark must start with an empty process-wide layer cache");
}

void TestLayerBenchmarks::recordRow(const QString& scenario, const QString& config,
                                    int iterations, int framesPerIteration,
                                    const Stats& stats, double ratio,
                                    const QString& verdict)
{
    const QString line = QStringLiteral(
        "PERF_ROW scenario=%1 config=%2 iterations=%3 frames_per_iteration=%4 "
        "mean_ms=%5 median_ms=%6 p95_ms=%7 min_ms=%8 max_ms=%9 ratio_median=%10 "
        "verdict=%11")
        .arg(scenario)
        .arg(config)
        .arg(iterations)
        .arg(framesPerIteration)
        .arg(stats.meanMs, 0, 'f', 4)
        .arg(stats.medianMs, 0, 'f', 4)
        .arg(stats.p95Ms, 0, 'f', 4)
        .arg(stats.minMs, 0, 'f', 4)
        .arg(stats.maxMs, 0, 'f', 4)
        .arg(ratio, 0, 'f', 3)
        .arg(verdict);

    rowLines_.append(line);
    std::puts(line.toUtf8().constData());
    std::fflush(stdout);
    writeOutputFile();
}

QString TestLayerBenchmarks::tableText() const
{
    QStringList lines;
    lines << QStringLiteral("PERF_TABLE layer-benchmarks v1");
    lines += preambleLines_;
    lines += rowLines_;
    lines << QStringLiteral("PERF_TABLE_END");
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

bool TestLayerBenchmarks::writeOutputFile()
{
    outputError_.clear();

    const QString path = qEnvironmentVariable("PERIODICITY_BENCH_OUT");
    if (path.isEmpty())
        return true;

    const QFileInfo info(path);
    const QString directory = info.absolutePath();
    if (!directory.isEmpty() && !QDir().mkpath(directory)) {
        outputError_ = QStringLiteral("could not create the benchmark output "
                                     "directory: %1")
                           .arg(directory);
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        outputError_ = QStringLiteral("could not open PERIODICITY_BENCH_OUT "
                                     "(%1): %2")
                           .arg(path, file.errorString());
        return false;
    }

    const QByteArray bytes = tableText().toUtf8();
    const qint64 written = file.write(bytes);
    file.close();

    if (written != bytes.size()) {
        outputError_ = QStringLiteral("short write to PERIODICITY_BENCH_OUT "
                                     "(%1): %2 of %3 bytes")
                           .arg(path)
                           .arg(written)
                           .arg(bytes.size());
        return false;
    }

    return true;
}

void TestLayerBenchmarks::recordTwoTimesBudget(const QString& scenario,
                                               const Stats& uncached,
                                               const Stats& clean)
{
    const double speedup =
        clean.medianMs > 0.0 ? uncached.medianMs / clean.medianMs : 0.0;

    QVERIFY2(clean.medianMs > 0.0 && speedup >= 2.0,
             qPrintable(QStringLiteral(
                 "%1: the cached clean frame is not 2x cheaper than the uncached "
                 "full paint (uncached median %2 ms, cached median %3 ms, "
                 "speedup %4x). That 2x is the acceptance rule for the layer "
                 "work; the numbers above are the measurement.")
                            .arg(scenario)
                            .arg(uncached.medianMs, 0, 'f', 4)
                            .arg(clean.medianMs, 0, 'f', 4)
                            .arg(speedup, 0, 'f', 3)));
}

void TestLayerBenchmarks::windowOfComponents()
{
    const int baselineLayers = md::view::liveLayerCount();
    {
        HostWindow window;
        window.resize(kComponentWindowWidth, kComponentWindowHeight);

        std::vector<CountedProbe*> probes;
        probes.reserve(kComponentCount);

        for (int index = 0; index < kComponentCount; ++index) {
            const QRect geometry = gridRect(index, kComponentColumns,
                                            kComponentCellWidth,
                                            kComponentCellHeight,
                                            kComponentGap, kComponentMargin);

            if (index % 2 == 0) {
                auto* button = new CountingView<md::ButtonView>(&window);
                button->setText(QStringLiteral("Button %1").arg(index));
                button->setGeometry(geometry);
                probes.push_back(button);
            } else {
                auto* label = new CountingView<md::TextView>(&window);
                label->setText(QStringLiteral("Label %1").arg(index));
                label->setGeometry(geometry);
                probes.push_back(label);
            }
        }

        QCOMPARE(static_cast<int>(probes.size()), kComponentCount);

        window.show();
        QCoreApplication::processEvents();

        QImage target = makeTarget(window);
        QCOMPARE(target.size(), pixelSizeFor(window));

        QString why;
        QVERIFY2(renderFrameChecked(window, target, &why), qPrintable(why));

        // The uncached full paint: every component must run its content hook on
        // every iteration, so a fast number cannot come from skipped children.
        resetCounts(probes);
        std::vector<double> uncachedSamples;
        uncachedSamples.reserve(kIterations);
        for (int iteration = 0; iteration < kIterations; ++iteration) {
            qint64 nanos = 0;
            QVERIFY(renderFrame(window, target, &nanos));
            uncachedSamples.push_back(static_cast<double>(nanos) / 1.0e6);
        }

        for (CountedProbe* probe : probes)
            QCOMPARE(probe->contentPaintCount(), kIterations);
        QCOMPARE(md::view::liveLayerCount(), baselineLayers);

        // Cached. Enabling a layer marks the view fully dirty; the warm-up pass
        // below is the discarded pass that creates and fills every surface.
        for (CountedProbe* probe : probes) {
            probe->view()->setLayerBacked(true);
            QVERIFY(probe->view()->isLayerBacked());
        }

        // The uncached configuration above already painted every probe once per
        // iteration, so the counters carry that history. Reset before measuring the
        // first layered frame, otherwise the assertion tests the earlier run.
        resetCounts(probes);
        QVERIFY(renderFrame(window, target, nullptr));
        for (CountedProbe* probe : probes)
            QVERIFY(probe->view()->hasLayerSurface());
        QCOMPARE(md::view::liveLayerCount(), baselineLayers + kComponentCount);
        for (CountedProbe* probe : probes)
            QCOMPARE(probe->contentPaintCount(), 1);

        // The clean frame: no content hook may run. A missed layer, a cache
        // miss or a fallback to the direct path would all run them.
        resetCounts(probes);
        std::vector<double> cleanSamples;
        cleanSamples.reserve(kIterations);
        for (int iteration = 0; iteration < kIterations; ++iteration) {
            qint64 nanos = 0;
            QVERIFY(renderFrame(window, target, &nanos));
            cleanSamples.push_back(static_cast<double>(nanos) / 1.0e6);
        }

        QCOMPARE(totalPaints(probes), 0);
        QCOMPARE(md::view::liveLayerCount(), baselineLayers + kComponentCount);
        QVERIFY(allHoldLayers(probes));

        QVERIFY2(renderFrameChecked(window, target, &why), qPrintable(why));
        QCOMPARE(totalPaints(probes), 0);

        // One component, one 1-rect damage. The probe colour changes without a
        // request of its own; the explicit requestUpdate(QRectF) is the whole
        // invalidation, so the layer must clip the pass to that rectangle. The
        // first pass is the discarded warm-up for this configuration.
        CountedProbe* damaged = probes.front();
        damaged->setObserveClip(true);
        damaged->setProbeColour(QColor(0x6a, 0x6a, 0x6a));
        damaged->view()->requestUpdate(QRectF(damaged->probeRect()));
        QVERIFY(renderFrame(window, target, nullptr));
        resetCounts(probes);

        std::vector<double> damageSamples;
        damageSamples.reserve(kIterations);
        for (int iteration = 0; iteration < kIterations; ++iteration) {
            resetCounts(probes);
            damaged->setProbeColour(iteration % 2 == 0 ? QColor(0xd4, 0x24, 0x24)
                                                       : QColor(0x1e, 0x4f, 0xd4));
            damaged->view()->requestUpdate(QRectF(damaged->probeRect()));

            qint64 nanos = 0;
            QVERIFY(renderFrame(window, target, &nanos));
            damageSamples.push_back(static_cast<double>(nanos) / 1.0e6);

            QCOMPARE(damaged->contentPaintCount(), 1);
            for (CountedProbe* probe : probes) {
                if (probe != damaged)
                    QCOMPARE(probe->contentPaintCount(), 0);
            }

            const QRect clip = damaged->lastContentClip();
            QVERIFY2(clip.contains(damaged->probeRect()),
                     qPrintable(QStringLiteral("the 1-rect damage was not fully "
                                               "rasterised (clip %1,%2 %3x%4)")
                                    .arg(clip.x())
                                    .arg(clip.y())
                                    .arg(clip.width())
                                    .arg(clip.height())));
            QVERIFY2(clip.width() <= damaged->probeRect().width() + 1
                         && clip.height() <= damaged->probeRect().height() + 1,
                     qPrintable(QStringLiteral("more than the 1-rect damage was "
                                               "rasterised (clip %1x%2 for a %3x%4 "
                                               "rectangle)")
                                    .arg(clip.width())
                                    .arg(clip.height())
                                    .arg(damaged->probeRect().width())
                                    .arg(damaged->probeRect().height())));
        }

        const Stats uncached = statsOf(uncachedSamples);
        const Stats clean = statsOf(cleanSamples);
        const Stats damagedStats = statsOf(damageSamples);
        const double cleanSpeedup =
            clean.medianMs > 0.0 ? uncached.medianMs / clean.medianMs : 0.0;
        const double damageSpeedup = damagedStats.medianMs > 0.0
            ? uncached.medianMs / damagedStats.medianMs
            : 0.0;

        recordRow(QStringLiteral("windowOfComponents"),
                  QStringLiteral("uncached-full"), kIterations, 1, uncached, 1.0,
                  QStringLiteral("reference"));
        recordRow(QStringLiteral("windowOfComponents"),
                  QStringLiteral("cached-clean"), kIterations, 1, clean,
                  cleanSpeedup, QStringLiteral("asserted-2x"));
        recordRow(QStringLiteral("windowOfComponents"),
                  QStringLiteral("cached-one-damage"), kIterations, 1,
                  damagedStats, damageSpeedup, QStringLiteral("reported"));

        recordTwoTimesBudget(QStringLiteral("windowOfComponents"), uncached, clean);
    }

    QCOMPARE(md::view::liveLayerCount(), baselineLayers);
}

void TestLayerBenchmarks::pageOfCharts()
{
    const int baselineLayers = md::view::liveLayerCount();
    {
        HostWindow window;
        window.resize(kChartWindowWidth, kChartWindowHeight);

        std::vector<CountingView<md::LineChartView>*> charts;
        std::vector<CountedProbe*> probes;
        charts.reserve(kChartCount);
        probes.reserve(kChartCount);

        for (int index = 0; index < kChartCount; ++index) {
            auto* chart = new CountingView<md::LineChartView>(&window);
            chart->setGeometry(gridRect(index, kChartColumns, kChartWidth,
                                        kChartHeight, kChartGap, kChartMargin));
            chart->setAnimated(false);
            chart->setInteractionFeedbackEnabled(false);
            chart->setTitle(QStringLiteral("Chart %1").arg(index));
            chart->setSeries(lineSeries(0.0));

            // The first data set must not leave a wall-clock fit animation
            // running: it would move the plot between samples. Auto-fit off
            // settles the range synchronously on the fitted bounds.
            chart->math()->setAutoFit(false);
            charts.push_back(chart);
            probes.push_back(chart);
        }

        window.show();
        QCoreApplication::processEvents();

        QImage target = makeTarget(window);
        QCOMPARE(target.size(), pixelSizeFor(window));

        QString why;
        QVERIFY2(renderFrameChecked(window, target, &why), qPrintable(why));

        resetCounts(probes);
        std::vector<double> uncachedSamples;
        uncachedSamples.reserve(kIterations);
        for (int iteration = 0; iteration < kIterations; ++iteration) {
            qint64 nanos = 0;
            QVERIFY(renderFrame(window, target, &nanos));
            uncachedSamples.push_back(static_cast<double>(nanos) / 1.0e6);
        }

        for (CountedProbe* probe : probes)
            QCOMPARE(probe->contentPaintCount(), kIterations);
        QCOMPARE(md::view::liveLayerCount(), baselineLayers);

        for (CountedProbe* probe : probes) {
            probe->view()->setLayerBacked(true);
            QVERIFY(probe->view()->isLayerBacked());
        }

        // The uncached configuration above already painted every probe once per
        // iteration, so the counters carry that history. Reset before measuring the
        // first layered frame, otherwise the assertion tests the earlier run.
        resetCounts(probes);
        QVERIFY(renderFrame(window, target, nullptr));
        for (CountedProbe* probe : probes)
            QVERIFY(probe->view()->hasLayerSurface());
        QCOMPARE(md::view::liveLayerCount(), baselineLayers + kChartCount);
        for (CountedProbe* probe : probes)
            QCOMPARE(probe->contentPaintCount(), 1);

        resetCounts(probes);
        std::vector<double> cleanSamples;
        cleanSamples.reserve(kIterations);
        for (int iteration = 0; iteration < kIterations; ++iteration) {
            qint64 nanos = 0;
            QVERIFY(renderFrame(window, target, &nanos));
            cleanSamples.push_back(static_cast<double>(nanos) / 1.0e6);
        }

        QCOMPARE(totalPaints(probes), 0);
        QCOMPARE(md::view::liveLayerCount(), baselineLayers + kChartCount);
        QVERIFY(allHoldLayers(probes));

        QVERIFY2(renderFrameChecked(window, target, &why), qPrintable(why));
        QCOMPARE(totalPaints(probes), 0);

        // One chart's data changes. setSeries() invalidates that chart fully,
        // which is the honest damage a data change produces; the other 24
        // layers stay clean. The first pass is the discarded warm-up for this
        // configuration.
        CountingView<md::LineChartView>* damagedChart = charts.at(kChartCount / 2);
        CountedProbe* damaged = damagedChart;
        damaged->setObserveClip(true);
        damagedChart->setSeries(lineSeries(-0.05));
        QVERIFY(renderFrame(window, target, nullptr));
        resetCounts(probes);

        std::vector<double> damageSamples;
        damageSamples.reserve(kIterations);
        for (int iteration = 0; iteration < kIterations; ++iteration) {
            resetCounts(probes);
            damagedChart->setSeries(lineSeries(iteration * 0.05));

            qint64 nanos = 0;
            QVERIFY(renderFrame(window, target, &nanos));
            damageSamples.push_back(static_cast<double>(nanos) / 1.0e6);

            QCOMPARE(damaged->contentPaintCount(), 1);
            for (CountedProbe* probe : probes) {
                if (probe != damaged)
                    QCOMPARE(probe->contentPaintCount(), 0);
            }

            const QRect clip = damaged->lastContentClip();
            QVERIFY2(clip.width() >= damaged->view()->width() - 1
                         && clip.height() >= damaged->view()->height() - 1,
                     qPrintable(QStringLiteral("a data change must repaint the "
                                               "whole chart layer (clip %1x%2 for "
                                               "a %3x%4 chart)")
                                    .arg(clip.width())
                                    .arg(clip.height())
                                    .arg(damaged->view()->width())
                                    .arg(damaged->view()->height())));
        }

        const Stats uncached = statsOf(uncachedSamples);
        const Stats clean = statsOf(cleanSamples);
        const Stats damagedStats = statsOf(damageSamples);
        const double cleanSpeedup =
            clean.medianMs > 0.0 ? uncached.medianMs / clean.medianMs : 0.0;
        const double damageSpeedup = damagedStats.medianMs > 0.0
            ? uncached.medianMs / damagedStats.medianMs
            : 0.0;

        recordRow(QStringLiteral("pageOfCharts"),
                  QStringLiteral("uncached-full"), kIterations, 1, uncached, 1.0,
                  QStringLiteral("reference"));
        recordRow(QStringLiteral("pageOfCharts"),
                  QStringLiteral("cached-clean"), kIterations, 1, clean,
                  cleanSpeedup, QStringLiteral("asserted-2x"));
        recordRow(QStringLiteral("pageOfCharts"),
                  QStringLiteral("cached-one-data-change"), kIterations, 1,
                  damagedStats, damageSpeedup, QStringLiteral("reported"));

        recordTwoTimesBudget(QStringLiteral("pageOfCharts"), uncached, clean);
    }

    QCOMPARE(md::view::liveLayerCount(), baselineLayers);
}

bool TestLayerBenchmarks::runRevealOnce(CountingView<RevealLineChart>& chart,
                                        QImage& target, int* frames,
                                        double* totalMs, QString* error)
{
    chart.resetContentPaintCount();
    chart.startBenchmarkReveal();

    if (!chart.isBenchmarkRevealing()) {
        *error = QStringLiteral("the reveal did not start");
        return false;
    }
    if (chart.benchmarkRevealProgress() != 0.0) {
        *error = QStringLiteral("the reveal did not start at progress 0");
        return false;
    }

    int painted = 0;
    target.fill(Qt::transparent);

    const auto started = std::chrono::steady_clock::now();
    if (!renderFrame(chart, target, nullptr, false)) {
        *error = QStringLiteral("the first reveal frame could not be rendered");
        return false;
    }
    ++painted;

    while (chart.isBenchmarkRevealing()) {
        if (painted >= kRevealSafetyFrameCap) {
            *error = QStringLiteral("the reveal did not settle within %1 frames")
                         .arg(kRevealSafetyFrameCap);
            return false;
        }

        chart.advanceBenchmarkReveal(std::chrono::milliseconds(kRevealStepMs));

        // The chart's value callback has already recorded the whole-chart
        // damage this frame; the render below is the frame itself. Both the
        // cached and the uncached run pay the same invalidation.
        if (!renderFrame(chart, target, nullptr, false)) {
            *error = QStringLiteral("a reveal frame could not be rendered");
            return false;
        }
        ++painted;
    }

    const auto finished = std::chrono::steady_clock::now();
    *frames = painted;
    *totalMs = std::chrono::duration<double, std::milli>(finished - started).count();

    if (chart.benchmarkRevealProgress() != 1.0) {
        *error = QStringLiteral("the reveal ended at progress %1, not 1")
                     .arg(chart.benchmarkRevealProgress());
        return false;
    }
    if (chart.contentPaintCount() != painted) {
        *error = QStringLiteral("the reveal painted %1 frames but ran %2 content "
                                "hooks")
                     .arg(painted)
                     .arg(chart.contentPaintCount());
        return false;
    }

    return true;
}

bool TestLayerBenchmarks::measureReveal(bool cached, std::vector<double>* samples,
                                        int* frames, QString* error)
{
    const int baselineLayers = md::view::liveLayerCount();
    {
        CountingView<RevealLineChart> chart;
        chart.resize(kRevealWidth, kRevealHeight);
        chart.setTitle(QStringLiteral("Reveal"));
        chart.setInteractionFeedbackEnabled(false);
        chart.setSeries(lineSeries(0.0));
        chart.math()->setAutoFit(false);
        chart.animation().setDuration(md::MotionLevel::Level3);

        if (cached)
            chart.setLayerBacked(true);

        chart.show();
        QCoreApplication::processEvents();

        QImage target = makeTarget(chart);
        if (target.isNull()) {
            *error = QStringLiteral("the reveal target image is null");
            return false;
        }

        // Build the static layer (and, when cached, the retained layer) with a
        // settled frame before the discarded warm-up run.
        if (!renderFrame(chart, target, nullptr)) {
            *error = QStringLiteral("the reveal warm-up frame could not be rendered");
            return false;
        }

        if (cached) {
            if (!chart.hasLayerSurface()
                || md::view::liveLayerCount() != baselineLayers + 1) {
                *error = QStringLiteral("the cached reveal did not acquire exactly "
                                        "one layer");
                return false;
            }
        } else if (chart.hasLayerSurface()
                   || md::view::liveLayerCount() != baselineLayers) {
            *error = QStringLiteral("the uncached reveal acquired a layer");
            return false;
        }

        int warmFrames = 0;
        double warmMs = 0.0;
        if (!runRevealOnce(chart, target, &warmFrames, &warmMs, error))
            return false;
        if (warmFrames < 2) {
            *error = QStringLiteral("the reveal settled in %1 frame; it is not "
                                    "animating")
                         .arg(warmFrames);
            return false;
        }

        samples->clear();
        samples->reserve(kIterations);
        int expectedFrames = -1;
        for (int iteration = 0; iteration < kIterations; ++iteration) {
            int runFrames = 0;
            double runMs = 0.0;
            if (!runRevealOnce(chart, target, &runFrames, &runMs, error))
                return false;

            if (expectedFrames < 0)
                expectedFrames = runFrames;
            else if (runFrames != expectedFrames) {
                *error = QStringLiteral("the deterministic reveal produced %1 "
                                        "frames in one run and %2 in another")
                             .arg(runFrames)
                             .arg(expectedFrames);
                return false;
            }

            samples->push_back(runMs);
        }

        *frames = expectedFrames;

        // The run is over: the chart is clean, so the cached path must not run
        // a content hook on a frame that changes nothing. This is the same hit
        // assertion the two page scenarios make, applied to the settled reveal.
        chart.resetContentPaintCount();
        if (!renderFrame(chart, target, nullptr)) {
            *error = QStringLiteral("the settled reveal frame could not be rendered");
            return false;
        }
        if (cached && chart.contentPaintCount() != 0) {
            *error = QStringLiteral("a settled cached reveal ran %1 content hooks")
                         .arg(chart.contentPaintCount());
            return false;
        }
        if (!cached && chart.contentPaintCount() != 1) {
            *error = QStringLiteral("a settled uncached reveal ran %1 content "
                                    "hooks; expected 1")
                         .arg(chart.contentPaintCount());
            return false;
        }

        if (cached && !chart.hasLayerSurface()) {
            *error = QStringLiteral("the cached reveal lost its layer");
            return false;
        }

        // The settled frame is the frame a user sees at rest: it must not be an
        // empty paint. This is the reveal's version of the "frame was actually
        // produced" check the two page scenarios make.
        QString why;
        if (!frameWasProduced(target, &why)) {
            *error = QStringLiteral("the settled reveal frame is not a produced "
                                    "frame: %1")
                         .arg(why);
            return false;
        }
    }

    if (md::view::liveLayerCount() != baselineLayers) {
        *error = QStringLiteral("the reveal leaked %1 layers")
                     .arg(md::view::liveLayerCount() - baselineLayers);
        return false;
    }

    return true;
}

void TestLayerBenchmarks::revealAnimation()
{
    std::vector<double> uncachedSamples;
    std::vector<double> cachedSamples;
    int uncachedFrames = 0;
    int cachedFrames = 0;
    QString error;

    const bool uncachedOk =
        measureReveal(false, &uncachedSamples, &uncachedFrames, &error);
    QVERIFY2(uncachedOk, qPrintable(error));

    error.clear();
    const bool cachedOk =
        measureReveal(true, &cachedSamples, &cachedFrames, &error);
    QVERIFY2(cachedOk, qPrintable(error));

    QCOMPARE(cachedFrames, uncachedFrames);

    const Stats uncached = statsOf(uncachedSamples);
    const Stats cached = statsOf(cachedSamples);
    const double speedup =
        cached.medianMs > 0.0 ? uncached.medianMs / cached.medianMs : 0.0;

    recordRow(QStringLiteral("revealAnimation"),
              QStringLiteral("uncached-reveal"), kIterations, uncachedFrames,
              uncached, 1.0, QStringLiteral("reference"));
    recordRow(QStringLiteral("revealAnimation"), QStringLiteral("cached-reveal"),
              kIterations, cachedFrames, cached, speedup,
              QStringLiteral("reported"));
}

void TestLayerBenchmarks::cleanupTestCase()
{
    const bool written = writeOutputFile();
    QVERIFY2(written, qPrintable(outputError_));

    std::puts("PERF_TABLE_END");
    std::fflush(stdout);

    md::view::setLayerDebugUncached(false);
}

int main(int argc, char* argv[])
{
    // The benchmarks are offscreen by contract (deterministic, no window
    // system). ctest sets the platform too; this makes a direct run of the
    // executable behave the same unless the caller chose a platform.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication application(argc, argv);
    TestLayerBenchmarks test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_layer_benchmarks.moc"
