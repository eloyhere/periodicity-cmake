// Periodicity — the AutocorrelationChart component.
//
// ChartBase paints the shared frame, so this file paints three layers inside
// plotRect(): the significance band, the zero baseline and the stems. The band
// and the baseline are frame-like and stay at full opacity during the reveal;
// the stems grow from the baseline with the reveal progress.
//
// The sequence is installed as the component's data series (lag, value), which
// is what selects paintPlot() over the empty state and what makes
// ChartBase::hitTest() work without a second hit-test implementation. stemAt()
// is the component-specific hit test: it resolves the nearest tip and reports
// the lag in the current axis unit.

#include <md/components/autocorrelation_chart.h>

#include <md/theme/theme.h>

#include <QColor>
#include <QPainter>
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

constexpr qreal kThresholdFillAlpha = 0.08;
constexpr qreal kThresholdEdgeAlpha = 0.45;
constexpr qreal kZeroLineAlpha = 0.60;
constexpr qreal kStemWidthDp = 1.2;
constexpr qreal kMarkerRadiusDp = 2.0;
constexpr qreal kMarkerSpacingDp = 3.0;
constexpr qreal kHitSlopDp = 6.0;
constexpr qreal kMinimumHitSlopPx = 4.0;
constexpr int kAutomaticLagLimit = 512;

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

AutocorrelationChart::AutocorrelationChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);

    // A correlation plot is a read-only data surface; the shared interaction
    // layer would tint the stems while the pointer rests on the chart.
    setInteractionFeedbackEnabled(false);

    math()->setYAxisLabel(localized(QStringLiteral("chart.autocorrelation.value"),
                                    QStringLiteral("Correlation")));
    math()->setTickCount(6);

    updateViewport();
    rebuildSeries();
}

void AutocorrelationChart::setValues(std::vector<double> values)
{
    values_ = std::move(values);
    derivedFromSamples_ = false;
    rebuildSeries();
    updateViewport();
    requestUpdate();
}

const std::vector<double>& AutocorrelationChart::values() const noexcept
{
    return values_;
}

void AutocorrelationChart::setSamples(std::vector<double> samples, double sampleRate)
{
    if (std::isfinite(sampleRate) && sampleRate > 0.0)
        sampleRate_ = sampleRate;

    samples_ = std::move(samples);
    derivedFromSamples_ = true;
    recomputeFromSamples();
    rebuildSeries();
    updateViewport();
    requestUpdate();
}

const std::vector<double>& AutocorrelationChart::samples() const noexcept
{
    return samples_;
}

double AutocorrelationChart::sampleRate() const noexcept
{
    return sampleRate_;
}

void AutocorrelationChart::setMaxLag(int maxLag)
{
    const int clamped = std::max(0, maxLag);
    if (maxLag_ == clamped)
        return;

    maxLag_ = clamped;

    if (derivedFromSamples_ && !samples_.empty()) {
        recomputeFromSamples();
        rebuildSeries();
        updateViewport();
        requestUpdate();
    }
}

int AutocorrelationChart::maxLag() const noexcept
{
    return maxLag_;
}

int AutocorrelationChart::effectiveMaxLag() const noexcept
{
    return values_.empty() ? 0 : static_cast<int>(values_.size()) - 1;
}

void AutocorrelationChart::setThreshold(qreal threshold)
{
    if (!std::isfinite(threshold))
        return;

    const qreal clamped = std::clamp(threshold, 0.0, 1.0);
    if (threshold_ == clamped)
        return;

    threshold_ = clamped;
    rebuildSeries();
    requestUpdate();
}

qreal AutocorrelationChart::threshold() const noexcept
{
    return threshold_;
}

void AutocorrelationChart::setLagUnit(LagUnit unit)
{
    if (lagUnit_ == unit)
        return;

    lagUnit_ = unit;
    rebuildSeries();
    updateViewport();
    requestUpdate();
}

AutocorrelationChart::LagUnit AutocorrelationChart::lagUnit() const noexcept
{
    return lagUnit_;
}

double AutocorrelationChart::lagScale() const noexcept
{
    if (lagUnit_ == LagUnit::Milliseconds && sampleRate_ > 0.0)
        return 1000.0 / sampleRate_;

    return 1.0;
}

double AutocorrelationChart::lagForIndex(int index) const
{
    if (index < 0)
        return 0.0;

    return static_cast<double>(index) * lagScale();
}

QVector<qreal> AutocorrelationChart::lagTicks() const
{
    return math()->xTicks();
}

QVector<qreal> AutocorrelationChart::valueTicks() const
{
    return math()->yTicks();
}

QString AutocorrelationChart::lagAxisLabel() const
{
    return math()->xAxisLabel();
}

void AutocorrelationChart::recomputeFromSamples()
{
    values_.clear();

    std::vector<double> clean;
    clean.reserve(samples_.size());
    for (const double value : samples_) {
        if (std::isfinite(value))
            clean.push_back(value);
    }

    const qsizetype count = static_cast<qsizetype>(clean.size());
    if (count <= 0)
        return;

    double mean = 0.0;
    for (const double value : clean)
        mean += value;
    mean /= static_cast<double>(count);

    std::vector<double> centred(clean.size(), 0.0);
    double energy = 0.0;
    for (size_t index = 0; index < clean.size(); ++index) {
        centred[index] = clean[index] - mean;
        energy += centred[index] * centred[index];
    }

    const int limit = maxLag_ > 0
        ? std::min(maxLag_, static_cast<int>(count) - 1)
        : std::min(static_cast<int>(count) - 1, kAutomaticLagLimit);

    values_.resize(static_cast<size_t>(limit) + 1, 0.0);

    if (!(energy > 0.0)) {
        // A constant signal has no variance; report perfect self-similarity at
        // lag zero and no correlation at any other lag.
        values_.at(0) = 1.0;
        return;
    }

    for (int lag = 0; lag <= limit; ++lag) {
        double sum = 0.0;
        const qsizetype length = count - lag;
        for (qsizetype index = 0; index < length; ++index) {
            sum += centred.at(static_cast<size_t>(index))
                * centred.at(static_cast<size_t>(index) + static_cast<size_t>(lag));
        }
        values_.at(static_cast<size_t>(lag)) = sum / energy;
    }
}

void AutocorrelationChart::updateViewport()
{
    const int count = static_cast<int>(values_.size());
    const double scale = lagScale();
    const double maximumX = count > 1
        ? static_cast<double>(count - 1) * scale
        : 1.0;

    math()->setAutoFit(false);
    math()->setDataBounds(QPointF(0.0, -1.0), QPointF(maximumX, 1.0));
    math()->setVisibleBounds(QPointF(0.0, -1.0), QPointF(maximumX, 1.0));

    math()->setXAxisLabel(lagUnit_ == LagUnit::Milliseconds
        ? localized(QStringLiteral("chart.autocorrelation.lag.ms"),
                    QStringLiteral("Lag (ms)"))
        : localized(QStringLiteral("chart.autocorrelation.lag.samples"),
                    QStringLiteral("Lag (samples)")));
}

void AutocorrelationChart::rebuildSeries()
{
    QVector<ChartSeries> entries;

    if (!values_.empty()) {
        ChartSeries correlation;
        correlation.name = QStringLiteral("Autocorrelation");
        correlation.points.reserve(static_cast<qsizetype>(values_.size()));

        for (size_t index = 0; index < values_.size(); ++index) {
            correlation.points.append(QPointF(
                lagForIndex(static_cast<int>(index)),
                std::clamp(values_.at(index), -1.0, 1.0)));
        }

        entries.append(correlation);

        if (threshold_ > 0.0) {
            ChartSeries band;
            band.name = QStringLiteral("Threshold");
            entries.append(band);
        }
    }

    ChartBase::setSeries(std::move(entries));
}

QPointF AutocorrelationChart::stemPoint(int index) const
{
    if (index < 0 || index >= static_cast<int>(values_.size()))
        return QPointF();

    const QRectF plot = plotRect();
    if (plot.isEmpty())
        return QPointF();

    const int count = static_cast<int>(values_.size());
    const double maximumX = count > 1
        ? static_cast<double>(count - 1) * lagScale()
        : 1.0;
    const double lag = lagForIndex(index);
    const qreal x = plot.left() + (maximumX > 0.0
        ? static_cast<qreal>(lag / maximumX) * plot.width()
        : 0.0);
    const qreal y = math()->toPixel(QPointF(
        lag, std::clamp(values_.at(static_cast<size_t>(index)), -1.0, 1.0))).y();

    return QPointF(x, y);
}

bool AutocorrelationChart::stemAt(QPointF position, int* index, double* lag,
                                  double* value) const
{
    if (!std::isfinite(position.x()) || !std::isfinite(position.y()))
        return false;

    const QRectF plot = plotRect();
    if (plot.isEmpty() || !plot.contains(position) || values_.empty())
        return false;

    const int count = static_cast<int>(values_.size());
    const double maximumX = count > 1
        ? static_cast<double>(count - 1) * lagScale()
        : 1.0;
    const double fraction = std::clamp(
        (position.x() - plot.left()) / plot.width(), 0.0, 1.0);
    const double lagValue = fraction * maximumX;
    const double scale = lagScale();

    const int nearestIndex = scale > 0.0
        ? std::clamp(static_cast<int>(std::lround(lagValue / scale)), 0, count - 1)
        : 0;

    const QPointF tip = stemPoint(nearestIndex);
    const qreal slop = std::max<qreal>(
        kMinimumHitSlopPx, Theme::forWidget(this).density().dp(kHitSlopDp));
    const qreal dx = tip.x() - position.x();
    const qreal dy = tip.y() - position.y();

    if (std::hypot(dx, dy) > slop)
        return false;

    if (index != nullptr)
        *index = nearestIndex;
    if (lag != nullptr)
        *lag = lagForIndex(nearestIndex);
    if (value != nullptr)
        *value = values_.at(static_cast<size_t>(nearestIndex));

    return true;
}

qreal AutocorrelationChart::revealProgress() const noexcept
{
    return animation().progress();
}

bool AutocorrelationChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

void AutocorrelationChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    paintPlot(painter, plot, 1.0);
}

void AutocorrelationChart::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    if (plot.isEmpty() || values_.empty())
        return;

    const PainterStateGuard guard(painter);

    try {
        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();
        const QVector<QColor> palette = chartPalette();

        painter.setRenderHint(QPainter::Antialiasing, true);

        // 1. The significance band, behind everything else.
        if (threshold_ > 0.0) {
            const qreal upper = math()->toPixel(QPointF(0.0, threshold_)).y();
            const qreal lower = math()->toPixel(QPointF(0.0, -threshold_)).y();

            QColor fill = colours.primary;
            fill.setAlphaF(static_cast<float>(kThresholdFillAlpha));
            painter.fillRect(QRectF(plot.left(), std::min(upper, lower),
                                    plot.width(), std::abs(lower - upper)),
                             fill);

            QColor edge = colours.primary;
            edge.setAlphaF(static_cast<float>(kThresholdEdgeAlpha));
            painter.setPen(QPen(edge, std::max<qreal>(1.0, density.dp(1.0)),
                                Qt::DashLine));
            painter.drawLine(QPointF(plot.left(), upper), QPointF(plot.right(), upper));
            painter.drawLine(QPointF(plot.left(), lower), QPointF(plot.right(), lower));
        }

        const qreal zeroY = math()->toPixel(QPointF(0.0, 0.0)).y();

        // 2. Zero baseline.
        QColor zeroColour = colours.onSurfaceVariant;
        zeroColour.setAlphaF(static_cast<float>(kZeroLineAlpha));
        painter.setPen(QPen(zeroColour, std::max<qreal>(1.0, density.dp(1.0))));
        painter.drawLine(QPointF(plot.left(), zeroY), QPointF(plot.right(), zeroY));

        // 3. Stems, grown from the baseline by the reveal progress.
        const QColor colour = seriesColour(0).isValid()
            ? seriesColour(0)
            : (palette.isEmpty() ? colours.primary : palette.at(0));

        const int count = static_cast<int>(values_.size());
        const double maximumX = count > 1
            ? static_cast<double>(count - 1) * lagScale()
            : 1.0;
        const qreal spacing = count > 1
            ? plot.width() / static_cast<qreal>(count - 1)
            : plot.width();
        const bool drawMarkers = spacing >= density.dp(kMarkerSpacingDp);
        const qreal reveal = std::clamp(progress, 0.0, 1.0);

        painter.save();
        painter.setClipRect(plot.adjusted(-2.0, -2.0, 2.0, 2.0));

        QPen stemPen(colour, std::max<qreal>(1.0, density.dp(kStemWidthDp)));
        stemPen.setCapStyle(Qt::RoundCap);

        for (int index = 0; index < count; ++index) {
            const double lag = lagForIndex(index);
            const qreal x = plot.left() + (maximumX > 0.0
                ? static_cast<qreal>(lag / maximumX) * plot.width()
                : 0.0);
            const double targetValue = std::clamp(
                values_.at(static_cast<size_t>(index)), -1.0, 1.0);
            const qreal targetY = math()->toPixel(QPointF(lag, targetValue)).y();
            const qreal tipY = zeroY + (targetY - zeroY) * reveal;

            painter.setPen(stemPen);
            painter.drawLine(QPointF(x, zeroY), QPointF(x, tipY));

            if (drawMarkers) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(colour);
                painter.drawEllipse(QPointF(x, tipY), density.dp(kMarkerRadiusDp),
                                    density.dp(kMarkerRadiusDp));
            }
        }

        painter.restore();
    } catch (const std::exception& error) {
        qWarning("md::AutocorrelationChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::AutocorrelationChart::paintPlot failed with an unknown exception");
    }
}

} // namespace md
