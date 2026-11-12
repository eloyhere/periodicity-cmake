// Periodicity — the CoherenceChart component.
//
// Coherence is presented as a filled trace against a fixed 0..1 axis. The
// significance band is chrome: it is painted before the grid, never clipped by
// the reveal, and labelled with the exact threshold value, so the reader can
// judge the trace against it without a separate legend lookup.
//
// The frequency axis has the same linear/logarithmic contract as
// SpectrumChart: logarithmic ticks are decades and a range narrower than one
// decade falls back to the linear nice-tick set. The frame is painted here
// because the shared transform is linear-only; the nice-tick and label helpers
// are reused from MathematicsCoordination, so the numeric style stays
// consistent with the Cartesian charts.
//
// Data cleaning happens once in setCoherence(), not in the paint pass: the
// sampled vectors are sorted by frequency and clamped to [0, 1]. binAt(),
// pointForIndex(), the tick getters and the paint pass therefore all describe
// the same monotone sequence.

#include <md/components/coherence_chart.h>

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
constexpr qreal kMinimumYAxisGutterDp = 30.0;
constexpr qreal kTopInsetDp = 8.0;
constexpr qreal kRightInsetDp = 8.0;
constexpr qreal kGridWidthDp = 1.0;
constexpr qreal kAxisWidthDp = 1.0;
constexpr qreal kTraceWidthDp = 2.0;
constexpr qreal kMinimumPenWidth = 0.75;
constexpr qreal kBandAlpha = 0.10;
constexpr qreal kFillAlpha = 0.18;
constexpr qreal kGridAlpha = 0.65;
constexpr qreal kMinorGridAlpha = 0.32;
constexpr qreal kThresholdLineAlpha = 0.85;
constexpr qreal kThresholdBadgePaddingDp = 4.0;
constexpr qreal kThresholdBadgeGapDp = 4.0;

constexpr double kCoherenceTickStep = 0.2;

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

    if (hertz >= 1.0e6) {
        return QStringLiteral("%1 MHz").arg(QString::number(hertz / 1.0e6, 'g', 4));
    }
    if (hertz >= 1.0e3) {
        return QStringLiteral("%1 kHz").arg(QString::number(hertz / 1.0e3, 'g', 4));
    }
    if (hertz >= 100.0)
        return QStringLiteral("%1 Hz").arg(QString::number(hertz, 'f', 0));
    if (hertz >= 1.0)
        return QStringLiteral("%1 Hz").arg(QString::number(hertz, 'f', 1));
    if (hertz > 0.0)
        return QStringLiteral("%1 Hz").arg(QString::number(hertz, 'f', 2));

    return QStringLiteral("0 Hz");
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

} // namespace

CoherenceChart::CoherenceChart(QWidget* parent)
    : ChartBase(parent)
{
    // The logarithmic frequency axis needs its own frame; the base frame is
    // Cartesian and would reserve gutters for the wrong labels.
    setAxesVisible(false);

    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(false);
    setInteractionFeedbackEnabled(false);

    // The transform is not used for the custom mapping; disabling auto-fit
    // keeps a data change from starting an invisible fit animation.
    math()->setAutoFit(false);

    // The fill grows in over the level-2 chart motion.
    animation().setDuration(MotionLevel::Level2);
}

void CoherenceChart::setCoherence(std::vector<double> frequencies,
                                  std::vector<double> coherence)
{
    const std::size_t count = std::min(frequencies.size(), coherence.size());
    std::vector<std::pair<double, double>> pairs;
    pairs.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const double frequency = frequencies.at(i);
        const double value = coherence.at(i);
        if (!std::isfinite(frequency) || !std::isfinite(value) || frequency < 0.0)
            continue;
        pairs.emplace_back(frequency, std::clamp(value, 0.0, 1.0));
    }

    std::stable_sort(pairs.begin(), pairs.end(),
                     [](const auto& left, const auto& right) {
                         return left.first < right.first;
                     });

    frequencies_.clear();
    coherence_.clear();
    frequencies_.reserve(pairs.size());
    coherence_.reserve(pairs.size());

    for (const auto& pair : pairs) {
        frequencies_.push_back(pair.first);
        coherence_.push_back(pair.second);
    }

    rebuildSeries();
    requestUpdate();
}

const std::vector<double>& CoherenceChart::frequencies() const noexcept
{
    return frequencies_;
}

const std::vector<double>& CoherenceChart::coherence() const noexcept
{
    return coherence_;
}

int CoherenceChart::sampleCount() const noexcept
{
    return static_cast<int>(frequencies_.size());
}

double CoherenceChart::frequencyAt(int index) const noexcept
{
    if (index < 0 || index >= sampleCount())
        return 0.0;
    return frequencies_.at(static_cast<std::size_t>(index));
}

double CoherenceChart::coherenceAt(int index) const noexcept
{
    if (index < 0 || index >= sampleCount())
        return 0.0;
    return coherence_.at(static_cast<std::size_t>(index));
}

void CoherenceChart::setThreshold(double threshold)
{
    if (!std::isfinite(threshold))
        return;

    const double clamped = std::clamp(threshold, 0.0, 1.0);
    if (threshold_ == clamped)
        return;

    threshold_ = clamped;
    rebuildSeries();
    requestUpdate();
}

double CoherenceChart::threshold() const noexcept
{
    return threshold_;
}

void CoherenceChart::setFrequencyAxis(FrequencyAxis axis)
{
    if (frequencyAxis_ == axis)
        return;

    frequencyAxis_ = axis;
    requestUpdate();
}

CoherenceChart::FrequencyAxis CoherenceChart::frequencyAxis() const noexcept
{
    return frequencyAxis_;
}

QRectF CoherenceChart::dataRect() const
{
    return computeLayout(plotRect()).data;
}

int CoherenceChart::binAt(QPointF position) const
{
    const Layout layout = computeLayout(plotRect());
    if (!layout.valid || !hasSamples())
        return -1;

    const qreal slop = std::max<qreal>(4.0, Theme::forWidget(this).density().dp(6.0));
    if (!layout.data.adjusted(-slop, -slop, slop, slop).contains(position))
        return -1;

    const double hertz = unmapFrequency(position.x(), layout, frequencyRange());
    if (!std::isfinite(hertz))
        return -1;

    return nearestSample(hertz);
}

QPointF CoherenceChart::pointForIndex(int index) const
{
    if (index < 0 || index >= sampleCount())
        return {};

    const Layout layout = computeLayout(plotRect());
    if (!layout.valid)
        return {};

    const double hertz = frequencyAt(index);
    if (frequencyAxis_ == FrequencyAxis::Logarithmic && !(hertz > 0.0))
        return {};

    return QPointF(mapFrequency(hertz, layout, frequencyRange()),
                   mapCoherence(coherenceAt(index), layout));
}

QVector<double> CoherenceChart::frequencyTicks() const
{
    if (!hasSamples())
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

QStringList CoherenceChart::frequencyTickLabels() const
{
    QStringList labels;
    const QVector<double> ticks = frequencyTicks();
    labels.reserve(ticks.size());
    for (double value : ticks)
        labels.append(formatFrequency(value));
    return labels;
}

QVector<double> CoherenceChart::coherenceTicks() const
{
    QVector<double> ticks;
    for (int step = 0; step <= 5; ++step)
        ticks.append(static_cast<double>(step) * kCoherenceTickStep);
    return ticks;
}

QStringList CoherenceChart::coherenceTickLabels() const
{
    QStringList labels;
    const QVector<double> ticks = coherenceTicks();
    labels.reserve(ticks.size());
    for (double value : ticks)
        labels.append(QString::number(value, 'f', 1));
    return labels;
}

QString CoherenceChart::xAxisTitle() const
{
    return localized(QStringLiteral("chart.coherence.frequency"),
                     QStringLiteral("Frequency (Hz)"));
}

QString CoherenceChart::yAxisTitle() const
{
    return localized(QStringLiteral("chart.coherence.axis"), QStringLiteral("Coherence"));
}

QString CoherenceChart::thresholdLabel() const
{
    return QString::number(threshold_, 'f', 2);
}

qreal CoherenceChart::revealProgress() const noexcept
{
    return animation().progress();
}

bool CoherenceChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

bool CoherenceChart::hasSamples() const noexcept
{
    if (frequencies_.empty() || coherence_.empty())
        return false;

    for (double value : coherence_) {
        if (std::isfinite(value))
            return true;
    }
    return false;
}

CoherenceChart::Interval CoherenceChart::frequencyRange() const
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

CoherenceChart::Layout CoherenceChart::computeLayout(const QRectF& plot) const
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
    for (const QString& label : coherenceTickLabels())
        widestY = std::max(widestY, tickMetrics.horizontalAdvance(label));

    qreal left = std::max<qreal>(density.dp(kMinimumYAxisGutterDp),
                                 widestY + tickLength + labelGap);
    left += titleMetrics.height() + labelGap;

    const qreal right = density.dp(kRightInsetDp);
    const qreal bottom = tickMetrics.height() + tickLength + labelGap
        + titleMetrics.height() + labelGap;
    const qreal top = density.dp(kTopInsetDp);

    const QRectF area(plot.left() + left, plot.top() + top,
                      plot.width() - left - right, plot.height() - top - bottom);

    if (area.width() <= 0.0 || area.height() <= 0.0)
        return layout;

    layout.data = area;
    layout.leftGutter = left;
    layout.bottomGutter = bottom;
    layout.valid = true;
    return layout;
}

qreal CoherenceChart::mapFrequency(double hertz, const Layout& layout,
                                   const Interval& range) const
{
    const qreal left = layout.data.left();
    const qreal width = layout.data.width();

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

qreal CoherenceChart::mapCoherence(double value, const Layout& layout) const
{
    const double clamped = std::clamp(value, 0.0, 1.0);
    return layout.data.bottom() - static_cast<qreal>(clamped) * layout.data.height();
}

double CoherenceChart::unmapFrequency(qreal x, const Layout& layout,
                                      const Interval& range) const
{
    if (!layout.valid || !(layout.data.width() > 0.0))
        return 0.0;

    double fraction = static_cast<double>((x - layout.data.left()) / layout.data.width());
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

int CoherenceChart::nearestSample(double hertz) const
{
    int best = -1;
    double bestDistance = std::numeric_limits<double>::infinity();

    for (int index = 0; index < sampleCount(); ++index) {
        const double distance = std::abs(frequencyAt(index) - hertz);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }

    return best;
}

void CoherenceChart::rebuildSeries()
{
    QVector<ChartSeries> list;

    if (hasSamples()) {
        ChartSeries trace;
        trace.name = localized(QStringLiteral("chart.coherence.axis"),
                               QStringLiteral("Coherence"));
        trace.points.reserve(sampleCount());

        for (int index = 0; index < sampleCount(); ++index)
            trace.points.append(QPointF(frequencyAt(index), coherenceAt(index)));

        if (!trace.points.isEmpty())
            list.append(std::move(trace));

        // The band is part of the chart's contract, so the legend names it with
        // the current threshold rather than leaving the shading unexplained.
        ChartSeries significance;
        significance.name = QStringLiteral("Significance ≥ %1").arg(thresholdLabel());
        significance.colour = Theme::forWidget(this).colours().primary;
        significance.colour.setAlpha(110);
        list.append(std::move(significance));
    }

    ChartBase::setSeries(std::move(list));
}

void CoherenceChart::drawFrame(QPainter& painter, const Layout& layout, const Theme& theme)
{
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const QRectF area = layout.data;
    if (area.isEmpty())
        return;

    const Interval frequency = frequencyRange();
    const qreal gridWidth = std::max(kMinimumPenWidth, density.dp(kGridWidthDp));
    const qreal axisWidth = std::max(kMinimumPenWidth, density.dp(kAxisWidthDp));
    const qreal tickLength = density.dp(kAxisTickLengthDp);
    const qreal labelGap = density.dp(kAxisLabelGapDp);

    const QFont labels = tickFont(this, theme);
    const QFontMetricsF labelMetrics(labels);
    const QFont titles = axisTitleFont(this, theme);
    const QFontMetricsF titleMetrics(titles);

    // 1. Significance band, behind the grid. The shaded side is the region at
    //    or above the threshold.
    {
        painter.save();
        painter.setClipRect(area.adjusted(-1.0, -1.0, 1.0, 1.0));
        QColor band = colours.primary;
        band.setAlphaF(static_cast<float>(kBandAlpha));
        painter.setPen(Qt::NoPen);
        painter.setBrush(band);
        const qreal thresholdY = mapCoherence(threshold_, layout);
        painter.drawRect(QRectF(area.left(), area.top(), area.width(),
                                thresholdY - area.top()));
        painter.restore();
    }

    // 2. Grid, clipped to the data rectangle.
    {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setClipRect(area.adjusted(-1.0, -1.0, 1.0, 1.0));

        QColor gridColour = colours.outlineVariant;
        gridColour.setAlphaF(static_cast<float>(kGridAlpha));
        painter.setPen(QPen(gridColour, gridWidth));

        for (double hertz : frequencyTicks()) {
            const qreal x = mapFrequency(hertz, layout, frequency);
            painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        }

        if (frequencyAxis_ == FrequencyAxis::Logarithmic
            && frequency.minimum > 0.0) {
            QColor minorColour = colours.outlineVariant;
            minorColour.setAlphaF(static_cast<float>(kMinorGridAlpha));
            painter.setPen(QPen(minorColour, gridWidth));

            const int first =
                static_cast<int>(std::floor(std::log10(frequency.minimum)));
            const int last =
                static_cast<int>(std::ceil(std::log10(frequency.maximum)));

            for (int exponent = first; exponent <= last; ++exponent) {
                for (double multiplier : {2.0, 5.0}) {
                    const double hertz = multiplier * std::pow(10.0, exponent);
                    if (hertz <= frequency.minimum || hertz >= frequency.maximum)
                        continue;
                    const qreal x = mapFrequency(hertz, layout, frequency);
                    painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
                }
            }
        }

        painter.setPen(QPen(gridColour, gridWidth));
        for (double level : coherenceTicks()) {
            const qreal y = mapCoherence(level, layout);
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }

        painter.restore();
    }

    // 3. Axes and tick marks.
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(colours.outline, axisWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(area.topLeft(), area.bottomLeft());
    painter.drawLine(area.bottomLeft(), area.bottomRight());

    painter.setFont(labels);

    const QVector<double> yTicks = coherenceTicks();
    const QStringList yLabels = coherenceTickLabels();
    const qreal yLabelX = area.left() - tickLength - labelGap;

    for (qsizetype i = 0; i < yTicks.size() && i < yLabels.size(); ++i) {
        const qreal y = mapCoherence(yTicks.at(i), layout);
        painter.setPen(QPen(colours.outline, axisWidth));
        painter.drawLine(QPointF(area.left() - tickLength, y), QPointF(area.left(), y));

        painter.setPen(colours.onSurfaceVariant);
        const qreal width = labelMetrics.horizontalAdvance(yLabels.at(i));
        const QRectF box(yLabelX - width, y - labelMetrics.height() * 0.5,
                         width, labelMetrics.height());
        painter.drawText(box, Qt::AlignRight | Qt::AlignVCenter, yLabels.at(i));
    }

    const QVector<double> xTicks = frequencyTicks();
    const QStringList xLabels = frequencyTickLabels();
    const qreal xLabelTop = area.bottom() + tickLength + labelGap;

    for (qsizetype i = 0; i < xTicks.size() && i < xLabels.size(); ++i) {
        const qreal x = mapFrequency(xTicks.at(i), layout, frequency);
        painter.setPen(QPen(colours.outline, axisWidth));
        painter.drawLine(QPointF(x, area.bottom()), QPointF(x, area.bottom() + tickLength));

        painter.setPen(colours.onSurfaceVariant);
        const qreal width = labelMetrics.horizontalAdvance(xLabels.at(i));
        const QRectF box(x - width * 0.5, xLabelTop, width, labelMetrics.height());
        painter.drawText(box, Qt::AlignHCenter | Qt::AlignTop, xLabels.at(i));
    }

    // 4. Axis titles.
    painter.setFont(titles);
    painter.setPen(colours.onSurfaceVariant);
    const qreal titleHeight = titleMetrics.height();

    painter.save();
    painter.translate(area.left() - layout.leftGutter + titleHeight * 0.5, area.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-area.height() * 0.5, -titleHeight * 0.5,
                            area.height(), titleHeight),
                     Qt::AlignCenter, yAxisTitle());
    painter.restore();

    const QString xTitle = xAxisTitle();
    const qreal xTitleWidth =
        std::min(area.width(), titleMetrics.horizontalAdvance(xTitle));
    const QRectF xTitleBox(area.center().x() - xTitleWidth * 0.5,
                           area.bottom() + tickLength + labelGap
                               + labelMetrics.height() + labelGap,
                           xTitleWidth, titleHeight);
    painter.drawText(xTitleBox, Qt::AlignCenter, xTitle);
}

void CoherenceChart::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    paintPlotAt(painter, plot, progress);
}

void CoherenceChart::paintPlotAt(QPainter& painter, const QRectF& plot, qreal progress)
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

        if (!hasSamples())
            return;

        const QRectF area = layout.data;
        const Interval frequency = frequencyRange();
        const qreal reveal = std::clamp(progress, 0.0, 1.0);
        const qreal revealedRight = area.left() + area.width() * reveal;

        const QColor traceColour = seriesColour(0);
        const qreal traceWidth =
            std::max<qreal>(kMinimumPenWidth, density.dp(kTraceWidthDp));

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setClipRect(area.adjusted(-2.0, -2.0, 2.0, 2.0), Qt::IntersectClip);
        if (reveal < 1.0) {
            painter.setClipRect(QRectF(area.left() - 2.0, area.top() - 2.0,
                                       revealedRight - area.left() + 4.0,
                                       area.height() + 4.0),
                                Qt::IntersectClip);
        }

        QVector<QPointF> samples;
        samples.reserve(sampleCount());
        for (int index = 0; index < sampleCount(); ++index) {
            const double hertz = frequencyAt(index);
            if (frequencyAxis_ == FrequencyAxis::Logarithmic && !(hertz > 0.0))
                continue;
            samples.append(QPointF(mapFrequency(hertz, layout, frequency),
                                   mapCoherence(coherenceAt(index), layout)));
        }

        if (traceColour.isValid() && samples.size() >= 2) {
            QPainterPath path;
            path.moveTo(samples.first());
            for (qsizetype i = 1; i < samples.size(); ++i)
                path.lineTo(samples.at(i));

            QColor fill = traceColour;
            fill.setAlphaF(static_cast<float>(kFillAlpha));
            QPainterPath fillPath = path;
            fillPath.lineTo(samples.last().x(), area.bottom());
            fillPath.lineTo(samples.first().x(), area.bottom());
            fillPath.closeSubpath();

            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawPath(fillPath);

            QPen tracePen(traceColour, traceWidth);
            tracePen.setCapStyle(Qt::RoundCap);
            tracePen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(tracePen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        }

        painter.restore();

        // 5. Threshold line and badge, painted over the trace so the reader can
        //    always see where the significance boundary is.
        const qreal thresholdY = mapCoherence(threshold_, layout);
        QColor thresholdColour = theme.colours().primary;
        thresholdColour.setAlphaF(static_cast<float>(kThresholdLineAlpha));
        QPen thresholdPen(thresholdColour,
                          std::max<qreal>(kMinimumPenWidth, density.dp(1.0)),
                          Qt::DashLine);
        painter.setPen(thresholdPen);
        painter.drawLine(QPointF(area.left(), thresholdY),
                         QPointF(area.right(), thresholdY));

        const QString badgeText = thresholdLabel();
        const QFont badgeFont = tickFont(this, theme);
        const QFontMetricsF badgeMetrics(badgeFont);
        const qreal padding = density.dp(kThresholdBadgePaddingDp);
        const qreal gap = density.dp(kThresholdBadgeGapDp);
        const qreal badgeWidth = badgeMetrics.horizontalAdvance(badgeText) + padding * 2.0;
        const qreal badgeHeight = badgeMetrics.height() + padding * 0.5;
        QRectF badge(area.right() - badgeWidth - gap, thresholdY - badgeHeight - gap,
                     badgeWidth, badgeHeight);
        if (badge.top() < area.top())
            badge.moveTop(thresholdY + gap);
        if (badge.left() < area.left())
            badge.moveLeft(area.left());

        QColor badgeBackground = theme.colours().surfaceContainerHigh;
        badgeBackground.setAlpha(230);
        painter.setPen(QPen(thresholdColour, 1.0));
        painter.setBrush(badgeBackground);
        painter.drawRoundedRect(badge, 3.0, 3.0);
        painter.setFont(badgeFont);
        painter.setPen(theme.colours().onSurface);
        painter.drawText(badge, Qt::AlignCenter, badgeText);
    } catch (const std::exception& error) {
        qWarning("md::CoherenceChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::CoherenceChart::paintPlot failed with an unknown exception");
    }
}

void CoherenceChart::paintEmptyState(QPainter& painter, const QRectF& plot)
{
    if (plot.isEmpty())
        return;

    painter.save();
    try {
        const Theme& theme = Theme::forWidget(this);
        painter.setFont(Font::resolve(this, theme.type().labelMedium, theme.density()));
        painter.setPen(theme.colours().onSurfaceVariant);
        painter.drawText(plot, Qt::AlignCenter,
                         localized(QStringLiteral("chart.coherence.noData"),
                                   QStringLiteral("No coherence data")));
    } catch (const std::exception& error) {
        qWarning("md::CoherenceChart::paintEmptyState failed: %s", error.what());
    } catch (...) {
        qWarning("md::CoherenceChart::paintEmptyState failed with an unknown exception");
    }
    painter.restore();
}

} // namespace md
