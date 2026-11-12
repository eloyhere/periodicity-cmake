// Periodicity — the RadarChart component.
//
// Geometry is polar and local to plotRect(): the centre is the plot centre and
// the outer radius is half the shorter side less the label margin. Axis i is
// placed at -90° + 360° * i / axisCount, so the first axis is at twelve o'clock
// and the order follows clockwise. The same helper builds the rings, the
// spokes and every series polygon, which keeps the three visually concentric
// even after a resize.
//
// The data value is points[i].y() for axis i: the x coordinate is not used,
// because a radar series is an ordered vector of axis values, not a set of
// Cartesian samples. A short series contributes the minimum for its missing
// axes; a NaN value does the same. Nothing outside [rangeMinimum,
// rangeMaximum] can draw outside the outer ring.
//
// ChartBase owns the title, legend and empty state. The base's frame is
// switched off in the constructor with setAxesVisible(false), because a polar
// chart has no numeric x/y axes; plotRect() is then the full inner rectangle and
// the radar is centred in exactly the area the base reports. The base selects
// paintPlot() from hasData() (any finite point); a series with no drawable value
// therefore reaches paintEmptyState() without the subclass doing anything. The
// one exception is a data set with fewer than three axes: the points are finite
// but no polygon exists, so paintPlot() forwards to the base empty state.
//
// polygonForSeries() is the only public geometry query; paintPlot() and the
// normalisation tests share it.

#include <md/components/radar_chart.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace md {
namespace {

constexpr qreal kLabelGapDp = 6.0;
constexpr qreal kLabelMarginDp = 26.0;
constexpr qreal kAxisLabelMaximumWidthDp = 96.0;
constexpr qreal kRingWidthDp = 1.0;
constexpr qreal kSpokeWidthDp = 1.0;
constexpr qreal kSeriesOutlineDp = 2.0;
constexpr qreal kRingAlpha = 0.8;
constexpr qreal kSpokeAlpha = 0.7;
constexpr qreal kPi = 3.141592653589793238462643383279502884;

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

[[nodiscard]] QPointF axisPoint(const QPointF& centre, qreal radius, int axisIndex, int axisCount)
{
    const qreal step = 2.0 * kPi / axisCount;
    const qreal angle = -kPi / 2.0 + step * axisIndex;
    return centre + QPointF(std::cos(angle) * radius, std::sin(angle) * radius);
}

[[nodiscard]] qreal radarRadius(const QRectF& plot, const Density& density)
{
    const qreal halfSide = std::min(plot.width(), plot.height()) * 0.5;
    return std::max<qreal>(0.0, halfSide - density.dp(kLabelMarginDp));
}

} // namespace

RadarChart::RadarChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(false);
    setAxesVisible(false);

    // Read-only data surface: a whole-surface hover tint would misrepresent the
    // painted colours; per-chart hover is painted by the chart itself.
    setInteractionFeedbackEnabled(false);
}

void RadarChart::setAxes(QStringList labels)
{
    if (axes_ == labels)
        return;

    axes_ = std::move(labels);
    requestUpdate();
}

QStringList RadarChart::axes() const
{
    return axes_;
}

void RadarChart::setRange(qreal minimum, qreal maximum)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum == maximum)
        return;

    if (minimum > maximum)
        std::swap(minimum, maximum);

    if (rangeMinimum_ == minimum && rangeMaximum_ == maximum)
        return;

    rangeMinimum_ = minimum;
    rangeMaximum_ = maximum;
    requestUpdate();
}

qreal RadarChart::rangeMinimum() const noexcept
{
    return rangeMinimum_;
}

qreal RadarChart::rangeMaximum() const noexcept
{
    return rangeMaximum_;
}

void RadarChart::setRingCount(int count)
{
    const int rings = std::clamp(count, 1, 12);
    if (ringCount_ == rings)
        return;

    ringCount_ = rings;
    requestUpdate();
}

int RadarChart::ringCount() const noexcept
{
    return ringCount_;
}

void RadarChart::setFillOpacity(qreal opacity)
{
    const qreal alpha = std::isfinite(opacity) ? std::clamp(opacity, 0.0, 1.0) : 0.0;
    if (fillOpacity_ == alpha)
        return;

    fillOpacity_ = alpha;
    requestUpdate();
}

qreal RadarChart::fillOpacity() const noexcept
{
    return fillOpacity_;
}

qreal RadarChart::normalisedValue(qreal value) const noexcept
{
    const qreal span = rangeMaximum_ - rangeMinimum_;
    if (!(span > 0.0) || !std::isfinite(value))
        return 0.0;

    const qreal clamped = std::clamp(value, rangeMinimum_, rangeMaximum_);
    return (clamped - rangeMinimum_) / span;
}

QVector<QPointF> RadarChart::polygonForSeries(int seriesIndex) const
{
    if (seriesIndex < 0 || seriesIndex >= series().size() || axes_.size() < 3)
        return {};

    const QRectF plot = plotRect();
    if (plot.isEmpty())
        return {};

    const Theme& theme = Theme::forWidget(this);
    const QPointF centre = plot.center();
    const qreal radius = radarRadius(plot, theme.density());
    const QVector<QPointF>& points = series().at(seriesIndex).points;
    const int axisCount = static_cast<int>(axes_.size());

    QVector<QPointF> polygon;
    polygon.reserve(axisCount);

    for (int i = 0; i < axisCount; ++i) {
        const qreal value = i < points.size() ? points.at(i).y() : rangeMinimum_;
        polygon.append(axisPoint(centre, radius * normalisedValue(value), i, axisCount));
    }

    return polygon;
}

void RadarChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    if (plot.isEmpty())
        return;

    const PainterStateGuard guard(painter);

    try {
        if (axes_.size() < 3) {
            // Finite values without a drawable polar frame: the base gate saw
            // data, so the themed empty state has to come from here.
            ChartBase::paintEmptyState(painter, plot);
            return;
        }

        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();

        const QPointF centre = plot.center();
        const qreal radius = radarRadius(plot, density);
        const int axisCount = static_cast<int>(axes_.size());

        if (!(radius > 0.0))
            return;

        // 1. Concentric rings, as polygons over the same axis vertices.
        QColor ringColour = colours.outlineVariant;
        ringColour.setAlphaF(static_cast<float>(kRingAlpha));
        painter.setPen(QPen(ringColour, std::max<qreal>(1.0, density.dp(kRingWidthDp))));
        painter.setBrush(Qt::NoBrush);

        for (int ring = 1; ring <= ringCount_; ++ring) {
            const qreal ringRadius = radius * ring / ringCount_;
            QPolygonF polygon;
            polygon.reserve(axisCount);

            for (int i = 0; i < axisCount; ++i)
                polygon.append(axisPoint(centre, ringRadius, i, axisCount));

            painter.drawPolygon(polygon);
        }

        // 2. Spokes from the centre to the outer ring.
        QColor spokeColour = colours.outlineVariant;
        spokeColour.setAlphaF(static_cast<float>(kSpokeAlpha));
        painter.setPen(QPen(spokeColour, std::max<qreal>(1.0, density.dp(kSpokeWidthDp))));

        for (int i = 0; i < axisCount; ++i)
            painter.drawLine(centre, axisPoint(centre, radius, i, axisCount));

        // 3. Series fills, all before any outline.
        QVector<QColor> seriesColours;
        seriesColours.reserve(series().size());

        for (qsizetype i = 0; i < series().size(); ++i) {
            QColor colour = seriesColour(static_cast<int>(i));
            if (!colour.isValid())
                colour = colours.primary;
            seriesColours.append(colour);
        }

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);

        for (qsizetype i = 0; i < series().size(); ++i) {
            const QVector<QPointF> vertices = polygonForSeries(static_cast<int>(i));
            if (vertices.size() < 3)
                continue;

            QPolygonF polygon;
            polygon.reserve(vertices.size());
            for (const QPointF& vertex : vertices)
                polygon.append(vertex);

            QColor fill = seriesColours.at(i);
            fill.setAlphaF(static_cast<float>(fillOpacity_));
            painter.setBrush(fill);
            painter.drawPolygon(polygon);
        }

        // 4. Series outlines, so no translucent fill can wash one away.
        const qreal outlineWidth = std::max<qreal>(1.0, density.dp(kSeriesOutlineDp));
        painter.setBrush(Qt::NoBrush);

        for (qsizetype i = 0; i < series().size(); ++i) {
            const QVector<QPointF> vertices = polygonForSeries(static_cast<int>(i));
            if (vertices.size() < 3)
                continue;

            QPolygonF polygon;
            polygon.reserve(vertices.size());
            for (const QPointF& vertex : vertices)
                polygon.append(vertex);

            QPen pen(seriesColours.at(i), outlineWidth);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            painter.drawPolygon(polygon);
        }

        // 5. Axis labels, outside the outer ring and clamped into the plot.
        const QFont labelFont = Font::resolve(this, theme.type().labelSmall, density);
        const QFontMetricsF labelMetrics(labelFont);
        const qreal labelGap = density.dp(kLabelGapDp);
        const qreal maximumWidth = std::min(
            density.dp(kAxisLabelMaximumWidthDp),
            std::max<qreal>(plot.width() * 0.5, labelMetrics.horizontalAdvance(QStringLiteral("W"))));

        painter.setFont(labelFont);
        painter.setPen(colours.onSurfaceVariant);

        for (int i = 0; i < axisCount; ++i) {
            const QString label = axes_.at(i);
            if (label.isEmpty())
                continue;

            const QPointF anchor = axisPoint(centre, radius + labelGap, i, axisCount);
            const QString text = labelMetrics.elidedText(label, Qt::ElideRight, maximumWidth);
            const qreal textWidth = labelMetrics.horizontalAdvance(text);
            QRectF textRect(anchor.x() - textWidth * 0.5,
                            anchor.y() - labelMetrics.height() * 0.5,
                            textWidth, labelMetrics.height());

            if (textRect.left() < plot.left())
                textRect.moveLeft(plot.left());
            if (textRect.right() > plot.right())
                textRect.moveRight(plot.right());
            if (textRect.top() < plot.top())
                textRect.moveTop(plot.top());
            if (textRect.bottom() > plot.bottom())
                textRect.moveBottom(plot.bottom());

            painter.drawText(textRect, Qt::AlignCenter, text);
        }
    } catch (const std::exception& error) {
        qWarning("md::RadarChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::RadarChart::paintPlot failed with an unknown exception");
    }
}

} // namespace md
