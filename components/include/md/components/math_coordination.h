// Periodicity — MathematicsCoordination, the shared data-to-pixel transform for
// every chart (framework.md §5.9, §7).
//
// One object owns three separate rectangles, and the distinction matters:
//
//   * dataBounds()    — the data the caller asked to display. Never expanded by
//                       rendering, but two degenerate axes are widened to a unit
//                       span so a transform always exists.
//   * fittedBounds()  — the range auto-fit chose for dataBounds(): the exact
//                       data range when auto-fit is off, and a 1/2/5 × 10^k
//                       "nice" range with 4–8 ticks when it is on.
//   * visibleBounds() — the range currently mapped to pixels. It equals
//                       fittedBounds() except while a fit animation is running
//                       and after zoomAt()/pan()/setVisibleBounds().
//
// toPixel()/toData() use visibleBounds() and plotArea(), the pixel rectangle the
// transform maps into. plotArea() is either installed by a host (ChartBase sets
// it from its own plotRect() on every paint) or computed from contentRect()
// inset by viewportPadding() plus room for the tick and axis labels.
//
// Y is flipped: data +Y points up, pixel +Y points down. toPixel() maps the
// minimum data y to plotArea().bottom() and the maximum to plotArea().top().
//
// Auto-fit. With setAutoFit(true) — the default — setDataBounds() chooses a
// nice range (1/2/5 × 10^k, 4–8 ticks) and animates visibleBounds() to it with
// the theme's medium2 motion spec. A reduced-motion theme snaps instead, as
// does any theme whose resolved duration is zero. The animation is retargeted
// from the current visible range, so a stream of bounds updates never jumps.
//
// Manual navigation. zoomAt() and pan() operate on the current visible range,
// stop a fit animation and switch auto-fit off; the view then stays where the
// user put it. setVisibleBounds() installs an explicit manual range. While
// auto-fit is off, setDataBounds() updates only dataBounds() (and therefore
// hit-testing and future fits); it does not move the viewport. Call
// setAutoFit(true) to hand control back and refit, or setVisibleBounds() to
// choose a range yourself. setAutoFit(false) stops a fit in flight at its
// target range.
//
// Painting. paintContent() draws the grid (outlineVariant), the left and bottom
// axes (outline), tick marks, tick labels (labelSmall, onSurfaceVariant) and
// optional axis titles (labelMedium, onSurfaceVariant). Every colour is a theme
// role; every length is density().dp(); no colour is written literally.
//
// Standalone use. The widget is fully self-contained: give it data bounds, a
// size and a theme scope, and it paints a labelled coordinate frame. A host
// that already owns a plot rectangle (ChartBase) calls setPlotArea() and then
// paintFrame() from its own paintContent(), so the same tick and label code is
// shared without nesting the widget into the paint path.
//
// Every virtual override runs through a local try/catch and reports a caught
// exception with qWarning; a broken subclass or a bad theme cannot unwind into
// Qt's paint loop.

#pragma once

#include <md/animation/animator.h>
#include <md/view/view.h>

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QtGlobal>

class QPainter;
class QResizeEvent;

namespace md {

class MathematicsCoordination : public View
{
    Q_OBJECT

public:
    explicit MathematicsCoordination(QWidget* parent = nullptr);
    ~MathematicsCoordination() override;

    MathematicsCoordination(const MathematicsCoordination&) = delete;
    MathematicsCoordination& operator=(const MathematicsCoordination&) = delete;

    // The data the chart wants to show. The two points are read as opposite
    // corners, so their order does not matter. All four coordinates must be
    // finite: a call containing NaN or an infinity is rejected as a whole and
    // leaves the previous bounds untouched (a qWarning names the rejection).
    //
    // An axis whose two values are equal is widened to a unit span centred on
    // the value (x: ±0.5, y: ±0.5), because a transform with a zero-width data
    // range does not exist. For magnitudes where ±0.5 is not representable the
    // neighbouring floating-point values are used instead.
    //
    // With auto-fit on, this also chooses the nice fitted range and animates
    // (or snaps) visibleBounds() to it. With auto-fit off it updates only
    // dataBounds(); the manual viewport is left alone.
    void setDataBounds(QPointF minimum, QPointF maximum);

    // `QRectF(xMin, yMin, xSpan, ySpan)` in data coordinates: left() is the
    // minimum x, top() is the MINIMUM y (the visual bottom, because +Y points
    // up), right() is the maximum x and bottom() is the maximum y.
    [[nodiscard]] QRectF dataBounds() const noexcept;

    // Installs an explicit manual viewport: auto-fit switches off, any fit
    // animation stops, and toPixel() maps exactly this range. Validation and
    // unit-span collapsing follow setDataBounds(). dataBounds() is unchanged.
    void setVisibleBounds(QPointF minimum, QPointF maximum);

    // The range currently mapped to pixels.
    [[nodiscard]] QRectF visibleBounds() const noexcept;

    // The range auto-fit chose (or the manual viewport when auto-fit is off).
    // This is the animation's destination while a fit runs.
    [[nodiscard]] QRectF fittedBounds() const noexcept;

    // Space between the widget's content rectangle and the automatically
    // computed plot area, in dp. It does not apply to an explicit plot area.
    // Negative or non-finite values are rejected. Default 12 dp.
    void setViewportPadding(qreal dp);
    [[nodiscard]] qreal viewportPadding() const noexcept;

    // Data → pixel, in the coordinate space of plotArea(). Y is flipped
    // (minimum data y → bottom, maximum → top). A degenerate plot area (zero
    // width or height) collapses the affected axis to the area's centre, and a
    // non-finite input maps to the centre, so painting never sees a NaN.
    [[nodiscard]] QPointF toPixel(QPointF data) const;

    // Pixel → data, the inverse of toPixel() wherever the plot area is
    // non-degenerate. A non-finite input maps to the centre of visibleBounds().
    [[nodiscard]] QPointF toData(QPointF pixel) const;

    void setXAxisLabel(const QString& label);
    [[nodiscard]] QString xAxisLabel() const;

    void setYAxisLabel(const QString& label);
    [[nodiscard]] QString yAxisLabel() const;

    // Number of ticks requested for labels and grid lines, clamped to 2..12.
    // Auto-fit clamps its own target to 4..8 because the nice-range search is
    // specified for that window. Default 6.
    void setTickCount(int count);
    [[nodiscard]] int tickCount() const noexcept;

    void setGridVisible(bool visible);
    [[nodiscard]] bool isGridVisible() const noexcept;

    // See the class comment. Default true.
    void setAutoFit(bool enabled);
    [[nodiscard]] bool isAutoFit() const noexcept;

    // Zooms around a pixel anchor: the data point under `pixelAnchor` stays
    // under it. A factor above 1 zooms in (the span shrinks), a factor below 1
    // zooms out; the factor is clamped into [1e-3, 1e3]. A non-finite or
    // non-positive factor is rejected and leaves the transform untouched.
    //
    // The call stops a fit animation and switches auto-fit off; the current
    // visible range is the base of the zoom. A factor of exactly 1 is a no-op
    // and does not change the auto-fit mode.
    void zoomAt(QPointF pixelAnchor, qreal factor);

    // Pans the plotted content by `deltaPixels` in pixel space: a positive dx
    // moves the content right, a positive dy moves it down. The data span does
    // not change. The call stops a fit animation and switches auto-fit off. A
    // zero delta is a no-op (it does not change the auto-fit mode), and a
    // non-finite delta is rejected. Panning is not clamped to the data bounds;
    // a chart that needs rubber-banding must clamp its own input.
    void pan(QPointF deltaPixels);

    // The nice tick values inside [minimum, maximum]. The step is the smallest
    // of 1, 2 or 5 × 10^k that is at least span/(count-1); the returned values
    // are the multiples of that step that fall inside the interval, so the
    // count is usually within one of `count` but is not forced to it. A
    // reversed range is normalised, `count <= 0` and non-finite input yield an
    // empty vector, and a single-value range yields that one value. A range
    // with no multiple inside it falls back to the two endpoints.
    //
    // This static helper does not read the widget, so it is usable from a
    // goldens tool or a test without a QApplication.
    [[nodiscard]] static QVector<qreal> niceTicks(qreal minimum, qreal maximum, int count);

    // Formats one tick for a label. `step` is the tick step that produced the
    // value; it selects the number of decimals (0 for steps >= 1, up to six
    // below that). Very large or very small magnitudes use scientific notation.
    // Negative zero is normalised to "0". This is public because a chart that
    // draws its own annotations should use exactly the axis format.
    [[nodiscard]] static QString tickLabel(qreal value, qreal step);

    // Tick values for the current visible range and tickCount(), computed with
    // niceTicks(). The frame paints exactly these values.
    [[nodiscard]] QVector<qreal> xTicks() const;
    [[nodiscard]] QVector<qreal> yTicks() const;

    // The pixel rectangle toPixel()/toData()/paintFrame() use. When a host has
    // not installed one it is computed from contentRect(), viewportPadding()
    // and the tick/axis-label metrics. An installed area is used verbatim.
    [[nodiscard]] QRectF plotArea() const;

    // Composition hook for a host such as ChartBase that owns the plot
    // rectangle: maps the transform into `area` (in the coordinate space the
    // host paints in) and suppresses the automatic plot-area computation. An
    // empty or invalid area clears the plot area (paintFrame() then paints
    // nothing) until clearPlotArea() or another setPlotArea() call.
    void setPlotArea(const QRectF& area);

    // Returns to the automatic contentRect()-derived plot area.
    void clearPlotArea();

    // Paints the grid, axes, tick marks, tick labels and axis titles into
    // plotArea(). A host calls this from its own paintContent(); the widget's
    // own paintContent() calls it when the widget is used standalone. The
    // painter is saved and restored around the drawing, and the method never
    // throws.
    void paintFrame(QPainter& painter);

    // True while the auto-fit animation is in flight. A reduced-motion theme
    // and a zero-duration spec never leave it true.
    [[nodiscard]] bool isAnimating() const noexcept;

signals:
    // Emitted whenever dataBounds(), visibleBounds() or fittedBounds() changes,
    // including once per animation frame. A host that paints the transform
    // itself (ChartBase) connects this to its own repaint request.
    void transformChanged();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void resizeEvent(QResizeEvent* event) override;
    void onThemeChanged() override;
    [[nodiscard]] bool focusable() const override;

private:
    struct NiceRange
    {
        qreal minimum = 0.0;
        qreal maximum = 1.0;
        qreal step = 1.0;
        int ticks = 2;
        bool valid = false;
    };

    // The nice range auto-fit uses for one axis: the smallest 1/2/5 × 10^k
    // step whose expanded range yields 4–8 ticks and comes closest to `count`.
    [[nodiscard]] static NiceRange chooseNiceRange(qreal minimum, qreal maximum, int count);

    // The smallest 1/2/5 × 10^k value that is >= rawStep.
    [[nodiscard]] static qreal niceStep(qreal rawStep);

    // The step between two consecutive nice ticks, or `fallback` when the list
    // has fewer than two entries.
    [[nodiscard]] static qreal tickSpan(const QVector<qreal>& ticks, qreal fallback);

    // Chooses the nice target for dataBounds() and either animates to it or
    // snaps, depending on the theme's motion scheme.
    void applyFit(bool animate);

    // Recomputes the automatic plot area. A no-op when a host installed one.
    void updatePlotArea() const;

    // The currently displayed range and the fit destination.
    QRectF dataBounds_{0.0, 0.0, 1.0, 1.0};
    QRectF visibleBounds_{0.0, 0.0, 1.0, 1.0};
    QRectF fittedBounds_{0.0, 0.0, 1.0, 1.0};

    // The visible range at the start of the current fit animation.
    QRectF animationFrom_{0.0, 0.0, 1.0, 1.0};

    qreal viewportPadding_ = 12.0;
    int tickCount_ = 6;
    bool gridVisible_ = true;
    bool autoFit_ = true;

    QString xAxisLabel_;
    QString yAxisLabel_;

    // The plot area is a cache in standalone mode and a host-provided value in
    // composition mode. The content rectangle is compared on every access so a
    // missed resize event cannot leave the transform stale.
    mutable QRectF plotArea_;
    mutable QRectF cachedContentRect_;
    mutable bool plotAreaDirty_ = true;
    bool plotAreaExplicit_ = false;

    animation::Animator animator_;
};

} // namespace md
