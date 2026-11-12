// Periodicity — the ScatterChart component.
//
// A multi-series scatter plot. ChartBase owns the shared surface — the title,
// the legend, the coordinate frame, the categorical palette, the empty state,
// auto-fit and hit testing — so this class paints only the markers inside
// plotRect() and tracks which one the pointer is over.
//
// Each finite sample becomes a Circle, Square or Diamond of a dp-sized
// silhouette, in its series' explicit colour or in the theme's categorical
// palette entry for the series index. mouseMoveEvent() asks the inherited
// hitTest() for the nearest sample within its density-scaled slop, stores the
// indices and requests a repaint; leaveEvent() clears them. paintPlot() validates
// the stored indices against the current series before drawing the halo, so
// replacing the data between two frames cannot index a stale point.
//
// Data bounds are not this class's job. ChartBase::setSeries() forwards the
// finite union of every point to math()->setDataBounds() with auto-fit enabled,
// and the base never overwrites the viewport on later paints.
//
// Empty state. When no series contains a finite point, ChartBase calls its
// inherited paintEmptyState() instead of paintPlot(): the base's themed,
// localized "No data" label is centred in the plot rectangle inside the normal
// coordinate frame. ScatterChart does not draw a second placeholder.
//
// Direction. The numeric axes belong to ChartBase/MathematicsCoordination and
// stay left-to-right in both layout directions; this class draws no text and
// derives every pixel from math()->toPixel(), so it makes no left/right
// assumption of its own and is RTL-safe by construction.
//
// Exception safety. measureOverride(), paintPlot(), mouseMoveEvent() and
// leaveEvent() catch their own component failures; the remaining event entry
// points are guarded by View/ChartBase.

#pragma once

#include <md/components/chart_common.h>

class QEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class ScatterChart : public ChartBase
{
    Q_OBJECT

public:
    // The marker silhouette. These names describe the drawn shape; they are
    // unrelated to the design baseline ShapeScale that View::setShape()/shape()
    // manage for the card itself, which is why the getter is pointShape().
    enum class PointShape {
        Circle,
        Square,
        Diamond,
    };

    explicit ScatterChart(QWidget* parent = nullptr);

    // Keep View::setShape(Shape) reachable as an overload next to the point
    // shape setter below.
    using View::setShape;

    // Forwards to ChartBase::setSeries() (which feeds the new finite union to
    // math()->setDataBounds()) and clears the hover indices, because they
    // describe points of the replaced data set.
    void setSeries(QVector<ChartSeries> seriesList);

    // Marker diameter in dp. Negative values are clamped to zero; painting
    // keeps a minimum visible radius.
    void setPointSize(qreal dp);
    [[nodiscard]] qreal pointSize() const noexcept;

    // The marker silhouette. The default is Circle.
    void setShape(PointShape value);
    [[nodiscard]] PointShape pointShape() const noexcept;

    // Indices of the sample under the last mouse-move, or -1 when the pointer
    // is not over a point (or has left the widget).
    [[nodiscard]] int hoveredPoint() const noexcept;
    [[nodiscard]] int hoveredSeries() const noexcept;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintPlot(QPainter& painter, const QRectF& plot) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void setHovered(int seriesIndex, int pointIndex);

    qreal pointSizeDp_ = 8.0;
    PointShape shape_ = PointShape::Circle;
    int hoveredSeries_ = -1;
    int hoveredPoint_ = -1;
};

} // namespace md
