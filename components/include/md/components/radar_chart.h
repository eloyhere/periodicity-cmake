// Periodicity — the RadarChart component.
//
// A polar chart. Every entry of setAxes() is one spoke on a shared circle, and
// every series is one closed polygon whose vertex i lies on axis i at the value
// of points[i].y(). Values are normalised against setRange(): the minimum maps
// to the centre, the maximum to the outer ring, and anything outside that
// interval is clamped, so a value above the range cannot draw past the frame.
// A missing or NaN value is treated as the minimum, which lets a short series
// close without inventing amplitude. At least three axes are required for a
// polygon; fewer paint the shared empty state.
//
// The rings are concentric polygons (one per axis, matching the grid), the
// spokes use outlineVariant, the axis labels use labelSmall/onSurfaceVariant,
// and each series is filled with its own colour at setFillOpacity() and
// outlined with a 2 dp pen. The fills of all series are painted before any
// outline, so a translucent fill cannot wash out another series' edge. The
// legend is the shared ChartBase legend: name the series and the base row
// draws it.
//
// Empty state: fewer than three axes, or no series with a finite value,
// selects paintEmptyState() instead of paintPlot().
//
// Chart chrome: ChartBase owns the title, legend and empty state. A polar chart
// has no Cartesian axes, so the constructor calls setAxesVisible(false):
// plotRect() is then the full inner rectangle, the base skips the frame, and
// the radar's centre is the plot's centre. The base's hasData() predicate (any
// finite point) selects paintPlot(); an empty series paints the base's themed
// empty state.
//
// polygonForSeries() exposes the exact pixel polygon paintPlot() uses. It is
// public because a host may want the same geometry for a tooltip, and it makes
// the normalisation and clamping observable without a screenshot.
//
// Exception safety: paintPlot() catches locally; nothing unwinds into Qt's
// paint loop.

#pragma once

#include <md/components/chart_common.h>
#include <md/view/view.h>

#include <QPointF>
#include <QRectF>
#include <QStringList>
#include <QVector>

class QPainter;

namespace md {

class RadarChart : public ChartBase
{
    Q_OBJECT

public:
    explicit RadarChart(QWidget* parent = nullptr);

    // The axis names, in the order the vertices are placed. The first axis is
    // at twelve o'clock and the others follow clockwise. Fewer than three axes
    // means the chart has no drawable polygon and paints its empty state.
    void setAxes(QStringList labels);
    [[nodiscard]] QStringList axes() const;

    // The data interval the radius represents: `minimum` maps to the centre,
    // `maximum` to the outer ring. Reversed endpoints are normalised. A
    // non-finite pair, or a pair with equal endpoints, is ignored because it
    // has no usable span. The default is 0..100.
    void setRange(qreal minimum, qreal maximum);
    [[nodiscard]] qreal rangeMinimum() const noexcept;
    [[nodiscard]] qreal rangeMaximum() const noexcept;

    // The number of concentric rings, clamped to 1..12. The default is 4.
    void setRingCount(int count);
    [[nodiscard]] int ringCount() const noexcept;

    // The series fill alpha, clamped to 0..1. The default is 0.22, which keeps
    // overlapping polygons readable. Zero still outlines the series.
    void setFillOpacity(qreal opacity);
    [[nodiscard]] qreal fillOpacity() const noexcept;

    // The polygon for one series in ChartBase-local pixels: one vertex per
    // axis, in axis order, at the normalised (and clamped) value of that
    // point's y. Empty for an invalid series index, fewer than three axes, or
    // before the widget has a plot rectangle. The polygon is not closed
    // explicitly; drawing it closes it, as QPainter::drawPolygon() does.
    [[nodiscard]] QVector<QPointF> polygonForSeries(int seriesIndex) const;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override;

private:
    // Clamps `value` into [rangeMinimum_, rangeMaximum_] and maps it to 0..1.
    // A degenerate range or a non-finite value yields 0 (the centre).
    [[nodiscard]] qreal normalisedValue(qreal value) const noexcept;

    QStringList axes_;
    qreal rangeMinimum_ = 0.0;
    qreal rangeMaximum_ = 100.0;
    int ringCount_ = 4;
    qreal fillOpacity_ = 0.22;
};

} // namespace md
