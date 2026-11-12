// Periodicity — the MellinChart component.
//
// One theme snapshot supplies every colour and length: the ramp runs from
// surfaceContainerHighest through primaryContainer to primary, the grid uses
// outlineVariant, the critical line uses tertiary, and the labels use
// onSurfaceVariant. No colour is literal and every length goes through
// Density::dp().
//
// The component evaluates no transform. It only takes magnitudes and phases of
// the caller's samples, maps them through a linear c axis and a linear or
// logarithmic t axis, and paints quads, ticks and a colour bar.

#include <md/components/mellin_chart.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QSizeF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace md {
namespace {

constexpr qreal kPreferredWidthDp = 440.0;
constexpr qreal kPreferredHeightDp = 330.0;

constexpr qreal kPanelInsetDp = 3.0;
constexpr qreal kTickLengthDp = 4.0;
constexpr qreal kLabelGapDp = 3.0;

constexpr qreal kColourBarWidthDp = 10.0;
constexpr qreal kColourBarLabelGutterDp = 38.0;
constexpr qreal kColourBarCaptionDp = 14.0;

constexpr qreal kHoverBoxAlpha = 0.92;
constexpr qreal kHoverCrosshairAlpha = 0.7;

constexpr qreal kMinimumFieldWidthDp = 120.0;
constexpr qreal kMinimumFieldHeightDp = 90.0;

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

[[nodiscard]] QColor rampColour(const QColor& low, const QColor& middle,
                                const QColor& high, qreal fraction)
{
    const qreal t = clamp01(fraction);
    if (t <= 0.5)
        return mixColour(low, middle, t * 2.0);
    return mixColour(middle, high, (t - 0.5) * 2.0);
}

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
        const double snapped = std::abs(value) < epsilon ? 0.0 : value;
        ticks.append(snapped);
        value += step;
    }

    return ticks;
}

} // namespace

MellinChart::MellinChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAxesVisible(false);
    setInteractionFeedbackEnabled(false);
    setLegendVisible(false);

    // The shared chart animation owns the reveal; this chart stages its field
    // and critical line from that progress.
    animation().setDuration(MotionLevel::Level2);
}

MellinChart::~MellinChart() = default;

void MellinChart::setSpectrum(std::vector<double> c, std::vector<double> t,
                              std::vector<std::complex<double>> values)
{
    cs_ = std::move(c);
    ts_ = std::move(t);
    values_ = std::move(values);

    if (!std::is_sorted(cs_.begin(), cs_.end()) || !std::is_sorted(ts_.begin(), ts_.end())) {
        cs_.clear();
        ts_.clear();
        values_.clear();
    }

    bool found = false;
    double minimum = 0.0;
    double maximum = 0.0;

    const std::size_t count = std::min(values_.size(), cs_.size() * ts_.size());
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

const std::vector<double>& MellinChart::realAxis() const noexcept
{
    return cs_;
}

const std::vector<double>& MellinChart::imaginaryAxis() const noexcept
{
    return ts_;
}

const std::vector<std::complex<double>>& MellinChart::values() const noexcept
{
    return values_;
}

bool MellinChart::hasFiniteSample() const noexcept
{
    const std::size_t count = std::min(values_.size(), cs_.size() * ts_.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (std::isfinite(values_.at(index).real())
            && std::isfinite(values_.at(index).imag())) {
            return true;
        }
    }
    return false;
}

bool MellinChart::hasSpectrum() const noexcept
{
    return cs_.size() >= 2 && ts_.size() >= 2 && hasFiniteSample();
}

void MellinChart::setCriticalLine(double c)
{
    const double next = std::isfinite(c)
        ? c
        : std::numeric_limits<double>::quiet_NaN();
    if (criticalLine_ == next)
        return;
    criticalLine_ = next;
    requestUpdate();
}

double MellinChart::criticalLine() const noexcept
{
    return criticalLine_;
}

bool MellinChart::hasCriticalLine() const noexcept
{
    return std::isfinite(criticalLine_);
}

void MellinChart::setLogT(bool logarithmic)
{
    if (logT_ == logarithmic)
        return;
    logT_ = logarithmic;
    requestUpdate();
}

bool MellinChart::isLogT() const noexcept
{
    return logT_;
}

void MellinChart::setColourBarVisible(bool visible)
{
    if (colourBarVisible_ == visible)
        return;
    colourBarVisible_ = visible;
    requestUpdate();
}

bool MellinChart::colourBarVisible() const noexcept
{
    return colourBarVisible_;
}

double MellinChart::minimumMagnitude() const noexcept
{
    return minimumMagnitude_;
}

double MellinChart::maximumMagnitude() const noexcept
{
    return maximumMagnitude_;
}

QColor MellinChart::colourForMagnitude(double magnitude) const
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();

    if (!std::isfinite(magnitude))
        return colours.surfaceContainerHighest;

    const double span = maximumMagnitude_ - minimumMagnitude_;
    qreal fraction = 1.0;
    if (span > 0.0)
        fraction = static_cast<qreal>((magnitude - minimumMagnitude_) / span);

    return rampColour(colours.surfaceContainerHighest, colours.primaryContainer,
                      colours.primary, std::sqrt(clamp01(fraction)));
}

double MellinChart::verticalFraction(double t) const
{
    if (!std::isfinite(t))
        return -1.0;

    if (logT_) {
        if (!(t > 0.0))
            return -1.0;

        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
        for (double value : ts_) {
            if (!(value > 0.0))
                continue;
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        if (!std::isfinite(minimum) || !(maximum > minimum) || !(minimum > 0.0))
            return 0.5;

        const double logMinimum = std::log10(minimum);
        const double logMaximum = std::log10(maximum);
        if (!(logMaximum > logMinimum))
            return 0.5;
        return (std::log10(t) - logMinimum) / (logMaximum - logMinimum);
    }

    if (ts_.size() < 2)
        return -1.0;
    const double span = ts_.back() - ts_.front();
    if (!(span > 0.0))
        return -1.0;
    return (t - ts_.front()) / span;
}

double MellinChart::valueFromVerticalFraction(double fraction) const
{
    if (logT_) {
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
        for (double value : ts_) {
            if (!(value > 0.0))
                continue;
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        if (!std::isfinite(minimum) || !(maximum > minimum) || !(minimum > 0.0))
            return 1.0;

        const double logMinimum = std::log10(minimum);
        const double logMaximum = std::log10(maximum);
        return std::pow(10.0, logMinimum + fraction * (logMaximum - logMinimum));
    }

    if (ts_.size() < 2)
        return 0.0;
    return ts_.front() + fraction * (ts_.back() - ts_.front());
}

QString MellinChart::formatTTick(double t) const
{
    if (!logT_ || !(t > 0.0))
        return formatTick(t);

    const double exponent = std::log10(t);
    const double rounded = std::round(exponent);
    if (std::abs(exponent - rounded) < 1e-6) {
        const int power = static_cast<int>(rounded);
        if (power == 0)
            return QStringLiteral("1");
        return QStringLiteral("10^%1").arg(power);
    }
    return formatTick(t);
}

QVector<double> MellinChart::cTicks(int count) const
{
    if (cs_.size() < 2)
        return {};
    return niceTicks(cs_.front(), cs_.back(), count);
}

QVector<double> MellinChart::tTicks(int count) const
{
    if (ts_.size() < 2)
        return {};

    if (!logT_)
        return niceTicks(ts_.front(), ts_.back(), count);

    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (double value : ts_) {
        if (!(value > 0.0))
            continue;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    if (!std::isfinite(minimum) || !(maximum > minimum) || !(minimum > 0.0))
        return {};

    QVector<double> ticks;
    const int first = static_cast<int>(std::ceil(std::log10(minimum) - 1e-9));
    const int last = static_cast<int>(std::floor(std::log10(maximum) + 1e-9));

    for (int power = first; power <= last; ++power) {
        const double value = std::pow(10.0, power);
        if (value >= minimum * (1.0 - 1e-9) && value <= maximum * (1.0 + 1e-9))
            ticks.append(value);
    }

    if (ticks.size() < 2)
        return niceTicks(minimum, maximum, count);
    return ticks;
}

MellinChart::Layout MellinChart::layoutFor(const QRectF& plot) const
{
    Layout layout;
    if (plot.isEmpty() || plot.width() <= 0.0 || plot.height() <= 0.0)
        return layout;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal gap = density.dp(6.0);

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

    if (content.width() < density.dp(kMinimumFieldWidthDp)
        || content.height() < density.dp(kMinimumFieldHeightDp)) {
        return layout;
    }

    const QFont tickFont = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF tickMetrics(tickFont);
    const QFont axisFont = Font::resolve(this, theme.type().labelMedium, density);
    const QFontMetricsF axisMetrics(axisFont);
    const qreal tickLength = density.dp(kTickLengthDp);
    const qreal labelGap = density.dp(kLabelGapDp);

    qreal widestYLabel = 0.0;
    for (double value : tTicks())
        widestYLabel = std::max(widestYLabel, tickMetrics.horizontalAdvance(formatTTick(value)));

    const qreal leftGutter = widestYLabel + tickLength + labelGap
        + axisMetrics.height() + 2.0;
    const qreal bottomGutter = tickMetrics.height() + tickLength + labelGap
        + axisMetrics.height() + 2.0;

    layout.field = content.adjusted(leftGutter, density.dp(kPanelInsetDp),
                                    -density.dp(kPanelInsetDp), -bottomGutter);

    if (layout.field.width() < 20.0 || layout.field.height() < 20.0)
        layout.field = QRectF();

    return layout;
}

QRectF MellinChart::fieldRect() const
{
    return layoutFor(plotRect()).field;
}

QRectF MellinChart::colourBarRect() const
{
    return layoutFor(plotRect()).colourBar;
}

QPointF MellinChart::toPixel(double c, double t) const
{
    const QRectF field = fieldRect();
    if (field.isEmpty() || cs_.size() < 2)
        return QPointF();
    if (!std::isfinite(c))
        return QPointF();

    const double span = cs_.back() - cs_.front();
    if (!(span > 0.0))
        return QPointF();

    const double yFraction = verticalFraction(t);
    if (yFraction < 0.0)
        return QPointF();

    const qreal xFraction = static_cast<qreal>((c - cs_.front()) / span);
    return QPointF(field.left() + xFraction * field.width(),
                   field.bottom() - static_cast<qreal>(yFraction) * field.height());
}

double MellinChart::sampleMagnitude(int cIndex, int tIndex) const
{
    if (cIndex < 0 || tIndex < 0)
        return std::numeric_limits<double>::quiet_NaN();
    if (cIndex >= static_cast<int>(cs_.size()) || tIndex >= static_cast<int>(ts_.size()))
        return std::numeric_limits<double>::quiet_NaN();

    const std::size_t index = static_cast<std::size_t>(cIndex) * ts_.size()
        + static_cast<std::size_t>(tIndex);
    if (index >= values_.size())
        return std::numeric_limits<double>::quiet_NaN();

    return std::abs(values_.at(index));
}

int MellinChart::nearestIndex(const std::vector<double>& axis, double value) const
{
    if (axis.size() < 2 || !std::isfinite(value))
        return -1;

    const double first = axis.front();
    const double span = axis.back() - first;
    if (!(span > 0.0))
        return -1;

    const double fraction = std::clamp((value - first) / span, 0.0, 1.0);
    const int index = static_cast<int>(std::lround(fraction
                                                   * static_cast<double>(axis.size() - 1)));
    return std::clamp(index, 0, static_cast<int>(axis.size()) - 1);
}

MellinSample MellinChart::sampleAt(QPointF position) const
{
    MellinSample sample;

    const QRectF field = fieldRect();
    if (field.isEmpty() || !field.contains(position))
        return sample;
    if (cs_.size() < 2 || ts_.size() < 2)
        return sample;

    const qreal xFraction = (position.x() - field.left()) / field.width();
    const qreal yFraction = (field.bottom() - position.y()) / field.height();

    const double c = cs_.front() + static_cast<double>(xFraction) * (cs_.back() - cs_.front());

    const int cIndex = nearestIndex(cs_, c);
    if (cIndex < 0)
        return sample;

    // The nearest t index is resolved in the mapped space so a log axis does
    // not bias the choice towards its high end.
    int tIndex = -1;
    double bestDistance = std::numeric_limits<double>::max();
    for (int index = 0; index < static_cast<int>(ts_.size()); ++index) {
        const double mapped = verticalFraction(ts_.at(static_cast<std::size_t>(index)));
        if (mapped < 0.0)
            continue;
        const double distance = std::abs(mapped - static_cast<double>(yFraction));
        if (distance < bestDistance) {
            bestDistance = distance;
            tIndex = index;
        }
    }
    if (tIndex < 0)
        return sample;

    const std::size_t index = static_cast<std::size_t>(cIndex) * ts_.size()
        + static_cast<std::size_t>(tIndex);
    if (index >= values_.size())
        return sample;

    const std::complex<double>& value = values_.at(index);
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
        return sample;

    sample.valid = true;
    sample.c = cs_.at(static_cast<std::size_t>(cIndex));
    sample.t = ts_.at(static_cast<std::size_t>(tIndex));
    sample.magnitude = std::abs(value);
    sample.phase = std::arg(value);
    return sample;
}

void MellinChart::syncPresenceSeries()
{
    QVector<ChartSeries> presence;
    if (hasFiniteSample() && cs_.size() >= 2 && ts_.size() >= 2) {
        ChartSeries marker;
        marker.points.append(QPointF(0.0, 0.0));
        presence.append(marker);
    }
    ChartBase::setSeries(std::move(presence));
}

void MellinChart::measureOverride(MeasureSpec width, MeasureSpec height)
{
    try {
        const Density& density = Theme::forWidget(this).density();
        setMeasuredSize(QSizeF(width.resolve(density.dp(kPreferredWidthDp)),
                               height.resolve(density.dp(kPreferredHeightDp))));
    } catch (const std::exception& error) {
        qWarning("md::MellinChart::measureOverride failed: %s", error.what());
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    } catch (...) {
        qWarning("md::MellinChart::measureOverride failed with an unknown exception");
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    }
}

void MellinChart::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
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

        paintField(painter, layout.field, reveal);
        paintGrid(painter, layout.field);
        paintAxes(painter, layout.field);
        paintCriticalLine(painter, layout.field, reveal);
        if (!layout.colourBar.isEmpty())
            paintColourBar(painter, layout.colourBar);
        paintHover(painter, layout.field);
    } catch (const std::exception& error) {
        qWarning("md::MellinChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::MellinChart::paintPlot failed with an unknown exception");
    }
}

void MellinChart::paintField(QPainter& painter, const QRectF& field, qreal progress)
{
    if (field.isEmpty() || cs_.size() < 2 || ts_.size() < 2)
        return;

    const qreal reveal = clamp01(progress);
    if (reveal <= 0.0)
        return;

    QRectF sweep(field.left(), field.top(), field.width() * reveal, field.height());
    sweep = sweep.intersected(field);
    if (sweep.isEmpty())
        return;

    const int cCount = static_cast<int>(cs_.size());
    const int tCount = static_cast<int>(ts_.size());
    const double cSpan = cs_.back() - cs_.front();
    if (!(cSpan > 0.0))
        return;

    std::vector<qreal> xPositions(static_cast<std::size_t>(cCount));
    std::vector<qreal> yPositions(static_cast<std::size_t>(tCount));
    for (int index = 0; index < cCount; ++index) {
        const double fraction = (cs_.at(static_cast<std::size_t>(index)) - cs_.front()) / cSpan;
        xPositions[static_cast<std::size_t>(index)] =
            field.left() + static_cast<qreal>(fraction) * field.width();
    }
    for (int index = 0; index < tCount; ++index) {
        const double fraction = verticalFraction(ts_.at(static_cast<std::size_t>(index)));
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
        if (magnitudeSpan > 0.0)
            fraction = static_cast<qreal>((magnitude - minimumMagnitude_) / magnitudeSpan);
        return rampColour(low, middle, high, std::sqrt(clamp01(fraction)));
    };

    painter.save();
    painter.setClipRect(sweep);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);

    for (int cIndex = 0; cIndex + 1 < cCount; ++cIndex) {
        const qreal x0 = xPositions.at(static_cast<std::size_t>(cIndex));
        const qreal x1 = xPositions.at(static_cast<std::size_t>(cIndex + 1));

        for (int tIndex = 0; tIndex + 1 < tCount; ++tIndex) {
            const qreal y0 = yPositions.at(static_cast<std::size_t>(tIndex));
            const qreal y1 = yPositions.at(static_cast<std::size_t>(tIndex + 1));

            double sum = 0.0;
            int finite = 0;
            const double corners[4] = {
                sampleMagnitude(cIndex, tIndex),
                sampleMagnitude(cIndex + 1, tIndex),
                sampleMagnitude(cIndex, tIndex + 1),
                sampleMagnitude(cIndex + 1, tIndex + 1),
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

            painter.fillRect(cell.adjusted(-0.5, -0.5, 0.5, 0.5),
                             colourFor(sum / finite));
        }
    }

    painter.restore();
}

void MellinChart::paintGrid(QPainter& painter, const QRectF& field)
{
    if (field.isEmpty() || cs_.size() < 2 || ts_.size() < 2)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setClipRect(field);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor grid = colours.outlineVariant;
    grid.setAlphaF(0.5f);
    painter.setPen(QPen(grid, std::max<qreal>(0.75, density.dp(1.0)), Qt::DotLine));

    for (double c : cTicks()) {
        const qreal x = toPixel(c, ts_.front()).x();
        painter.drawLine(QPointF(x, field.top()), QPointF(x, field.bottom()));
    }
    for (double t : tTicks()) {
        const qreal y = toPixel(cs_.front(), t).y();
        painter.drawLine(QPointF(field.left(), y), QPointF(field.right(), y));
    }

    painter.restore();
}

void MellinChart::paintAxes(QPainter& painter, const QRectF& field)
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

    QColor frame = colours.outline;
    frame.setAlphaF(0.8f);
    painter.setPen(QPen(frame, std::max<qreal>(1.0, density.dp(1.0))));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(field);

    painter.setFont(tickFont);
    painter.setPen(colours.onSurfaceVariant);

    for (double value : cTicks()) {
        const qreal x = toPixel(value, ts_.front()).x();
        painter.drawLine(QPointF(x, field.bottom()), QPointF(x, field.bottom() + tickLength));
        const QRectF label(x - 24.0, field.bottom() + tickLength + 1.0, 48.0,
                           tickMetrics.height());
        painter.drawText(label, Qt::AlignHCenter | Qt::AlignTop, formatTick(value));
    }

    for (double value : tTicks()) {
        const qreal y = toPixel(cs_.front(), value).y();
        painter.drawLine(QPointF(field.left() - tickLength, y), QPointF(field.left(), y));
        const QRectF label(field.left() - tickLength - labelGap - 56.0,
                           y - tickMetrics.height() / 2.0, 56.0, tickMetrics.height());
        painter.drawText(label, Qt::AlignRight | Qt::AlignVCenter, formatTTick(value));
    }

    painter.setFont(axisFont);
    painter.setPen(colours.onSurfaceVariant);

    const QString yCaption = logT_
        ? localized(QStringLiteral("chart.mellin.logT"), QStringLiteral("Im(s) log t"))
        : localized(QStringLiteral("chart.mellin.imaginary"), QStringLiteral("Im(s)"));
    painter.drawText(QRectF(field.left() - tickLength - labelGap - 90.0,
                            field.top() - density.dp(14.0) - 2.0, 90.0,
                            density.dp(14.0)),
                     Qt::AlignRight | Qt::AlignBottom, yCaption);

    painter.drawText(QRectF(field.left(), field.bottom() + tickLength + 1.0
                                + tickMetrics.height() + 1.0,
                            field.width(), density.dp(14.0)),
                     Qt::AlignRight | Qt::AlignTop,
                     localized(QStringLiteral("chart.mellin.real"),
                               QStringLiteral("Re(s)")));

    painter.restore();
}

void MellinChart::paintCriticalLine(QPainter& painter, const QRectF& field, qreal progress)
{
    if (field.isEmpty() || !std::isfinite(criticalLine_))
        return;
    if (cs_.size() < 2 || criticalLine_ < cs_.front() || criticalLine_ > cs_.back())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    if (progress <= 0.0)
        return;

    const qreal x = toPixel(criticalLine_, ts_.front()).x();

    painter.save();
    painter.setClipRect(field);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor line = colours.tertiary;
    line.setAlphaF(0.95f);
    QPen pen(line, std::max<qreal>(1.25, density.dp(1.25)), Qt::DashLine);
    pen.setDashPattern({5.0, 4.0});
    painter.setPen(pen);

    // The line draws on from the bottom, matching the field's left-to-right
    // sweep rather than appearing all at once.
    painter.drawLine(QPointF(x, field.bottom()),
                     QPointF(x, field.bottom() - field.height() * progress));

    if (progress > 0.6) {
        const QFont font = Font::resolve(this, theme.type().labelSmall, density);
        const QFontMetricsF metrics(font);
        const QString caption = QStringLiteral("c = %1").arg(formatTick(criticalLine_));
        const qreal width = metrics.horizontalAdvance(caption) + density.dp(8.0);
        QRectF box(x + density.dp(4.0), field.top() + density.dp(4.0), width,
                   metrics.height() + density.dp(4.0));
        if (box.right() > field.right())
            box.moveLeft(x - width - density.dp(4.0));
        if (box.left() < field.left())
            box.moveLeft(field.left() + 1.0);

        QColor background = colours.surfaceContainerHigh;
        background.setAlphaF(0.9f);
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(box, 3.0, 3.0);

        painter.setFont(font);
        painter.setPen(colours.tertiary);
        painter.drawText(box, Qt::AlignCenter, caption);
    }

    painter.restore();
}

void MellinChart::paintColourBar(QPainter& painter, const QRectF& bar)
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
                     localized(QStringLiteral("chart.mellin.magnitudeLabel"),
                               QStringLiteral("|F(s)|")));

    painter.restore();
}

void MellinChart::paintHover(QPainter& painter, const QRectF& field)
{
    if (!hovering_ || field.isEmpty())
        return;

    const MellinSample sample = sampleAt(hoverPosition_);
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
    const QString lineOne = QStringLiteral("c %1   t %2")
        .arg(formatTick(sample.c), formatTick(sample.t));
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

void MellinChart::paintNoData(QPainter& painter, const QRectF& plot) const
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
                     localized(QStringLiteral("chart.mellin.noData"),
                               QStringLiteral("No Mellin spectrum")));
    painter.restore();
}

void MellinChart::paintEmptyState(QPainter& painter, const QRectF& plot)
{
    const PainterStateGuard guard(painter);
    try {
        paintNoData(painter, plot);
    } catch (const std::exception& error) {
        qWarning("md::MellinChart::paintEmptyState failed: %s", error.what());
    } catch (...) {
        qWarning("md::MellinChart::paintEmptyState failed with an unknown exception");
    }
}

void MellinChart::mouseMoveEvent(QMouseEvent* event)
{
    try {
        View::mouseMoveEvent(event);
        hoverPosition_ = event->position();
        hovering_ = fieldRect().contains(hoverPosition_);
        requestUpdate();
    } catch (const std::exception& error) {
        qWarning("md::MellinChart::mouseMoveEvent failed: %s", error.what());
        hovering_ = false;
    } catch (...) {
        qWarning("md::MellinChart::mouseMoveEvent failed with an unknown exception");
        hovering_ = false;
    }
}

void MellinChart::leaveEvent(QEvent* event)
{
    try {
        View::leaveEvent(event);
        hovering_ = false;
        requestUpdate();
    } catch (const std::exception& error) {
        qWarning("md::MellinChart::leaveEvent failed: %s", error.what());
    } catch (...) {
        qWarning("md::MellinChart::leaveEvent failed with an unknown exception");
    }
}

qreal MellinChart::revealProgress() const noexcept
{
    return animation().progress();
}

bool MellinChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

} // namespace md
