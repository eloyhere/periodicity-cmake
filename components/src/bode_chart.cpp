// Periodicity — the BodeChart component.
//
// One frame, two plot areas. The magnitude and phase areas share the x range
// and every vertical grid line, so a feature in one trace lines up with the
// other. Each area keeps its own y grid, tick labels and rotated axis title;
// the x tick labels and title are painted once, under the phase area.
//
// The frame is painted here rather than by MathematicsCoordination because the
// frequency axis is logarithmic by default, which the shared linear transform
// cannot express. Nice ticks and label formatting still come from
// MathematicsCoordination, so the numeric style matches the Cartesian charts.
//
// Phase wrap handling. Stored phase is already wrapped into −180..180. A path
// is broken when consecutive samples jump by more than 180 degrees, which is
// exactly a wrap rather than a real phase transition.
//
// Reveal is a horizontal clip applied independently to both areas. The frame
// is painted in full, so a partially revealed chart still reads as a chart.

#include <md/components/bode_chart.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
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

constexpr qreal kAxisTickLengthDp = 4.0;
constexpr qreal kAxisLabelGapDp = 4.0;
constexpr qreal kMinimumLeftGutterDp = 34.0;
constexpr qreal kRightInsetDp = 8.0;
constexpr qreal kTopInsetDp = 8.0;
constexpr qreal kAreaGapDp = 18.0;
constexpr qreal kGridWidthDp = 1.0;
constexpr qreal kAxisWidthDp = 1.0;
constexpr qreal kTraceWidthDp = 2.0;
constexpr qreal kMinimumPenWidth = 0.75;
constexpr qreal kGridAlpha = 0.65;
constexpr qreal kMinorGridAlpha = 0.32;
constexpr qreal kMagnitudeShare = 0.56;

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

[[nodiscard]] QFont tickFont(const QWidget* context, const Theme& theme)
{
    return Font::resolve(context, theme.type().labelSmall, theme.density());
}

[[nodiscard]] QFont axisTitleFont(const QWidget* context, const Theme& theme)
{
    return Font::resolve(context, theme.type().labelMedium, theme.density());
}

[[nodiscard]] QString formatFrequency(double hertz)
{
    if (!std::isfinite(hertz))
        return QStringLiteral("—");
    if (hertz < 0.0)
        hertz = 0.0;

    if (hertz >= 1.0e6)
        return QStringLiteral("%1 MHz").arg(QString::number(hertz / 1.0e6, 'g', 4));
    if (hertz >= 1.0e3)
        return QStringLiteral("%1 kHz").arg(QString::number(hertz / 1.0e3, 'g', 4));
    if (hertz >= 100.0)
        return QStringLiteral("%1 Hz").arg(QString::number(hertz, 'f', 0));
    if (hertz >= 1.0)
        return QStringLiteral("%1 Hz").arg(QString::number(hertz, 'f', 1));
    if (hertz > 0.0)
        return QStringLiteral("%1 Hz").arg(QString::number(hertz, 'f', 2));

    return QStringLiteral("0 Hz");
}

[[nodiscard]] double wrappedPhase(double degrees)
{
    if (!std::isfinite(degrees))
        return 0.0;

    double wrapped = std::fmod(degrees + 180.0, 360.0);
    if (wrapped < 0.0)
        wrapped += 360.0;
    return wrapped - 180.0;
}

[[nodiscard]] QVector<double> decadeTicks(double minimum, double maximum)
{
    QVector<double> ticks;
    if (!(minimum > 0.0) || !(maximum > minimum))
        return ticks;

    const int first = static_cast<int>(std::ceil(std::log10(minimum) - 1.0e-12));
    const int last = static_cast<int>(std::floor(std::log10(maximum) + 1.0e-12));

    for (int exponent = first; exponent <= last; ++exponent) {
        const double value = std::pow(10.0, exponent);
        if (std::isfinite(value) && value >= minimum * (1.0 - 1.0e-12)
            && value <= maximum * (1.0 + 1.0e-12)) {
            ticks.append(value);
        }
    }

    return ticks;
}

[[nodiscard]] qreal tickStep(const QVector<double>& ticks)
{
    if (ticks.size() >= 2) {
        const double step = ticks.at(1) - ticks.at(0);
        if (step > 0.0 && std::isfinite(step))
            return static_cast<qreal>(step);
    }
    return 1.0;
}

[[nodiscard]] QVector<double> phaseTickValues()
{
    return {-180.0, -90.0, 0.0, 90.0, 180.0};
}

} // namespace

BodeChart::BodeChart(QWidget* parent)
    : ChartBase(parent)
{
    // The stacked frame is painted by this component; the base would reserve
    // Cartesian gutters and paint a single plot area.
    setAxesVisible(false);

    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(false);
    setInteractionFeedbackEnabled(false);

    math()->setAutoFit(false);

    // Both traces sweep in over the level-2 chart motion.
    animation().setDuration(MotionLevel::Level2);
}

void BodeChart::setResponse(std::vector<double> frequencies, std::vector<double> magnitudeDb,
                            std::vector<double> phaseDegrees)
{
    const std::size_t count =
        std::min({frequencies.size(), magnitudeDb.size(), phaseDegrees.size()});

    struct Sample
    {
        double frequency = 0.0;
        double magnitude = 0.0;
        double phase = 0.0;
    };

    std::vector<Sample> samples;
    samples.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const double frequency = frequencies.at(i);
        const double magnitude = magnitudeDb.at(i);
        const double phase = phaseDegrees.at(i);
        if (!std::isfinite(frequency) || !std::isfinite(magnitude)
            || !std::isfinite(phase) || frequency < 0.0) {
            continue;
        }
        samples.push_back(Sample{frequency, magnitude, wrappedPhase(phase)});
    }

    std::stable_sort(samples.begin(), samples.end(),
                     [](const Sample& left, const Sample& right) {
                         return left.frequency < right.frequency;
                     });

    frequencies_.clear();
    magnitudesDb_.clear();
    phasesDeg_.clear();
    frequencies_.reserve(samples.size());
    magnitudesDb_.reserve(samples.size());
    phasesDeg_.reserve(samples.size());

    for (const Sample& sample : samples) {
        frequencies_.push_back(sample.frequency);
        magnitudesDb_.push_back(sample.magnitude);
        phasesDeg_.push_back(sample.phase);
    }

    rebuildSeries();
    requestUpdate();
}

const std::vector<double>& BodeChart::frequencies() const noexcept
{
    return frequencies_;
}

const std::vector<double>& BodeChart::magnitudesDb() const noexcept
{
    return magnitudesDb_;
}

const std::vector<double>& BodeChart::phasesDegrees() const noexcept
{
    return phasesDeg_;
}

int BodeChart::pointCount() const noexcept
{
    return static_cast<int>(frequencies_.size());
}

double BodeChart::frequencyAt(int index) const noexcept
{
    if (index < 0 || index >= pointCount())
        return 0.0;
    return frequencies_.at(static_cast<std::size_t>(index));
}

double BodeChart::magnitudeDbAt(int index) const noexcept
{
    if (index < 0 || index >= pointCount())
        return 0.0;
    return magnitudesDb_.at(static_cast<std::size_t>(index));
}

double BodeChart::phaseDegAt(int index) const noexcept
{
    if (index < 0 || index >= pointCount())
        return 0.0;
    return phasesDeg_.at(static_cast<std::size_t>(index));
}

void BodeChart::setFrequencyAxis(FrequencyAxis axis)
{
    if (frequencyAxis_ == axis)
        return;

    frequencyAxis_ = axis;
    requestUpdate();
}

BodeChart::FrequencyAxis BodeChart::frequencyAxis() const noexcept
{
    return frequencyAxis_;
}

QRectF BodeChart::magnitudeRect() const
{
    return computeLayout(plotRect()).magnitude;
}

QRectF BodeChart::phaseRect() const
{
    return computeLayout(plotRect()).phase;
}

int BodeChart::nearestIndex(QPointF position) const
{
    const Layout layout = computeLayout(plotRect());
    if (!layout.valid || !hasResponse())
        return -1;

    const qreal slop = std::max<qreal>(4.0, Theme::forWidget(this).density().dp(6.0));
    const QRectF expandedMagnitude = layout.magnitude.adjusted(-slop, -slop, slop, slop);
    const QRectF expandedPhase = layout.phase.adjusted(-slop, -slop, slop, slop);

    const bool nearMagnitude = expandedMagnitude.contains(position);
    const bool nearPhase = expandedPhase.contains(position);
    if (!nearMagnitude && !nearPhase)
        return -1;

    // When the pointer is in the gap both rectangles match; pick the area whose
    // centre is closer, so a point just below the magnitude axis still tests
    // the magnitude trace.
    const bool useMagnitude = nearMagnitude
        && (!nearPhase
            || std::abs(position.y() - layout.magnitude.center().y())
                <= std::abs(position.y() - layout.phase.center().y()));

    int best = -1;
    qreal bestDistance = slop * slop;

    for (int index = 0; index < pointCount(); ++index) {
        const QPointF pixel = useMagnitude ? magnitudePoint(index) : phasePoint(index);
        if (pixel.isNull())
            continue;

        const qreal dx = pixel.x() - position.x();
        const qreal dy = pixel.y() - position.y();
        const qreal distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }

    return best;
}

QPointF BodeChart::magnitudePoint(int index) const
{
    if (index < 0 || index >= pointCount())
        return {};

    const Layout layout = computeLayout(plotRect());
    if (!layout.valid)
        return {};

    const double hertz = frequencyAt(index);
    if (frequencyAxis_ == FrequencyAxis::Logarithmic && !(hertz > 0.0))
        return {};

    return QPointF(mapFrequency(hertz, layout, frequencyRange()),
                   mapMagnitude(magnitudeDbAt(index), layout, magnitudeRange()));
}

QPointF BodeChart::phasePoint(int index) const
{
    if (index < 0 || index >= pointCount())
        return {};

    const Layout layout = computeLayout(plotRect());
    if (!layout.valid)
        return {};

    const double hertz = frequencyAt(index);
    if (frequencyAxis_ == FrequencyAxis::Logarithmic && !(hertz > 0.0))
        return {};

    return QPointF(mapFrequency(hertz, layout, frequencyRange()),
                   mapPhase(phaseDegAt(index), layout));
}

QVector<double> BodeChart::frequencyTicks() const
{
    if (!hasResponse())
        return {};

    const Interval range = frequencyRange();
    if (!(range.maximum > range.minimum))
        return {};

    if (frequencyAxis_ == FrequencyAxis::Logarithmic) {
        const QVector<double> decades = decadeTicks(range.minimum, range.maximum);
        if (decades.size() >= 2)
            return decades;
    }

    QVector<double> ticks = MathematicsCoordination::niceTicks(
        static_cast<qreal>(range.minimum), static_cast<qreal>(range.maximum), 6);
    if (ticks.size() < 2) {
        ticks.clear();
        ticks.append(range.minimum);
        ticks.append(range.maximum);
    }

    return ticks;
}

QStringList BodeChart::frequencyTickLabels() const
{
    QStringList labels;
    const QVector<double> ticks = frequencyTicks();
    labels.reserve(ticks.size());
    for (double value : ticks)
        labels.append(formatFrequency(value));
    return labels;
}

QVector<double> BodeChart::magnitudeTicks() const
{
    if (!hasResponse())
        return {};

    const Interval range = magnitudeRange();
    const double step = magnitudeStep();
    QVector<double> ticks;

    for (double value = range.minimum; value <= range.maximum + 1.0e-9; value += step)
        ticks.append(value);

    if (ticks.size() < 2)
        ticks = {range.minimum, range.maximum};

    return ticks;
}

QStringList BodeChart::magnitudeTickLabels() const
{
    QStringList labels;
    const QVector<double> ticks = magnitudeTicks();
    labels.reserve(ticks.size());

    const qreal step = static_cast<qreal>(magnitudeStep());
    for (double value : ticks)
        labels.append(MathematicsCoordination::tickLabel(static_cast<qreal>(value), step));

    return labels;
}

QVector<double> BodeChart::phaseTicks() const
{
    return phaseTickValues();
}

QStringList BodeChart::phaseTickLabels() const
{
    QStringList labels;
    const QVector<double> ticks = phaseTicks();
    labels.reserve(ticks.size());
    for (double value : ticks)
        labels.append(QString::number(value, 'f', 0));
    return labels;
}

QString BodeChart::xAxisTitle() const
{
    return localized(QStringLiteral("chart.bode.frequency"), QStringLiteral("Frequency (Hz)"));
}

QString BodeChart::magnitudeAxisTitle() const
{
    return localized(QStringLiteral("chart.bode.magnitude"), QStringLiteral("Magnitude (dB)"));
}

QString BodeChart::phaseAxisTitle() const
{
    return localized(QStringLiteral("chart.bode.phase"), QStringLiteral("Phase (°)"));
}

qreal BodeChart::revealProgress() const noexcept
{
    return animation().progress();
}

bool BodeChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

bool BodeChart::hasResponse() const noexcept
{
    if (frequencies_.empty() || magnitudesDb_.empty() || phasesDeg_.empty())
        return false;

    for (double value : magnitudesDb_) {
        if (std::isfinite(value))
            return true;
    }
    return false;
}

BodeChart::Interval BodeChart::frequencyRange() const
{
    if (frequencies_.empty())
        return {0.0, 1.0};

    const double maximum = frequencies_.back();
    if (!(maximum > 0.0))
        return {0.0, 1.0};

    if (frequencyAxis_ == FrequencyAxis::Logarithmic) {
        double minimum = 0.0;
        for (double value : frequencies_) {
            if (value > 0.0) {
                minimum = value;
                break;
            }
        }
        if (!(minimum > 0.0) || minimum >= maximum)
            minimum = maximum / 1000.0;
        if (!(minimum > 0.0) || minimum >= maximum)
            minimum = maximum * 0.5;
        return {minimum, maximum};
    }

    return {0.0, maximum};
}

double BodeChart::magnitudeStep() const
{
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();

    for (double value : magnitudesDb_) {
        if (!std::isfinite(value))
            continue;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }

    if (!std::isfinite(minimum) || !std::isfinite(maximum))
        return 20.0;

    const double span = std::max(0.0, maximum - minimum);
    double step = 20.0;
    while (step < 200.0 && span / step > 7.0)
        step *= 2.0;

    return step;
}

BodeChart::Interval BodeChart::magnitudeRange() const
{
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();

    for (double value : magnitudesDb_) {
        if (!std::isfinite(value))
            continue;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }

    if (!std::isfinite(minimum) || !std::isfinite(maximum))
        return {-20.0, 20.0};

    const double step = magnitudeStep();
    double bottom = std::floor(minimum / step) * step;
    double top = std::ceil(maximum / step) * step;

    if (!(top > bottom))
        top = bottom + step;

    return {bottom, top};
}

BodeChart::Layout BodeChart::computeLayout(const QRectF& plot) const
{
    Layout layout;
    if (plot.isEmpty() || plot.width() <= 0.0 || plot.height() <= 0.0)
        return layout;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFontMetricsF tickMetrics(tickFont(this, theme));
    const QFontMetricsF titleMetrics(axisTitleFont(this, theme));

    const qreal tickLength = density.dp(kAxisTickLengthDp);
    const qreal labelGap = density.dp(kAxisLabelGapDp);

    qreal widestY = 0.0;
    for (const QString& label : magnitudeTickLabels())
        widestY = std::max(widestY, tickMetrics.horizontalAdvance(label));
    for (const QString& label : phaseTickLabels())
        widestY = std::max(widestY, tickMetrics.horizontalAdvance(label));

    qreal left = std::max<qreal>(density.dp(kMinimumLeftGutterDp),
                                 widestY + tickLength + labelGap);
    left += titleMetrics.height() + labelGap;

    const qreal right = density.dp(kRightInsetDp);
    const qreal top = density.dp(kTopInsetDp);
    const qreal bottom = tickMetrics.height() + tickLength + labelGap
        + titleMetrics.height() + labelGap;
    const qreal gap = density.dp(kAreaGapDp);

    const qreal availableHeight = plot.height() - top - bottom - gap;
    if (availableHeight <= 0.0)
        return layout;

    const qreal magnitudeHeight = availableHeight * kMagnitudeShare;
    const qreal phaseHeight = availableHeight - magnitudeHeight;
    if (magnitudeHeight <= 0.0 || phaseHeight <= 0.0)
        return layout;

    const qreal width = plot.width() - left - right;
    if (width <= 0.0)
        return layout;

    layout.magnitude = QRectF(plot.left() + left, plot.top() + top, width, magnitudeHeight);
    layout.phase = QRectF(plot.left() + left, layout.magnitude.bottom() + gap,
                          width, phaseHeight);
    layout.leftGutter = left;
    layout.bottomGutter = bottom;
    layout.areaGap = gap;
    layout.valid = true;
    return layout;
}

qreal BodeChart::mapFrequency(double hertz, const Layout& layout,
                              const Interval& range) const
{
    const QRectF area = layout.magnitude;
    const qreal left = area.left();
    const qreal width = area.width();

    if (frequencyAxis_ == FrequencyAxis::Logarithmic) {
        if (!(range.minimum > 0.0) || !(range.maximum > range.minimum))
            return left + width * 0.5;

        const double clamped = std::clamp(hertz, range.minimum, range.maximum);
        const double fraction = (std::log10(clamped) - std::log10(range.minimum))
            / (std::log10(range.maximum) - std::log10(range.minimum));
        return left + static_cast<qreal>(fraction) * width;
    }

    if (!(range.maximum > range.minimum))
        return left + width * 0.5;

    const double clamped = std::clamp(hertz, range.minimum, range.maximum);
    const double fraction = (clamped - range.minimum) / (range.maximum - range.minimum);
    return left + static_cast<qreal>(fraction) * width;
}

qreal BodeChart::mapMagnitude(double decibels, const Layout& layout,
                              const Interval& range) const
{
    if (!(range.maximum > range.minimum))
        return layout.magnitude.center().y();

    const double clamped = std::clamp(decibels, range.minimum, range.maximum);
    const double fraction = (clamped - range.minimum) / (range.maximum - range.minimum);
    return layout.magnitude.bottom()
        - static_cast<qreal>(fraction) * layout.magnitude.height();
}

qreal BodeChart::mapPhase(double degrees, const Layout& layout) const
{
    const double clamped = std::clamp(degrees, -180.0, 180.0);
    const double fraction = (clamped + 180.0) / 360.0;
    return layout.phase.bottom() - static_cast<qreal>(fraction) * layout.phase.height();
}

double BodeChart::unmapFrequency(qreal x, const Layout& layout,
                                 const Interval& range) const
{
    if (!layout.valid || !(layout.magnitude.width() > 0.0))
        return 0.0;

    double fraction =
        static_cast<double>((x - layout.magnitude.left()) / layout.magnitude.width());
    fraction = std::clamp(fraction, 0.0, 1.0);

    if (frequencyAxis_ == FrequencyAxis::Logarithmic) {
        if (!(range.minimum > 0.0) || !(range.maximum > range.minimum))
            return range.minimum;

        const double low = std::log10(range.minimum);
        const double high = std::log10(range.maximum);
        return std::pow(10.0, low + fraction * (high - low));
    }

    if (!(range.maximum > range.minimum))
        return range.minimum;

    return range.minimum + fraction * (range.maximum - range.minimum);
}

void BodeChart::rebuildSeries()
{
    QVector<ChartSeries> list;

    if (hasResponse()) {
        ChartSeries magnitude;
        magnitude.name = magnitudeAxisTitle();
        magnitude.points.reserve(pointCount());

        ChartSeries phase;
        phase.name = phaseAxisTitle();
        phase.points.reserve(pointCount());

        for (int index = 0; index < pointCount(); ++index) {
            magnitude.points.append(QPointF(frequencyAt(index), magnitudeDbAt(index)));
            phase.points.append(QPointF(frequencyAt(index), phaseDegAt(index)));
        }

        list.append(std::move(magnitude));
        list.append(std::move(phase));
    }

    ChartBase::setSeries(std::move(list));
}

void BodeChart::drawFrame(QPainter& painter, const Layout& layout, const Theme& theme)
{
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const QRectF magnitude = layout.magnitude;
    const QRectF phase = layout.phase;
    if (magnitude.isEmpty() || phase.isEmpty())
        return;

    const Interval frequency = frequencyRange();
    const Interval magnitudeValues = magnitudeRange();

    const qreal gridWidth = std::max(kMinimumPenWidth, density.dp(kGridWidthDp));
    const qreal axisWidth = std::max(kMinimumPenWidth, density.dp(kAxisWidthDp));
    const qreal tickLength = density.dp(kAxisTickLengthDp);
    const qreal labelGap = density.dp(kAxisLabelGapDp);

    const QFont labels = tickFont(this, theme);
    const QFontMetricsF labelMetrics(labels);
    const QFont titles = axisTitleFont(this, theme);
    const QFontMetricsF titleMetrics(titles);

    // 1. Grid: one shared frequency tick set, painted per area. The two areas
    //    are disjoint rectangles, so a single clip cannot cover both; a clip
    //    per area also keeps the vertical grid out of the gap between them.
    const QVector<double> xTicks = frequencyTicks();

    const auto drawVerticalGrid = [&](const QRectF& area, const QColor& colour,
                                      qreal width, const QVector<double>& ticks) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setClipRect(area.adjusted(-1.0, -1.0, 1.0, 1.0));
        painter.setPen(QPen(colour, width));
        for (double hertz : ticks) {
            const qreal x = mapFrequency(hertz, layout, frequency);
            painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        }
        painter.restore();
    };

    QColor gridColour = colours.outlineVariant;
    gridColour.setAlphaF(static_cast<float>(kGridAlpha));
    drawVerticalGrid(magnitude, gridColour, gridWidth, xTicks);
    drawVerticalGrid(phase, gridColour, gridWidth, xTicks);

    if (frequencyAxis_ == FrequencyAxis::Logarithmic && frequency.minimum > 0.0) {
        QColor minorColour = colours.outlineVariant;
        minorColour.setAlphaF(static_cast<float>(kMinorGridAlpha));

        QVector<double> minorTicks;
        const int first =
            static_cast<int>(std::floor(std::log10(frequency.minimum)));
        const int last = static_cast<int>(std::ceil(std::log10(frequency.maximum)));

        for (int exponent = first; exponent <= last; ++exponent) {
            for (double multiplier : {2.0, 5.0}) {
                const double hertz = multiplier * std::pow(10.0, exponent);
                if (hertz <= frequency.minimum || hertz >= frequency.maximum)
                    continue;
                minorTicks.append(hertz);
            }
        }

        drawVerticalGrid(magnitude, minorColour, gridWidth, minorTicks);
        drawVerticalGrid(phase, minorColour, gridWidth, minorTicks);
    }

    {
        painter.save();
        painter.setClipRect(magnitude.adjusted(-1.0, -1.0, 1.0, 1.0));
        QColor levelGrid = colours.outlineVariant;
        levelGrid.setAlphaF(static_cast<float>(kGridAlpha));
        painter.setPen(QPen(levelGrid, gridWidth));
        for (double level : magnitudeTicks()) {
            const qreal y = mapMagnitude(level, layout, magnitudeValues);
            painter.drawLine(QPointF(magnitude.left(), y), QPointF(magnitude.right(), y));
        }
        painter.restore();
    }

    {
        painter.save();
        painter.setClipRect(phase.adjusted(-1.0, -1.0, 1.0, 1.0));
        QColor levelGrid = colours.outlineVariant;
        levelGrid.setAlphaF(static_cast<float>(kGridAlpha));
        painter.setPen(QPen(levelGrid, gridWidth));
        for (double level : phaseTicks()) {
            const qreal y = mapPhase(level, layout);
            painter.drawLine(QPointF(phase.left(), y), QPointF(phase.right(), y));
        }
        painter.restore();
    }

    // 2. Axes and ticks.
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(colours.outline, axisWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(magnitude.topLeft(), magnitude.bottomLeft());
    painter.drawLine(phase.topLeft(), phase.bottomLeft());
    painter.drawLine(phase.bottomLeft(), phase.bottomRight());

    painter.setFont(labels);

    const QVector<double> magnitudeY = magnitudeTicks();
    const QStringList magnitudeLabels = magnitudeTickLabels();
    const qreal magnitudeLabelX = magnitude.left() - tickLength - labelGap;

    for (qsizetype i = 0; i < magnitudeY.size() && i < magnitudeLabels.size(); ++i) {
        const qreal y = mapMagnitude(magnitudeY.at(i), layout, magnitudeValues);
        painter.setPen(QPen(colours.outline, axisWidth));
        painter.drawLine(QPointF(magnitude.left() - tickLength, y),
                         QPointF(magnitude.left(), y));

        painter.setPen(colours.onSurfaceVariant);
        const qreal width = labelMetrics.horizontalAdvance(magnitudeLabels.at(i));
        painter.drawText(QRectF(magnitudeLabelX - width,
                                y - labelMetrics.height() * 0.5,
                                width, labelMetrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter, magnitudeLabels.at(i));
    }

    const QVector<double> phaseY = phaseTicks();
    const QStringList phaseLabels = phaseTickLabels();
    const qreal phaseLabelX = phase.left() - tickLength - labelGap;

    for (qsizetype i = 0; i < phaseY.size() && i < phaseLabels.size(); ++i) {
        const qreal y = mapPhase(phaseY.at(i), layout);
        painter.setPen(QPen(colours.outline, axisWidth));
        painter.drawLine(QPointF(phase.left() - tickLength, y), QPointF(phase.left(), y));

        painter.setPen(colours.onSurfaceVariant);
        const qreal width = labelMetrics.horizontalAdvance(phaseLabels.at(i));
        painter.drawText(QRectF(phaseLabelX - width,
                                y - labelMetrics.height() * 0.5,
                                width, labelMetrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter, phaseLabels.at(i));
    }

    const QStringList xLabels = frequencyTickLabels();
    const qreal xLabelTop = phase.bottom() + tickLength + labelGap;

    for (qsizetype i = 0; i < xTicks.size() && i < xLabels.size(); ++i) {
        const qreal x = mapFrequency(xTicks.at(i), layout, frequency);
        painter.setPen(QPen(colours.outline, axisWidth));
        painter.drawLine(QPointF(x, phase.bottom()), QPointF(x, phase.bottom() + tickLength));

        painter.setPen(colours.onSurfaceVariant);
        const qreal width = labelMetrics.horizontalAdvance(xLabels.at(i));
        painter.drawText(QRectF(x - width * 0.5, xLabelTop, width, labelMetrics.height()),
                         Qt::AlignHCenter | Qt::AlignTop, xLabels.at(i));
    }

    // 3. Axis titles: one rotated per y axis, one horizontal under the x axis.
    painter.setFont(titles);
    painter.setPen(colours.onSurfaceVariant);
    const qreal titleHeight = titleMetrics.height();

    painter.save();
    painter.translate(magnitude.left() - layout.leftGutter + titleHeight * 0.5,
                      magnitude.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-magnitude.height() * 0.5, -titleHeight * 0.5,
                            magnitude.height(), titleHeight),
                     Qt::AlignCenter, magnitudeAxisTitle());
    painter.restore();

    painter.save();
    painter.translate(phase.left() - layout.leftGutter + titleHeight * 0.5,
                      phase.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-phase.height() * 0.5, -titleHeight * 0.5,
                            phase.height(), titleHeight),
                     Qt::AlignCenter, phaseAxisTitle());
    painter.restore();

    const QString xTitle = xAxisTitle();
    const qreal xTitleWidth =
        std::min(phase.width(), titleMetrics.horizontalAdvance(xTitle));
    const QRectF xTitleBox(phase.center().x() - xTitleWidth * 0.5,
                           phase.bottom() + tickLength + labelGap
                               + labelMetrics.height() + labelGap,
                           xTitleWidth, titleHeight);
    painter.drawText(xTitleBox, Qt::AlignCenter, xTitle);
}

void BodeChart::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    paintPlotAt(painter, plot, progress);
}

void BodeChart::paintPlotAt(QPainter& painter, const QRectF& plot, qreal progress)
{
    if (plot.isEmpty())
        return;

    [[maybe_unused]] const PainterStateGuard guard(painter);
    try {
        const Theme theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const Layout layout = computeLayout(plot);
        if (!layout.valid)
            return;

        drawFrame(painter, layout, theme);

        if (!hasResponse())
            return;

        const Interval frequency = frequencyRange();
        const Interval magnitudeValues = magnitudeRange();
        const qreal reveal = std::clamp(progress, 0.0, 1.0);
        const qreal magnitudeWidth = layout.magnitude.width() * reveal;
        const qreal phaseWidth = layout.phase.width() * reveal;
        const qreal traceWidth =
            std::max<qreal>(kMinimumPenWidth, density.dp(kTraceWidthDp));

        // 1. Magnitude trace.
        {
            const QColor colour = seriesColour(0);
            if (colour.isValid()) {
                painter.save();
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setClipRect(layout.magnitude.adjusted(-2.0, -2.0, 2.0, 2.0),
                                    Qt::IntersectClip);
                if (reveal < 1.0) {
                    painter.setClipRect(QRectF(layout.magnitude.left() - 2.0,
                                               layout.magnitude.top() - 2.0,
                                               magnitudeWidth + 4.0,
                                               layout.magnitude.height() + 4.0),
                                        Qt::IntersectClip);
                }

                QPainterPath path;
                bool started = false;
                for (int index = 0; index < pointCount(); ++index) {
                    const double hertz = frequencyAt(index);
                    if (frequencyAxis_ == FrequencyAxis::Logarithmic && !(hertz > 0.0))
                        continue;

                    const QPointF point(mapFrequency(hertz, layout, frequency),
                                        mapMagnitude(magnitudeDbAt(index), layout,
                                                     magnitudeValues));
                    if (!started) {
                        path.moveTo(point);
                        started = true;
                    } else {
                        path.lineTo(point);
                    }
                }

                QPen pen(colour, traceWidth);
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(path);
                painter.restore();
            }
        }

        // 2. Phase trace, with a break at every wrap.
        {
            const QColor colour = seriesColour(1);
            if (colour.isValid()) {
                painter.save();
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setClipRect(layout.phase.adjusted(-2.0, -2.0, 2.0, 2.0),
                                    Qt::IntersectClip);
                if (reveal < 1.0) {
                    painter.setClipRect(QRectF(layout.phase.left() - 2.0,
                                               layout.phase.top() - 2.0,
                                               phaseWidth + 4.0,
                                               layout.phase.height() + 4.0),
                                        Qt::IntersectClip);
                }

                QPainterPath path;
                bool started = false;
                double previousPhase = 0.0;

                for (int index = 0; index < pointCount(); ++index) {
                    const double hertz = frequencyAt(index);
                    if (frequencyAxis_ == FrequencyAxis::Logarithmic && !(hertz > 0.0))
                        continue;

                    const double degrees = phaseDegAt(index);
                    const QPointF point(mapFrequency(hertz, layout, frequency),
                                        mapPhase(degrees, layout));

                    if (!started) {
                        path.moveTo(point);
                        started = true;
                    } else if (std::abs(degrees - previousPhase) > 180.0) {
                        path.moveTo(point);
                    } else {
                        path.lineTo(point);
                    }

                    previousPhase = degrees;
                }

                QPen pen(colour, traceWidth);
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(path);
                painter.restore();
            }
        }
    } catch (const std::exception& error) {
        qWarning("md::BodeChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::BodeChart::paintPlot failed with an unknown exception");
    }
}

void BodeChart::paintEmptyState(QPainter& painter, const QRectF& plot)
{
    if (plot.isEmpty())
        return;

    painter.save();
    try {
        const Theme& theme = Theme::forWidget(this);
        painter.setFont(Font::resolve(this, theme.type().labelMedium, theme.density()));
        painter.setPen(theme.colours().onSurfaceVariant);
        painter.drawText(plot, Qt::AlignCenter,
                         localized(QStringLiteral("chart.bode.noData"),
                                   QStringLiteral("No frequency response")));
    } catch (const std::exception& error) {
        qWarning("md::BodeChart::paintEmptyState failed: %s", error.what());
    } catch (...) {
        qWarning("md::BodeChart::paintEmptyState failed with an unknown exception");
    }
    painter.restore();
}

} // namespace md
