// Periodicity — the ScatterChart component.
//
// ChartBase has already painted the frame and installed plotRect() on the
// transform when paintPlot() runs, so this file maps the finite samples, draws
// the marker silhouettes and, for the hovered sample, a soft halo. The painter
// is clipped to the plot rectangle plus room for edge markers because ChartBase
// deliberately leaves paintPlot() unclipped.
//
// Hover state is in series/point indices, not in pixels: hitTest() resolves the
// nearest sample for the current frame, and paintPlot() re-validates the
// indices against the current vectors before indexing them. The halo therefore
// disappears immediately when new data replaces the old one.
//
// A throwing paintPlot() or mouse handler is caught locally even though
// ChartBase/View also guard their entry points; the component must not unwind
// into Qt from any of its own overrides.

#include <md/components/scatter_chart.h>

#include <md/theme/theme.h>

#include <QColor>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace md {
namespace {

constexpr qreal kHoverHaloDp = 4.0;
constexpr qreal kHoverRingDp = 1.5;
constexpr qreal kHoverAlpha = 0.22;
constexpr qreal kMinimumPenWidth = 0.5;
constexpr qreal kMinimumRadius = 1.0;

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

void drawMarker(QPainter& painter, const QPointF& centre, qreal radius, ScatterChart::PointShape shape,
                const QColor& colour)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);

    switch (shape) {
    case ScatterChart::PointShape::Circle:
        painter.drawEllipse(centre, radius, radius);
        break;
    case ScatterChart::PointShape::Square:
        painter.drawRect(
            QRectF(centre.x() - radius, centre.y() - radius, radius * 2.0, radius * 2.0));
        break;
    case ScatterChart::PointShape::Diamond: {
        QPolygonF diamond;
        diamond << QPointF(centre.x(), centre.y() - radius)
                << QPointF(centre.x() + radius, centre.y())
                << QPointF(centre.x(), centre.y() + radius)
                << QPointF(centre.x() - radius, centre.y());
        painter.drawPolygon(diamond);
        break;
    }
    }
}

} // namespace

ScatterChart::ScatterChart(QWidget* parent)
    : ChartBase(parent)
{
    // Hover is the chart's only pointer interaction, and it must work without
    // a held button. ChartBase already disables focus.
    setMouseTracking(true);

    // Read-only data surface: a whole-surface hover tint would misrepresent the
    // painted colours; per-chart hover is painted by the chart itself.
    setInteractionFeedbackEnabled(false);
}

void ScatterChart::setSeries(QVector<ChartSeries> seriesList)
{
    ChartBase::setSeries(std::move(seriesList));
    // The stored indices describe points of the old data set.
    setHovered(-1, -1);
}

void ScatterChart::setPointSize(qreal dp)
{
    const qreal size = dp > 0.0 ? dp : 0.0;
    if (pointSizeDp_ == size)
        return;

    pointSizeDp_ = size;
    requestUpdate();
}

qreal ScatterChart::pointSize() const noexcept
{
    return pointSizeDp_;
}

void ScatterChart::setShape(PointShape value)
{
    if (shape_ == value)
        return;

    shape_ = value;
    requestUpdate();
}

ScatterChart::PointShape ScatterChart::pointShape() const noexcept
{
    return shape_;
}

int ScatterChart::hoveredPoint() const noexcept
{
    return hoveredPoint_;
}

int ScatterChart::hoveredSeries() const noexcept
{
    return hoveredSeries_;
}

void ScatterChart::setHovered(int seriesIndex, int pointIndex)
{
    if (hoveredSeries_ == seriesIndex && hoveredPoint_ == pointIndex)
        return;

    hoveredSeries_ = seriesIndex;
    hoveredPoint_ = pointIndex;
    requestUpdate();
}

void ScatterChart::measureOverride(MeasureSpec width, MeasureSpec height)
{
    try {
        // The base owns the preferred chart size (and the title/legend-aware
        // layout that consumes it), so the subclass only guard-rails the call.
        ChartBase::measureOverride(width, height);
    } catch (const std::exception& error) {
        qWarning("md::ScatterChart::measureOverride failed: %s", error.what());
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    } catch (...) {
        qWarning("md::ScatterChart::measureOverride failed with an unknown exception");
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    }
}

void ScatterChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    if (plot.isEmpty())
        return;

    [[maybe_unused]] const PainterStateGuard guard(painter);
    try {
        const Theme theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const QVector<ChartSeries>& entries = series();

        const qreal radius = std::max(kMinimumRadius, density.dp(pointSizeDp_) / 2.0);
        const qreal haloRadius = radius + density.dp(kHoverHaloDp);
        const qreal ringWidth = std::max(kMinimumPenWidth, density.dp(kHoverRingDp));

        const qsizetype seriesCount = entries.size();
        const bool hasHover = hoveredSeries_ >= 0
            && static_cast<qsizetype>(hoveredSeries_) < seriesCount && hoveredPoint_ >= 0
            && static_cast<qsizetype>(hoveredPoint_)
                < entries.at(hoveredSeries_).points.size();

        [[maybe_unused]] const PainterStateGuard clipGuard(painter);
        const qreal clipMargin = haloRadius + 2.0;
        painter.setClipRect(plot.adjusted(-clipMargin, -clipMargin, clipMargin, clipMargin),
                            Qt::IntersectClip);

        for (qsizetype i = 0; i < seriesCount; ++i) {
            const ChartSeries& entry = entries.at(i);
            const QColor colour = seriesColour(static_cast<int>(i));
            if (!colour.isValid())
                continue;

            for (qsizetype j = 0; j < entry.points.size(); ++j) {
                const QPointF& point = entry.points.at(j);
                if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
                    continue;

                const QPointF pixel = math()->toPixel(point);
                const bool hovered = hasHover && hoveredSeries_ == static_cast<int>(i)
                    && hoveredPoint_ == static_cast<int>(j);

                if (hovered) {
                    QColor halo = colour;
                    halo.setAlphaF(kHoverAlpha);
                    painter.setPen(QPen(colour, ringWidth));
                    painter.setBrush(halo);
                    painter.drawEllipse(pixel, haloRadius, haloRadius);
                }

                drawMarker(painter, pixel, radius, shape_, colour);
            }
        }
    } catch (const std::exception& error) {
        qWarning("md::ScatterChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::ScatterChart::paintPlot failed with an unknown exception");
    }
}

void ScatterChart::mouseMoveEvent(QMouseEvent* event)
{
    try {
        View::mouseMoveEvent(event);

        int seriesIndex = -1;
        int pointIndex = -1;
        const bool found = hitTest(event->position(), &seriesIndex, &pointIndex);
        setHovered(found ? seriesIndex : -1, found ? pointIndex : -1);
    } catch (const std::exception& error) {
        qWarning("md::ScatterChart::mouseMoveEvent failed: %s", error.what());
        hoveredSeries_ = -1;
        hoveredPoint_ = -1;
        update();
    } catch (...) {
        qWarning("md::ScatterChart::mouseMoveEvent failed with an unknown exception");
        hoveredSeries_ = -1;
        hoveredPoint_ = -1;
        update();
    }
}

void ScatterChart::leaveEvent(QEvent* event)
{
    try {
        View::leaveEvent(event);
        setHovered(-1, -1);
    } catch (const std::exception& error) {
        qWarning("md::ScatterChart::leaveEvent failed: %s", error.what());
        hoveredSeries_ = -1;
        hoveredPoint_ = -1;
        update();
    } catch (...) {
        qWarning("md::ScatterChart::leaveEvent failed with an unknown exception");
        hoveredSeries_ = -1;
        hoveredPoint_ = -1;
        update();
    }
}

} // namespace md
