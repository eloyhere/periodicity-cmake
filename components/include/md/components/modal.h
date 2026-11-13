// Periodicity — Modal, the shared scrim and layer manager.
//
// Every floating surface in the library needs the same two things: a scrim
// that dims and blocks the content behind it, and a place in a deterministic
// stacking order. Dialog, Drawer, Menu, Dropdown and Notification each used to
// invent their own scrim and rely on raise() calls scattered through the call
// sites, which is why a dropdown could end up above a dialog or behind a
// drawer.
//
// Modal is that shared piece. It is a full-window scrim widget, and it also
// owns a tiny per-window layer registry: a widget registers with a Level, and
// the registry restacks the registered widgets in ascending level order so the
// ordering is stable regardless of which widget was shown last.
//
// A Modal may also present a card: a child widget placed above the scrim. The
// card is animated by ContainerTransform (fade, scale, rise) and the scrim
// fades with it. Because the card is an ordinary child and the animation is a
// single graphics effect on it, nothing is painted over the card's children
// and no sibling is raised above them. This is the modal half of the design baseline's
// container transform: the content lives inside the surface that moves.
//
// Entry uses Level2 (500 ms) with EmphasizedDecelerate for the card and Level1
// (250 ms) with Standard for the scrim; dismissal uses Level1 with
// EmphasizedAccelerate. Reduced motion snaps in one turn and never starts an
// animator.
//
// Levels are values, not enums only, so a host can place its own surface
// between two of ours without editing the library.

#pragma once

#include <md/view/view.h>

#include <QHash>
#include <QPointer>
#include <QRectF>
#include <QString>

#include <chrono>

class QEvent;
class QHideEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QWidget;

namespace md {

namespace animation {
class Animator;
}

class ContainerTransform;

class Modal final : public View
{
    Q_OBJECT

public:
    enum class Level {
        Content = 0,
        Dropdown = 100,
        Drawer = 200,
        Dialog = 300,
        Notification = 400,
        Tooltip = 500,
    };

    explicit Modal(QWidget* parent = nullptr);
    ~Modal() override;

    // The scrim's place in the stack. Registering happens on open(); changing
    // the level while open restacks immediately.
    void setLevel(Level level);
    void setLevelValue(int level);
    [[nodiscard]] int levelValue() const;

    // 0 is transparent, 1 is the theme's scrim colour at full strength. The
    // default is 0.32, the baseline modal scrim.
    void setDim(qreal dim);
    [[nodiscard]] qreal dim() const noexcept;

    void setScrimColour(const QColor& colour);
    [[nodiscard]] QColor scrimColour() const;

    void setDismissOnClick(bool dismiss);
    [[nodiscard]] bool dismissesOnClick() const noexcept;
    void setDismissOnEscape(bool dismiss);
    [[nodiscard]] bool dismissesOnEscape() const noexcept;

    // Card content -------------------------------------------------------
    // The surface shown above the scrim. The modal reparents the card and takes
    // ownership of it; replacing it deletes the previous card. A null card
    // leaves the modal as a bare scrim, which is what the layer tests use.
    void setCard(QWidget* card);
    [[nodiscard]] QWidget* card() const noexcept;

    // The card's final rectangle in modal coordinates. An empty rect (the
    // default) places the card against the anchor when one is set, or centres
    // it otherwise. Setting a rectangle disables automatic placement.
    void setCardRect(const QRectF& rect);
    [[nodiscard]] QRectF cardRect() const;

    // The chip a selector grows from. The card is placed adjacent to it
    // (preferring below, flipping above when there is no room) and the scrim
    // still covers the window, so the chip stays visible behind it.
    void setAnchor(QWidget* anchor);
    void setAnchorRect(const QRectF& globalRect);
    [[nodiscard]] QWidget* anchor() const noexcept;

    // Where focus goes when the modal closes. The default is the widget that
    // had focus when open() was called, which is the chip for a chip → modal
    // interaction.
    void setFocusReturn(QWidget* widget);
    [[nodiscard]] QWidget* focusReturn() const noexcept;

    // Motion -------------------------------------------------------------
    // When false every open()/close() applies its end state synchronously.
    void setAnimated(bool animated);
    [[nodiscard]] bool isAnimated() const noexcept;
    [[nodiscard]] bool isAnimating() const noexcept;

    // The scrim's current strength in 0..1, after the fade. Exposed so a host
    // can paint a matching scrim, and so tests can observe the fade.
    [[nodiscard]] qreal scrimOpacity() const noexcept;

    // The card transform's progress in 0..1; 1 without a card.
    [[nodiscard]] qreal cardAnimationProgress() const noexcept;

    // Deterministic clock. While enabled, open()/close() arm no timer and
    // advanceAnimation() consumes exactly the time it is given; the scrim and
    // card end states, openFinished() and closeFinished() are unchanged. A
    // caller that renders on its own cadence, and a test, use this instead of
    // sleeping.
    void setManualAnimationClock(bool manual);
    [[nodiscard]] bool isManualAnimationClock() const noexcept;
    void advanceAnimation(std::chrono::milliseconds delta);

    // Settles a running entrance or exit at its end state immediately. With
    // the manual clock this is the same as advancing past the motion duration;
    // without it the current wall-clock run is completed synchronously.
    void finishAnimation();

    void open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept;

    [[nodiscard]] QRectF scrimRect() const;

    // Layer registry -----------------------------------------------------
    // Registers a window-level widget. The widget must be a child of the
    // window; registerLayer() restacks the window after storing the level.
    static void registerLayer(QWidget* layer, Level level);
    static void registerLayer(QWidget* layer, int level);
    static void unregisterLayer(QWidget* layer);
    [[nodiscard]] static int layerOf(const QWidget* layer);
    // Raises `layer` within its own level without letting it jump above a
    // higher level (a dropdown cannot cover a dialog that opened after it).
    static void raiseWithinLayer(QWidget* layer);
    [[nodiscard]] static QWidget* topLayer(QWidget* window);
    static void restack(QWidget* window);

signals:
    void clicked();
    void opened();
    void closed();
    // The entrance reached its end state; the exit finished and the modal is
    // hidden. With reduced motion (or setAnimated(false)) both are emitted
    // synchronously from open()/close().
    void openFinished();
    void closeFinished();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Phase
    {
        Idle,
        Opening,
        Open,
        Closing,
    };

    void applyWindowGeometry();
    void positionCard();
    [[nodiscard]] QRectF effectiveAnchorRect() const;
    void installFilters();
    void removeFilters();

    void startEntrance();
    void startExit();
    void animateScrim(bool visible, bool animate);
    void maybeFinishOpen();
    void maybeFinishClose();
    void finishClose();
    void abort();
    void restoreFocus();
    [[nodiscard]] QList<QWidget*> focusableWidgets() const;
    bool trapFocus(QKeyEvent* event);

    // The window this modal is currently attached to. A QPointer because the
    // host can be destroyed while the modal survives: Language::showSelector()
    // reparents an open modal to a new window before the old window goes away,
    // and close()/removeFilters() must then skip the dead host instead of
    // calling removeEventFilter() on freed memory.
    QPointer<QWidget> host_;
    int level_ = static_cast<int>(Level::Dialog);
    qreal dim_ = 0.32;
    QColor scrimColour_;
    bool dismissOnClick_ = true;
    bool dismissOnEscape_ = true;
    bool open_ = false;
    bool closeEmitted_ = false;

    QPointer<QWidget> card_;
    QPointer<QWidget> anchor_;
    QPointer<QWidget> focusReturn_;
    QPointer<QWidget> previousFocus_;
    QRectF anchorRect_;
    bool hasAnchorRect_ = false;
    QRectF explicitCardRect_;
    bool hasExplicitCardRect_ = false;

    ContainerTransform* cardTransform_ = nullptr;
    animation::Animator* scrimAnimator_ = nullptr;

    Phase phase_ = Phase::Idle;
    bool animated_ = true;
    bool scrimDone_ = true;
    bool cardDone_ = true;
    qreal scrimOpacity_ = 1.0;
};

} // namespace md
