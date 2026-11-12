// Periodicity — BottomSheet, the Material 3 bottom sheet.
//
// A BottomSheet is an in-window overlay, exactly like Drawer and Dialog: it
// creates no platform window. A modal sheet reparents itself to the host
// window, paints the scrim and the bottom-anchored surface into the same
// backing store as its host, registers with the Modal layer above Content and
// below Dialog, and takes focus. A non-modal sheet is only the surface: nothing
// is reparented to the window, no scrim is painted, open() does not take focus,
// and Escape is left to the application. It still registers, at the Content
// rung, so it stacks above ordinary page widgets while a dialog opened later
// restacks above it.
//
// The surface is bottom-anchored and has 28 dp top corners, the Material 3
// large shape. It carries a 32x4 dp drag handle with 22 dp of top padding, an
// optional titleLarge title inset 24 dp from the sides, and a content viewport
// below it. The surface never grows taller than 90% of the host, so a tall body
// scrolls inside the viewport instead of covering the whole window.
//
// The transition interpolates a single progress value with a QTimer and reads
// the theme's short motion token. A reduced-motion theme resolves that token to
// zero, so the sheet snaps open or shut. open() emits opened() once the sheet
// is shown; close() is synchronous for a non-modal sheet and starts a slide-out
// for a modal one, which emits closed() when the slide ends.
//
// Dismissal follows the two Material paths and the accessibility contract: a
// modal sheet closes on Escape and on a backdrop press while
// dismissesOnBackdrop() is true, and dragging the handle down past a quarter of
// the sheet height closes it. Escape is the keyboard equivalent of that drag
// (WCAG 2.5.7), so the pointer gesture never carries a function that a keyboard
// user cannot reach.
//
// The content is reparented into the sheet's internal viewport and shown while
// the sheet is open. The sheet does not own it: replacing the content, or
// destroying the sheet, detaches the widget again so a stack-owned body
// survives its host.

#pragma once

#include <md/view/view.h>

#include <QPointer>
#include <QString>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;
class QTimer;
class QWheelEvent;
class QWidget;

namespace md {

class BottomSheet : public View
{
    Q_OBJECT

public:
    explicit BottomSheet(QWidget* parent = nullptr);
    ~BottomSheet() override;

    // The heading painted under the drag handle. An empty title removes the
    // block and the content viewport starts directly below the handle.
    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;

    // Reparents `content` into the sheet's viewport. The sheet does not own it:
    // a null pointer detaches and hides the current content, and destroying the
    // sheet detaches it again so the caller's widget stays alive.
    void setContent(QWidget* content);
    [[nodiscard]] QWidget* content() const;

    // Modal is the default. A modal sheet paints the scrim, registers at the
    // Drawer rung of the Modal layer, traps focus and takes it on open(); a
    // non-modal sheet does none of those and registers at Content instead, so
    // it is still above ordinary page widgets but never covers a dialog.
    // Changing the mode while open re-applies the overlay immediately.
    void setModal(bool modal);
    [[nodiscard]] bool isModal() const noexcept;

    // Whether a press on the backdrop dismisses a modal sheet. A non-modal
    // sheet has no backdrop to press.
    void setDismissOnBackdrop(bool dismiss);
    [[nodiscard]] bool dismissesOnBackdrop() const noexcept;

    // Modal: open() covers the host and slides the surface in; close() slides
    // it out and hides it when the transition ends. Non-modal: both only toggle
    // the surface, and they complete synchronously.
    void open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept;

    // 0 — fully hidden, 1 — fully open. Non-modal sheets are always 0 or 1.
    [[nodiscard]] qreal animationProgress() const noexcept;
    [[nodiscard]] bool isAnimating() const;

    // Ends a running transition at its final state, including the hide() and
    // the closed() signal of a closing sheet. Tests use it to assert a settled
    // layout without waiting for the timer.
    void finishAnimation();

    // Geometry in widget coordinates. A modal sheet covers the host, so its
    // scrim is the whole widget; a non-modal sheet is only the surface and has
    // no scrim. The surface is the bottom-anchored panel, the handle is the
    // 32x4 dp affordance, and the viewport is the clipped body area. All of
    // them are empty while the sheet is closed.
    [[nodiscard]] QRectF surfaceRect() const;
    [[nodiscard]] QRectF scrimRect() const;
    [[nodiscard]] QRectF handleRect() const;
    [[nodiscard]] QRectF titleRect() const;
    [[nodiscard]] QRectF viewportRect() const;

signals:
    void opened();
    void closed();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeOverride(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    void onThemeChanged() override;
    // The keyboard indicator is the focused control inside the sheet; a ring
    // around the whole overlay would suggest the scrim itself is one control.
    [[nodiscard]] bool focusable() const override;

private:
    [[nodiscard]] QWidget* hostWidget() const;
    [[nodiscard]] qreal maxSurfaceHeight() const;
    [[nodiscard]] qreal headerHeight() const;
    [[nodiscard]] qreal contentHeightForWidth(qreal width) const;
    [[nodiscard]] qreal maxScrollOffset() const;
    [[nodiscard]] QRectF dragAreaRect() const;
    [[nodiscard]] bool isTopLayer() const;

    void relayout();
    void syncLayout();
    void applyOverlayGeometry();
    void updateViewportGeometry();
    void setScrollOffset(qreal offset);

    void attachLayer();
    void detachLayer();
    void installFilters();
    void removeFilters();

    void animateTo(qreal target);
    void finishTransition();
    [[nodiscard]] int transitionDuration() const;
    [[nodiscard]] qreal easedProgress(qreal linear) const;

    void advanceFocus(bool forward);

    QWidget* viewport_ = nullptr;
    QPointer<QWidget> content_;
    QString title_;

    bool modal_ = true;
    bool dismissOnBackdrop_ = true;
    bool open_ = false;
    bool closing_ = false;
    bool filtersInstalled_ = false;
    bool layerAttached_ = false;
    bool pressedBackdrop_ = false;
    bool dragging_ = false;

    // Downward displacement of the surface while the handle is dragged.
    qreal dragOffset_ = 0.0;
    qreal dragStartY_ = 0.0;
    qreal scrollOffset_ = 0.0;
    qreal contentHeight_ = 0.0;

    qreal progress_ = 0.0;
    qreal startProgress_ = 0.0;
    qreal targetProgress_ = 0.0;
    int elapsedMs_ = 0;

    QTimer* timer_ = nullptr;
};

} // namespace md
