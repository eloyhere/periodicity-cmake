// Periodicity — the ZTransformChart component.
//
// The plane is square and centred on the origin: the same pixels-per-unit scale
// is used on both axes, so the unit circle is drawn as a circle. The domain is
// the smallest symmetric square that contains the unit circle, every finite
// sample and every pole/zero, plus a small margin.
//
// Every colour comes from the theme snapshot: the ramp from
// surfaceContainerHighest through primaryContainer to primary, outlineVariant
// for the grid, secondary for the unit circle, error for poles, primary for
// zeros and onSurfaceVariant for the labels. No colour is literal and every
// length passes through Density::dp().

#include <md/components/z_transform_chart.h>

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

constexpr qreal kPi = 3.14159265358979323846;

constexpr qreal kPreferredWidthDp = 430.0;
constexpr qreal kPreferredHeightDp = 360.0;

constexpr qreal kPanelInsetDp = 3.0;
constexpr qreal kTickLengthDp = 4.0;
constexpr qreal kLabelGapDp = 3.0;

constexpr qreal kColourBarWidthDp = 10.0;
constexpr qreal kColourBarLabelGutterDp = 38.0;
constexpr qreal kColourBarCaptionDp = 14.0;

constexpr qreal kMarkerSizeDp = 5.0;
constexpr qreal kMarkerPenDp = 2.0;
constexpr qreal kHoverBoxAlpha = 0.92;
constexpr qreal kHoverCrosshairAlpha = 0.7;

constexpr qreal kMinimumPlaneDp = 110.0;
constexpr qreal kDefaultExtent = 1.25;

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

ZTransformChart::ZTransformChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAxesVisible(false);
    setInteractionFeedbackEnabled(false);
    setLegendVisible(false);

    // The shared chart animation owns the reveal; this chart stages its field,
    // unit circle and markers from that progress.
    animation().setDuration(MotionLevel::Level2);
}

ZTransformChart::~ZTransformChart() = default;

void ZTransformChart::setSpectrum(std::vector<double> re, std::vector<double> im,
                                  std::vector<std::complex<double>> values)
{
    re_ = std::move(re);
    im_ = std::move(im);
    values_ = std::move(values);

    if (!std::is_sorted(re_.begin(), re_.end()) || !std::is_sorted(im_.begin(), im_.end())) {
        re_.clear();
        im_.clear();
        values_.clear();
    }

    bool found = false;
    double minimum = 0.0;
    double maximum = 0.0;

    const std::size_t count = std::min(values_.size(), re_.size() * im_.size());
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

const std::vector<double>& ZTransformChart::realAxis() const noexcept
{
    return re_;
}

const std::vector<double>& ZTransformChart::imaginaryAxis() const noexcept
{
    return im_;
}

const std::vector<std::complex<double>>& ZTransformChart::values() const noexcept
{
    return values_;
}

bool ZTransformChart::hasFiniteSample() const noexcept
{
    const std::size_t count = std::min(values_.size(), re_.size() * im_.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (std::isfinite(values_.at(index).real())
            && std::isfinite(values_.at(index).imag())) {
            return true;
        }
    }
    return false;
}

bool ZTransformChart::hasSpectrum() const noexcept
{
    return re_.size() >= 2 && im_.size() >= 2 && hasFiniteSample();
}

void ZTransformChart::setPoles(std::vector<std::complex<double>> poles)
{
    poles_ = std::move(poles);
    requestUpdate();
}

void ZTransformChart::setZeros(std::vector<std::complex<double>> zeros)
{
    zeros_ = std::move(zeros);
    requestUpdate();
}

const std::vector<std::complex<double>>& ZTransformChart::poles() const noexcept
{
    return poles_;
}

const std::vector<std::complex<double>>& ZTransformChart::zeros() const noexcept
{
    return zeros_;
}

QPointF ZTransformChart::polePixel(int index) const
{
    if (index < 0 || index >= static_cast<int>(poles_.size()))
        return QPointF();
    const std::complex<double>& pole = poles_.at(static_cast<std::size_t>(index));
    if (!std::isfinite(pole.real()) || !std::isfinite(pole.imag()))
        return QPointF();
    return toPixel(pole.real(), pole.imag());
}

QPointF ZTransformChart::zeroPixel(int index) const
{
    if (index < 0 || index >= static_cast<int>(zeros_.size()))
        return QPointF();
    const std::complex<double>& zero = zeros_.at(static_cast<std::size_t>(index));
    if (!std::isfinite(zero.real()) || !std::isfinite(zero.imag()))
        return QPointF();
    return toPixel(zero.real(), zero.imag());
}

double ZTransformChart::domainExtent() const
{
    double extent = kDefaultExtent;

    for (double value : re_)
        extent = std::max(extent, std::abs(value));
    for (double value : im_)
        extent = std::max(extent, std::abs(value));
    for (const std::complex<double>& value : poles_)
        extent = std::max(extent, std::abs(value));
    for (const std::complex<double>& value : zeros_)
        extent = std::max(extent, std::abs(value));

    return extent * 1.08;
}

double ZTransformChart::plottedMagnitude(double magnitude) const
{
    if (!std::isfinite(magnitude))
        return magnitude;
    return logMagnitude_ ? std::log10(1.0 + std::max(0.0, magnitude)) : magnitude;
}

double ZTransformChart::unitCircleRadius() const
{
    const QRectF plane = zPlaneRect();
    if (plane.isEmpty())
        return 0.0;
    const double extent = domainExtent();
    if (!(extent > 0.0))
        return 0.0;
    return plane.width() / (2.0 * extent);
}

void ZTransformChart::setLogMagnitude(bool logarithmic)
{
    if (logMagnitude_ == logarithmic)
        return;
    logMagnitude_ = logarithmic;
    requestUpdate();
}

bool ZTransformChart::isLogMagnitude() const noexcept
{
    return logMagnitude_;
}

void ZTransformChart::setColourBarVisible(bool visible)
{
    if (colourBarVisible_ == visible)
        return;
    colourBarVisible_ = visible;
    requestUpdate();
}

bool ZTransformChart::colourBarVisible() const noexcept
{
    return colourBarVisible_;
}

double ZTransformChart::minimumMagnitude() const noexcept
{
    return minimumMagnitude_;
}

double ZTransformChart::maximumMagnitude() const noexcept
{
    return maximumMagnitude_;
}

QColor ZTransformChart::colourForMagnitude(double magnitude) const
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();

    if (!std::isfinite(magnitude))
        return colours.surfaceContainerHighest;

    const double minimum = plottedMagnitude(minimumMagnitude_);
    const double maximum = plottedMagnitude(maximumMagnitude_);
    const double span = maximum - minimum;

    qreal fraction = 1.0;
    if (span > 0.0)
        fraction = static_cast<qreal>((plottedMagnitude(magnitude) - minimum) / span);

    return rampColour(colours.surfaceContainerHighest, colours.primaryContainer,
                      colours.primary, std::sqrt(clamp01(fraction)));
}

QVector<double> ZTransformChart::realTicks(int count) const
{
    const double extent = domainExtent();
    return niceTicks(-extent, extent, count);
}

QVector<double> ZTransformChart::imaginaryTicks(int count) const
{
    const double extent = domainExtent();
    return niceTicks(-extent, extent, count);
}

ZTransformChart::Layout ZTransformChart::layoutFor(const QRectF& plot) const
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

    const QFont tickFont = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF tickMetrics(tickFont);
    const QFont axisFont = Font::resolve(this, theme.type().labelMedium, density);
    const QFontMetricsF axisMetrics(axisFont);
    const qreal tickLength = density.dp(kTickLengthDp);
    const qreal labelGap = density.dp(kLabelGapDp);

    qreal widestYLabel = 0.0;
    for (double value : imaginaryTicks())
        widestYLabel = std::max(widestYLabel, tickMetrics.horizontalAdvance(formatTick(value)));

    const qreal leftGutter = widestYLabel + tickLength + labelGap
        + axisMetrics.height() + 2.0;
    const qreal bottomGutter = tickMetrics.height() + tickLength + labelGap
        + axisMetrics.height() + 2.0;

    QRectF available = content.adjusted(leftGutter, density.dp(14.0),
                                        -density.dp(kPanelInsetDp), -bottomGutter);

    if (available.width() < density.dp(kMinimumPlaneDp)
        || available.height() < density.dp(kMinimumPlaneDp)) {
        return layout;
    }

    const qreal side = std::min(available.width(), available.height());
    layout.plane = QRectF(available.left() + (available.width() - side) / 2.0,
                          available.top() + (available.height() - side) / 2.0,
                          side, side);

    if (layout.plane.width() < 40.0)
        layout.plane = QRectF();

    return layout;
}

QRectF ZTransformChart::zPlaneRect() const
{
    return layoutFor(plotRect()).plane;
}

QRectF ZTransformChart::colourBarRect() const
{
    return layoutFor(plotRect()).colourBar;
}

QPointF ZTransformChart::toPixel(double real, double imaginary) const
{
    const QRectF plane = zPlaneRect();
    if (plane.isEmpty() || !std::isfinite(real) || !std::isfinite(imaginary))
        return QPointF();

    const double extent = domainExtent();
    if (!(extent > 0.0))
        return QPointF();

    const qreal scale = plane.width() / (2.0 * static_cast<qreal>(extent));
    return QPointF(plane.center().x() + static_cast<qreal>(real) * scale,
                   plane.center().y() - static_cast<qreal>(imaginary) * scale);
}

double ZTransformChart::sampleMagnitude(int realIndex, int imaginaryIndex) const
{
    if (realIndex < 0 || imaginaryIndex < 0)
        return std::numeric_limits<double>::quiet_NaN();
    if (realIndex >= static_cast<int>(re_.size())
        || imaginaryIndex >= static_cast<int>(im_.size())) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const std::size_t index = static_cast<std::size_t>(realIndex) * im_.size()
        + static_cast<std::size_t>(imaginaryIndex);
    if (index >= values_.size())
        return std::numeric_limits<double>::quiet_NaN();

    return std::abs(values_.at(index));
}

int ZTransformChart::nearestIndex(const std::vector<double>& axis, double value) const
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

ZSample ZTransformChart::sampleAt(QPointF position) const
{
    ZSample sample;

    const QRectF plane = zPlaneRect();
    if (plane.isEmpty() || !plane.contains(position))
        return sample;
    if (re_.size() < 2 || im_.size() < 2)
        return sample;

    const double extent = domainExtent();
    if (!(extent > 0.0))
        return sample;

    const double scale = plane.width() / (2.0 * extent);
    if (!(scale > 0.0))
        return sample;

    const double real = (position.x() - plane.center().x()) / scale;
    const double imaginary = (plane.center().y() - position.y()) / scale;

    const int realIndex = nearestIndex(re_, real);
    const int imaginaryIndex = nearestIndex(im_, imaginary);
    if (realIndex < 0 || imaginaryIndex < 0)
        return sample;

    const std::size_t index = static_cast<std::size_t>(realIndex) * im_.size()
        + static_cast<std::size_t>(imaginaryIndex);
    if (index >= values_.size())
        return sample;

    const std::complex<double>& value = values_.at(index);
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
        return sample;

    sample.valid = true;
    sample.real = re_.at(static_cast<std::size_t>(realIndex));
    sample.imaginary = im_.at(static_cast<std::size_t>(imaginaryIndex));
    sample.magnitude = std::abs(value);
    sample.phase = std::arg(value);
    return sample;
}

void ZTransformChart::syncPresenceSeries()
{
    QVector<ChartSeries> presence;
    if (hasFiniteSample() && re_.size() >= 2 && im_.size() >= 2) {
        ChartSeries marker;
        marker.points.append(QPointF(0.0, 0.0));
        presence.append(marker);
    }
    ChartBase::setSeries(std::move(presence));
}

void ZTransformChart::measureOverride(MeasureSpec width, MeasureSpec height)
{
    try {
        const Density& density = Theme::forWidget(this).density();
        setMeasuredSize(QSizeF(width.resolve(density.dp(kPreferredWidthDp)),
                               height.resolve(density.dp(kPreferredHeightDp))));
    } catch (const std::exception& error) {
        qWarning("md::ZTransformChart::measureOverride failed: %s", error.what());
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    } catch (...) {
        qWarning("md::ZTransformChart::measureOverride failed with an unknown exception");
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    }
}

void ZTransformChart::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    if (plot.isEmpty())
        return;

    const qreal reveal = clamp01(progress);
    if (reveal <= 0.0)
        return;

    const PainterStateGuard guard(painter);

    try {
        const Layout layout = layoutFor(plot);
        if (layout.plane.isEmpty())
            return;

        paintField(painter, layout.plane, reveal);
        paintGrid(painter, layout.plane);
        paintUnitCircle(painter, layout.plane, reveal);
        paintAxes(painter, layout.plane);
        paintMarkers(painter, layout.plane, reveal);
        if (!layout.colourBar.isEmpty())
            paintColourBar(painter, layout.colourBar);
        paintHover(painter, layout.plane);
    } catch (const std::exception& error) {
        qWarning("md::ZTransformChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::ZTransformChart::paintPlot failed with an unknown exception");
    }
}

void ZTransformChart::paintField(QPainter& painter, const QRectF& plane, qreal progress)
{
    if (plane.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const QColor background = theme.colours().surfaceContainerLow;
    painter.fillRect(plane, background);

    if (re_.size() < 2 || im_.size() < 2)
        return;

    const qreal reveal = clamp01(progress);
    if (reveal <= 0.0)
        return;

    QRectF sweep(plane.left(), plane.top(), plane.width() * reveal, plane.height());
    sweep = sweep.intersected(plane);
    if (sweep.isEmpty())
        return;

    const int realCount = static_cast<int>(re_.size());
    const int imaginaryCount = static_cast<int>(im_.size());

    std::vector<qreal> xPositions(static_cast<std::size_t>(realCount));
    std::vector<qreal> yPositions(static_cast<std::size_t>(imaginaryCount));
    for (int index = 0; index < realCount; ++index)
        xPositions[static_cast<std::size_t>(index)] =
            toPixel(re_.at(static_cast<std::size_t>(index)), 0.0).x();
    for (int index = 0; index < imaginaryCount; ++index)
        yPositions[static_cast<std::size_t>(index)] =
            toPixel(0.0, im_.at(static_cast<std::size_t>(index))).y();

    const ColorScheme& colours = theme.colours();
    const QColor low = colours.surfaceContainerHighest;
    const QColor middle = colours.primaryContainer;
    const QColor high = colours.primary;
    const double minimum = plottedMagnitude(minimumMagnitude_);
    const double maximum = plottedMagnitude(maximumMagnitude_);
    const double span = maximum - minimum;

    const auto colourFor = [&](double magnitude) {
        qreal fraction = 1.0;
        if (span > 0.0) {
            fraction = static_cast<qreal>((plottedMagnitude(magnitude) - minimum) / span);
        }
        return rampColour(low, middle, high, std::sqrt(clamp01(fraction)));
    };

    painter.save();
    painter.setClipRect(sweep);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);

    for (int realIndex = 0; realIndex + 1 < realCount; ++realIndex) {
        const qreal x0 = xPositions.at(static_cast<std::size_t>(realIndex));
        const qreal x1 = xPositions.at(static_cast<std::size_t>(realIndex + 1));

        for (int imaginaryIndex = 0; imaginaryIndex + 1 < imaginaryCount; ++imaginaryIndex) {
            const qreal y0 = yPositions.at(static_cast<std::size_t>(imaginaryIndex));
            const qreal y1 = yPositions.at(static_cast<std::size_t>(imaginaryIndex + 1));

            double sum = 0.0;
            int finite = 0;
            const double corners[4] = {
                sampleMagnitude(realIndex, imaginaryIndex),
                sampleMagnitude(realIndex + 1, imaginaryIndex),
                sampleMagnitude(realIndex, imaginaryIndex + 1),
                sampleMagnitude(realIndex + 1, imaginaryIndex + 1),
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

void ZTransformChart::paintGrid(QPainter& painter, const QRectF& plane)
{
    if (plane.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setClipRect(plane);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor grid = colours.outlineVariant;
    grid.setAlphaF(0.55f);
    painter.setPen(QPen(grid, std::max<qreal>(0.75, density.dp(1.0)), Qt::DotLine));

    for (double value : realTicks()) {
        const qreal x = toPixel(value, 0.0).x();
        painter.drawLine(QPointF(x, plane.top()), QPointF(x, plane.bottom()));
    }
    for (double value : imaginaryTicks()) {
        const qreal y = toPixel(0.0, value).y();
        painter.drawLine(QPointF(plane.left(), y), QPointF(plane.right(), y));
    }

    // Emphasize the real and imaginary axes through the origin.
    QColor axis = colours.outline;
    axis.setAlphaF(0.85f);
    painter.setPen(QPen(axis, std::max<qreal>(1.0, density.dp(1.0))));
    const QPointF origin = toPixel(0.0, 0.0);
    painter.drawLine(QPointF(origin.x(), plane.top()), QPointF(origin.x(), plane.bottom()));
    painter.drawLine(QPointF(plane.left(), origin.y()), QPointF(plane.right(), origin.y()));

    painter.restore();
}

void ZTransformChart::paintUnitCircle(QPainter& painter, const QRectF& plane, qreal progress)
{
    if (plane.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    if (progress <= 0.0)
        return;

    const qreal radius = static_cast<qreal>(unitCircleRadius());
    if (!(radius > 0.0))
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(plane);

    QColor ring = colours.secondary;
    ring.setAlphaF(0.95f);
    painter.setPen(QPen(ring, std::max<qreal>(1.25, density.dp(1.25))));
    painter.setBrush(Qt::NoBrush);

    const QRectF circle(plane.center().x() - radius, plane.center().y() - radius,
                        radius * 2.0, radius * 2.0);

    // The ring draws on clockwise from the top, like a compass sweep.
    const int span = -static_cast<int>(std::lround(360.0 * 16.0 * progress));
    painter.drawArc(circle, 90 * 16, span);

    if (progress > 0.7) {
        const QFont font = Font::resolve(this, theme.type().labelSmall, density);
        painter.setFont(font);
        painter.setPen(colours.onSurfaceVariant);
        const QString caption = localized(QStringLiteral("chart.z.unitCircle"),
                                          QStringLiteral("|z| = 1"));
        const QPointF anchor(plane.center().x() + radius * 0.72,
                             plane.center().y() - radius * 0.72);
        painter.drawText(QRectF(anchor.x() + 4.0, anchor.y() - 8.0, 70.0, 16.0),
                         Qt::AlignLeft | Qt::AlignVCenter, caption);
    }

    painter.restore();
}

void ZTransformChart::paintAxes(QPainter& painter, const QRectF& plane)
{
    if (plane.isEmpty())
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
    painter.drawRect(plane);

    painter.setFont(tickFont);
    painter.setPen(colours.onSurfaceVariant);

    for (double value : realTicks()) {
        if (std::abs(value) < 1e-9)
            continue;
        const qreal x = toPixel(value, 0.0).x();
        painter.drawLine(QPointF(x, plane.bottom()), QPointF(x, plane.bottom() + tickLength));
        const QRectF label(x - 24.0, plane.bottom() + tickLength + 1.0, 48.0,
                           tickMetrics.height());
        painter.drawText(label, Qt::AlignHCenter | Qt::AlignTop, formatTick(value));
    }

    for (double value : imaginaryTicks()) {
        if (std::abs(value) < 1e-9)
            continue;
        const qreal y = toPixel(0.0, value).y();
        painter.drawLine(QPointF(plane.left() - tickLength, y), QPointF(plane.left(), y));
        const QRectF label(plane.left() - tickLength - labelGap - 48.0,
                           y - tickMetrics.height() / 2.0, 48.0, tickMetrics.height());
        painter.drawText(label, Qt::AlignRight | Qt::AlignVCenter, formatTick(value));
    }

    painter.setFont(axisFont);
    painter.setPen(colours.onSurfaceVariant);

    painter.drawText(QRectF(plane.left() - tickLength - labelGap - 80.0,
                            plane.top() - density.dp(14.0) - 2.0, 80.0,
                            density.dp(14.0)),
                     Qt::AlignRight | Qt::AlignBottom,
                     localized(QStringLiteral("chart.z.imaginary"), QStringLiteral("Im z")));

    painter.drawText(QRectF(plane.left(), plane.bottom() + tickLength + 1.0
                                + tickMetrics.height() + 1.0,
                            plane.width(), density.dp(14.0)),
                     Qt::AlignRight | Qt::AlignTop,
                     localized(QStringLiteral("chart.z.real"), QStringLiteral("Re z")));

    painter.restore();
}

void ZTransformChart::paintMarkers(QPainter& painter, const QRectF& plane, qreal progress)
{
    if (plane.isEmpty())
        return;
    if (poles_.empty() && zeros_.empty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const qreal pop = clamp01((clamp01(progress) - 0.55) / 0.45);
    if (pop <= 0.0)
        return;

    const qreal size = density.dp(kMarkerSizeDp) * (0.45 + 0.55 * pop);
    const qreal penWidth = std::max<qreal>(1.5, density.dp(kMarkerPenDp));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(plane.adjusted(1.0, 1.0, -1.0, -1.0));
    painter.setBrush(Qt::NoBrush);

    const auto drawMarker = [&](const std::complex<double>& point, bool pole) {
        if (!std::isfinite(point.real()) || !std::isfinite(point.imag()))
            return;
        const QPointF pixel = toPixel(point.real(), point.imag());
        if (!plane.adjusted(-size, -size, size, size).contains(pixel))
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

    const QFont font = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF metrics(font);
    const bool hasPoles = !poles_.empty();
    const bool hasZeros = !zeros_.empty();
    const QString text = hasPoles && hasZeros
        ? localized(QStringLiteral("chart.z.markerKeyBoth"),
                    QStringLiteral("× pole    ○ zero"))
        : (hasPoles ? localized(QStringLiteral("chart.z.markerKeyPole"),
                                QStringLiteral("× pole"))
                    : localized(QStringLiteral("chart.z.markerKeyZero"),
                                QStringLiteral("○ zero")));

    const qreal padding = density.dp(4.0);
    const QRectF key(plane.left() + density.dp(5.0), plane.top() + density.dp(5.0),
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

void ZTransformChart::paintColourBar(QPainter& painter, const QRectF& bar)
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
                     localized(QStringLiteral("chart.z.magnitudeLabel"),
                               QStringLiteral("|H(z)|")));

    painter.restore();
}

void ZTransformChart::paintHover(QPainter& painter, const QRectF& plane)
{
    if (!hovering_ || plane.isEmpty())
        return;

    const ZSample sample = sampleAt(hoverPosition_);
    if (!sample.valid)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(plane);

    QColor crosshair = colours.onSurface;
    crosshair.setAlphaF(static_cast<float>(kHoverCrosshairAlpha));
    QPen pen(crosshair, std::max<qreal>(1.0, density.dp(1.0)), Qt::DashLine);
    pen.setDashPattern({3.0, 3.0});
    painter.setPen(pen);
    painter.drawLine(QPointF(hoverPosition_.x(), plane.top()),
                     QPointF(hoverPosition_.x(), plane.bottom()));
    painter.drawLine(QPointF(plane.left(), hoverPosition_.y()),
                     QPointF(plane.right(), hoverPosition_.y()));

    painter.setPen(QPen(colours.onSurface, std::max<qreal>(1.0, density.dp(1.0))));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(hoverPosition_, density.dp(3.0), density.dp(3.0));

    const QFont font = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF metrics(font);
    const double modulus = std::hypot(sample.real, sample.imaginary);
    const QString lineOne = QStringLiteral("|z| %1   arg z %2 rad")
        .arg(formatTick(modulus), formatTick(sample.phase));
    const QString lineTwo = QStringLiteral("|H| %1").arg(formatMagnitude(sample.magnitude));

    const qreal padding = density.dp(5.0);
    const qreal width = std::max(metrics.horizontalAdvance(lineOne),
                                 metrics.horizontalAdvance(lineTwo)) + padding * 2.0;
    const qreal height = metrics.height() * 2.0 + padding * 2.0;

    QPointF anchor = hoverPosition_ + QPointF(density.dp(12.0), -height - density.dp(8.0));
    if (anchor.x() + width > plane.right())
        anchor.setX(hoverPosition_.x() - width - density.dp(12.0));
    anchor.setX(std::max(plane.left() + 1.0, anchor.x()));
    anchor.setY(std::clamp(anchor.y(), plane.top() + 1.0,
                           std::max(plane.top() + 1.0, plane.bottom() - height - 1.0)));

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

void ZTransformChart::paintNoData(QPainter& painter, const QRectF& plot) const
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
                     localized(QStringLiteral("chart.z.noData"),
                               QStringLiteral("No z-plane spectrum")));
    painter.restore();
}

void ZTransformChart::paintEmptyState(QPainter& painter, const QRectF& plot)
{
    const PainterStateGuard guard(painter);
    try {
        paintNoData(painter, plot);
    } catch (const std::exception& error) {
        qWarning("md::ZTransformChart::paintEmptyState failed: %s", error.what());
    } catch (...) {
        qWarning("md::ZTransformChart::paintEmptyState failed with an unknown exception");
    }
}

void ZTransformChart::mouseMoveEvent(QMouseEvent* event)
{
    try {
        View::mouseMoveEvent(event);
        hoverPosition_ = event->position();
        hovering_ = zPlaneRect().contains(hoverPosition_);
        requestUpdate();
    } catch (const std::exception& error) {
        qWarning("md::ZTransformChart::mouseMoveEvent failed: %s", error.what());
        hovering_ = false;
    } catch (...) {
        qWarning("md::ZTransformChart::mouseMoveEvent failed with an unknown exception");
        hovering_ = false;
    }
}

void ZTransformChart::leaveEvent(QEvent* event)
{
    try {
        View::leaveEvent(event);
        hovering_ = false;
        requestUpdate();
    } catch (const std::exception& error) {
        qWarning("md::ZTransformChart::leaveEvent failed: %s", error.what());
    } catch (...) {
        qWarning("md::ZTransformChart::leaveEvent failed with an unknown exception");
    }
}

qreal ZTransformChart::revealProgress() const noexcept
{
    return animation().progress();
}

bool ZTransformChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

} // namespace md
