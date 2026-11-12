// Periodicity — the shared chart surface (framework.md §5.9, §7).
//
// ChartBase is the common parent of every chart component. It owns a
// MathematicsCoordination, keeps that transform mapped into plotRect(), and
// paints the parts every chart shares: the title, the legend, the grid, the
// axes and their tick labels. A concrete chart implements paintPlot() and
// draws only its data marks.
//
// The paint order inside paintContent() is fixed:
//
//   1. the title (titleMedium, onSurface), top-left of the content rectangle;
//   2. the legend (labelSmall, onSurfaceVariant) when setLegendVisible(true)
//      and at least one series has a non-empty name;
//   3. the coordinate frame: grid (outlineVariant), axes and tick marks
//      (outline), tick labels and axis titles (labelSmall/labelMedium,
//      onSurfaceVariant), drawn by MathematicsCoordination::paintFrame();
//   4. paintPlot(painter, plotRect()) for the data marks.
//
// The frame is painted before paintPlot(), so a series never has to erase a
// grid line. paintPlot() is not clipped by the base class; a chart that can paint
// outside its plot rectangle (labels, overflow markers) should clip itself.
//
// Coordinate contract. plotRect() is the data area in ChartBase-local pixels.
// On every paint the base installs it with MathematicsCoordination::setPlotArea(),
// so math()->toPixel() returns ChartBase-local coordinates directly:
//
//     const QPointF pixel = math()->toPixel(series.points.at(i));
//     painter->drawEllipse(pixel, 3.0, 3.0);
//
// math()->toData() is the inverse and is safe before the first paint: hitTest()
// installs the plot area itself, and chart interaction code may call it from a
// mouse handler.
//
// Data bounds. setSeries() forwards the union of every finite point to
// math()->setDataBounds(). A chart that needs a different domain (bars from
// zero, a padded range, a fixed time window) calls math()->setVisibleBounds()
// or math()->setDataBounds() after setSeries(); the base does not overwrite it
// on later paints. The forwarding call is the default, not a policy.
//
// Empty-series contract. series() may be empty, and individual series may have
// no finite points. In both cases hasData() is false. The base still paints the
// title, legend and coordinate frame (with auto-fit on, math() falls back to a
// 0..1 range), but it calls paintEmptyState() instead of paintPlot(). The
// default empty state is a centred "No data" in labelMedium/onSurfaceVariant; a
// chart may override paintEmptyState() to draw its own message. plotRect()
// remains valid, so the placeholder is centred in the same rectangle the data
// would use.
//
// Colours. chartPalette() returns at least eight categorical colours derived
// from the current theme's colour roles — no literal colour is used anywhere.
// A ChartSeries with an invalid colour takes the palette entry at its own
// index (modulo the palette size) at paint time, so a theme switch recolours
// the series automatically. An explicitly set valid colour is used verbatim.
//
// Legend. drawLegend() lays the entries out in one horizontal row inside the
// rectangle it is given; entries that do not fit are omitted, and series with
// an empty name are skipped. The base reserves a single labelSmall row between
// the title and the plot. A chart that wants a vertical or wrapping legend can
// call drawLegend() itself with its own rectangle (and keep
// setLegendVisible(false) so the base reserves no space).

#pragma once

#include <md/components/math_coordination.h>
#include <md/view/view.h>

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;

namespace md {

// One named data series. Points are in data coordinates, the same space
// MathematicsCoordination maps. Non-finite points are ignored by boundingRect(),
// hitTest(), the automatic data bounds and hasData(); a chart that draws a line
// should break the path at them.
struct ChartSeries
{
    QString name;
    QVector<QPointF> points;
    QColor colour;   // invalid => themed categorical colour by series index
    bool filled = false;

    // The axis-aligned bounds of the finite points, in data coordinates.
    //
    //   * no finite point        -> a default-constructed, null QRectF;
    //   * one finite point       -> a zero-size QRectF at that point (several
    //                               identical points behave the same way);
    //   * otherwise              -> left/top are the minima, right/bottom the
    //                               maxima (top is the MINIMUM y because data
    //                               +Y points up).
    //
    // A zero-size rectangle therefore means "one point", not "no points"; use
    // points() when the difference matters.
    [[nodiscard]] QRectF boundingRect() const;
};

// The shared chart surface. Subclasses implement paintPlot(); everything else
// is available to them through the public API or the protected hooks below.
class ChartBase : public View
{
    Q_OBJECT

public:
    explicit ChartBase(QWidget* parent = nullptr);
    ~ChartBase() override;

    ChartBase(const ChartBase&) = delete;
    ChartBase& operator=(const ChartBase&) = delete;

    // The title drawn at the top-left of the content rectangle. An empty title
    // reserves no space. Default empty.
    void setTitle(const QString& text);
    [[nodiscard]] QString title() const;

    // Whether the base reserves a legend row and paints it. Default true; the
    // row is only reserved when at least one series has a non-empty name.
    void setLegendVisible(bool visible);
    [[nodiscard]] bool isLegendVisible() const noexcept;

    // Whether the base paints the Cartesian frame (grid, axes, tick labels and
    // axis titles) and reserves the axis gutters. Default true. A chart without
    // Cartesian axes (a pie, a radar, a gauge) calls setAxesVisible(false);
    // plotRect() then uses the full inner rectangle and paintContent() skips
    // the frame, while the title, legend and data marks still paint.
    void setAxesVisible(bool visible);
    [[nodiscard]] bool isAxesVisible() const noexcept;

    // Replaces the series and forwards their finite union to
    // math()->setDataBounds(), so auto-fit follows the data. An empty set (or
    // one with no finite point) resets the data bounds to 0..1; auto-fit then
    // shows that range, while a manual viewport is left where it is. The base
    // does not touch the transform on later paints, so a chart may install its
    // own data/visible bounds after this call.
    void setSeries(QVector<ChartSeries> seriesList);
    [[nodiscard]] const QVector<ChartSeries>& series() const noexcept;

    // At least eight categorical colours for the current theme, in a stable
    // order. Indexing wraps, so `palette[i % palette.size()]` is always valid.
    [[nodiscard]] QVector<QColor> chartPalette() const;

    // The effective colour of series `index`: its explicit valid colour, or
    // chartPalette() at that index. An out-of-range index yields an invalid
    // QColor.
    [[nodiscard]] QColor seriesColour(int index) const;

    // The data rectangle in ChartBase-local pixels: the content rectangle after
    // the outer margin, the title and legend rows, and the axis gutters for
    // tick labels, axis titles and the trailing inset. The rectangle is empty
    // when the widget is too small to fit those parts.
    //
    // Computing the rectangle also installs it on math() (a host-provided plot
    // area), so math()->toPixel()/toData() use exactly this rectangle
    // immediately, without waiting for the first paint.
    //
    // The gutters are measured from math()->fittedBounds(), not from the
    // animated visible range, so the plot rectangle does not jitter while a fit
    // animation runs.
    [[nodiscard]] QRectF plotRect() const;

    // Finds the nearest finite data point to `position` (ChartBase-local
    // pixels) within a density-scaled slop (6 dp, at least 4 px). Returns true
    // and, when the out parameters are non-null, writes the series index and
    // the index inside that series' points vector. Returns false when nothing
    // is near, and writes -1 to both out parameters in that case (also when
    // they are stale from a previous call).
    //
    // plotRect() installs the transform before testing, so hitTest() is safe
    // before the first paint. Ties go to the lowest series index and then the
    // lowest point index.
    [[nodiscard]] bool hitTest(QPointF position, int* seriesIndex = nullptr,
                               int* pointIndex = nullptr);

    // Draws the legend entries for the named series inside `rect` (ChartBase-
    // local pixels). The base calls this with the row it reserved; a chart may
    // call it with its own rectangle, for example a vertical legend. Entries
    // are laid out left to right and those that do not fit are omitted. Does
    // nothing when the rectangle is empty or no series has a name.
    //
    // The method does not consult setLegendVisible(): that flag only controls
    // the row the base reserves and paints. A chart that wants a custom legend
    // can hide the base row and call drawLegend() itself.
    void drawLegend(QPainter& painter, const QRectF& rect);

    // The owned transform. Never null, never delete it, and do not reparent it:
    // ChartBase connects its transformChanged() signal to its own repaint and
    // keeps its plot area installed on every paint. It is a hidden child widget
    // so it resolves the same ThemeScope as the chart; it never paints itself
    // in that role.
    [[nodiscard]] MathematicsCoordination* math() const noexcept;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    // Paints the data marks. Called with the plot rectangle after the frame is
    // painted, and only when hasData() is true. The painter is not clipped to
    // the rectangle. A chart must not paint the axes or the legend here; those
    // belong to the base.
    virtual void paintPlot(QPainter& painter, const QRectF& plot) = 0;

    // Paints the state shown when hasData() is false. The default is a centred
    // localized "No data" label. Called instead of paintPlot(), never before or
    // after it.
    virtual void paintEmptyState(QPainter& painter, const QRectF& plot);

    // True when at least one point of at least one series is finite. This is
    // the condition that selects paintPlot() over paintEmptyState().
    [[nodiscard]] bool hasData() const noexcept;

private:
    [[nodiscard]] QRectF innerRect() const;
    [[nodiscard]] bool legendHasEntries() const;
    [[nodiscard]] QRectF legendRect() const;
    void paintTitle(QPainter& painter);

    QVector<ChartSeries> series_;
    QString title_;
    bool legendVisible_ = true;
    bool axesVisible_ = true;
    MathematicsCoordination* math_ = nullptr;
};

} // namespace md
