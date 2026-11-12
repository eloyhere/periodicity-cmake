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
#include <md/view/error_guard.h>
#include <md/view/lifecycle.h>

#include <QColor>
#include <QList>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QRectF>
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

    // Paints child widgets between the content and the foreground stages.
    // ViewGroup overrides it; a plain View has no children and does nothing.
    virtual void paintChildren(QPainter& painter) { Q_UNUSED(painter); }

    // Whether the view may show a focus indicator. Interactive components
    // override this when they can be focused while disabled.
    [[nodiscard]] virtual bool focusable() const { return isEnabled(); }

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

    // The last pointer position seen, used to give PointerLeave a meaningful
    // local position when Qt's leave event carries none.
    QPointF lastPointerPosition_;

    // True when the current focus came from the keyboard (Tab, a shortcut, a
    // programmatic focus), false when a mouse press or a popup established it.
    // The focus ring is a keyboard indicator; the state layer covers the rest.
    bool keyboardFocus_ = false;

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
