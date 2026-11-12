// Periodicity — the BoxPlotChart component.
//
// One box per ChartSeries; the values are the finite y coordinates of that
// series. The category order is the series order, and each series keeps its
// ChartSeries::name in series(); ChartBase owns the frame, tick labels and
// legend, so this component does not paint a duplicate category axis.
//
// The five-number summary is computed with the type-7 (linear interpolation)
// quantile:
//
//     h = (n - 1) * p
//     Q(p) = x[floor(h)] + (h - floor(h)) * (x[ceil(h)] - x[floor(h)])
//
// so the values 1..10 give Q1 = 3.25, median = 5.5 and Q3 = 7.75. The whiskers
// reach the most extreme values still inside the 1.5 * IQR fences; everything
// beyond them is an outlier and is drawn as a dot (setShowOutliers(false) hides
// the dots but leaves the values in the scale).
//
// Orientation: vertical boxes put the value on y and the categories on x;
// setHorizontal(true) swaps the axes, with category 0 at the top. The numeric
// transform remains left-to-right under Qt::RightToLeft; ChartBase owns the
// chrome, so no mirroring happens in this subclass.
//
// Hover: boxAt() returns the index of the box under a widget pixel or -1. The
// chart tracks the pointer with mouse tracking on, highlights the hovered box
// and clears the highlight on leave. boxRectAt() exposes the same geometry the
// paint path uses, which makes hit testing and layout testable.
//
// ChartBase paints the title, legend, frame and empty state. The legend is
// left at the base default, so one entry per named series keeps the category
// names visible; setLegendVisible(false) hides it. Every override is wrapped in
// try/catch.

#pragma once

#include <md/components/chart_common.h>
#include <md/view/view.h>

#include <QPointF>
#include <QRectF>
#include <QVector>

class QEvent;
class QMouseEvent;
class QPainter;

namespace md {

class BoxPlotChart : public ChartBase
{
    Q_OBJECT

public:
    // The five-number summary plus the whisker positions and the outliers.
    // `valid` is false for a series with no finite y value.
    struct BoxSummary
    {
        bool valid = false;
        qreal minimum = 0.0;
        qreal q1 = 0.0;
        qreal median = 0.0;
        qreal q3 = 0.0;
        qreal maximum = 0.0;
        qreal lowerWhisker = 0.0;
        qreal upperWhisker = 0.0;
        QVector<qreal> outliers;
    };

    explicit BoxPlotChart(QWidget* parent = nullptr);

    // Hides ChartBase::setSeries() so replacing the data also invalidates the
    // hovered index. The series order defines the category order.
    void setSeries(QVector<ChartSeries> seriesList);

    // Vertical (the default) or horizontal boxes.
    void setHorizontal(bool horizontal);
    [[nodiscard]] bool isHorizontal() const noexcept;

    // Outlier dots are on by default.
    void setShowOutliers(bool showOutliers);
    [[nodiscard]] bool showsOutliers() const noexcept;

    // Box thickness as a fraction of the category slot, clamped into 0.1..0.9.
    // The default is 0.6.
    void setBoxWidthRatio(qreal ratio);
    [[nodiscard]] qreal boxWidthRatio() const noexcept;

    // The computed summaries in series order.
    [[nodiscard]] QVector<BoxSummary> summaries() const;
    [[nodiscard]] BoxSummary summaryAt(int index) const;

    // The painted box rectangle in widget coordinates, or an empty rectangle
    // for an invalid index or before the chart has a usable plot.
    [[nodiscard]] QRectF boxRectAt(int index);

    // Index of the box under `pixel`, or -1. The hit area is the box itself
    // (with a small dp padding for touch/pointer comfort), not the whiskers.
    [[nodiscard]] int boxAt(QPointF pixel);

    // The index highlighted by the last mouse move, or -1.
    [[nodiscard]] int hoveredBox() const noexcept;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintPlot(QPainter& painter, const QRectF& plot) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // Recomputes the category/value bounds and installs them in math(). Returns
    // false when no series has a finite value.
    bool ensureBounds(const QVector<BoxSummary>& all);

    // Recomputes the summaries and installs the current orientation's bounds
    // before a paint, so ChartBase's frame uses the category/value domain too.
    void refreshBounds();

    // The category coordinate used by the value transform: 0 at the left for
    // vertical boxes and 0 at the top for horizontal boxes.
    [[nodiscard]] qreal categoryCoordinate(int index, int count) const noexcept;

    // Builds the box rectangle from the summary, the plot rectangle and the
    // current transform. `plot` must already be valid.
    [[nodiscard]] QRectF boxRectForSummary(const BoxSummary& summary, int index, int count,
                                           const QRectF& plot);

    // Feeds explicit bounds to MathematicsCoordination once per change. The
    // viewport is not animated, so the geometry a caller reads through
    // boxRectAt() is exactly the geometry the last paint used.
    void syncTransform(const QPointF& minimum, const QPointF& maximum);

    void setHoveredBox(int index);

    bool horizontal_ = false;
    bool showOutliers_ = true;
    qreal boxWidthRatio_ = 0.6;

    int hoveredBox_ = -1;

    QPointF boundsMin_{0.0, 0.0};
    QPointF boundsMax_{1.0, 1.0};
    bool boundsFed_ = false;
};

} // namespace md
