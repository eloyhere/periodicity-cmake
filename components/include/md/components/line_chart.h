// Periodicity — the LineChart component.
//
// A multi-series line chart. ChartBase owns everything the charts share —
// the title, the legend, the coordinate frame (grid, axes, tick labels, axis
// titles), the categorical palette, the empty state and the auto-fit transform
// — so this class paints only the data marks inside plotRect():
//
//   * one stroked polyline per series, in the series' explicit colour or in
//     the theme's categorical palette entry for its index;
//   * an optional translucent area fill down to the bottom of the plot when
//     ChartSeries::filled is true, painted before every stroke so one series
//     cannot wash over another's line;
//   * optional point markers, independently switchable with setShowMarkers().
//
// Curves are straight segments by default. setSmooth(true) replaces them with a
// Catmull-Rom spline converted to cubic Béziers: the curve still passes through
// every sample but rounds the corners between them. Non-finite samples are
// skipped, exactly as ChartSeries::boundingRect() and the base's automatic
// bounds/hitTest() ignore them.
//
// Data bounds are not this class's job. ChartBase::setSeries() forwards the
// finite union of every point to math()->setDataBounds() with auto-fit enabled,
// and the base never overwrites the viewport on later paints, so a caller may
// still install a manual range or zoom/pan after setting the series.
//
// Empty state. When no series contains a finite point, ChartBase calls its
// inherited paintEmptyState() instead of paintPlot(): the base's themed,
// localized "No data" label is centred in the same plot rectangle the data
// would use, inside the normal coordinate frame. LineChart does not draw a
// second placeholder.
//
// Direction. The numeric x axis belongs to ChartBase/MathematicsCoordination
// and stays left-to-right in both layout directions; this class draws no text
// and derives every pixel from math()->toPixel(), so it makes no left/right
// assumption of its own and is RTL-safe by construction.
//
// Exception safety. measureOverride() and paintPlot() catch their own
// component failures; the inherited event entry points are guarded by
// View/ChartBase.

#pragma once

#include <md/components/chart_common.h>

#include <QVector>

class QPainter;
class QRectF;

namespace md {

class LineChart : public ChartBase
{
    Q_OBJECT

public:
    explicit LineChart(QWidget* parent = nullptr);

    // Straight segments (the default) or a Catmull-Rom spline.
    void setSmooth(bool smooth);
    [[nodiscard]] bool isSmooth() const noexcept;

    // Draw a filled disc at every sample. Off by default.
    void setShowMarkers(bool showMarkers);
    [[nodiscard]] bool showsMarkers() const noexcept;

    // Stroke width in dp. Negative values are clamped to zero; the painted pen
    // is never narrower than half a device-independent pixel.
    void setLineWidth(qreal dp);
    [[nodiscard]] qreal lineWidth() const noexcept;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintPlot(QPainter& painter, const QRectF& plot) override;

private:
    bool smooth_ = false;
    bool showMarkers_ = false;
    qreal lineWidthDp_ = 2.0;
};

} // namespace md
