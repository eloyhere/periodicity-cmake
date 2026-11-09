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
//   5. the foreground stack — the state overlay and the outline;
//   6. the ripple;
//   7. the focus ring, when the view is focusable and actually focused.
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
#include <md/view/lifecycle.h>

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSizeF>
#include <QWidget>
#include <Qt>

#include <chrono>

class QPainter;
class QShowEvent;
class QHideEvent;

namespace md {

// The kernel every component inherits from.
//
// A View owns its shape, elevation, colours, state and layer stack. It answers
// Qt's geometry questions by running the measure contract, and it paints the
// fixed pipeline above; a subclass supplies the content and, when it has
// children, the arrangement.
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

    [[nodiscard]] QSizeF measuredSize() const noexcept;
    [[nodiscard]] QRectF contentRect() const noexcept;

    [[nodiscard]] ViewStates state() const noexcept;

    // Where this widget is in its lifecycle (docs/backlog.md, "Component
    // lifecycle"). Components read it to assert their own invariants; the
    // transitions themselves belong to the base class.
    [[nodiscard]] LifecycleState lifecycleState() const noexcept;

    // Sets or clears one interaction flag and schedules the repaint. The
    // Enabled flag is special: it mirrors QWidget::isEnabled(), so it changes
    // the widget's enabled state and every other interaction flag is cleared
    // when the view is disabled.
    void setState(ViewState flag, bool on);

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
    // All three return the cached measurement rounded up.
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

protected:
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

    // How large the view asked to be. Zero until the first measurement pass.
    QSizeF measuredSize_{0.0, 0.0};

    Shape shape_{Shape::small()};
    Elevation elevation_ = Elevation::Level0;
    ViewStates state_;
    LifecycleState lifecycle_ = LifecycleState::Created;
    QColor backgroundColour_;
    QColor contentColour_;

    // The last pointer position seen, used to give PointerLeave a meaningful
    // local position when Qt's leave event carries none.
    QPointF lastPointerPosition_;

    layer::BackgroundLayer backgroundLayer_;
    layer::ElevationLayer elevationLayer_;
    layer::FocusRingLayer focusRing_;
    layer::LayerStack foreground_;

    ripple::RippleController ripple_;

    // Stable coalescing key; unique for the lifetime of the process.
    quint64 updateKey_ = 0;
};

} // namespace md
