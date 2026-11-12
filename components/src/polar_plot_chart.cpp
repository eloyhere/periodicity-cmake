// Periodicity — the PolarPlotChart component.
//
// Geometry is polar and local to polarRect(): the centre is the middle of a
// square inset from plotRect() by enough room for the outside angle labels.
// Every helper derives from that one square, so the rings, the spokes, the
// data marks and the hit test stay concentric through a resize.
//
// The chart is a data surface, not a control: the Cartesian frame is switched
// off, the interaction tint is disabled and the pointer is only used by
// nearestIndex(). ChartBase still paints the title, legend and empty state,
// and the chart's own frame covers the grid, radial ticks, angle labels and
// the two axis titles.
//
// The reveal is an angular clip (a growing wedge from 0 degrees clockwise).
// The frame is complete from the first frame; only the fill, stroke and sample
// markers are swept. pointForIndex() and nearestIndex() describe the settled
// geometry, never the partial wedge, so a host tooltip cannot disagree with the
// final chart.

#include <md/components/polar_plot_chart.h>

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

constexpr qreal kPi = 3.141592653589793238462643383279502884;
constexpr qreal kOuterMarginDp = 24.0;
constexpr qreal kAngleLabelGapDp = 6.0;
constexpr qreal kRadialLabelGapDp = 4.0;
constexpr qreal kAxisTitleGapDp = 2.0;
constexpr qreal kGridWidthDp = 1.0;
constexpr qreal kSpokeWidthDp = 1.0;
constexpr qreal kOuterRingWidthDp = 1.4;
constexpr qreal kTraceWidthDp = 2.0;
constexpr qreal kMinimumPenWidth = 0.75;
constexpr qreal kMarkerRadiusDp = 2.0;
constexpr qreal kGridAlpha = 0.65;
constexpr qreal kSpokeAlpha = 0.55;
constexpr qreal kOuterRingAlpha = 0.95;
constexpr qreal kFillAlpha = 0.16;
constexpr qreal kMirrorFillAlpha = 0.08;
constexpr qreal kMirrorStrokeAlpha = 0.60;
constexpr qreal kMinimumFloorDb = -200.0;
constexpr qreal kMaximumFloorDb = -1.0;
constexpr qreal kDefaultFloorDb = -40.0;
constexpr qreal kMinimumSlopPx = 4.0;
constexpr qreal kHitSlopDp = 6.0;
constexpr qreal kClosedPatternSpanDegrees = 300.0;

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

[[nodiscard]] double normaliseAngle(double degrees)
{
    if (!std::isfinite(degrees))
        return 0.0;

    double value = std::fmod(degrees, 360.0);
    if (value < 0.0)
        value += 360.0;
    return value;
}

[[nodiscard]] QString formatDegrees(double degrees)
{
    return QStringLiteral("%1°").arg(QString::number(degrees, 'f', 0));
}

} // namespace

PolarPlotChart::PolarPlotChart(QWidget* parent)
    : ChartBase(parent)
{
    // A polar plot has no Cartesian frame; the component paints its own rings,
    // spokes and labels into the full inner rectangle.
    setAxesVisible(false);

    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(false);
    setInteractionFeedbackEnabled(false);

    math()->setAutoFit(false);

    // The pattern sweeps in over the level-2 chart motion.
    animation().setDuration(MotionLevel::Level2);
}

void PolarPlotChart::setAngles(std::vector<double> degrees, std::vector<double> values)
{
    const std::size_t count = std::min(degrees.size(), values.size());

    struct Sample
    {
        double angle = 0.0;
        double value = 0.0;
    };

    std::vector<Sample> samples;
    samples.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const double angle = degrees.at(i);
        const double value = values.at(i);
        if (!std::isfinite(angle) || !std::isfinite(value))
            continue;
        samples.push_back(Sample{normaliseAngle(angle), std::abs(value)});
    }

    std::stable_sort(samples.begin(), samples.end(),
                     [](const Sample& left, const Sample& right) {
                         return left.angle < right.angle;
                     });

    angles_.clear();
    values_.clear();
    angles_.reserve(samples.size());
    values_.reserve(samples.size());

    for (const Sample& sample : samples) {
        angles_.push_back(sample.angle);
        values_.push_back(sample.value);
    }

    rebuildSeries();
    requestUpdate();
}

void PolarPlotChart::setComplexResponse(std::vector<std::complex<double>> response,
                                        double startDegrees)
{
    if (!std::isfinite(startDegrees))
        startDegrees = 0.0;

    const std::size_t count = response.size();
    if (count == 0) {
        setAngles({}, {});
        return;
    }

    const double step = 360.0 / static_cast<double>(count);
    std::vector<double> angles;
    std::vector<double> values;
    angles.reserve(count);
    values.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        angles.push_back(startDegrees + step * static_cast<double>(i));
        values.push_back(std::abs(response.at(i)));
    }

    setAngles(std::move(angles), std::move(values));
}

const std::vector<double>& PolarPlotChart::angles() const noexcept
{
    return angles_;
}

const std::vector<double>& PolarPlotChart::values() const noexcept
{
    return values_;
}

int PolarPlotChart::pointCount() const noexcept
{
    return static_cast<int>(angles_.size());
}

double PolarPlotChart::angleAt(int index) const noexcept
{
    if (index < 0 || index >= pointCount())
        return 0.0;
    return angles_.at(static_cast<std::size_t>(index));
}

double PolarPlotChart::valueAt(int index) const noexcept
{
    if (index < 0 || index >= pointCount())
        return 0.0;
    return values_.at(static_cast<std::size_t>(index));
}

double PolarPlotChart::radialMaximum() const noexcept
{
    double maximum = 0.0;
    for (double value : values_) {
        if (std::isfinite(value))
            maximum = std::max(maximum, std::abs(value));
    }
    return maximum > 0.0 ? maximum : 1.0;
}

double PolarPlotChart::radialFraction(int index) const
{
    if (index < 0 || index >= pointCount())
        return 0.0;

    return radialFractionForValue(valueAt(index));
}

void PolarPlotChart::setRadialScale(RadialScale scale)
{
    if (scale_ == scale)
        return;

    scale_ = scale;
    requestUpdate();
}

PolarPlotChart::RadialScale PolarPlotChart::radialScale() const noexcept
{
    return scale_;
}

void PolarPlotChart::setRadialFloorDb(double decibels)
{
    if (!std::isfinite(decibels))
        return;

    const double floor = std::clamp(decibels, kMinimumFloorDb, kMaximumFloorDb);
    if (radialFloorDb_ == floor)
        return;

    radialFloorDb_ = floor;
    requestUpdate();
}

double PolarPlotChart::radialFloorDb() const noexcept
{
    return radialFloorDb_;
}

void PolarPlotChart::setMirror(bool mirror)
{
    if (mirror_ == mirror)
        return;

    mirror_ = mirror;
    requestUpdate();
}

bool PolarPlotChart::isMirrored() const noexcept
{
    return mirror_;
}

QRectF PolarPlotChart::polarRect() const
{
    const QRectF plot = plotRect();
    if (plot.isEmpty())
        return {};

    const qreal margin = Theme::forWidget(this).density().dp(kOuterMarginDp);
    const qreal side = std::min(plot.width(), plot.height()) - margin * 2.0;
    if (!(side > 0.0))
        return {};

    const QPointF middle = plot.center();
    return QRectF(middle.x() - side * 0.5, middle.y() - side * 0.5, side, side);
}

QPointF PolarPlotChart::centre() const
{
    return polarRect().center();
}

qreal PolarPlotChart::radius() const
{
    return polarRect().width() * 0.5;
}

QPointF PolarPlotChart::pointForIndex(int index) const
{
    if (index < 0 || index >= pointCount())
        return {};

    const QRectF area = polarRect();
    if (area.isEmpty())
        return {};

    return polarPoint(angleAt(index), area.width() * 0.5 * radialFraction(index));
}

int PolarPlotChart::nearestIndex(QPointF position) const
{
    if (!hasPattern() || polarRect().isEmpty())
        return -1;

    const qreal slop =
        std::max<qreal>(kMinimumSlopPx, Theme::forWidget(this).density().dp(kHitSlopDp));
    const qreal slopSquared = slop * slop;

    int best = -1;
    qreal bestDistance = slopSquared;

    for (int index = 0; index < pointCount(); ++index) {
        const QPointF pixel = pointForIndex(index);
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

QVector<double> PolarPlotChart::radialTicks() const
{
    if (scale_ == RadialScale::Db) {
        const double floor = radialFloorDb_;
        const double span = -floor;
        double step = 10.0;
        if (span > 140.0)
            step = 40.0;
        else if (span > 60.0)
            step = 20.0;

        QVector<double> ticks;
        for (double value = floor; value <= 1.0e-9; value += step)
            ticks.append(value);

        if (ticks.isEmpty() || ticks.last() < -1.0e-9)
            ticks.append(0.0);

        return ticks;
    }

    const double maximum = radialMaximum();
    QVector<double> ticks = MathematicsCoordination::niceTicks(
        0.0, static_cast<qreal>(maximum), 5);
    if (ticks.size() < 2) {
        ticks.clear();
        ticks.append(0.0);
        ticks.append(maximum);
    }

    return ticks;
}

QStringList PolarPlotChart::radialTickLabels() const
{
    QStringList labels;
    const QVector<double> ticks = radialTicks();
    labels.reserve(ticks.size());

    if (scale_ == RadialScale::Db) {
        for (double value : ticks)
            labels.append(QString::number(value, 'f', 0));
        return labels;
    }

    const qreal step = ticks.size() >= 2
        ? static_cast<qreal>(ticks.at(1) - ticks.at(0))
        : 1.0;
    for (double value : ticks)
        labels.append(MathematicsCoordination::tickLabel(static_cast<qreal>(value), step));

    return labels;
}

QVector<double> PolarPlotChart::angleTicks() const
{
    QVector<double> ticks;
    for (int step = 0; step < 8; ++step)
        ticks.append(static_cast<double>(step) * 45.0);
    return ticks;
}

QStringList PolarPlotChart::angleTickLabels() const
{
    QStringList labels;
    const QVector<double> ticks = angleTicks();
    labels.reserve(ticks.size());
    for (double value : ticks)
        labels.append(formatDegrees(value));
    return labels;
}

QString PolarPlotChart::radialAxisTitle() const
{
    if (scale_ == RadialScale::Db)
        return localized(QStringLiteral("chart.polar.levelDb"), QStringLiteral("Level (dB)"));

    return localized(QStringLiteral("chart.polar.magnitude"), QStringLiteral("Magnitude"));
}

QString PolarPlotChart::angleAxisTitle() const
{
    return localized(QStringLiteral("chart.polar.angle"), QStringLiteral("Angle (°)"));
}

qreal PolarPlotChart::revealProgress() const noexcept
{
    return animation().progress();
}

bool PolarPlotChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

bool PolarPlotChart::hasPattern() const noexcept
{
    for (double value : values_) {
        if (std::isfinite(value))
            return true;
    }
    return false;
}

double PolarPlotChart::radialAxisMinimum() const
{
    return scale_ == RadialScale::Db ? radialFloorDb_ : 0.0;
}

double PolarPlotChart::radialAxisMaximum() const
{
    return scale_ == RadialScale::Db ? 0.0 : radialMaximum();
}

double PolarPlotChart::radialFractionForValue(double value) const
{
    const double magnitude = std::abs(value);
    const double maximum = radialMaximum();

    if (!(maximum > 0.0))
        return 0.0;

    if (scale_ == RadialScale::Db) {
        if (!(magnitude > 0.0))
            return 0.0;

        const double decibels = 20.0 * std::log10(magnitude / maximum);
        if (!std::isfinite(decibels))
            return 0.0;

        const double floor = radialFloorDb_;
        const double fraction = (decibels - floor) / (0.0 - floor);
        return std::clamp(fraction, 0.0, 1.0);
    }

    return std::clamp(magnitude / maximum, 0.0, 1.0);
}

void PolarPlotChart::rebuildSeries()
{
    QVector<ChartSeries> list;

    if (hasPattern()) {
        ChartSeries pattern;
        pattern.name = localized(QStringLiteral("chart.polar.pattern"),
                                 QStringLiteral("Pattern"));
        pattern.points.reserve(pointCount());

        for (int index = 0; index < pointCount(); ++index)
            pattern.points.append(QPointF(angleAt(index), valueAt(index)));

        list.append(std::move(pattern));
    }

    ChartBase::setSeries(std::move(list));
}

QPointF PolarPlotChart::polarPoint(double degrees, qreal radiusPixels) const
{
    const double radians = normaliseAngle(degrees) * kPi / 180.0;
    const QPointF middle = centre();
    return QPointF(middle.x() + std::sin(radians) * radiusPixels,
                   middle.y() - std::cos(radians) * radiusPixels);
}

void PolarPlotChart::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    paintPlotAt(painter, plot, progress);
}

void PolarPlotChart::paintPlotAt(QPainter& painter, const QRectF& plot, qreal progress)
{
    if (plot.isEmpty())
        return;

    [[maybe_unused]] const PainterStateGuard guard(painter);
    try {
        const Theme theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();

        const QRectF circle = polarRect();
        if (circle.isEmpty())
            return;

        const QPointF middle = circle.center();
        const qreal outer = circle.width() * 0.5;
        if (!(outer > 0.0))
            return;

        const qreal gridWidth = std::max(kMinimumPenWidth, density.dp(kGridWidthDp));
        const qreal traceWidth = std::max(kMinimumPenWidth, density.dp(kTraceWidthDp));
        const QFont labels = tickFont(this, theme);
        const QFontMetricsF labelMetrics(labels);

        painter.setRenderHint(QPainter::Antialiasing, true);

        // 1. Rings, spokes and the outer ring.
        QColor gridColour = colours.outlineVariant;
        gridColour.setAlphaF(static_cast<float>(kGridAlpha));
        painter.setPen(QPen(gridColour, gridWidth));
        painter.setBrush(Qt::NoBrush);

        const QVector<double> ticks = radialTicks();
        const double radialMinimum = radialAxisMinimum();
        const double radialMaximumValue = radialAxisMaximum();
        const double radialSpan = radialMaximumValue - radialMinimum;

        for (double tick : ticks) {
            if (!(radialSpan > 0.0))
                continue;
            const qreal fraction = static_cast<qreal>(
                std::clamp((tick - radialMinimum) / radialSpan, 0.0, 1.0));
            if (!(fraction > 0.0))
                continue;
            painter.drawEllipse(middle, outer * fraction, outer * fraction);
        }

        QColor spokeColour = colours.outlineVariant;
        spokeColour.setAlphaF(static_cast<float>(kSpokeAlpha));
        painter.setPen(QPen(spokeColour, std::max(kMinimumPenWidth,
                                                  density.dp(kSpokeWidthDp))));

        for (double degrees : angleTicks())
            painter.drawLine(middle, polarPoint(degrees, outer));

        QColor outerColour = colours.outline;
        outerColour.setAlphaF(static_cast<float>(kOuterRingAlpha));
        painter.setPen(QPen(outerColour, std::max(kMinimumPenWidth,
                                                  density.dp(kOuterRingWidthDp))));
        painter.drawEllipse(middle, outer, outer);

        // 2. Angle labels outside the ring, clamped into the plot.
        painter.setFont(labels);
        painter.setPen(colours.onSurfaceVariant);
        const qreal angleGap = density.dp(kAngleLabelGapDp);

        const QVector<double> angleValues = angleTicks();
        const QStringList angleLabels = angleTickLabels();
        for (qsizetype i = 0; i < angleValues.size() && i < angleLabels.size(); ++i) {
            const QString& text = angleLabels.at(i);
            const QPointF anchor = polarPoint(angleValues.at(i), outer + angleGap);
            const qreal width = labelMetrics.horizontalAdvance(text);
            const qreal height = labelMetrics.height();
            QRectF box(anchor.x() - width * 0.5, anchor.y() - height * 0.5,
                       width, height);

            if (box.left() < plot.left())
                box.moveLeft(plot.left());
            if (box.right() > plot.right())
                box.moveRight(plot.right());
            if (box.top() < plot.top())
                box.moveTop(plot.top());
            if (box.bottom() > plot.bottom())
                box.moveBottom(plot.bottom());

            painter.drawText(box, Qt::AlignCenter, text);
        }

        // 3. Radial labels down the 0° spoke, to the right of the vertical
        //    axis. The outermost label is moved just inside the ring so it
        //    cannot collide with the 0° angle label.
        const QStringList radialLabels = radialTickLabels();
        const qreal radialGap = density.dp(kRadialLabelGapDp);

        for (qsizetype i = 0; i < ticks.size() && i < radialLabels.size(); ++i) {
            if (!(radialSpan > 0.0))
                continue;

            const qreal fraction = static_cast<qreal>(
                std::clamp((ticks.at(i) - radialMinimum) / radialSpan, 0.0, 1.0));
            if (!(fraction > 0.0))
                continue;

            const QString& text = radialLabels.at(i);
            const QPointF anchor = polarPoint(0.0, outer * fraction);
            const qreal width = labelMetrics.horizontalAdvance(text);
            const qreal height = labelMetrics.height();
            QRectF box(anchor.x() + radialGap, anchor.y() - height * 0.5,
                       width, height);

            if (fraction > 0.98)
                box.moveTop(circle.top() + radialGap);
            if (box.right() > plot.right())
                box.moveRight(plot.right());
            if (box.top() < plot.top())
                box.moveTop(plot.top());
            if (box.bottom() > plot.bottom())
                box.moveBottom(plot.bottom());

            painter.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, text);
        }

        // 4. Axis titles in the two free top corners.
        painter.setFont(axisTitleFont(this, theme));
        painter.setPen(colours.onSurfaceVariant);
        const QFontMetricsF titleMetrics(axisTitleFont(this, theme));
        const qreal titleGap = density.dp(kAxisTitleGapDp);

        painter.drawText(QRectF(plot.left() + titleGap, plot.top() + titleGap,
                                plot.width() * 0.5, titleMetrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, radialAxisTitle());
        painter.drawText(QRectF(plot.center().x(), plot.top() + titleGap,
                                plot.width() * 0.5 - titleGap, titleMetrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter, angleAxisTitle());

        if (!hasPattern())
            return;

        // 5. The pattern, or its mirrored pair, clipped to the circle and to
        //    the reveal wedge.
        QVector<QPointF> points;
        points.reserve(pointCount());
        for (int index = 0; index < pointCount(); ++index)
            points.append(pointForIndex(index));

        if (points.isEmpty())
            return;

        const double span = angleAt(pointCount() - 1) - angleAt(0);
        const bool closed = pointCount() >= 3 && span >= kClosedPatternSpanDegrees;
        const QColor colour = seriesColour(0);
        const QColor patternColour = colour.isValid() ? colour : colours.primary;

        const qreal reveal = std::clamp(progress, 0.0, 1.0);
        if (reveal <= 0.0)
            return;

        painter.save();
        painter.setClipRect(circle.adjusted(-3.0, -3.0, 3.0, 3.0), Qt::IntersectClip);
        if (reveal < 1.0) {
            QPainterPath wedge;
            wedge.moveTo(middle);
            wedge.arcTo(circle, 90.0, -360.0 * reveal);
            wedge.closeSubpath();
            painter.setClipPath(wedge, Qt::IntersectClip);
        }

        // Mirror first, dimmer, so the measured half stays dominant.
        if (mirror_) {
            QPainterPath mirrored;
            mirrored.moveTo(polarPoint(360.0 - angleAt(0), outer * radialFraction(0)));
            for (int index = 1; index < pointCount(); ++index) {
                mirrored.lineTo(polarPoint(360.0 - angleAt(index),
                                           outer * radialFraction(index)));
            }
            if (closed)
                mirrored.closeSubpath();

            QColor mirrorFill = patternColour;
            mirrorFill.setAlphaF(static_cast<float>(kMirrorFillAlpha));
            QColor mirrorStroke = patternColour;
            mirrorStroke.setAlphaF(static_cast<float>(kMirrorStrokeAlpha));

            if (closed) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(mirrorFill);
                painter.drawPath(mirrored);
            }

            QPen pen(mirrorStroke, traceWidth);
            pen.setJoinStyle(Qt::RoundJoin);
            pen.setCapStyle(Qt::RoundCap);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(mirrored);
        }

        QPainterPath path;
        path.moveTo(points.first());
        for (qsizetype i = 1; i < points.size(); ++i)
            path.lineTo(points.at(i));
        if (closed)
            path.closeSubpath();

        if (closed) {
            QColor fill = patternColour;
            fill.setAlphaF(static_cast<float>(kFillAlpha));
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawPath(path);
        }

        QPen pen(patternColour, traceWidth);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        const qreal markerRadius = std::max<qreal>(1.0, density.dp(kMarkerRadiusDp));
        painter.setPen(QPen(colours.surface, 1.0));
        painter.setBrush(patternColour);
        for (const QPointF& point : points)
            painter.drawEllipse(point, markerRadius, markerRadius);

        painter.restore();
    } catch (const std::exception& error) {
        qWarning("md::PolarPlotChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::PolarPlotChart::paintPlot failed with an unknown exception");
    }
}

void PolarPlotChart::paintEmptyState(QPainter& painter, const QRectF& plot)
{
    if (plot.isEmpty())
        return;

    painter.save();
    try {
        const Theme& theme = Theme::forWidget(this);
        painter.setFont(Font::resolve(this, theme.type().labelMedium, theme.density()));
        painter.setPen(theme.colours().onSurfaceVariant);
        painter.drawText(plot, Qt::AlignCenter,
                         localized(QStringLiteral("chart.polar.noData"),
                                   QStringLiteral("No directivity data")));
    } catch (const std::exception& error) {
        qWarning("md::PolarPlotChart::paintEmptyState failed: %s", error.what());
    } catch (...) {
        qWarning("md::PolarPlotChart::paintEmptyState failed with an unknown exception");
    }
    painter.restore();
}

} // namespace md
