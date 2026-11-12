// Periodicity — the BarChart component.
//
// Bar semantics. Every ChartSeries is one category: its name is the category
// label the legend shows and its points are the bars inside that group. Point j
// of category i is bar (i, j) everywhere in the API — barActivated(),
// setSelectedBar(), barRect() and barAt() — and a category may carry a different
// number of bars from its neighbours. A non-finite value is skipped (its bar is
// not drawn and cannot be hit) but keeps its index, so a missing sample never
// shifts the bars after it.
//
// Coordinates. As in the rest of the chart family (FunnelChart, RadarChart,
// BoxPlotChart), a point's y coordinate is the value the value axis reads,
// while x is the position of the bar inside its category; the category itself
// comes from the series index. A series whose values sit in x therefore draws
// zero-height bars.
//
// Colours. A bar takes the category's own colour when the series has a valid
// one; otherwise it takes chartPalette() at the bar index, so the same position
// in every group shares a colour and grouped bars stay distinguishable. The
// legend names the categories, because a ChartSeries is a category here.
//
// Grouped and stacked. Grouped (the default) places the bars of one category
// side by side inside the category slot; stacked builds them up from the zero
// line, positive values growing one way and negative values the other. A
// category is one horizontal slot in the vertical orientation and one vertical
// slot in the horizontal orientation, and setGroupGap() reserves that fraction
// of the slot as empty space around its bars.
//
// Axis. ChartBase owns the frame: it paints the grid, the axes, the tick marks
// and the tick labels from the chart's MathematicsCoordination. This component
// supplies the domain, because bars grow from zero: setSeries(), setStacked()
// and setOrientation() snap the value range outwards to whole nice ticks
// (MathematicsCoordination::niceTicks()) that contain zero and install it on
// math() with auto-fit off, so the frame the base paints and the bars share one
// transform and a label can never drift away from the bar it describes. The
// category axis is given the slot indices 0..categoryCount as its data range,
// so its labels name category boundaries rather than a fallback span.
//
// barRect() reports the painted geometry without painting, using the same
// linear interpolation the coordination performs over the same range, which
// makes the accessor usable from a hit test or a test before the first paint.
//
// Orientation. Vertical bars grow along y and categories advance along x;
// Horizontal swaps the two roles. Under Qt::RightToLeft the category order
// mirrors for vertical bars (category 0 at the right edge) and the bars inside a
// group mirror with it; horizontal bars keep their top-to-bottom category order,
// because the reading direction does not mirror the vertical axis.
//
// Values. setShowValues(true) labels each bar whose rectangle can hold the text,
// drawn inside the bar at its value end in a colour that contrasts with the bar.
//
// Empty state. With no category that carries a finite point hasData() is false
// and ChartBase paints its themed "No data" label instead of calling
// paintPlot(); this component paints nothing in that case.
//
// Exception safety. measureOverride(), paintPlot() and the mouse handlers each
// catch component exceptions, so a broken theme or a throwing subclass cannot
// unwind into Qt's paint or event loop.

#pragma once

#include <md/components/chart_common.h>
#include <md/view/view.h>

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QVector>

class QMouseEvent;
class QPainter;

namespace md {

class BarChart : public ChartBase
{
    Q_OBJECT

public:
    // Which way the bars grow. Vertical is the default.
    enum class Orientation {
        Vertical,
        Horizontal,
    };

    explicit BarChart(QWidget* parent = nullptr);

    // Replaces the categories, re-derives the value axis and clears the
    // selection, because the old pair described a bar that may no longer exist.
    // This hides ChartBase::setSeries(), so a direct call on a BarChart also
    // refreshes the component's own state and the mathematics coordination.
    void setSeries(QVector<ChartSeries> series);

    void setOrientation(Orientation orientation);
    [[nodiscard]] Orientation orientation() const noexcept;

    // Stack the bars of a category instead of placing them side by side. Off by
    // default.
    void setStacked(bool stacked);
    [[nodiscard]] bool isStacked() const noexcept;

    // Label each bar that can hold the text. Off by default.
    void setShowValues(bool show);
    [[nodiscard]] bool showsValues() const noexcept;

    // Fraction of a category slot kept empty around its bars, clamped into
    // 0..0.9. Default 0.25.
    void setGroupGap(qreal ratio);
    [[nodiscard]] qreal groupGap() const noexcept;

    // Corner radius of a bar in dp, clamped to zero from below. Painting clamps
    // it again to half the bar's shorter side, so a thin bar never inverts its
    // corners. Default 4.
    void setBarRadius(qreal dp);
    [[nodiscard]] qreal barRadius() const noexcept;

    // The emphasised bar, or (-1, -1) for none. An index pair that no longer
    // names a bar simply draws nothing selected; the next setSeries() clears it.
    void setSelectedBar(int series, int point);
    [[nodiscard]] int selectedSeries() const noexcept;
    [[nodiscard]] int selectedPoint() const noexcept;

    // The painted rectangle of one bar, in widget-local coordinates, or an empty
    // rectangle when the pair names no finite value. Stacked bars report the
    // segment of that bar, not the whole stack.
    //
    // The geometry follows the same axis range and zero baseline the paint pass
    // uses, and does not require a prior paint; before the first explicit layout
    // it falls back to contentRect().
    [[nodiscard]] QRectF barRect(int series, int point) const;

    // The bar under a widget-local pixel. Returns false and leaves the outputs
    // alone when no bar contains the point; a non-null output receives the
    // series/point pair otherwise. Bars are tested in painting order, so the
    // topmost stacked segment wins.
    [[nodiscard]] bool barAt(QPointF pixel, int* seriesIndex, int* pointIndex) const;

    // True when at least one category carries a finite point (ChartBase's
    // hasData()). An empty chart paints the base's themed empty state.
    [[nodiscard]] bool hasBarData() const noexcept;

    // The number of category slots, which is the number of series (a category
    // without bars still keeps its slot so the remaining categories do not
    // move).
    [[nodiscard]] int categoryCount() const noexcept;

signals:
    // Emitted on the release of a click that started and ended on the same bar.
    // The bar is selected first, so a handler may read selectedSeries() and
    // selectedPoint().
    void barActivated(int series, int point);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintPlot(QPainter& painter, const QRectF& plot) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    // Recomputes the value-axis range from the current series. Called from
    // setSeries() and from setStacked(), whose stacking changes the totals.
    void recomputeAxis();

    // Installs the exact axis range on the chart's MathematicsCoordination,
    // once per range change, so the frame the base paints and the bars share
    // one transform. The base installs the plot rectangle itself on every
    // paint. barRect() interpolates the same range linearly.
    void feedMathematics();

    [[nodiscard]] bool rightToLeft() const;

    // Category order mirrors on the category axis: right-to-left under
    // RightToLeft for vertical bars only.
    [[nodiscard]] bool mirrorCategories() const;

    // plotRect(), or contentRect() before the first explicit layout.
    [[nodiscard]] QRectF plotOrContentRect() const;

    Orientation orientation_ = Orientation::Vertical;
    bool stacked_ = false;
    bool showValues_ = false;
    qreal groupGap_ = 0.25;
    qreal barRadiusDp_ = 4.0;
    int selectedSeries_ = -1;
    int selectedPoint_ = -1;

    // The inclusive value-axis range. It always contains zero; a chart with no
    // data keeps 0..1 so the transform stays valid.
    qreal axisMinimum_ = 0.0;
    qreal axisMaximum_ = 1.0;

    // The bounds last installed on the coordination, so a paint that changes
    // nothing does not retarget the transform (which would cancel a caller's
    // zoom or restart a fit animation on every frame).
    QPointF fedMinimum_;
    QPointF fedMaximum_;
    bool fed_ = false;

    // The bar the current press landed on, so a release has to match it.
    int pressedSeries_ = -1;
    int pressedPoint_ = -1;
};

} // namespace md
