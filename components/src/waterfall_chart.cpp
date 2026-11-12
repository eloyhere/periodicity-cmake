// Periodicity — the WaterfallChart component.
//
// ChartBase has already painted the frequency/level frame, so this file only
// maps the stacked spectra and their depth offsets into plotRect(). The
// painter's algorithm is the whole 3D cue: traces are drawn oldest first, each
// one lighter and thinner than the trace in front of it, and the newest trace
// sits on the shared level axis. A translucent baseline under every trace makes
// the spacing legible when spectra overlap.
//
// Every length comes from Density::dp(), every colour from the series palette
// or a theme role, and the whole data pass is clipped to the plot rectangle so
// a full-scale trace cannot paint over the axis labels.

#include <md/components/waterfall_chart.h>

#include <md/theme/theme.h>

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace md {
namespace {

constexpr qreal kAmplitudeHeadroom = 0.94;
constexpr qreal kMinimumFillAlpha = 0.05;
constexpr qreal kMaximumFillAlpha = 0.14;
constexpr qreal kMinimumStrokeAlpha = 0.30;
constexpr qreal kMaximumStrokeAlpha = 1.0;
constexpr qreal kNewestStrokeDp = 1.8;
constexpr qreal kOldestStrokeDp = 1.0;
constexpr qreal kBaselineAlpha = 0.35;
constexpr qreal kHitSlopDp = 6.0;
constexpr qreal kMinimumHitSlopPx = 4.0;

[[nodiscard]] qreal emptyValue() noexcept
{
    return std::numeric_limits<qreal>::quiet_NaN();
}

class PainterStateGuard
{
public:
    explicit PainterStateGuard(QPainter& painter) : painter_(painter) { painter_.save(); }
    ~PainterStateGuard() { painter_.restore(); }

    PainterStateGuard(const PainterStateGuard&) = delete;
    PainterStateGuard& operator=(const PainterStateGuard&) = delete;

private:
    QPainter& painter_;
};

} // namespace

WaterfallChart::WaterfallChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);

    // A waterfall has no per-series legend row: every trace is the same signal
    // at another instant, and the depth grade already communicates the order.
    setLegendVisible(false);

    // A waterfall is a read-only data surface; the shared interaction layer
    // would tint every trace while the pointer rests on the chart.
    setInteractionFeedbackEnabled(false);

    math()->setXAxisLabel(localized(QStringLiteral("chart.waterfall.frequency"),
                                    QStringLiteral("Frequency (Hz)")));
    math()->setYAxisLabel(localized(QStringLiteral("chart.waterfall.level"),
                                    QStringLiteral("Level (dB)")));
    math()->setTickCount(6);

    updateViewport();
}

void WaterfallChart::setTraces(std::vector<std::vector<double>> traces)
{
    traces_ = std::move(traces);
    syncPresenceSeries();

    // The new stack may change the bin count and therefore the useful tick
    // density, but the axis domains themselves do not move.
    requestUpdate();
}

const std::vector<std::vector<double>>& WaterfallChart::traces() const noexcept
{
    return traces_;
}

int WaterfallChart::traceCount() const noexcept
{
    return static_cast<int>(traces_.size());
}

int WaterfallChart::binCount() const noexcept
{
    size_t count = 0;
    for (const std::vector<double>& trace : traces_)
        count = std::max(count, trace.size());

    return static_cast<int>(count);
}

void WaterfallChart::setSampleRate(double sampleRate)
{
    if (!std::isfinite(sampleRate) || !(sampleRate > 0.0))
        return;

    if (sampleRate_ == sampleRate)
        return;

    sampleRate_ = sampleRate;
    updateViewport();
    requestUpdate();
}

double WaterfallChart::sampleRate() const noexcept
{
    return sampleRate_;
}

double WaterfallChart::nyquistFrequency() const noexcept
{
    if (!(sampleRate_ > 0.0))
        return 0.0;

    return sampleRate_ * 0.5;
}

void WaterfallChart::setDbRange(double minimumDb, double maximumDb)
{
    if (!std::isfinite(minimumDb) || !std::isfinite(maximumDb))
        return;

    double low = std::min(minimumDb, maximumDb);
    double high = std::max(minimumDb, maximumDb);
    if (!(high > low)) {
        low -= 0.5;
        high += 0.5;
    }

    if (dbMinimum_ == low && dbMaximum_ == high)
        return;

    dbMinimum_ = low;
    dbMaximum_ = high;
    updateViewport();
    requestUpdate();
}

double WaterfallChart::minimumDb() const noexcept
{
    return dbMinimum_;
}

double WaterfallChart::maximumDb() const noexcept
{
    return dbMaximum_;
}

void WaterfallChart::setDepth(qreal fraction)
{
    if (!std::isfinite(fraction))
        return;

    const qreal clamped = std::clamp(fraction, 0.0, 0.9);
    if (depth_ == clamped)
        return;

    depth_ = clamped;
    requestUpdate();
}

qreal WaterfallChart::depth() const noexcept
{
    return depth_;
}

QVector<qreal> WaterfallChart::frequencyTicks() const
{
    return math()->xTicks();
}

QVector<qreal> WaterfallChart::levelTicks() const
{
    return math()->yTicks();
}

qreal WaterfallChart::baselineFor(const QRectF& plot, int traceIndex) const
{
    const int count = traceCount();
    if (count <= 1)
        return plot.bottom();

    const qreal span = plot.height() * std::clamp(depth_, 0.0, 0.9);
    const qreal step = span / static_cast<qreal>(count - 1);
    const int index = std::clamp(traceIndex, 0, count - 1);

    return plot.bottom() - static_cast<qreal>(count - 1 - index) * step;
}

qreal WaterfallChart::amplitudeHeightFor(const QRectF& plot) const
{
    return plot.height() * (1.0 - std::clamp(depth_, 0.0, 0.9))
        * kAmplitudeHeadroom;
}

qreal WaterfallChart::traceY(const QRectF& plot, int traceIndex, double levelDb,
                             qreal reveal) const
{
    const double span = dbMaximum_ - dbMinimum_;
    const qreal fraction = span > 0.0
        ? static_cast<qreal>(std::clamp((levelDb - dbMinimum_) / span, 0.0, 1.0))
        : 0.0;
    const qreal amplitude = amplitudeHeightFor(plot) * fraction
        * std::clamp(reveal, 0.0, 1.0);

    return baselineFor(plot, traceIndex) - amplitude;
}

qreal WaterfallChart::levelAt(int traceIndex, int bin) const
{
    if (traceIndex < 0 || traceIndex >= traceCount())
        return emptyValue();

    const std::vector<double>& trace = traces_.at(static_cast<size_t>(traceIndex));
    if (bin < 0 || static_cast<size_t>(bin) >= trace.size())
        return emptyValue();

    const double value = trace.at(static_cast<size_t>(bin));
    return std::isfinite(value) ? static_cast<qreal>(value) : emptyValue();
}

QPointF WaterfallChart::tracePoint(int traceIndex, int bin) const
{
    const int bins = traceIndex >= 0 && traceIndex < traceCount()
        ? static_cast<int>(traces_.at(static_cast<size_t>(traceIndex)).size())
        : 0;
    if (bins <= 0 || bin < 0 || bin >= bins)
        return QPointF();

    const qreal level = levelAt(traceIndex, bin);
    if (!std::isfinite(level))
        return QPointF();

    const QRectF plot = plotRect();
    if (plot.isEmpty())
        return QPointF();

    const qreal x = bins == 1
        ? plot.center().x()
        : plot.left() + static_cast<qreal>(bin) / static_cast<qreal>(bins - 1) * plot.width();

    return QPointF(x, traceY(plot, traceIndex, static_cast<double>(level), 1.0));
}

bool WaterfallChart::traceAt(QPointF position, int* traceIndex, double* frequency,
                             double* level) const
{
    if (traceIndex != nullptr)
        *traceIndex = -1;

    if (!std::isfinite(position.x()) || !std::isfinite(position.y()))
        return false;

    const QRectF plot = plotRect();
    if (plot.isEmpty() || !plot.contains(position))
        return false;

    const double top = std::max(nyquistFrequency(), 1.0);
    const int count = traceCount();
    const qreal slop = std::max<qreal>(
        kMinimumHitSlopPx, Theme::forWidget(this).density().dp(kHitSlopDp));

    qreal nearest = slop;
    int nearestTrace = -1;
    int nearestBin = -1;

    for (int index = 0; index < count; ++index) {
        const int bins = static_cast<int>(traces_.at(static_cast<size_t>(index)).size());
        if (bins <= 0)
            continue;

        const qreal fraction = std::clamp(
            (position.x() - plot.left()) / plot.width(), 0.0, 1.0);
        const int bin = bins == 1
            ? 0
            : std::clamp(static_cast<int>(std::lround(
                             static_cast<double>(fraction) * static_cast<double>(bins - 1))),
                         0, bins - 1);

        const qreal traceLevel = levelAt(index, bin);
        if (!std::isfinite(traceLevel))
            continue;

        const qreal y = traceY(plot, index, static_cast<double>(traceLevel), 1.0);
        const qreal distance = std::abs(y - position.y());
        if (distance < nearest) {
            nearest = distance;
            nearestTrace = index;
            nearestBin = bin;
        }
    }

    if (nearestTrace < 0)
        return false;

    if (traceIndex != nullptr)
        *traceIndex = nearestTrace;
    if (frequency != nullptr) {
        const qreal fraction = std::clamp(
            (position.x() - plot.left()) / plot.width(), 0.0, 1.0);
        *frequency = static_cast<double>(fraction) * top;
    }
    if (level != nullptr)
        *level = static_cast<double>(levelAt(nearestTrace, nearestBin));

    return true;
}

bool WaterfallChart::hasLevels() const noexcept
{
    for (const std::vector<double>& trace : traces_) {
        for (const double value : trace) {
            if (std::isfinite(value))
                return true;
        }
    }

    return false;
}

void WaterfallChart::syncPresenceSeries()
{
    QVector<ChartSeries> presence;

    if (hasLevels()) {
        ChartSeries marker;
        marker.points.append(QPointF(0.0, 0.0));
        presence.append(marker);
    }

    ChartBase::setSeries(std::move(presence));
}

void WaterfallChart::updateViewport()
{
    const double top = std::max(nyquistFrequency(), 1.0);

    math()->setAutoFit(false);
    math()->setDataBounds(QPointF(0.0, dbMinimum_), QPointF(top, dbMaximum_));
    math()->setVisibleBounds(QPointF(0.0, dbMinimum_), QPointF(top, dbMaximum_));
}

qreal WaterfallChart::revealProgress() const noexcept
{
    return animation().progress();
}

bool WaterfallChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

void WaterfallChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    paintPlot(painter, plot, 1.0);
}

void WaterfallChart::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    if (plot.isEmpty())
        return;

    const PainterStateGuard guard(painter);

    try {
        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();
        const int count = traceCount();
        if (count <= 0)
            return;

        const QColor colour = seriesColour(0);
        const QColor effective = colour.isValid() ? colour : colours.primary;

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.save();
        painter.setClipRect(plot);

        const qreal depthSpan = plot.height() * std::clamp(depth_, 0.0, 0.9);
        const qreal baselineStep = count > 1
            ? depthSpan / static_cast<qreal>(count - 1)
            : 0.0;

        // 1. Baselines, when the stack is deep enough for them to read as
        //    separate planes rather than as a haze.
        if (baselineStep >= density.dp(3.0)) {
            QColor baseline = colours.outlineVariant;
            baseline.setAlphaF(static_cast<float>(kBaselineAlpha));
            painter.setPen(QPen(baseline, std::max<qreal>(1.0, density.dp(1.0)),
                                Qt::DashLine));
            painter.setBrush(Qt::NoBrush);

            for (int index = 0; index < count; ++index) {
                const qreal y = baselineFor(plot, index);
                painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            }
        }

        // 2. The spectra, oldest first. The newest trace is painted last and
        //    covers the ones behind it, which is the painter's algorithm.
        for (int index = 0; index < count; ++index) {
            const std::vector<double>& trace = traces_.at(static_cast<size_t>(index));
            const int bins = static_cast<int>(trace.size());
            if (bins <= 0)
                continue;

            const qreal local = std::clamp(
                progress * (static_cast<qreal>(count) + 1.0) - static_cast<qreal>(index),
                0.0, 1.0);
            if (local <= 0.0)
                continue;

            const qreal rank = count > 1
                ? static_cast<qreal>(index) / static_cast<qreal>(count - 1)
                : 1.0;

            QColor stroke = effective;
            stroke.setAlphaF(static_cast<float>(
                kMinimumStrokeAlpha + (kMaximumStrokeAlpha - kMinimumStrokeAlpha) * rank));

            QColor fill = effective;
            fill.setAlphaF(static_cast<float>(
                kMinimumFillAlpha + (kMaximumFillAlpha - kMinimumFillAlpha) * rank));

            QPen pen(stroke, std::max<qreal>(
                density.dp(kOldestStrokeDp),
                density.dp(kOldestStrokeDp)
                    + (density.dp(kNewestStrokeDp) - density.dp(kOldestStrokeDp)) * rank));
            pen.setJoinStyle(Qt::RoundJoin);
            pen.setCapStyle(Qt::RoundCap);

            const qreal baseline = baselineFor(plot, index);

            // A spectrum may contain gaps; each run is stroked and filled on its
            // own so a gap does not close across the missing bins.
            QVector<QPointF> run;

            const auto emitRun = [&](QVector<QPointF>& points) {
                if (points.isEmpty())
                    return;
                if (points.size() == 1) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(stroke);
                    painter.drawEllipse(points.first(), density.dp(1.5),
                                        density.dp(1.5));
                    points.clear();
                    return;
                }

                QPainterPath path(points.first());
                for (qsizetype at = 1; at < points.size(); ++at)
                    path.lineTo(points.at(at));

                QPainterPath area = path;
                area.lineTo(points.last().x(), baseline);
                area.lineTo(points.first().x(), baseline);
                area.closeSubpath();

                painter.setPen(Qt::NoPen);
                painter.setBrush(fill);
                painter.drawPath(area);

                painter.setBrush(Qt::NoBrush);
                painter.setPen(pen);
                painter.drawPath(path);
                points.clear();
            };

            for (int bin = 0; bin < bins; ++bin) {
                const qreal traceLevel = levelAt(index, bin);
                if (!std::isfinite(traceLevel)) {
                    emitRun(run);
                    continue;
                }

                const qreal x = bins == 1
                    ? plot.center().x()
                    : plot.left() + static_cast<qreal>(bin)
                        / static_cast<qreal>(bins - 1) * plot.width();
                run.append(QPointF(
                    x, traceY(plot, index, static_cast<double>(traceLevel), local)));
            }

            emitRun(run);
        }

        painter.restore();
    } catch (const std::exception& error) {
        qWarning("md::WaterfallChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::WaterfallChart::paintPlot failed with an unknown exception");
    }
}

} // namespace md
