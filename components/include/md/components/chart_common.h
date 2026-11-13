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
//
// Animation. Every chart animates its data marks through ChartAnimation, which
// ChartBase drives with md::animation::Animator over the md::tokens::Motion
// ladder. The rules are:
//
//   * Reveal. The first paint of a visible chart starts the reveal at progress
//     0; the base repaints on every frame until progress reaches 1. A paint of
//     a widget that is not visible (an offscreen QWidget::render() or grab
//     before the chart is shown) paints the settled frame and leaves the reveal
//     pending, so a snapshot is never a half-faded chart.
//   * Update. Changing the data (setSeries(), and therefore setSamples() and
//     setRows() in the charts that funnel through it) after the reveal started
//     restarts at progress 0 in Update mode. The old data is not kept: the
//     "morph" is a re-reveal at the shorter Update duration, not an
//     interpolation between two data sets.
//   * Reduced motion. When Theme::forWidget(this).motion().reducedMotion is
//     true (or ChartAnimation::setReducedMotion(true) was called, or
//     setAnimated(false)), the chart settles on progress 1 without starting an
//     animator. A theme change that turns reduced motion on while a run is in
//     flight stops it and settles the chart.
//
// paintContent() calls paintPlot(painter, plot, progress). The default
// implementation of that overload multiplies the painter opacity by the eased
// progress, confines the marks to the plot rectangle while the transition is in
// flight, and calls paintPlot(painter, plot) — so a chart that only implements
// the settled overload gets the reveal for free. A chart that wants per-element
// timing overrides the progress overload and stages its own elements from the
// progress value.

#pragma once

#include <md/animation/animator.h>
#include <md/components/chart_animation.h>
#include <md/components/math_coordination.h>
#include <md/view/view.h>

#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;
class QResizeEvent;

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
// The ChartAnimation::Driver base is private: it is how ChartAnimation asks the
// chart to start or stop the run, not part of the chart's public surface.
// Antialiasing strategy for the data marks. Qt's raster engine exposes only an
// on/off antialiasing switch, so every other strategy is implemented by the
// base. They fall into four families:
//
//   * aliased / coverage: None draws hard edges, Grayscale is Qt's raster
//     coverage antialiasing.
//   * coverage sampling: Supersample2x/4x render the plot into an offscreen
//     image at Nx and blit it down with a smooth transform.
//   * sample accumulation: Jittered4x/8x render the marks once per sub-pixel
//     offset in a fixed sample pattern and average the premultiplied frames.
//   * post-process: Fxaa, Fmaa, Mlaa, Smaa and Cmaa render the marks aliased and
//     filter the finished plot image, each with its own edge model.
//
// The sampling modes trade fill rate for edge quality; the post-process modes
// trade a little edge accuracy for a single aliased render. The default is the
// cheap grey raster, and the caller opts into the more expensive modes.
enum class ChartAntialiasing {
    None,            // Aliased edges; cheapest, useful for dense heat maps.
    Grayscale,       // Qt's raster antialiasing; the default.
    Supersample2x,   // 2x2 supersampling, downsampled on blit.
    Supersample4x,   // 4x4 supersampling, the highest quality offered.
    Fxaa,            // Post-process edge filter (FXAA family) on the plot image.
    Fmaa,            // Fast morphological AA (MLAA family), run-length based.
    Mlaa,            // Morphological AA, span-classified trapezoid blending.
    Smaa,            // Area-based core of the SMAA family; no lookup texture.
    Cmaa,            // Conservative morphological AA; monotone edges only.
    Jittered4x,      // Four rotated-grid samples, averaged in a buffer.
    Jittered8x,      // Eight eight-queens samples, averaged in a buffer.
};

class ChartBase : public View, private ChartAnimation::Driver
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

    // Replaces the chart's animation configuration. The chart keeps a copy, so
    // the argument may be a temporary and later changes to it do not reach the
    // chart. Adopting a value stops a run in flight (the copied progress is
    // painted) and applies to the next reveal or update; it never replays the
    // reveal of a chart that is already on screen.
    void setAnimation(ChartAnimation animation);

    // The chart's animation state. A chart may change the duration, delay and
    // easing before it is shown (or at any time, for the next run) and may call
    // play(PlayMode::Reveal) to replay the reveal. The reference stays valid
    // for the lifetime of the chart; setAnimation() replaces its contents.
    [[nodiscard]] ChartAnimation& animation() noexcept;
    [[nodiscard]] const ChartAnimation& animation() const noexcept;

    // Whether the chart animates at all. Default true. With animated(false) the
    // chart paints every frame at progress 1 and never starts an animator; the
    // reveal of a chart that has not been painted yet is suppressed, and a run
    // in flight is stopped and settled. Turning it back on does not replay a
    // reveal that already happened.
    void setAnimated(bool animated);

    // How the data marks are antialiased. Changing it invalidates the plot (not
    // the cached furniture, which never uses the plot strategy) and repaints.
    void setAntialiasing(ChartAntialiasing mode);
    [[nodiscard]] ChartAntialiasing antialiasing() const noexcept;
    [[nodiscard]] bool isAnimated() const noexcept;

    // -- Paint caches -----------------------------------------------------
    //
    // The static furniture — title, legend, grid, axes, tick marks, tick labels
    // and anything a chart adds in paintStaticLayer() — is rendered once into a
    // device-pixel-ratio-aware QPixmap and re-blitted on every repaint. Only the
    // data marks are painted per frame.
    //
    // The layer is rebuilt when, and only when, one of these changes:
    //
    //   * the widget size or the device pixel ratio;
    //   * the theme revision (a ThemeChange event, which is what a ThemeScope
    //     install and setApplicationTheme() both deliver);
    //   * the layout-derived content/plot rectangles (title, legend visibility,
    //     axes visibility, font metrics, tick count are part of the key);
    //   * the visible axis range (a fit animation therefore rebuilds it while
    //     the labels interpolate, and never after it settles);
    //   * the legend signature (series names or explicit series colours);
    //   * an explicit invalidateStaticLayer() call.
    //
    // A plain repaint, a hover, or an animation frame does not rebuild it:
    // staticLayerBuilds() counts the actual renders and stays constant across
    // them.
    void invalidateStaticLayer();
    [[nodiscard]] bool isStaticLayerCached() const noexcept;
    [[nodiscard]] quint64 staticLayerBuilds() const noexcept;

    // Marks the chart-side precomputed data stale (QPainterPaths, formatted
    // label strings, per-element geometry). The next paint calls
    // updateChartCache() once and then reuses the result until the next
    // invalidation. The base invalidates it on a new series set, a theme
    // change, a resize and a transform change.
    void invalidateChartCache();

    // At least eight categorical colours for the current theme, in a stable
    // order. Indexing wraps, so `palette[i % palette.size()]` is always valid.
    //
    // The reference is owned by the chart and rebuilt on a theme change, so the
    // paint path can read it without copying: a chart must not keep it across a
    // ThemeChange.
    [[nodiscard]] const QVector<QColor>& chartPalette() const;

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
    void resizeEvent(QResizeEvent* event) override;
    void onThemeChanged() override;
    // A chart hidden mid-reveal has no frames to show: the run is settled on the
    // finished frame instead of keeping the animator's timer alive off-screen.
    void onHidden() override;

    // Starts the reveal if it has not run yet. Called from the paint path, and
    // from a subclass that wants the run to begin with the show instead of with
    // the first paint (PieChart does). A hidden chart leaves the reveal pending.
    void startRevealIfNeeded();
    void onStoreChanged(Store* store, const QString& key, const QVariant& value) override;

    // Paints the settled data marks. Called with the plot rectangle after the
    // frame is painted, and only when hasData() is true. The painter is not
    // clipped to the rectangle. A chart must not paint the axes or the legend
    // here; those belong to the base.
    //
    // A chart implements this overload when it does not need per-element
    // timing: the progress overload below fades it in and calls it. The base
    // implementation paints nothing, so a chart must implement this overload or
    // the progress overload.
private:
    ChartAntialiasing antialiasing_ = ChartAntialiasing::Grayscale;

public:
    virtual void paintPlot(QPainter& painter, const QRectF& plot);

    // Paints the data marks at `progress` (0..1, already eased). paintContent()
    // calls this overload; it is never called when hasData() is false.
    //
    // The default implementation is the whole reveal for a chart that has no
    // per-element timing: it returns without painting at progress 0, multiplies
    // the painter opacity by the progress while the transition is in flight,
    // confines the marks to the plot rectangle (the painter clip is intersected
    // with `plot`, so a mark cannot spill outside the data area mid-reveal), and
    // calls paintPlot(painter, plot). At progress 1 it calls the settled
    // overload with the painter untouched, so the finished frame keeps the
    // unclipped contract documented above.
    //
    // A chart that wants per-element timing (bars that grow one after another,
    // a line that draws itself, labels that count up) overrides this overload
    // and derives each element's own progress from `progress`; it owns the
    // painter state and any clip it needs, and may call the settled overload or
    // its own drawing code.
    virtual void paintPlot(QPainter& painter, const QRectF& plot, qreal progress);

    // Called on every animation frame with the eased 0..1 progress, before the
    // chart repaints. A chart can advance its own per-element clocks here
    // instead of recomputing them in paintPlot(); the default does nothing.
    //
    // The hook may throw. ChartBase catches the exception, reports it through
    // lastError(), stops the run and settles on the final frame, so a broken
    // hook cannot leave the chart half-revealed or unwind into the animator.
    virtual void onAnimationProgress(qreal progress);

    // Renders chart-specific static furniture into the cached layer: a colour
    // bar and its labels, a fixed unit annotation, a watermark. Called only
    // when the static layer is rebuilt, never per frame. The default does
    // nothing.
    virtual void paintStaticLayer(QPainter& painter, const QRectF& content);

    // Rebuilds the chart-side precomputed data after invalidateChartCache():
    // QPainterPaths in pixel space, formatted label strings, per-element
    // geometry. Called lazily from the paint path, never per frame unless the
    // chart invalidates again. The default does nothing.
    //
    // A chart that maps data to pixels here must call plotRect() first; the
    // base has already installed the current transform when the hook runs.
    virtual void updateChartCache();

    // Paints the state shown when hasData() is false. The default is a centred
    // localized "No data" label. Called instead of paintPlot(), never before or
    // after it.
    virtual void paintEmptyState(QPainter& painter, const QRectF& plot);

    // True when at least one point of at least one series is finite. This is
    // the condition that selects paintPlot() over paintEmptyState().
    [[nodiscard]] bool hasData() const noexcept;

private:
    // ChartAnimation::Driver. Starts the run described by `animation` with the
    // chart's Animator, or settles it when the chart must not animate.
    void startChartAnimation(ChartAnimation& animation, ChartAnimation::PlayMode mode) override;
    void stopChartAnimation(ChartAnimation& animation) override;

    // The layout cache: inner rectangle, legend row and plot rectangle. It is
    // recomputed only when the size, theme revision, fitted range, tick count,
    // axis-label presence or one of the title/legend/axes flags changes.
    [[nodiscard]] QRectF innerRect() const;
    [[nodiscard]] bool legendHasEntries() const;
    [[nodiscard]] QRectF legendRect() const;
    void rebuildLayout() const;
    void paintTitle(QPainter& painter);

    // The static-layer cache. ensureStaticLayer() compares the key and rebuilds
    // only on a mismatch; rebuildStaticLayer() renders the furniture once.
    void ensureStaticLayer(const QRectF& content, const QRectF& plot);
    void rebuildStaticLayer(const QRectF& content, const QRectF& plot);

    // The chart-side cache: calls updateChartCache() at most once per
    // invalidation.
    void ensureChartCache();

    // Cached theme resources. refreshFonts() and rebuildPalette() run on a theme
    // revision change, never per frame.
    void refreshFonts() const;
    [[nodiscard]] const QFont& titleFont() const;
    [[nodiscard]] const QFont& legendFont() const;
    [[nodiscard]] const QFont& emptyStateFont() const;
    void rebuildPalette() const;
    void rebuildSeriesColours() const;
    void refreshLegendSignature();

    // One progress value from the Animator: updates the state, runs the chart
    // hook and schedules the repaint. A throwing hook is contained here. The
    // repaint itself is throttled to roughly one frame per 16 ms; the final
    // frame is always requested.
    void onAnimatorValue(qreal value);

    // Stops the run, settles the value on the finished frame and records the
    // diagnostic. The diagnostic is delivered to View's error guard on the next
    // paint (see paintContent()), which is how lastError() learns about it.
    void reportAnimationFailure(const QString& message);

    // Throws the recorded failure after the final frame was painted, so the
    // guarded paint entry point records it. Never called when nothing failed.
    void reportPendingAnimationFailure();

    QVector<ChartSeries> series_;
    QString title_;
    bool legendVisible_ = true;
    bool axesVisible_ = true;
    MathematicsCoordination* math_ = nullptr;

    ChartAnimation animation_;
    animation::Animator animator_;
    bool animated_ = true;

    // True once a reveal actually started. Data changes before that point are
    // covered by the reveal itself; after it they are Update runs.
    bool revealStarted_ = false;

    // The message of an animator callback that threw, waiting for the next
    // paint to hand it to the guarded entry point.
    QString pendingAnimationError_;

    // A theme revision for this chart, bumped for every ThemeChange event. It
    // keys the fonts, the palette, the layout and the static layer without
    // comparing whole Theme values per frame.
    quint64 themeRevision_ = 0;

    // The layout cache.
    mutable QRectF layoutInner_;
    mutable QRectF layoutLegend_;
    mutable QRectF layoutPlot_;
    mutable bool layoutValid_ = false;
    mutable QSize layoutSize_;
    mutable quint64 layoutThemeRevision_ = 0;
    mutable QRectF layoutFittedBounds_;
    mutable int layoutTickCount_ = 0;
    mutable QString layoutTitle_;
    mutable bool layoutLegendVisible_ = false;
    mutable bool layoutAxesVisible_ = true;
    mutable bool layoutHasXLabel_ = false;
    mutable bool layoutHasYLabel_ = false;

    // Cached theme resources. `fontsRevision_` starts at the maximum value so
    // the first request always resolves them.
    mutable QFont titleFont_;
    mutable QFont legendFont_;
    mutable QFont emptyStateFont_;
    mutable quint64 fontsRevision_ = ~quint64(0);
    mutable QVector<QColor> palette_;
    mutable bool paletteValid_ = false;
    mutable QVector<QColor> seriesColours_;
    mutable bool seriesColoursValid_ = false;

    // The legend's names and explicit colours. Refreshed on a data change and
    // compared as part of the static-layer key, so no string is built per
    // frame.
    QString legendSignature_;

    // The localized "No data" label, resolved once per language change instead
    // of per paint.
    mutable QString emptyStateText_;
    mutable bool emptyStateTextValid_ = false;

    // The static layer and its key.
    QPixmap staticLayer_;
    bool staticLayerValid_ = false;
    quint64 staticLayerBuilds_ = 0;
    QSize staticLayerSize_;
    qreal staticLayerDevicePixelRatio_ = 0.0;
    quint64 staticLayerThemeRevision_ = 0;
    QRectF staticLayerContent_;
    QRectF staticLayerPlot_;
    QRectF staticLayerVisibleBounds_;
    QString staticLayerTitle_;
    QString staticLayerLegendSignature_;
    bool staticLayerLegendVisible_ = false;
    bool staticLayerAxesVisible_ = true;

    // The chart-side cache flag and the repaint throttle.
    bool chartCacheValid_ = false;
    QElapsedTimer animationRepaintClock_;
};

} // namespace md
