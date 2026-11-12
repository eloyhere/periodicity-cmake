// Periodicity — Pager, a horizontally swiped page container.
//
// The pages are real View children, arranged side by side in one viewport and
// displaced by a single continuous offset. That offset — not a stack of pixmaps
// and not a QPropertyAnimation — is the animation: a drag moves it with the
// pointer, and a release either snaps it back to zero or runs it to the next
// page on a QTimer. Because the pages only ever move, a page keeps its widgets,
// its focus chain and its accessibility subtree across a swipe.
//
// The resting state shows exactly one page: every other child is hidden, which
// is what makes a pager with heavy pages cost one page's paint. While a swipe or
// a snap is in flight the neighbour that is entering the viewport is shown too;
// syncVisibility() is the single place that decides this.
//
// A swipe is damped past the first and the last page when loop() is off, so the
// boundary feels elastic instead of dead. The indicator dots live in a band
// under the viewport and are painted — and hit-tested — by the pager itself, so
// no page can ever cover its own controls.

#pragma once

#include <md/view/view_group.h>

#include <QList>
#include <QPointF>
#include <Qt>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;
class QTimer;

namespace md {

class Pager : public ViewGroup
{
    Q_OBJECT

public:
    explicit Pager(QWidget* parent = nullptr);

    // Replaces every page. The removed views are detached, not deleted: the
    // caller keeps ownership, exactly as ViewGroup::removeView documents.
    void setPages(const QList<View*>& pages);
    void addPage(View* page);
    void removePage(View* page);
    void clearPages();

    [[nodiscard]] QList<View*> pages() const;
    [[nodiscard]] int pageCount() const;

    [[nodiscard]] int currentIndex() const noexcept;

    // The index changes immediately and pageChanged() is emitted before the
    // snap runs; the animation only catches the visual up. A jump of more than
    // one page, a reduced-motion theme and `animated == false` all snap
    // instantly.
    void setCurrentIndex(int index, bool animated = true);
    void next();
    void previous();

    // With loop off (the default) next()/previous() stop at the ends.
    void setLoop(bool loop);
    [[nodiscard]] bool loop() const noexcept;

    void setSwipeEnabled(bool enabled);
    [[nodiscard]] bool isSwipeEnabled() const noexcept;

    void setIndicatorVisible(bool visible);
    [[nodiscard]] bool isIndicatorVisible() const noexcept;

    [[nodiscard]] bool isDragging() const noexcept;
    [[nodiscard]] bool isAnimating() const;

    // Ends a running snap at its final state. A test uses it to assert a settled
    // layout without waiting for the timer.
    void finishAnimation();

    // The current horizontal displacement of the pages, in pixels, measured
    // from the resting position. Negative while the next page is being pulled
    // in, positive while the previous one is.
    [[nodiscard]] qreal dragOffset() const noexcept;

    // Geometry in widget coordinates. The viewport excludes the indicator band,
    // which is empty when there is nothing to indicate.
    [[nodiscard]] QRectF pageArea() const;
    [[nodiscard]] QRectF indicatorBarRect() const;

    // Where page `index` sits right now, including a dragging offset.
    [[nodiscard]] QRectF pageRect(int index) const;

    // The hit target of one indicator dot, in widget coordinates.
    [[nodiscard]] QRectF indicatorRect(int index) const;

signals:
    void pageChanged(int index);
    void dragStateChanged(bool dragging);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    void onThemeChanged() override;

private:
    [[nodiscard]] int wrapIndex(int index) const;
    [[nodiscard]] int clampIndex(int index) const;
    [[nodiscard]] bool canGoForward() const;
    [[nodiscard]] bool canGoBackward() const;
    [[nodiscard]] bool indicatorsShown() const;
    [[nodiscard]] qreal pageWidth() const;
    [[nodiscard]] qreal dampedShift(qreal shift) const;
    [[nodiscard]] int transitionDuration() const;
    [[nodiscard]] qreal easedProgress(qreal linear) const;
    [[nodiscard]] int indicatorAt(const QPointF& position) const;

    void stepTo(int target, bool animated);
    void settle(bool animated);
    void beginAnimation(qreal target);
    void commitAnimation();
    void activatePage(int index);
    void syncVisibility();
    void syncLayout();
    void prunePage(View* page);

    QList<View*> pages_;

    // current_ is the logical page; anchor_ is the page that sat at the
    // resting position when the running transition started. shift_ is the
    // continuous displacement in page units, so a page's x is
    // (index - anchor_ + shift_) * width.
    int current_ = 0;
    int anchor_ = 0;
    qreal shift_ = 0.0;

    bool loop_ = false;
    bool swipeEnabled_ = true;
    bool indicatorVisible_ = true;

    QTimer* timer_ = nullptr;
    qreal animationFrom_ = 0.0;
    qreal animationTo_ = 0.0;
    int elapsedMs_ = 0;

    bool pressed_ = false;
    bool dragging_ = false;
    int pressedIndicator_ = -1;
    QPointF pressPosition_;
    qreal pressShift_ = 0.0;
    qreal lastMoveX_ = 0.0;
    quint64 lastMoveTimestamp_ = 0;
    qreal velocity_ = 0.0;
};

} // namespace md
