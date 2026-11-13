// Periodicity — the PieChart component.
//
// Slice semantics. Every ChartSeries is one slice: its name is the slice label
// the legend shows and its value is points[0].y(); the x component is ignored.
// A series that is empty, whose first point is not finite, or whose value is not
// positive contributes no angle and can never be hit. The slice index used by
// every accessor and by the signal is the series index, so a skipped series
// leaves a hole in the index space instead of shifting the slices after it.
//
// Angles. The chart measures angles in degrees clockwise from twelve o'clock:
// 0° points up and 90° points right. setStartAngle() moves the leading edge of
// the first slice and is normalised into [0, 360). Under Qt::RightToLeft the
// sweep is mirrored — slices run counter-clockwise from the start angle — which
// keeps the reading order starting on the reading-start side of the top instead
// of the reading-end side.
//
// Percentages. With setShowPercentages(true) (the default) each slice whose
// share of the total is at least 5 % carries an integer percentage label at its
// mid angle; smaller slices are left bare so a long tail of tiny slices stays
// readable. A label is also dropped when the slice's arc at the label radius is
// too short for the text, or when a donut ring is thinner than the line, so the
// label can never overflow its slice. sliceLabel() reports exactly the text the
// paint pass would use, which makes the threshold observable without rendering.
//
// Donut. setDonut(true) removes the wedge in the middle: the inner radius is
// setDonutRatio() × the outer radius (0.55 by default, clamped to 0..0.95) and
// the hole is not painted at all, so whatever is behind the chart shows through.
// Percentage labels then sit in the middle of the ring.
//
// Reveal animation. The first time the chart is shown it sweeps in from the
// start angle: slices are painted up to a growing angular budget over the
// theme's medium2 duration. The sweep is presentation only — sliceAt(),
// sliceSpan() and sliceLabel() describe the settled geometry. A reduced-motion
// theme, or a spec whose duration resolves to zero, snaps to the finished state
// synchronously. A chart that was never shown also paints finished, so an
// offscreen render or a QWidget::grab() is complete rather than blank.
//
// Hit-testing. ChartBase::hitTest() reports data points, which a pie does not
// have in pixel space, so sliceAt() maps a widget-local pixel through the angle
// and the radius to a slice index, or -1 outside the disc, inside the donut
// hole, or when there is no data. The mouse handlers use it, and the gaps
// between slices are not dead zones: the hit test follows the settled angular
// spans, not the painted gap.
//
// Painting. ChartBase owns the furniture: it paints the title, the legend row
// and the empty state, and this component suppresses the Cartesian frame with
// setAxesVisible(false) — a disc has no axes — so the base lays the plot out over
// the full inner rectangle. Slice colours come from the base's seriesColour(),
// percentage labels and chrome use theme roles only, and every length passes
// through Density::dp(). The selected slice is exploded outward by a few dp and
// outlined, so selection survives a palette with similar hues.
//
// Exception safety. measureOverride(), paintPlot(), the mouse handlers, the
// reveal start and the theme hook all catch component exceptions, so a broken
// theme or a throwing subclass cannot unwind into Qt's paint or event loop.

#pragma once

#include <md/animation/animator.h>
#include <md/components/chart_common.h>
#include <md/view/view.h>

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

class QMouseEvent;
class QPainter;

namespace md {

class PieChart : public ChartBase
{
    Q_OBJECT

public:
    explicit PieChart(QWidget* parent = nullptr);

    // Replaces the slices and clears the selection, because the old index
    // described a slice that may no longer exist. This hides
    // ChartBase::setSeries(), so a direct call on a PieChart also resets the
    // component's own state and schedules the repaint.
    void setSeries(QVector<ChartSeries> series);

    // Draw the ring instead of the full disc. Off by default.
    void setDonut(bool donut);
    [[nodiscard]] bool isDonut() const noexcept;

    // Inner radius as a fraction of the outer radius. The value is clamped into
    // 0..0.95 so the ring never collapses or inverts. Default 0.55, used only
    // while isDonut() is true.
    void setDonutRatio(qreal ratio);
    [[nodiscard]] qreal donutRatio() const noexcept;

    // Draw an integer percentage inside every slice wide enough to hold it.
    // On by default.
    void setShowPercentages(bool show);
    [[nodiscard]] bool showsPercentages() const noexcept;

    // Angle of the first slice's leading edge, in degrees clockwise from twelve
    // o'clock. Normalised into [0, 360); mirrored under Qt::RightToLeft.
    void setStartAngle(qreal degrees);
    [[nodiscard]] qreal startAngle() const noexcept;

    // Degrees removed between neighbouring slices; each side of a slice loses
    // half the gap, so adjacent slices stay symmetric. Clamped into 0..120.
    // Default 0, which makes the slices meet exactly.
    void setSliceGap(qreal degrees);
    [[nodiscard]] qreal sliceGap() const noexcept;

    // Index of the emphasised slice, or -1 for none. The value is not clamped to
    // the current data: an index that no longer names a slice simply draws
    // nothing selected, and the next setSeries() clears it anyway.
    void setSelectedSlice(int index);
    [[nodiscard]] int selectedSlice() const noexcept;

    // The number of slices that actually carry an angle: series with an empty
    // point list or a value that is not finite and positive are not counted.
    [[nodiscard]] int sliceCount() const;

    // The drawn angular span of one slice, in degrees, after the slice gap has
    // been removed. `index` is a series index; 0 is returned for a series that
    // is not a slice. With a zero gap the spans of every slice sum to 360.
    [[nodiscard]] qreal sliceSpan(int index) const;

    // The percentage text the paint pass would draw inside the slice, or an
    // empty string when percentages are off, the series is not a slice, or its
    // share is below the 5 % label threshold.
    [[nodiscard]] QString sliceLabel(int index) const;

    // The slice under a widget-local pixel, or -1. The test uses the settled
    // geometry: angle first, then the radius against the outer edge and, for a
    // donut, the inner edge.
    [[nodiscard]] int sliceAt(QPointF pixel) const;

    // The square the disc is inscribed in, in widget-local coordinates. It is
    // inset from plotRect() by a margin that leaves room for the selected
    // slice's explosion, and falls back to contentRect() before the first
    // explicit layout so geometry is usable from a test or a hit test.
    [[nodiscard]] QRectF pieRect() const;

    // 0..1 sweep progress. 1 before the first show (a chart that has never been
    // shown paints finished) and 1 immediately after a reduced-motion show.
    [[nodiscard]] qreal revealProgress() const noexcept;

    // True while the sweep is still running, so a caller (or a test) can tell an
    // animation in flight from a finished chart without watching the clock.
    [[nodiscard]] bool isRevealing() const noexcept;

signals:
    // Emitted on the release of a click that started and ended on the same
    // slice. The slice is selected first, so a handler may read
    // selectedSlice().
    void sliceActivated(int index);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintPlot(QPainter& painter, const QRectF& plot) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

    // The sweep starts with the show, not with the first paint: a chart that has
    // been shown but not yet painted already reports a running reveal. The run
    // itself belongs to ChartBase, so this only asks the base to start it.
    void onShown() override;

private:
    // One slice in the settled angular layout. `offset` and `share` are the raw
    // cumulative position and width in degrees from the start angle, before the
    // gap; `span` is what is painted. Both the paint pass and sliceAt() derive
    // their start angle from these fields, which keeps them in step.
    struct Slice
    {
        int index = -1;
        qreal offset = 0.0;
        qreal share = 0.0;
        qreal span = 0.0;
        qreal value = 0.0;
    };

    [[nodiscard]] QVector<Slice> buildSlices() const;

    // The sum of every slice value. Zero when nothing can be drawn.
    [[nodiscard]] qreal totalValue() const;

    [[nodiscard]] bool rightToLeft() const;

    bool donut_ = false;
    qreal donutRatio_ = 0.55;
    bool showPercentages_ = true;
    qreal startAngle_ = 0.0;
    qreal sliceGap_ = 0.0;
    int selectedSlice_ = -1;

    // The sweep is ChartBase's run, not a second animation: the pie paints
    // animation().progress(), so the base owns the single animator, the single
    // reduced-motion decision and the single repaint stream. A pie therefore
    // reveals on the first show exactly like every other chart, and a data
    // update re-runs that one animation instead of running a hidden duplicate.
    // The slice the current press landed on, so a release has to match it.
    int pressedSlice_ = -1;
};

} // namespace md
