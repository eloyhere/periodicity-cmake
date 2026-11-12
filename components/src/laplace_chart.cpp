// Periodicity — the LaplaceChart component.
//
// The field, the slice profiles and every marker are resolved from one theme
// snapshot at the start of the pass: the ramp runs from surfaceContainerHighest
// through primaryContainer to primary, the grid and frame use outlineVariant and
// outline, and the labels use onSurfaceVariant. No colour is written literally
// and every length passes through Density::dp().
//
// The local maths is deliberately tiny: the caller supplies F(s), so the only
// computations here are the magnitude/phase of a complex sample, a linear axis
// mapping and a "nice number" tick generator. Nothing in this file includes the
// mathematics module or evaluates a transform.

#include <md/components/laplace_chart.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QBrush>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QSizeF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace md {
namespace {

constexpr qreal kPi = 3.14159265358979323846;

constexpr qreal kPreferredWidthDp = 440.0;
constexpr qreal kPreferredHeightDp = 340.0;

constexpr qreal kFieldGapDp = 6.0;
constexpr qreal kPanelInsetDp = 3.0;
constexpr qreal kTickLengthDp = 4.0;
constexpr qreal kLabelGapDp = 3.0;

constexpr qreal kColourBarWidthDp = 10.0;
constexpr qreal kColourBarLabelGutterDp = 38.0;
constexpr qreal kColourBarCaptionDp = 14.0;

constexpr qreal kSliceBottomFraction = 0.19;
constexpr qreal kSliceRightFraction = 0.20;

constexpr qreal kMarkerSizeDp = 5.0;
constexpr qreal kMarkerPenDp = 2.0;
constexpr qreal kHoverBoxAlpha = 0.92;
constexpr qreal kHoverCrosshairAlpha = 0.7;

constexpr qreal kMinimumFieldWidthDp = 120.0;
constexpr qreal kMinimumFieldHeightDp = 90.0;
constexpr qreal kMinimumSlicedWidthDp = 300.0;
constexpr qreal kMinimumSlicedHeightDp = 210.0;

// Restores the painter on every exit path, including one taken by an exception.
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

[[nodiscard]] qreal clamp01(qreal value)
{
    return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] QColor mixColour(const QColor& from, const QColor& to, qreal fraction)
{
    const qreal t = clamp01(fraction);
    const auto channel = [t](int start, int end) {
        return static_cast<int>(qRound(start + (end - start) * t));
    };

    return QColor(channel(from.red(), to.red()),
                  channel(from.green(), to.green()),
                  channel(from.blue(), to.blue()));
}

// Three-stop sequential ramp: low -> middle -> high. Keeping the middle stop
// distinct from a straight blend keeps mid magnitudes from washing out.
[[nodiscard]] QColor rampColour(const QColor& low, const QColor& middle,
                                const QColor& high, qreal fraction)
{
    const qreal t = clamp01(fraction);
    if (t <= 0.5)
        return mixColour(low, middle, t * 2.0);
    return mixColour(middle, high, (t - 0.5) * 2.0);
}

// A compact numeric label for an axis tick or a colour-bar stop.
[[nodiscard]] QString formatTick(double value)
{
    if (!std::isfinite(value))
        return QString();
    if (std::abs(value) < 1e-12)
        return QStringLiteral("0");

    const double magnitude = std::abs(value);
    if (magnitude >= 10000.0 || magnitude < 0.01)
        return QString::number(value, 'g', 3);

    QString text = QString::number(value, 'f', magnitude >= 100.0 ? 0 : 1);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(QLatin1Char('.')))
            text.chop(1);
    }
    return text;
}

[[nodiscard]] QString formatMagnitude(double value)
{
    if (!std::isfinite(value))
        return QStringLiteral("—");
    if (value == 0.0)
        return QStringLiteral("0");
    return QString::number(value, 'g', 3);
}

// Evenly spaced "nice" ticks (1, 2, 5 × 10^k) that lie inside [minimum,
// maximum]. The returned values are ascending and include at most `count`
// entries; a degenerate range yields an empty list.
[[nodiscard]] QVector<double> niceTicks(double minimum, double maximum, int count)
{
    QVector<double> ticks;

    if (!std::isfinite(minimum) || !std::isfinite(maximum) || !(maximum > minimum))
        return ticks;

    const int target = std::clamp(count, 2, 12);
    const double rawStep = (maximum - minimum) / static_cast<double>(target - 1);
    if (!(rawStep > 0.0) || !std::isfinite(rawStep))
        return ticks;

    const double exponent = std::floor(std::log10(rawStep));
    const double base = std::pow(10.0, exponent);
    const double normalised = rawStep / base;

    double step = base;
    if (normalised > 5.0)
        step = base * 10.0;
    else if (normalised > 2.0)
        step = base * 5.0;
    else if (normalised > 1.0)
        step = base * 2.0;

    const double epsilon = step * 1e-9;
    double value = std::ceil((minimum - epsilon) / step) * step;

    for (int index = 0; index < 40 && value <= maximum + epsilon; ++index) {
        // Snap -0 and values within the epsilon of zero to an exact zero.
        const double snapped = std::abs(value) < epsilon ? 0.0 : value;
        ticks.append(snapped);
        value += step;
    }

    return ticks;
}

[[nodiscard]] int tickIndexFor(const QVector<double>& ticks, double value)
{
    int nearest = -1;
    qreal distance = std::numeric_limits<qreal>::max();

    for (int index = 0; index < ticks.size(); ++index) {
        const qreal current = std::abs(ticks.at(index) - value);
        if (current < distance) {
            distance = current;
            nearest = index;
        }
    }

    return nearest;
}

} // namespace

LaplaceChart::LaplaceChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAxesVisible(false);

    // A magnitude field is a reading surface, not a control: the shared
    // interaction layer would tint the whole plot after paintPlot(), so hover
    // feedback is local to the sample under the pointer instead.
    setInteractionFeedbackEnabled(false);

    setLegendVisible(false);

    // The shared chart animation is the reveal driver; this chart only stages
    // the elements from its progress. Level2 keeps the sweep at 500 ms.
    animation().setDuration(MotionLevel::Level2);
}

LaplaceChart::~LaplaceChart() = default;

void LaplaceChart::setSpectrum(std::vector<double> sigmas, std::vector<double> omegas,
                               std::vector<std::complex<double>> values)
{
    sigmas_ = std::move(sigmas);
    omegas_ = std::move(omegas);
    values_ = std::move(values);

    // Drop invalid and unsorted axes rather than mapping through them: a
    // descending axis would silently mirror the field.
    if (!std::is_sorted(sigmas_.begin(), sigmas_.end())
        || !std::is_sorted(omegas_.begin(), omegas_.end())) {
        sigmas_.clear();
        omegas_.clear();
        values_.clear();
    }

    bool found = false;
    double minimum = 0.0;
    double maximum = 0.0;

    const std::size_t count = std::min(values_.size(),
                                       sigmas_.size() * omegas_.size());
    for (std::size_t index = 0; index < count; ++index) {
        const double magnitude = std::abs(values_.at(index));
        if (!std::isfinite(magnitude))
            continue;

        if (!found) {
            minimum = magnitude;
            maximum = magnitude;
            found = true;
        } else {
            minimum = std::min(minimum, magnitude);
            maximum = std::max(maximum, magnitude);
        }
    }

    minimumMagnitude_ = found ? minimum : 0.0;
    maximumMagnitude_ = found ? maximum : 0.0;

    syncPresenceSeries();
    requestUpdate();
}

const std::vector<double>& LaplaceChart::sigmas() const noexcept
{
    return sigmas_;
}

const std::vector<double>& LaplaceChart::omegas() const noexcept
{
    return omegas_;
}

const std::vector<std::complex<double>>& LaplaceChart::values() const noexcept
{
    return values_;
}

bool LaplaceChart::hasFiniteSample() const noexcept
{
    const std::size_t count = std::min(values_.size(),
                                       sigmas_.size() * omegas_.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (std::isfinite(values_.at(index).real())
            && std::isfinite(values_.at(index).imag())) {
            return true;
        }
    }
    return false;
}

bool LaplaceChart::hasSpectrum() const noexcept
{
    return sigmas_.size() >= 2 && omegas_.size() >= 2 && hasFiniteSample();
}

void LaplaceChart::setConvergenceAbscissa(double sigma)
{
    const double next = std::isfinite(sigma)
        ? sigma
        : std::numeric_limits<double>::quiet_NaN();
    if (convergenceAbscissa_ == next)
        return;

    convergenceAbscissa_ = next;
    requestUpdate();
}

double LaplaceChart::convergenceAbscissa() const noexcept
{
    return convergenceAbscissa_;
}

void LaplaceChart::setPoles(std::vector<std::complex<double>> poles)
{
    poles_ = std::move(poles);
    requestUpdate();
}

void LaplaceChart::setZeros(std::vector<std::complex<double>> zeros)
{
    zeros_ = std::move(zeros);
    requestUpdate();
}

const std::vector<std::complex<double>>& LaplaceChart::poles() const noexcept
{
    return poles_;
}

const std::vector<std::complex<double>>& LaplaceChart::zeros() const noexcept
{
    return zeros_;
}

QPointF LaplaceChart::polePixel(int index) const
{
    if (index < 0 || index >= static_cast<int>(poles_.size()))
        return QPointF();
    const std::complex<double>& pole = poles_.at(static_cast<std::size_t>(index));
    if (!std::isfinite(pole.real()) || !std::isfinite(pole.imag()))
        return QPointF();
    return toPixel(pole.real(), pole.imag());
}

QPointF LaplaceChart::zeroPixel(int index) const
{
    if (index < 0 || index >= static_cast<int>(zeros_.size()))
        return QPointF();
    const std::complex<double>& zero = zeros_.at(static_cast<std::size_t>(index));
    if (!std::isfinite(zero.real()) || !std::isfinite(zero.imag()))
        return QPointF();
    return toPixel(zero.real(), zero.imag());
}

void LaplaceChart::setSlicesVisible(bool visible)
{
    if (slicesVisible_ == visible)
        return;
    slicesVisible_ = visible;
    requestUpdate();
}

bool LaplaceChart::slicesVisible() const noexcept
{
    return slicesVisible_;
}

void LaplaceChart::setSliceSigma(double sigma)
{
    const double next = std::isfinite(sigma)
        ? sigma
        : std::numeric_limits<double>::quiet_NaN();
    if (sliceSigma_ == next)
        return;
    sliceSigma_ = next;
    requestUpdate();
}

double LaplaceChart::sliceSigma() const noexcept
{
    return effectiveSliceSigma();
}

void LaplaceChart::setSliceOmega(double omega)
{
    const double next = std::isfinite(omega)
        ? omega
        : std::numeric_limits<double>::quiet_NaN();
    if (sliceOmega_ == next)
        return;
    sliceOmega_ = next;
    requestUpdate();
}

double LaplaceChart::sliceOmega() const noexcept
{
    return effectiveSliceOmega();
}

double LaplaceChart::effectiveSliceSigma() const
{
    if (std::isfinite(sliceSigma_))
        return sliceSigma_;
    if (sigmas_.empty())
        return 0.0;
    return sigmas_.at(sigmas_.size() / 2);
}

double LaplaceChart::effectiveSliceOmega() const
{
    if (std::isfinite(sliceOmega_))
        return sliceOmega_;
    if (omegas_.empty())
        return 0.0;

    const double first = omegas_.front();
    const double last = omegas_.back();
    if (first <= 0.0 && last >= 0.0)
        return 0.0;
    return omegas_.at(omegas_.size() / 2);
}

void LaplaceChart::setUnitGridVisible(bool visible)
{
    if (unitGridVisible_ == visible)
        return;
    unitGridVisible_ = visible;
    requestUpdate();
}

bool LaplaceChart::showsUnitGrid() const noexcept
{
    return unitGridVisible_;
}

void LaplaceChart::setColourBarVisible(bool visible)
{
    if (colourBarVisible_ == visible)
        return;
    colourBarVisible_ = visible;
    requestUpdate();
}

bool LaplaceChart::colourBarVisible() const noexcept
{
    return colourBarVisible_;
}

double LaplaceChart::minimumMagnitude() const noexcept
{
    return minimumMagnitude_;
}

double LaplaceChart::maximumMagnitude() const noexcept
{
    return maximumMagnitude_;
}

QColor LaplaceChart::colourForMagnitude(double magnitude) const
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();

    if (!std::isfinite(magnitude))
        return colours.surfaceContainerHighest;

    const double span = maximumMagnitude_ - minimumMagnitude_;
    qreal fraction = 1.0;
    if (span > 0.0)
        fraction = static_cast<qreal>((magnitude - minimumMagnitude_) / span);
    fraction = std::sqrt(clamp01(fraction));

    return rampColour(colours.surfaceContainerHighest, colours.primaryContainer,
                      colours.primary, fraction);
}

QVector<double> LaplaceChart::sigmaTicks(int count) const
{
    if (sigmas_.size() < 2)
        return {};
    return niceTicks(sigmas_.front(), sigmas_.back(), count);
}

QVector<double> LaplaceChart::omegaTicks(int count) const
{
    if (omegas_.size() < 2)
        return {};
    return niceTicks(omegas_.front(), omegas_.back(), count);
}

LaplaceChart::Layout LaplaceChart::layoutFor(const QRectF& plot) const
{
    Layout layout;

    if (plot.isEmpty() || plot.width() <= 0.0 || plot.height() <= 0.0)
        return layout;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal gap = density.dp(kFieldGapDp);

    QRectF content = plot;

    if (colourBarVisible_) {
        const qreal barWidth = density.dp(kColourBarWidthDp);
        const qreal labelGutter = density.dp(kColourBarLabelGutterDp);
        const qreal caption = density.dp(kColourBarCaptionDp);
        const qreal reserve = barWidth + gap + labelGutter;

        if (content.width() > reserve + density.dp(160.0)) {
            layout.colourBar = QRectF(content.right() - labelGutter - barWidth,
                                      content.top() + caption,
                                      barWidth,
                                      std::max<qreal>(0.0, content.height() - caption));
            content.setRight(layout.colourBar.left() - gap - labelGutter);
        }
    }

    const bool slices = slicesVisible_
        && content.width() >= density.dp(kMinimumSlicedWidthDp)
        && content.height() >= density.dp(kMinimumSlicedHeightDp);

    if (!slices) {
        const QRectF fieldBox = content.adjusted(0.0, 0.0, 0.0, 0.0);
        if (fieldBox.width() < density.dp(kMinimumFieldWidthDp)
            || fieldBox.height() < density.dp(kMinimumFieldHeightDp)) {
            return layout;
        }
        layout.field = fieldBox;
        return layout;
    }

    const qreal sliceHeight = std::max<qreal>(0.0, content.height() * kSliceBottomFraction);
    const qreal sliceWidth = std::max<qreal>(0.0, content.width() * kSliceRightFraction);

    const QRectF fieldBox(content.left(),
                          content.top(),
                          std::max<qreal>(0.0, content.width() - sliceWidth - gap),
                          std::max<qreal>(0.0, content.height() - sliceHeight - gap));
    const QRectF omegaBox(fieldBox.right() + gap,
                          content.top(),
                          std::max<qreal>(0.0, content.right() - fieldBox.right() - gap),
                          fieldBox.height());
    const QRectF sigmaBox(content.left(),
                          fieldBox.bottom() + gap,
                          fieldBox.width(),
                          std::max<qreal>(0.0, content.bottom() - fieldBox.bottom() - gap));

    if (fieldBox.width() < density.dp(kMinimumFieldWidthDp)
        || fieldBox.height() < density.dp(kMinimumFieldHeightDp)) {
        return layout;
    }

    // The field owns two labelled axes; the slices only label the axis they
    // plot against, so they need much smaller gutters.
    const QFont tickFont = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF tickMetrics(tickFont);
    const QFont axisFont = Font::resolve(this, theme.type().labelMedium, density);
    const QFontMetricsF axisMetrics(axisFont);
    const qreal tickLength = density.dp(kTickLengthDp);
    const qreal labelGap = density.dp(kLabelGapDp);

    qreal widestYLabel = 0.0;
    for (double value : omegaTicks())
        widestYLabel = std::max(widestYLabel, tickMetrics.horizontalAdvance(formatTick(value)));

    const qreal fieldLeft = widestYLabel + tickLength + labelGap + axisMetrics.height() + 2.0;
    const qreal fieldBottom = tickMetrics.height() + tickLength + labelGap
        + axisMetrics.height() + 2.0;
    const qreal smallLeft = axisMetrics.height() + 2.0;
    const qreal smallBottom = tickMetrics.height() + tickLength + labelGap;

    layout.field = fieldBox.adjusted(fieldLeft, density.dp(kPanelInsetDp),
                                     -density.dp(kPanelInsetDp), -fieldBottom);
    layout.omegaSlice = omegaBox.adjusted(smallLeft, density.dp(kPanelInsetDp),
                                          -density.dp(kPanelInsetDp), -smallBottom);
    layout.sigmaSlice = sigmaBox.adjusted(fieldLeft, density.dp(kPanelInsetDp),
                                          -density.dp(kPanelInsetDp), -smallBottom);

    if (layout.field.width() < 20.0 || layout.field.height() < 20.0)
        layout.field = QRectF();
    if (layout.omegaSlice.width() < 12.0 || layout.omegaSlice.height() < 20.0)
        layout.omegaSlice = QRectF();
    if (layout.sigmaSlice.width() < 20.0 || layout.sigmaSlice.height() < 12.0)
        layout.sigmaSlice = QRectF();

    return layout;
}

QRectF LaplaceChart::fieldRect() const
{
    return layoutFor(plotRect()).field;
}

QRectF LaplaceChart::sigmaSliceRect() const
{
    return layoutFor(plotRect()).sigmaSlice;
}

QRectF LaplaceChart::omegaSliceRect() const
{
    return layoutFor(plotRect()).omegaSlice;
}

QRectF LaplaceChart::colourBarRect() const
{
    return layoutFor(plotRect()).colourBar;
}

QPointF LaplaceChart::toPixel(double sigma, double omega) const
{
    const QRectF field = fieldRect();
    if (field.isEmpty() || sigmas_.size() < 2 || omegas_.size() < 2)
        return QPointF();
    if (!std::isfinite(sigma) || !std::isfinite(omega))
        return QPointF();

    const double sigmaSpan = sigmas_.back() - sigmas_.front();
    const double omegaSpan = omegas_.back() - omegas_.front();
    if (!(sigmaSpan > 0.0) || !(omegaSpan > 0.0))
        return QPointF();

    const qreal xFraction = static_cast<qreal>((sigma - sigmas_.front()) / sigmaSpan);
    const qreal yFraction = static_cast<qreal>((omega - omegas_.front()) / omegaSpan);

    return QPointF(field.left() + xFraction * field.width(),
                   field.bottom() - yFraction * field.height());
}

double LaplaceChart::sampleMagnitude(int sigmaIndex, int omegaIndex) const
{
    if (sigmaIndex < 0 || omegaIndex < 0)
        return std::numeric_limits<double>::quiet_NaN();
    if (sigmaIndex >= static_cast<int>(sigmas_.size())
        || omegaIndex >= static_cast<int>(omegas_.size())) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const std::size_t index = static_cast<std::size_t>(sigmaIndex) * omegas_.size()
        + static_cast<std::size_t>(omegaIndex);
    if (index >= values_.size())
        return std::numeric_limits<double>::quiet_NaN();

    return std::abs(values_.at(index));
}

int LaplaceChart::nearestIndex(const std::vector<double>& axis, double value) const
{
    if (axis.size() < 2 || !std::isfinite(value))
        return -1;

    const double first = axis.front();
    const double span = axis.back() - first;
    if (!(span > 0.0))
        return -1;

    const double fraction = std::clamp((value - first) / span, 0.0, 1.0);
    const int index = static_cast<int>(std::lround(fraction * static_cast<double>(axis.size() - 1)));
    return std::clamp(index, 0, static_cast<int>(axis.size()) - 1);
}

LaplaceSample LaplaceChart::sampleAt(QPointF position) const
{
    LaplaceSample sample;

    const QRectF field = fieldRect();
    if (field.isEmpty() || !field.contains(position))
        return sample;
    if (sigmas_.size() < 2 || omegas_.size() < 2)
        return sample;

    const qreal xFraction = (position.x() - field.left()) / field.width();
    const qreal yFraction = (field.bottom() - position.y()) / field.height();

    const double sigma = sigmas_.front()
        + static_cast<double>(xFraction) * (sigmas_.back() - sigmas_.front());
    const double omega = omegas_.front()
        + static_cast<double>(yFraction) * (omegas_.back() - omegas_.front());

    const int sigmaIndex = nearestIndex(sigmas_, sigma);
    const int omegaIndex = nearestIndex(omegas_, omega);
    if (sigmaIndex < 0 || omegaIndex < 0)
        return sample;

    const std::size_t index = static_cast<std::size_t>(sigmaIndex) * omegas_.size()
        + static_cast<std::size_t>(omegaIndex);
    if (index >= values_.size())
        return sample;

    const std::complex<double>& value = values_.at(index);
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
        return sample;

    sample.valid = true;
    sample.sigma = sigmas_.at(static_cast<std::size_t>(sigmaIndex));
    sample.omega = omegas_.at(static_cast<std::size_t>(omegaIndex));
    sample.magnitude = std::abs(value);
    sample.phase = std::arg(value);
    return sample;
}

void LaplaceChart::syncPresenceSeries()
{
    // ChartBase decides between paintPlot() and the empty state from hasData(),
    // which reads the series list. The field is not a series, so "has data" is
    // mirrored onto two unnamed single-point series. They are never painted and
    // their empty names keep them out of the legend; the second one also makes
    // seriesColour(1) valid for the slice curves.
    QVector<ChartSeries> presence;
    if (hasFiniteSample() && sigmas_.size() >= 2 && omegas_.size() >= 2) {
        ChartSeries first;
        first.points.append(QPointF(0.0, 0.0));
        presence.append(first);

        ChartSeries second;
        second.points.append(QPointF(0.0, 1.0));
        presence.append(second);
    }
    ChartBase::setSeries(std::move(presence));
}

void LaplaceChart::measureOverride(MeasureSpec width, MeasureSpec height)
{
    try {
        const Density& density = Theme::forWidget(this).density();
        setMeasuredSize(QSizeF(width.resolve(density.dp(kPreferredWidthDp)),
                               height.resolve(density.dp(kPreferredHeightDp))));
    } catch (const std::exception& error) {
        qWarning("md::LaplaceChart::measureOverride failed: %s", error.what());
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    } catch (...) {
        qWarning("md::LaplaceChart::measureOverride failed with an unknown exception");
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    }
}

void LaplaceChart::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    if (plot.isEmpty())
        return;

    const qreal reveal = clamp01(progress);
    if (reveal <= 0.0)
        return;

    const PainterStateGuard guard(painter);

    try {
        const Layout layout = layoutFor(plot);
        if (layout.field.isEmpty())
            return;

        paintField(painter, layout, reveal);
        paintUnitGrid(painter, layout.field);
        paintAxes(painter, layout.field);
        paintConvergence(painter, layout.field, reveal);
        paintSlices(painter, layout, reveal);
        paintMarkers(painter, layout.field, reveal);
        if (!layout.colourBar.isEmpty())
            paintColourBar(painter, layout.colourBar);
        paintHover(painter, layout.field);
    } catch (const std::exception& error) {
        qWarning("md::LaplaceChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::LaplaceChart::paintPlot failed with an unknown exception");
    }
}

void LaplaceChart::paintField(QPainter& painter, const Layout& layout, qreal progress)
{
    const QRectF& field = layout.field;
    if (field.isEmpty() || sigmas_.size() < 2 || omegas_.size() < 2)
        return;

    const qreal reveal = clamp01(progress);
    if (reveal <= 0.0)
        return;

    QRectF sweep(field.left(), field.top(), field.width() * reveal, field.height());
    sweep = sweep.intersected(field);
    if (sweep.isEmpty())
        return;

    painter.save();
    painter.setClipRect(sweep);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);

    const int sigmaCount = static_cast<int>(sigmas_.size());
    const int omegaCount = static_cast<int>(omegas_.size());
    const double sigmaSpan = sigmas_.back() - sigmas_.front();
    const double omegaSpan = omegas_.back() - omegas_.front();

    if (!(sigmaSpan > 0.0) || !(omegaSpan > 0.0)) {
        painter.restore();
        return;
    }

    // Resolve the pixel position of every grid line once. Calling toPixel()
    // per cell would recompute the layout (and its font metrics) O(n²) times.
    std::vector<qreal> xPositions(static_cast<std::size_t>(sigmaCount));
    std::vector<qreal> yPositions(static_cast<std::size_t>(omegaCount));
    for (int index = 0; index < sigmaCount; ++index) {
        const double fraction = (sigmas_.at(static_cast<std::size_t>(index))
                                 - sigmas_.front()) / sigmaSpan;
        xPositions[static_cast<std::size_t>(index)] =
            field.left() + static_cast<qreal>(fraction) * field.width();
    }
    for (int index = 0; index < omegaCount; ++index) {
        const double fraction = (omegas_.at(static_cast<std::size_t>(index))
                                 - omegas_.front()) / omegaSpan;
        yPositions[static_cast<std::size_t>(index)] =
            field.bottom() - static_cast<qreal>(fraction) * field.height();
    }

    const ColorScheme& colours = Theme::forWidget(this).colours();
    const QColor low = colours.surfaceContainerHighest;
    const QColor middle = colours.primaryContainer;
    const QColor high = colours.primary;
    const double magnitudeSpan = maximumMagnitude_ - minimumMagnitude_;

    const auto colourFor = [&](double magnitude) {
        qreal fraction = 1.0;
        if (magnitudeSpan > 0.0) {
            fraction = static_cast<qreal>((magnitude - minimumMagnitude_) / magnitudeSpan);
        }
        return rampColour(low, middle, high,
                          std::sqrt(clamp01(fraction)));
    };

    for (int sigmaIndex = 0; sigmaIndex + 1 < sigmaCount; ++sigmaIndex) {
        const qreal x0 = xPositions.at(static_cast<std::size_t>(sigmaIndex));
        const qreal x1 = xPositions.at(static_cast<std::size_t>(sigmaIndex + 1));

        for (int omegaIndex = 0; omegaIndex + 1 < omegaCount; ++omegaIndex) {
            const qreal y0 = yPositions.at(static_cast<std::size_t>(omegaIndex));
            const qreal y1 = yPositions.at(static_cast<std::size_t>(omegaIndex + 1));

            double sum = 0.0;
            int finite = 0;
            const double corners[4] = {
                sampleMagnitude(sigmaIndex, omegaIndex),
                sampleMagnitude(sigmaIndex + 1, omegaIndex),
                sampleMagnitude(sigmaIndex, omegaIndex + 1),
                sampleMagnitude(sigmaIndex + 1, omegaIndex + 1),
            };
            for (double corner : corners) {
                if (std::isfinite(corner)) {
                    sum += corner;
                    ++finite;
                }
            }
            if (finite == 0)
                continue;

            const QRectF cell(QPointF(std::min(x0, x1), std::min(y0, y1)),
                              QPointF(std::max(x0, x1), std::max(y0, y1)));
            if (cell.width() <= 0.0 || cell.height() <= 0.0)
                continue;

            // A half-pixel overdraw closes seams between adjacent quads that
            // would otherwise show the surface through the field.
            painter.fillRect(cell.adjusted(-0.5, -0.5, 0.5, 0.5),
                             colourForMagnitude(sum / finite));
        }
    }

    painter.restore();
}

void LaplaceChart::paintUnitGrid(QPainter& painter, const QRectF& field)
{
    if (field.isEmpty() || sigmas_.size() < 2 || omegas_.size() < 2)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(field);
    painter.setBrush(Qt::NoBrush);

    if (unitGridVisible_) {
        QColor hairline = colours.outlineVariant;
        hairline.setAlphaF(0.55f);
        painter.setPen(QPen(hairline, std::max<qreal>(0.75, density.dp(1.0)), Qt::DotLine));

        const double sigmaFirst = std::ceil(sigmas_.front());
        const double sigmaLast = std::floor(sigmas_.back());
        for (double sigma = sigmaFirst; sigma <= sigmaLast; sigma += 1.0) {
            if (std::abs(sigma) < 1e-9)
                continue;
            const qreal x = toPixel(sigma, 0.0).x();
            painter.drawLine(QPointF(x, field.top()), QPointF(x, field.bottom()));
        }

        const double omegaFirst = std::ceil(omegas_.front());
        const double omegaLast = std::floor(omegas_.back());
        for (double omega = omegaFirst; omega <= omegaLast; omega += 1.0) {
            if (std::abs(omega) < 1e-9)
                continue;
            const qreal y = toPixel(0.0, omega).y();
            painter.drawLine(QPointF(field.left(), y), QPointF(field.right(), y));
        }
    }

    // The origin axes are stronger than the exponent grid.
    QColor axis = colours.outline;
    axis.setAlphaF(0.9f);
    painter.setPen(QPen(axis, std::max<qreal>(1.0, density.dp(1.0)), Qt::SolidLine));

    if (sigmas_.front() <= 0.0 && sigmas_.back() >= 0.0) {
        const qreal x = toPixel(0.0, omegas_.front()).x();
        painter.drawLine(QPointF(x, field.top()), QPointF(x, field.bottom()));
    }
    if (omegas_.front() <= 0.0 && omegas_.back() >= 0.0) {
        const qreal y = toPixel(sigmas_.front(), 0.0).y();
        painter.drawLine(QPointF(field.left(), y), QPointF(field.right(), y));
    }

    painter.restore();
}

void LaplaceChart::paintAxes(QPainter& painter, const QRectF& field)
{
    if (field.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const QFont tickFont = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF tickMetrics(tickFont);
    const QFont axisFont = Font::resolve(this, theme.type().labelMedium, density);

    const qreal tickLength = density.dp(kTickLengthDp);
    const qreal labelGap = density.dp(kLabelGapDp);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor grid = colours.outlineVariant;
    grid.setAlphaF(0.45f);
    painter.setPen(QPen(grid, std::max<qreal>(0.75, density.dp(1.0)), Qt::DotLine));

    const QVector<double> xTicks = sigmaTicks();
    const QVector<double> yTicks = omegaTicks();

    for (double value : xTicks) {
        const qreal x = toPixel(value, omegas_.front()).x();
        painter.drawLine(QPointF(x, field.top()), QPointF(x, field.bottom()));
    }
    for (double value : yTicks) {
        const qreal y = toPixel(sigmas_.front(), value).y();
        painter.drawLine(QPointF(field.left(), y), QPointF(field.right(), y));
    }

    QColor frame = colours.outline;
    frame.setAlphaF(0.8f);
    painter.setPen(QPen(frame, std::max<qreal>(1.0, density.dp(1.0))));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(field);

    painter.setFont(tickFont);
    painter.setPen(colours.onSurfaceVariant);

    for (double value : xTicks) {
        const qreal x = toPixel(value, omegas_.front()).x();
        painter.drawLine(QPointF(x, field.bottom()),
                         QPointF(x, field.bottom() + tickLength));
        const QRectF labelRect(x - 24.0, field.bottom() + tickLength + 1.0, 48.0,
                               tickMetrics.height());
        painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, formatTick(value));
    }

    for (double value : yTicks) {
        const qreal y = toPixel(sigmas_.front(), value).y();
        painter.drawLine(QPointF(field.left() - tickLength, y), QPointF(field.left(), y));
        const QRectF labelRect(field.left() - tickLength - labelGap - 48.0,
                               y - tickMetrics.height() / 2.0, 48.0, tickMetrics.height());
        painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, formatTick(value));
    }

    // Axis captions: ω above the y labels, σ under the x labels.
    painter.setFont(axisFont);
    painter.setPen(colours.onSurfaceVariant);

    const QRectF omegaCaption(field.left() - tickLength - labelGap - 60.0,
                              field.top() - density.dp(14.0) - 2.0, 60.0,
                              density.dp(14.0));
    painter.drawText(omegaCaption, Qt::AlignRight | Qt::AlignBottom,
                     localized(QStringLiteral("chart.laplace.omega"), QStringLiteral("ω")));

    const QRectF sigmaCaption(field.left(), field.bottom() + tickLength + 1.0
                                  + tickMetrics.height() + 1.0,
                              field.width(), density.dp(14.0));
    painter.drawText(sigmaCaption, Qt::AlignRight | Qt::AlignTop,
                     localized(QStringLiteral("chart.laplace.sigma"), QStringLiteral("σ")));

    painter.restore();
}

void LaplaceChart::paintConvergence(QPainter& painter, const QRectF& field, qreal progress)
{
    if (field.isEmpty() || !std::isfinite(convergenceAbscissa_))
        return;
    if (sigmas_.size() < 2 || convergenceAbscissa_ < sigmas_.front()
        || convergenceAbscissa_ > sigmas_.back()) {
        return;
    }

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const qreal reveal = clamp01(progress);
    if (reveal <= 0.0)
        return;

    const qreal x = toPixel(convergenceAbscissa_, omegas_.front()).x();

    painter.save();
    painter.setClipRect(field.adjusted(-1.0, 0.0, 1.0, 0.0));
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor line = colours.tertiary;
    line.setAlphaF(0.95f);
    QPen pen(line, std::max<qreal>(1.25, density.dp(1.25)), Qt::DashLine);
    pen.setDashPattern({5.0, 4.0});
    painter.setPen(pen);
    painter.drawLine(QPointF(x, field.bottom()),
                     QPointF(x, field.bottom() - field.height() * reveal));

    const QFont font = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF metrics(font);
    const QString caption = localized(QStringLiteral("chart.laplace.roc"),
                                      QStringLiteral("ROC"));
    const qreal width = metrics.horizontalAdvance(caption) + density.dp(8.0);
    QRectF box(x + density.dp(4.0), field.top() + density.dp(4.0), width,
               metrics.height() + density.dp(4.0));
    if (box.right() > field.right())
        box.moveLeft(x - width - density.dp(4.0));
    if (box.right() > field.right())
        box.setRight(field.right() - 1.0);

    QColor background = colours.surfaceContainerHigh;
    background.setAlphaF(0.9f);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(box, 3.0, 3.0);

    painter.setFont(font);
    painter.setPen(colours.tertiary);
    painter.drawText(box, Qt::AlignCenter, caption);

    painter.restore();
}

void LaplaceChart::paintSlices(QPainter& painter, const Layout& layout, qreal progress)
{
    if (layout.sigmaSlice.isEmpty() && layout.omegaSlice.isEmpty())
        return;
    if (sigmas_.size() < 2 || omegas_.size() < 2)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const qreal reveal = clamp01(progress);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int sigmaCount = static_cast<int>(sigmas_.size());
    const int omegaCount = static_cast<int>(omegas_.size());
    const int sigmaIndex = std::clamp(nearestIndex(sigmas_, effectiveSliceSigma()), 0,
                                      sigmaCount - 1);
    const int omegaIndex = std::clamp(nearestIndex(omegas_, effectiveSliceOmega()), 0,
                                      omegaCount - 1);

    const auto drawSliceFrame = [&](const QRectF& rect, const QColor& curve) {
        QColor baseline = colours.outlineVariant;
        baseline.setAlphaF(0.8f);
        painter.setPen(QPen(baseline, std::max<qreal>(1.0, density.dp(1.0))));
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(rect.left(), rect.bottom()),
                         QPointF(rect.right(), rect.bottom()));
        painter.drawLine(QPointF(rect.left(), rect.top()),
                         QPointF(rect.left(), rect.bottom()));
        Q_UNUSED(curve);
    };

    // σ-slice: fixed ω, |F(σ, ω)| across σ.
    if (!layout.sigmaSlice.isEmpty()) {
        const QRectF rect = layout.sigmaSlice;
        drawSliceFrame(rect, seriesColour(0));

        double maximum = 0.0;
        bool found = false;
        for (int index = 0; index < sigmaCount; ++index) {
            const double magnitude = sampleMagnitude(index, omegaIndex);
            if (!std::isfinite(magnitude))
                continue;
            maximum = found ? std::max(maximum, magnitude) : magnitude;
            found = true;
        }

        if (found) {
            const double scale = maximum > 0.0 ? maximum : 1.0;
            const QPen pen(seriesColour(0), std::max<qreal>(1.5, density.dp(1.5)));
            painter.setPen(pen);

            QPolygonF points;
            const int visible = std::max(1, static_cast<int>(
                std::ceil(static_cast<double>(sigmaCount) * static_cast<double>(reveal))));

            for (int index = 0; index < visible; ++index) {
                const double magnitude = sampleMagnitude(index, omegaIndex);
                if (!std::isfinite(magnitude))
                    continue;
                const qreal x = rect.left()
                    + rect.width() * static_cast<qreal>(index)
                        / static_cast<qreal>(sigmaCount - 1);
                const qreal y = rect.bottom()
                    - rect.height() * static_cast<qreal>(magnitude / scale) * 0.92;
                points.append(QPointF(x, y));
            }
            painter.setBrush(Qt::NoBrush);
            if (points.size() >= 2)
                painter.drawPolyline(points);

            painter.setFont(Font::resolve(this, theme.type().labelSmall, density));
            painter.setPen(colours.onSurfaceVariant);
            painter.drawText(QRectF(rect.left(), rect.top() - density.dp(12.0),
                                    rect.width(), density.dp(12.0)),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             localized(QStringLiteral("chart.laplace.sigmaSlice"),
                                       QStringLiteral("|F(σ, ω₀)|")));
        }
    }

    // ω-slice: fixed σ, |F(σ, ω)| across ω, magnitude on the horizontal axis.
    if (!layout.omegaSlice.isEmpty()) {
        const QRectF rect = layout.omegaSlice;
        drawSliceFrame(rect, seriesColour(1));

        double maximum = 0.0;
        bool found = false;
        for (int index = 0; index < omegaCount; ++index) {
            const double magnitude = sampleMagnitude(sigmaIndex, index);
            if (!std::isfinite(magnitude))
                continue;
            maximum = found ? std::max(maximum, magnitude) : magnitude;
            found = true;
        }

        if (found) {
            const double scale = maximum > 0.0 ? maximum : 1.0;
            const QPen pen(seriesColour(1), std::max<qreal>(1.5, density.dp(1.5)));
            painter.setPen(pen);

            QPolygonF points;
            const int visible = std::max(1, static_cast<int>(
                std::ceil(static_cast<double>(omegaCount) * static_cast<double>(reveal))));

            for (int index = 0; index < visible; ++index) {
                const double magnitude = sampleMagnitude(sigmaIndex, index);
                if (!std::isfinite(magnitude))
                    continue;
                const qreal y = rect.bottom()
                    - rect.height() * static_cast<qreal>(index)
                        / static_cast<qreal>(omegaCount - 1);
                const qreal x = rect.left()
                    + rect.width() * static_cast<qreal>(magnitude / scale) * 0.92;
                points.append(QPointF(x, y));
            }
            painter.setBrush(Qt::NoBrush);
            if (points.size() >= 2)
                painter.drawPolyline(points);

            painter.setFont(Font::resolve(this, theme.type().labelSmall, density));
            painter.setPen(colours.onSurfaceVariant);
            painter.drawText(QRectF(rect.left(), rect.top() - density.dp(12.0),
                                    rect.width(), density.dp(12.0)),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             localized(QStringLiteral("chart.laplace.omegaSlice"),
                                       QStringLiteral("|F(σ₀, ω)|")));
        }
    }

    painter.restore();
}

void LaplaceChart::paintMarkers(QPainter& painter, const QRectF& field, qreal progress)
{
    if (field.isEmpty())
        return;
    if (poles_.empty() && zeros_.empty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const qreal pop = clamp01((clamp01(progress) - 0.6) / 0.4);
    if (pop <= 0.0)
        return;

    const qreal size = density.dp(kMarkerSizeDp) * (0.45 + 0.55 * pop);
    const qreal penWidth = std::max<qreal>(1.5, density.dp(kMarkerPenDp));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(field.adjusted(1.0, 1.0, -1.0, -1.0));
    painter.setBrush(Qt::NoBrush);

    const auto drawMarker = [&](const std::complex<double>& point, bool pole) {
        if (!std::isfinite(point.real()) || !std::isfinite(point.imag()))
            return;
        const QPointF pixel = toPixel(point.real(), point.imag());
        if (!field.adjusted(-size, -size, size, size).contains(pixel))
            return;

        QColor colour = pole ? colours.error : colours.primary;
        colour.setAlphaF(static_cast<float>(0.35 + 0.65 * pop));
        painter.setPen(QPen(colour, penWidth, Qt::SolidLine, Qt::RoundCap));

        if (pole) {
            painter.drawLine(QPointF(pixel.x() - size, pixel.y() - size),
                             QPointF(pixel.x() + size, pixel.y() + size));
            painter.drawLine(QPointF(pixel.x() - size, pixel.y() + size),
                             QPointF(pixel.x() + size, pixel.y() - size));
        } else {
            painter.drawEllipse(pixel, size, size);
        }
    };

    for (const std::complex<double>& pole : poles_)
        drawMarker(pole, true);
    for (const std::complex<double>& zero : zeros_)
        drawMarker(zero, false);

    // A compact marker key, only when at least one list has an entry.
    const QFont font = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF metrics(font);
    const bool hasPoles = !poles_.empty();
    const bool hasZeros = !zeros_.empty();
    const QString text = hasPoles && hasZeros
        ? localized(QStringLiteral("chart.laplace.markerKeyBoth"),
                    QStringLiteral("× pole    ○ zero"))
        : (hasPoles ? localized(QStringLiteral("chart.laplace.markerKeyPole"),
                                QStringLiteral("× pole"))
                    : localized(QStringLiteral("chart.laplace.markerKeyZero"),
                                QStringLiteral("○ zero")));

    const qreal padding = density.dp(4.0);
    const QRectF key(field.left() + density.dp(5.0), field.top() + density.dp(5.0),
                     metrics.horizontalAdvance(text) + padding * 2.0,
                     metrics.height() + padding);
    QColor background = colours.surface;
    background.setAlphaF(0.82f);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(key, 3.0, 3.0);
    painter.setFont(font);
    painter.setPen(colours.onSurfaceVariant);
    painter.drawText(key, Qt::AlignCenter, text);

    painter.restore();
}

void LaplaceChart::paintColourBar(QPainter& painter, const QRectF& bar)
{
    if (bar.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient gradient(bar.topLeft(), bar.bottomLeft());
    gradient.setColorAt(0.0, colourForMagnitude(maximumMagnitude_));
    gradient.setColorAt(0.5, colourForMagnitude((minimumMagnitude_ + maximumMagnitude_) / 2.0));
    gradient.setColorAt(1.0, colourForMagnitude(minimumMagnitude_));

    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRect(bar);

    painter.setPen(QPen(colours.outlineVariant, std::max<qreal>(1.0, density.dp(1.0))));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(bar);

    painter.setFont(Font::resolve(this, theme.type().labelSmall, density));
    painter.setPen(colours.onSurfaceVariant);

    const int stops = 5;
    for (int index = 0; index < stops; ++index) {
        const qreal fraction = static_cast<qreal>(index) / static_cast<qreal>(stops - 1);
        const qreal y = bar.top() + fraction * bar.height();
        const double value = maximumMagnitude_
            - (maximumMagnitude_ - minimumMagnitude_) * static_cast<double>(fraction);
        const QRectF label(bar.right() + density.dp(4.0), y - 8.0,
                           density.dp(kColourBarLabelGutterDp) - density.dp(4.0), 16.0);
        painter.drawText(label, Qt::AlignLeft | Qt::AlignVCenter,
                         formatMagnitude(value));
    }

    painter.setPen(colours.onSurfaceVariant);
    painter.drawText(QRectF(bar.left(), bar.top() - density.dp(kColourBarCaptionDp),
                            bar.width() + density.dp(kColourBarLabelGutterDp),
                            density.dp(kColourBarCaptionDp)),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     localized(QStringLiteral("chart.laplace.magnitudeLabel"),
                               QStringLiteral("|F(s)|")));

    painter.restore();
}

void LaplaceChart::paintHover(QPainter& painter, const QRectF& field)
{
    if (!hovering_ || field.isEmpty())
        return;

    const LaplaceSample sample = sampleAt(hoverPosition_);
    if (!sample.valid)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(field);

    QColor crosshair = colours.onSurface;
    crosshair.setAlphaF(static_cast<float>(kHoverCrosshairAlpha));
    QPen pen(crosshair, std::max<qreal>(1.0, density.dp(1.0)), Qt::DashLine);
    pen.setDashPattern({3.0, 3.0});
    painter.setPen(pen);
    painter.drawLine(QPointF(hoverPosition_.x(), field.top()),
                     QPointF(hoverPosition_.x(), field.bottom()));
    painter.drawLine(QPointF(field.left(), hoverPosition_.y()),
                     QPointF(field.right(), hoverPosition_.y()));

    painter.setPen(QPen(colours.onSurface, std::max<qreal>(1.0, density.dp(1.0))));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(hoverPosition_, density.dp(3.0), density.dp(3.0));

    const QFont font = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF metrics(font);
    const QString lineOne = QStringLiteral("σ %1   ω %2")
        .arg(formatTick(sample.sigma), formatTick(sample.omega));
    const QString lineTwo = QStringLiteral("|F| %1   arg %2 rad")
        .arg(formatMagnitude(sample.magnitude), formatTick(sample.phase));

    const qreal padding = density.dp(5.0);
    const qreal width = std::max(metrics.horizontalAdvance(lineOne),
                                 metrics.horizontalAdvance(lineTwo)) + padding * 2.0;
    const qreal height = metrics.height() * 2.0 + padding * 2.0;

    QPointF anchor = hoverPosition_ + QPointF(density.dp(12.0), -height - density.dp(8.0));
    if (anchor.x() + width > field.right())
        anchor.setX(hoverPosition_.x() - width - density.dp(12.0));
    anchor.setX(std::max(field.left() + 1.0, anchor.x()));
    anchor.setY(std::clamp(anchor.y(), field.top() + 1.0,
                           std::max(field.top() + 1.0, field.bottom() - height - 1.0)));

    const QRectF box(anchor, QSizeF(width, height));
    QColor background = colours.surfaceContainerHigh;
    background.setAlphaF(static_cast<float>(kHoverBoxAlpha));
    painter.setPen(QPen(colours.outlineVariant, std::max<qreal>(1.0, density.dp(1.0))));
    painter.setBrush(background);
    painter.drawRoundedRect(box, 4.0, 4.0);

    painter.setFont(font);
    painter.setPen(colours.onSurface);
    painter.drawText(box.adjusted(padding, padding, -padding, -padding),
                     Qt::AlignLeft | Qt::AlignTop, lineOne);
    painter.setPen(colours.onSurfaceVariant);
    painter.drawText(box.adjusted(padding, padding + metrics.height(), -padding, -padding),
                     Qt::AlignLeft | Qt::AlignTop, lineTwo);

    painter.restore();
}

void LaplaceChart::paintNoData(QPainter& painter, const QRectF& plot) const
{
    if (plot.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFont font = Font::resolve(this, theme.type().labelMedium, density);

    painter.save();
    painter.setFont(font);
    painter.setPen(theme.colours().onSurfaceVariant);
    painter.drawText(plot, Qt::AlignCenter,
                     localized(QStringLiteral("chart.laplace.noData"),
                               QStringLiteral("No s-plane spectrum")));
    painter.restore();
}

void LaplaceChart::paintEmptyState(QPainter& painter, const QRectF& plot)
{
    const PainterStateGuard guard(painter);
    try {
        paintNoData(painter, plot);
    } catch (const std::exception& error) {
        qWarning("md::LaplaceChart::paintEmptyState failed: %s", error.what());
    } catch (...) {
        qWarning("md::LaplaceChart::paintEmptyState failed with an unknown exception");
    }
}

void LaplaceChart::mouseMoveEvent(QMouseEvent* event)
{
    try {
        View::mouseMoveEvent(event);
        hoverPosition_ = event->position();
        hovering_ = fieldRect().contains(hoverPosition_);
        requestUpdate();
    } catch (const std::exception& error) {
        qWarning("md::LaplaceChart::mouseMoveEvent failed: %s", error.what());
        hovering_ = false;
    } catch (...) {
        qWarning("md::LaplaceChart::mouseMoveEvent failed with an unknown exception");
        hovering_ = false;
    }
}

void LaplaceChart::leaveEvent(QEvent* event)
{
    try {
        View::leaveEvent(event);
        hovering_ = false;
        requestUpdate();
    } catch (const std::exception& error) {
        qWarning("md::LaplaceChart::leaveEvent failed: %s", error.what());
    } catch (...) {
        qWarning("md::LaplaceChart::leaveEvent failed with an unknown exception");
    }
}

qreal LaplaceChart::revealProgress() const noexcept
{
    // The base owns the run; the chart only reports it.
    return animation().progress();
}

bool LaplaceChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

} // namespace md
