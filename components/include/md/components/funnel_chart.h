// Periodicity — the FunnelChart component.
//
// A single-series stage chart. The y values of series().first() are the stages
// in draw order and setStageLabels() names them; the x coordinate is not used.
// The first value is the reference width (100 %), every later stage is a
// fraction of it, and each stage is a trapezoid whose leading edge carries the
// stage's own fraction and whose trailing edge carries the next stage's
// fraction. The last stage tapers to a point, so a decreasing series produces
// the classic funnel silhouette without sorting the caller's data.
//
// A stage's percentage is exactly value[i] / value[0] * 100, so the first stage
// is 100 % whenever the first value is positive and finite. Negative values are
// clamped to a zero-width stage; a zero or non-positive first value makes every
// percentage 0 and leaves the funnel invisible, and a non-finite value is
// treated as zero. The stage labels and percentages are painted inside each
// trapezoid with the theme's content colours.
//
// Geometry. Stages run top-to-bottom by default and left-to-right when
// setHorizontal(true) is set; under Qt::RightToLeft the horizontal layout is
// mirrored so stage 0 starts on the right. setGap() separates consecutive
// stages along the main axis; the cross-axis width uses 90 % of the plot so the
// silhouette never touches the edge.
//
// Hover. mouseMoveEvent() resolves the stage polygon under the pointer and
// repaints it with an onSurface overlay and outline; leaveEvent() clears it.
// stageAt() performs the same hit test without going through an event.
//
// Empty state: no series, no stage, or no finite value paints the shared
// "No data" state.
//
// Chart chrome: ChartBase owns the title, legend and empty state. A funnel has
// no Cartesian axes, so the constructor calls setAxesVisible(false): plotRect()
// is then the full inner rectangle, the base skips the frame, and the
// silhouette uses the whole data area. The base's hasData() predicate (any
// finite point) selects paintPlot(); a series with no finite value paints the
// base's themed empty state.
//
// Exception safety: paintPlot(), mouseMoveEvent() and leaveEvent() each catch
// locally.

#pragma once

#include <md/components/chart_common.h>
#include <md/view/view.h>

#include <QPointF>
#include <QRectF>
#include <QStringList>
#include <QVector>

class QEvent;
class QMouseEvent;
class QPainter;

namespace md {

class FunnelChart : public ChartBase
{
    Q_OBJECT

public:
    explicit FunnelChart(QWidget* parent = nullptr);

    // The stage names, in stage order. Missing entries are simply not drawn;
    // an empty list leaves every stage unlabelled. The list never changes the
    // stage count, which always follows the first series' points.
    void setStageLabels(QStringList labels);
    [[nodiscard]] QStringList stageLabels() const;

    // Whether each stage prints its percentage of the first value. Default
    // true; the text is skipped when a stage is too small to hold it.
    void setShowPercentages(bool show);
    [[nodiscard]] bool showsPercentages() const noexcept;

    // The gap between consecutive stages, in dp. Negative and non-finite
    // values are clamped to zero. Default 4 dp.
    void setGap(qreal dp);
    [[nodiscard]] qreal gap() const noexcept;

    // Top-to-bottom (the default) or left-to-right. The horizontal layout is
    // mirrored under Qt::RightToLeft; the vertical layout is unchanged.
    void setHorizontal(bool horizontal);
    [[nodiscard]] bool isHorizontal() const noexcept;

    // The number of stages, taken from the first series' points. Zero when no
    // series is set.
    [[nodiscard]] int stageCount() const noexcept;

    // The stage's size as a percentage of the first value: 100 for stage 0
    // when the first value is positive and finite, 0 for an invalid index or a
    // non-positive reference. Values above the first are not clamped here; the
    // drawn width is clamped to the reference width.
    [[nodiscard]] qreal stagePercentage(int stage) const;

    // The stage polygon under a ChartBase-local pixel, or -1 outside every
    // stage (including the gaps between them).
    [[nodiscard]] int stageAt(QPointF position) const;

    // The stage under the pointer, or -1.
    [[nodiscard]] int hoveredStage() const noexcept;

    // The bounding rectangle of one stage in ChartBase-local pixels, or an
    // empty rectangle for an invalid stage or before the chart is laid out.
    [[nodiscard]] QRectF stageRect(int stage) const;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // The four corners of one stage, in stage order: leading edge first, then
    // trailing edge. Empty for an invalid stage or an empty plot. The builder
    // is shared by paintPlot(), stageAt() and stageRect().
    [[nodiscard]] QVector<QPointF> stagePolygon(int stage, const QRectF& plot) const;

    void setHoveredStage(int stage);

    QStringList stageLabels_;
    qreal gapDp_ = 4.0;
    bool showPercentages_ = true;
    bool horizontal_ = false;
    int hoveredStage_ = -1;
};

} // namespace md
