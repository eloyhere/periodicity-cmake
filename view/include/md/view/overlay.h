// Periodicity — Overlay, the window-level host for floating surfaces.
//
// The library draws several kinds of surface above ordinary content: the modal
// scrim and its card, a dialog, the busy switch surface, a sheet, a menu. They
// all share the same window-level problems, and none of them should solve
// those problems itself:
//
//   * which widget is the top-level window, and which widget inside it is the
//     content root;
//   * the stacking order when two surfaces are up at once, including a surface
//     opened from another surface (a dialog from a picker sheet);
//   * which surface receives input, and how everything below it is blocked
//     again once the last surface leaves;
//   * the exact enabled state of the content root, so a descendant that was
//     already disabled before the overlay came up stays disabled afterwards;
//   * the focus that existed before the first surface appeared, and where it
//     goes when the last one leaves.
//
// Overlay answers those once per top-level window. It is not a visual and it
// does not paint: the individual surfaces paint their own scrim, card and
// content, and the layer/ package paints what belongs to a single widget's
// surface. Overlay only owns the stack of live surfaces, z order, the input
// gate and the window-level state.
//
// Z-order contract
// ----------------
// When Overlay first stacks a surface it creates one host widget as a direct
// child of the window, above the content root, and raises it once. Every push
// then reparents the surface to the window and raises it, so the stack order
// is the sibling order: a new surface is above every earlier surface. Nested
// pushes are legal and expected. Overlay never infers the top surface from
// creation order, sibling order or a visible-widget scan; `top()` is the last
// entry of the stack, and `push()` raises exactly that widget.
//
// The host is transparent to mouse events and paints nothing. It exists only
// as the z-order floor that surfaces are raised above; the input gate is the
// event filter described below, not the host widget.
//
// Input contract
// --------------
// While at least one surface is on the stack, Overlay installs itself as an
// application event filter (and removes it again when the stack empties). For
// an event delivered to a widget of this window:
//
//   * if the widget is the top surface or a descendant, the event is
//     forwarded untouched. Tab and Backtab therefore reach the surface's own
//     focus trap (Modal's, for example), and key handling inside the surface
//     is exactly what it would be without an Overlay;
//   * if the widget is anywhere else in the window, and the top surface asked
//     for `blockInput`, mouse press/release/double-click/move, wheel, context
//     menu, touch, tablet, drag-and-drop, key press/release, ShortcutOverride
//     and Shortcut are swallowed. That covers pointer input, focus traversal
//     keys and window shortcuts; a shortcut cannot fire behind the surface.
//     Hover, enter/leave and focus events are forwarded because they are
//     notifications, not activation: swallowing them would leave a widget's
//     hover state stuck and desynchronise Qt's focus bookkeeping;
//   * if the widget belongs to a different window, or the top surface asked
//     for `blockInput = false`, the event is forwarded.
//
// The filter cannot get stuck: it is removed on the last pop, on the last
// surface being destroyed, and in the destructor; every removal path is
// idempotent, and `eventFilter()` returns immediately while the stack is
// empty. The filter is also never installed while no surface is up.
//
// The native-window boundary
// --------------------------
// Overlay guarantees the topmost *in-window* surface only. A native menu, a
// tooltip window, a platform file dialog or any other top-level QWindow is a
// separate window: the platform compositor puts it above this window by
// definition, and Overlay neither sees nor orders it. A surface that puts a
// widget into its own native window is outside this contract.
//
// The same is true, transitionally, of an in-window surface that still uses the
// legacy Modal::Level registry instead of push(): Overlay does not know it, so
// it treats input aimed at it as input below the top surface. Such a surface
// must be migrated before it is opened above a Modal or Dialog.
//
// Lifetime
// --------
// One Overlay exists per top-level window. `forWidget()` looks it up or creates
// it lazily as a QObject child of the window, so it is destroyed with the
// window and is never leaked. Surfaces are not owned by Overlay; a surface that
// is destroyed while pushed loses its stack entry immediately, and the
// window-level restore (content root, focus, host, input filter) follows on the
// next event-loop turn. QWidget emits destroyed() from inside its own
// destructor, before the children are gone; touching the window's focus or
// enabled state there would run against a tree that is still being taken
// apart. When the window dies while surfaces are up, the whole tree (including
// the surfaces, unless they were reparented away first) is destroyed together
// and Overlay removes its application filter without touching the
// half-destroyed tree.
//
// Focus restoration uses QWidget::setFocus(). If the window is not active, Qt
// records that widget as the window's focus child and QApplication::focusWidget()
// keeps returning null until the window is activated; Overlay deliberately does
// not call activateWindow(), because raising a window is the host's decision,
// not a side effect of closing a surface.

#pragma once

#include <QList>
#include <QObject>
#include <QPointer>

class QEvent;
class QWidget;

namespace md {

class Overlay final : public QObject
{
    Q_OBJECT

public:
    // How one surface participates in the window-level contract. The defaults
    // describe a modal surface: it covers the window, it receives all input,
    // and focus returns to the invoker when it leaves.
    struct Options
    {
        // While this surface is top-most, swallow input aimed at anything
        // outside it. A non-modal surface sets this to false so the page below
        // stays usable.
        bool blockInput = true;

        // Disable the window's content root while this surface is on the
        // stack. The exact per-root enabled state is saved before the first
        // disable and restored after the last pop. Off by default because a
        // translucent scrim usually blocks input without changing the paint
        // state of the content; a surface that covers the content opaquely
        // (the busy switch surface) turns it on.
        bool disableContent = false;

        // Restore the focus captured on the outermost push when the stack
        // empties. Off for a surface that owns focus another way.
        bool restoreFocus = true;

        // Keep the surface at the window's rectangle, including on resize.
        // A full-window surface (scrim, dialog) leaves this on; an anchored
        // surface that positions itself sets it to false.
        bool fillWindow = true;

        // Optional focus target that is not the captured widget. With
        // invokerFirst it is the preferred target (Modal's setFocusReturn);
        // without it, it is the fallback tried after the captured widget
        // (Modal's setFocusFallback).
        QWidget* invoker = nullptr;

        // Prefer `invoker` over the widget captured on the outermost push.
        // Off by default, because a surface that only supplies an invoker as a
        // fallback wants the captured focus back when it still exists. Modal
        // turns it on when setFocusReturn() named an explicit target, which is
        // documented to win over the previous focus.
        bool invokerFirst = false;
    };

    // The Overlay of `widget`'s top-level window, created on first use. Never
    // returns nullptr when `widget` is non-null and has a window.
    static Overlay* forWidget(QWidget* widget);

    ~Overlay() override;

    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    // Adds `surface` on top of the stack, reparents it to the window and raises
    // it. `surface` must already belong to this window. A surface that is
    // already on the stack is not pushed twice; the call returns false.
    //
    // The surface is not shown or hidden here: the caller owns its visibility
    // (its entrance and exit motion included). The geometry of a `fillWindow`
    // surface is set to the window rectangle immediately and on every resize.
    bool push(QWidget* surface, Options options = Options());

    // Removes `surface` from the stack. The surface is left untouched: the
    // caller hides it when its exit is done. Popping a surface that is not on
    // the stack returns false. When the stack empties, the content root is
    // re-enabled, focus is restored and the host and input filter are removed.
    bool pop(QWidget* surface);

    // Arms or disarms one surface's request to disable the content root. This
    // is what a surface that must not disable during its entrance uses: it
    // pushes with disableContent = false and turns the request on when it is
    // fully opaque. The disable and restore still happen exactly once across
    // the stack, in the same way as Options::disableContent.
    bool setContentDisabled(QWidget* surface, bool disabled);

    // The surface that owns input, or nullptr when the stack is empty. This is
    // the only definition of "top-most" an in-window surface may use.
    [[nodiscard]] QWidget* top() const;

    [[nodiscard]] bool contains(const QWidget* surface) const;

    [[nodiscard]] int count() const;

    // 0 is the bottom of the stack. -1 when the surface is not pushed.
    [[nodiscard]] int depthOf(const QWidget* surface) const;

    // The transparent z-order floor and the window it belongs to. Both are
    // null after the window has been destroyed; neither is owned by the
    // caller.
    [[nodiscard]] QWidget* hostWidget() const;
    [[nodiscard]] QWidget* window() const;

    // True when the stack is non-empty and its top surface asked for
    // blockInput.
    [[nodiscard]] bool blocksInput() const;

signals:
    // The stack, its order or its top surface changed.
    void changed();

    // The window was hidden. A surface that must not stay open over a hidden
    // window (Dialog) or must settle a motion that can no longer be seen
    // (Modal) reacts here; Overlay itself does not close anything, because
    // exit motion belongs to the surfaces.
    void windowHidden();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    explicit Overlay(QWidget* window);

    struct Entry
    {
        quint64 id = 0;
        QPointer<QWidget> surface;
        Options options;
    };

    struct ContentEntry
    {
        QPointer<QWidget> widget;
        bool wasEnabled = true;
    };

    [[nodiscard]] int indexOf(const QWidget* surface) const;
    void ensureHost();
    void updateHostGeometry();
    void updateContentDisabled();
    void captureFocus(const Options& options);
    void restoreFocus();
    void installApplicationFilter();
    void removeApplicationFilter();
    void removeEntry(quint64 id);
    void finishAfterSurfaceGone();
    [[nodiscard]] bool isSurface(const QWidget* widget) const;
    [[nodiscard]] bool isFocusTarget(QWidget* widget) const;

    QPointer<QWidget> window_;
    QPointer<QWidget> host_;
    QList<Entry> entries_;
    QList<ContentEntry> content_;
    QPointer<QWidget> focusBefore_;
    QPointer<QWidget> invoker_;
    bool restoreFocusOnPop_ = true;
    bool invokerFirst_ = false;
    bool contentDisabled_ = false;
    bool filterInstalled_ = false;
    bool windowDying_ = false;
    // A surface that was destroyed while pushed leaves the window-level
    // restore to the next event-loop turn. QWidget emits destroyed() from
    // inside ~QWidget, before the children are gone; calling setFocus(),
    // setEnabled() or hide() on the window from there would run against a
    // widget tree that is still being taken apart. restoreToken_ invalidates
    // the queued call when a normal pop already restored the window.
    bool restorePending_ = false;
    quint64 restoreToken_ = 0;
    quint64 nextId_ = 1;
};

} // namespace md
