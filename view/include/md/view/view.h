// Periodicity — the component kernel (framework.md §5.7, §6, §7).
//
// View is the base class of every Periodicity widget and the single place
// where the three contracts — measurement, arrangement and painting — are
// orchestrated. A component implements measureOverride(), arrangeOverride()
// and paintContent(); it cannot replace the paint pipeline, the state
// transitions or the Qt-to-Event translation, because those are deliberately
// final or non-virtual.
//
// The paint order is fixed (framework.md §7):
//
//   1. the elevation shadow, outside the bounds and unclipped;
//   2. the background, clipped to the shape;
//   3. paintContent(), the component's own drawing;
//   4. paintChildren(), the ViewGroup hook (a plain View has none);
//   5. the foreground stack — the state overlay;
//   6. the ripple;
//   7. the focus ring, when the view is focusable, actually focused, and the
//      focus was established by the keyboard in the design baseline.
//
// Input arrives twice. Qt's event handlers convert a Qt event with
// md/event/qt_bridge.h and dispatch it through the EventDispatcher; onEvent()
// applies the state transitions exactly once, in the Target phase, so an
// ancestor that later sees a bubbled event does not mistake it for its own
// press.

#pragma once

#include <md/core/measure_spec.h>
#include <md/core/view_state.h>
#include <md/event/event.h>
#include <md/event/event_target.h>
#include <md/layer/background.h>
#include <md/layer/elevation.h>
#include <md/layer/foreground.h>
#include <md/layer/layer_stack.h>
#include <md/ripple/ripple.h>
#include <md/tokens/elevation.h>
#include <md/tokens/shape.h>
#include <md/view/damage_tracker.h>
#include <md/view/error_guard.h>
#include <md/view/layer_debug.h>
#include <md/view/lifecycle.h>
#include <md/view/render_backend.h>

#include <QColor>
#include <QList>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QVariant>
#include <QWidget>
#include <Qt>

#include <chrono>
#include <memory>

class QPainter;
class QShowEvent;
class QHideEvent;

namespace md {

class Store;
class Theme;

// The private state of the layer-backed paint path. Defined in
// view/src/layer_backing.cpp; a view that never opts in never allocates one.
struct LayerBacking;

namespace layer {
struct SurfaceContext;
}

// The kernel every component inherits from.
//
// A View owns its shape, elevation, colours, state and layer stack. It answers
// Qt's geometry questions by running the measure contract, and it paints the
// fixed pipeline above; a subclass supplies the content and, when it has
// children, the arrangement.
//
// The entry points that run component code — paintEvent(), measure(), arrange()
// and event() — go through the error guard (md/view/error_guard.h): a hook that
// throws is caught, reported once per entry point and skipped instead of
// unwinding into Qt. lastError() exposes the diagnostic.
class View : public QWidget, public EventTarget
{
    Q_OBJECT

public:
    explicit View(QWidget* parent = nullptr);
    ~View() override;

    // Runs measureOverride() and caches the result. The base class does not
    // clamp the result again: measureOverride() is expected to apply the specs
    // with MeasureSpec::resolve(), which is what makes Exactly authoritative
    // and AtMost a cap.
    void measure(MeasureSpec width, MeasureSpec height);

    // Applies the Qt geometry and then arranges the content in the resulting
    // content rectangle. This is the entry point of the layout bridge
    // (framework.md §6.3); components never call setGeometry() on a child.
    void arrange(const QRect& geometry);

    // How large the last explicit measure() pass resolved. A Qt geometry query
    // (sizeHint, minimumSizeHint, heightForWidth) never displaces it; before
    // the first explicit measurement the most recent query result is visible
    // here, because there is no measurement to protect yet.
    [[nodiscard]] QSizeF measuredSize() const noexcept;

    // The rectangle the component's content and every layer paint into. A
    // subclass that reserves a shadow inset or padding overrides this; the
    // paint pipeline, the ripple bounds and arrangeOverride() all follow the
    // result. Not noexcept so an override may compute it.
    [[nodiscard]] virtual QRectF contentRect() const;

    [[nodiscard]] ViewStates state() const noexcept;

    // Where this widget is in its lifecycle (docs/backlog.md, "Component
    // lifecycle"). Components read it to assert their own invariants; the
    // transitions themselves belong to the base class.
    [[nodiscard]] LifecycleState lifecycleState() const noexcept;

    // The diagnostic of the last failure caught at a guarded entry point
    // (paintEvent, measure, arrange or event), empty until one happens. The
    // failure itself is logged once per entry point, but this string is
    // refreshed on every failure, so it always describes the most recent one.
    [[nodiscard]] QString lastError() const;

    // Sets or clears one interaction flag and schedules the repaint. The
    // Enabled flag is special: it mirrors QWidget::isEnabled(), so it changes
    // the widget's enabled state and every other interaction flag is cleared
    // when the view is disabled.
    void setState(ViewState flag, bool on);

    // The state overlay and the ripple are the right feedback for a button; a
    // scrollbar wants only its thumb colour change, so it turns the shared
    // interaction layers off instead of letting a full-width ripple wash over
    // the track.
    void setInteractionFeedbackEnabled(bool enabled);
    [[nodiscard]] bool isInteractionFeedbackEnabled() const noexcept;

    // Damage tracking (opt-in) ---------------------------------------------
    //
    // Off by default, so every existing component keeps painting exactly as it
    // did and retains nothing. A view that opts in accumulates the regions that
    // changed; requestUpdate(QRectF) records one rectangle in this view's local
    // coordinate space and schedules the same coalesced repaint as the
    // protected requestUpdate(). The no-argument path keeps its meaning: it is
    // a full repaint, so it marks the whole view through DamageTracker::addAll.
    //
    // paintContent() reads the accumulated regions through currentDamage().
    // The tracker is cleared only after a paint pass that runs to completion;
    // a pass whose hook throws — or that Qt clips to part of the widget — keeps
    // the record for the next attempt.
    void requestUpdate(QRectF rect);
    void setDamageTrackingEnabled(bool enabled);
    [[nodiscard]] bool isDamageTrackingEnabled() const noexcept;

    // The damage accumulated since the last completed paint. Empty while
    // tracking is disabled.
    [[nodiscard]] const DamageTracker& damage() const noexcept;

    // Discards the accumulated damage without painting. Called by
    // setDamageTrackingEnabled(false); a component may also use it when its
    // content is about to change wholesale and it knows a full repaint follows.
    void clearDamage();

    // Retained layers (opt-in) ---------------------------------------------
    //
    // Off by default: a view keeps painting straight into its widget exactly
    // as before. When a view opts in, its paint pipeline renders into a
    // retained layer owned by the process-wide LayerCache, and paintEvent()
    // blits that layer instead of re-running the pipeline. A later repaint
    // rasterises only the damaged region; the rest of the layer is reused.
    //
    // Enabling this also enables damage tracking, because the damage record is
    // what says which part of the layer is stale. Turning layer backing off
    // releases the layer and leaves damage tracking on; a caller that wants
    // neither calls setDamageTrackingEnabled(false), which drops the layer
    // first.
    //
    // A layer-backed view must not:
    //   * contain a native window (a QOpenGLWidget, a QQuickWidget, a child
    //     with Qt::WA_NativeWindow): the platform composites that surface
    //     itself, so it can never be captured into the layer and would appear
    //     above it, untouched by the layer's clip and opacity;
    //   * rely on its child *widgets* being part of the layer. Child widgets
    //     are painted by Qt after the parent's paintEvent, not by it; only
    //     paintContent() and paintChildren(QPainter&) reach the layer. A
    //     generic container with real child widgets must stay uncached, or its
    //     children paint the live tree while the parent blits a stale surface;
    //   * paint outside its own rectangle and expect that overhang to survive:
    //     the layer is exactly rect() at the current device pixel ratio;
    //   * forget to record damage for a visual change. A missed rectangle is a
    //     ghost, not a crash; md::view::setLayerDebugUncached(true) is the
    //     comparison that catches it.
    void setLayerBacked(bool enabled);
    [[nodiscard]] bool isLayerBacked() const noexcept;

    // Diagnostics for tests, benchmarks and the debug comparison. They report
    // the live surface, so `hasLayerSurface()` is false before the first
    // layered paint and while the uncached debug mode is on.
    [[nodiscard]] bool hasLayerSurface() const noexcept;

    // The allocated layer size in device pixels; empty when no layer is held.
    [[nodiscard]] QSize layerPixelSize() const noexcept;

    // The theme revision the current layer was acquired with. It advances when
    // the resolved Theme changes, which is what invalidates a layer whose
    // owner never saw a ThemeChange event.
    [[nodiscard]] quint64 layerThemeRevision() const noexcept;

    // GPU acceleration (opt-in) -------------------------------------------
    //
    // The default is off: every view paints with QPainter on the software
    // path, exactly as before this API existed. The switch is the same for
    // every component because it lives on the kernel, not on a component.
    //
    // The flag is a declaration of intent that a GPU container honours. The
    // container is md::GpuCompositor: it renders each enabled direct child
    // once into a texture and composites the textures on the GPU. A view that
    // is enabled but is not inside a compositor keeps painting on the software
    // path and records that fact in lastError().
    //
    // setGpuAccelerated() applies to this view and, recursively, to its
    // current View descendants, so a caller may flip one root and have the
    // subtree follow (this mirrors how a ThemeScope applies to descendants).
    // A later reparent does not retroactively change the flag: call it again
    // after building a new subtree.
    //
    // Requesting an unavailable backend records the reason in lastError() and
    // leaves the view (and its descendants) on Software. isGpuAccelerated()
    // then returns false. Availability is compile-time (Qt6::OpenGLWidgets) and
    // run-time (a non-headless QPA platform with a creatable GL context).
    void setGpuAccelerated(bool enabled);
    [[nodiscard]] bool isGpuAccelerated() const noexcept;
    [[nodiscard]] bool isGpuAccelerationAvailable() const;

    // The same switch expressed as a backend. setRenderBackend(OpenGL) is
    // setGpuAccelerated(true); the getter reports Software whenever the request
    // could not be honoured, so it always names the path the view is on.
    void setRenderBackend(RenderBackend backend);
    [[nodiscard]] RenderBackend renderBackend() const noexcept;

    void setElevation(Elevation level);
    [[nodiscard]] Elevation elevation() const noexcept;

    void setShape(Shape shape);
    [[nodiscard]] Shape shape() const noexcept;

    // The container fill. An empty colour (the default) means the tonal
    // surface for the current elevation; a valid colour is painted verbatim.
    void setBackgroundColour(const QColor& colour);

    // The colour interaction overlays and the ripple are made of. An empty
    // colour (the default) means the theme's onSurface.
    void setContentColour(const QColor& colour);

    // Qt's geometry questions, answered through the measure contract
    // (framework.md §6.2):
    //   sizeHint        — unspecified width and height;
    //   minimumSizeHint — atMost(0) on both axes;
    //   heightForWidth  — exactly(width) and an unspecified height.
    // All three round the hint result up and leave measuredSize() alone.
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] bool hasHeightForWidth() const override;
    [[nodiscard]] int heightForWidth(int width) const override;

    // EventTarget. The visual tree is the widget tree: the nearest ancestor
    // View is the parent target, and points are mapped with QWidget's own
    // coordinate conversion so device pixel ratios cancel out.
    [[nodiscard]] EventTarget* parentTarget() const noexcept override;
    [[nodiscard]] QPointF mapPointFrom(const EventTarget& source,
                                       const QPointF& point) const override;
    bool onEvent(Event& event) override;
    bool onInterceptEvent(Event& event) override;

    // Store bindings ------------------------------------------------------
    // A view may bind any number of named stores. Every value change is
    // delivered to onStoreChanged() (which applies the framework's own keys:
    // "theme", "font.family" and "font.scale") and re-emitted as
    // storeChanged() for the host. Connections are dropped with the view.
    void bindStore(Store* store);
    void bindStore(const QString& storeId);
    void unbindStore(Store* store = nullptr);
    void unbindAllStores();
    [[nodiscard]] QList<Store*> boundStores() const;

    // A localized string for this widget. The framework reads the "strings"
    // map of the "md.language" store; without a Language container (or a key)
    // the fallback is returned unchanged, so components stay usable standalone.
    [[nodiscard]] QString localized(const QString& key, const QString& fallback) const;

signals:
    void storeChanged(const QString& key, const QVariant& value);
    void storeBindingChanged();

    // Emitted whenever the effective flag changes. GpuCompositor listens to
    // this to start or stop uploading the view's frame as a texture.
    void gpuAccelerationChanged(bool enabled);

protected:
    // The framework applies "theme" by installing a ThemeScope on this view;
    // subclasses may extend it (Text applies the font keys, for example).
    virtual void onStoreChanged(Store* store, const QString& key, const QVariant& value);

    // The whole pipeline. final because a component must not be able to skip
    // the state layer, the focus ring or the shadow (framework.md §5.7).
    void paintEvent(QPaintEvent* event) final;

    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

    // Tracks QEvent::EnabledChange and QEvent::ThemeChange so the state bits
    // and the ripple never paint from a stale configuration.
    void changeEvent(QEvent* event) override;

    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool event(QEvent* event) override;

    // The measure contract. An implementation computes its desired size and
    // stores it with setMeasuredSize(), resolving the specs itself.
    virtual void measureOverride(MeasureSpec width, MeasureSpec height) = 0;

    // Places this view's own content in `content`, which is contentRect().
    // The default does nothing, which is correct for a leaf whose content fits
    // its rectangle.
    virtual void arrangeOverride(const QRectF& content) { Q_UNUSED(content); }

    // Paints the component's content after the background and before the
    // foreground stages.
    virtual void paintContent(QPainter& painter, const QRectF& content) = 0;

    // The damage the current paint pass is consuming, in view-local
    // coordinates. It is the record requestUpdate(QRectF) filled; empty while
    // tracking is disabled. Valid for the duration of paintContent() and
    // paintChildren() and cleared only after a successful pass. Kept as an
    // accessor instead of a parameter so the paintContent() signature — and
    // every existing component override — stays unchanged.
    [[nodiscard]] const DamageTracker& currentDamage() const noexcept;

    // Paints child widgets between the content and the foreground stages.
    // ViewGroup overrides it; a plain View has no children and does nothing.
    virtual void paintChildren(QPainter& painter) { Q_UNUSED(painter); }

    // Whether the view may show a focus indicator. Interactive components
    // override this when they can be focused while disabled.
    [[nodiscard]] virtual bool focusable() const { return isEnabled(); }

    // The device pixel ratio the retained layer is allocated at. The default is
    // QWidget::devicePixelRatioF(), which is what a real screen reports. A host
    // that composites at a fixed scale, or a test that simulates a screen
    // change, overrides it; the layer sees the change as a geometry change and
    // repaints everything.
    [[nodiscard]] virtual qreal layerDevicePixelRatio() const;

    // Lifecycle hooks. They have empty defaults so that a leaf component can
    // ignore the ones it does not need; a component that owns an animation or a
    // cache overrides onShown/onHidden or onAttached/onDetached instead of
    // overriding showEvent/hideEvent itself (which would fight the base class).
    virtual void onAttached() {}
    virtual void onShown() {}
    virtual void onHidden() {}
    virtual void onDetached() {}
    virtual void onThemeChanged() {}
    virtual void onEnabledChanged(bool enabled) { Q_UNUSED(enabled); }

    // Called after the effective GPU flag changed. The default does nothing;
    // a component whose paint output is cached in a texture may invalidate it
    // here. The hook runs on the UI thread.
    virtual void onGpuAccelerationChanged(bool enabled) { Q_UNUSED(enabled); }

    // Stores the result of one measurement pass. Negative sizes are clamped
    // to zero.
    void setMeasuredSize(QSizeF size);

    // Coalesced repaint through the shared task queue. Repeated calls for one
    // view collapse into a single update(), so a burst of property changes
    // paints once (framework.md §5.12).
    void requestUpdate();

private:
    bool interactionFeedbackEnabled_ = true;
    QList<QPointer<Store>> stores_;

    // The scheduling half of both requestUpdate() overloads: one coalesced
    // repaint request, without touching the damage record. Keeping it separate
    // is what lets the damage overload avoid the full-rect path.
    void queueRepaint();

    // Re-publishes the widget rectangle and the resolved corner radius to the
    // ripple controller. Called on resize, when the shape changes and before
    // painting.
    void updateRippleGeometry();

    // Refreshes the ripple controller's theme and content colour from the
    // current theme and the explicit content colour.
    void syncRippleAppearance();

    // Builds a pointer event from a Qt handler and dispatches it.
    void dispatchPointer(EventType type, const QPointF& local, const QPointF& global,
                         Qt::MouseButton button, Qt::MouseButtons buttons,
                         Qt::KeyboardModifiers modifiers,
                         std::chrono::milliseconds timestamp);

    // One Qt geometry query: runs the measure contract without displacing an
    // explicit measurement (see measuredSize()).
    [[nodiscard]] QSizeF hintSize(const char* where, MeasureSpec width,
                                  MeasureSpec height);

    // The full condition under which the ripple controller may animate:
    // feedback enabled by the caller, the widget enabled and the view
    // focusable. Every path that changes one of the three re-applies it.
    void syncRippleEnabled();

    // Applies the effective flag without consulting availability. The public
    // setter resolves availability once and then walks the subtree.
    void applyGpuAccelerated(bool enabled);

    // Builds the diagnostic for a request that could not be honoured and
    // records it in the guard report (lastError). Warns once per view.
    void recordGpuFallback(const QString& reason);

    // True when this widget or one of its ancestors is a GpuCompositor.
    [[nodiscard]] bool hasGpuCompositorAncestor() const;

    // Runs the fixed paint pipeline (shadow, background, content, children,
    // foreground, ripple, focus ring) on `painter`. The direct path calls it
    // with a painter on the widget, the layer-backed path with a painter on the
    // retained surface; both therefore produce the same picture by
    // construction, which is what the pixel-equality test asserts.
    void paintStages(QPainter& painter, const Theme& theme, const QRectF& content,
                     const layer::SurfaceContext& context);

    // Paints this frame through the retained layer and blits it. Returns false
    // when the direct path must paint instead (not layer-backed, the uncached
    // debug mode is on, or the layer could not be acquired this frame).
    bool paintThroughLayer(const Theme& theme, const QRectF& content,
                           const layer::SurfaceContext& context);

    // How large the view asked to be. Zero until the first measurement pass.
    QSizeF measuredSize_{0.0, 0.0};

    // True once a non-hint measure() pass has populated the cache.
    bool measuredExplicitly_ = false;

    // One report shared by the four guarded entry points: one warning per
    // entry point, plus the text lastError() returns.
    view::GuardReport guardReport_;

    // Nesting depth of arrange(). setGeometry() emits a resize synchronously;
    // resizeEvent() skips its own arrangement while this is non-zero so one
    // arrange() call places the content exactly once.
    int arrangeDepth_ = 0;

    Shape shape_{Shape::small()};
    Elevation elevation_ = Elevation::Level0;
    ViewStates state_;
    LifecycleState lifecycle_ = LifecycleState::Created;
    QColor backgroundColour_;
    QColor contentColour_;

    // Opt-in damage record. The serial changes whenever a request records
    // damage while tracking is on; paintEvent() snapshots it so a request made
    // from inside a paint hook survives the post-paint clear instead of being
    // silently consumed by the pass it was meant to schedule.
    bool damageTrackingEnabled_ = false;
    DamageTracker damage_;
    quint64 damageRequestSerial_ = 0;

    // The retained-layer state, allocated only when setLayerBacked(true) is
    // called. Declared after the damage record because the layer is fed from
    // it; both are destroyed with the view in reverse order.
    std::unique_ptr<LayerBacking> layerBacking_;

    // The last pointer position seen, used to give PointerLeave a meaningful
    // local position when Qt's leave event carries none.
    QPointF lastPointerPosition_;

    // True when the current focus came from the keyboard (Tab, a shortcut, a
    // programmatic focus), false when a mouse press or a popup established it.
    // The focus ring is a keyboard indicator; the state layer covers the rest.
    bool keyboardFocus_ = false;

    // Effective GPU opt-in. False (the software path) until a successful
    // setGpuAccelerated(true), and forced back to false whenever the request
    // cannot be honoured.
    bool gpuAccelerated_ = false;

    layer::BackgroundLayer backgroundLayer_;
    layer::ElevationLayer elevationLayer_;
    // A widget's painter is clipped to the widget rectangle, so the ring is
    // painted inside the bounds: an outside ring would survive only as corner
    // fragments (the "corner dot" artefact).
    layer::FocusRingLayer focusRing_{layer::FocusRingLayer::Placement::Inside};
    layer::LayerStack foreground_;

    ripple::RippleController ripple_;

    // Stable coalescing key; unique for the lifetime of the process.
    quint64 updateKey_ = 0;
};

} // namespace md
