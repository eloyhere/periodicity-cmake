// Periodicity — the Carousel component (framework.md §5.9).
//
// A slide container. The slides are real View children, arranged side by side
// and shifted horizontally so that the current one fills the viewport; a
// transition between two slides animates that shift from 0 to 1 over the
// configured duration, which is why the arrangement — not a pixmap cache — is
// the animation: every slide keeps its own widgets, focus and accessibility
// while it moves.
//
// Only the current slide (and, during a transition, the outgoing one) is
// visible, so a carousel with heavy children costs one slide's paint. Loop
// decides whether next()/previous() wrap; auto play is a QTimer that advances
// the carousel and stops on its own when there is nothing to advance to.
//
// A slide can be dragged with the mouse as well as advanced with the keyboard:
// a press records the origin, a horizontal move past the touch slop pulls the
// slides with the pointer (with an elastic limit at an unloopable end), and a
// release either advances, goes back, or springs to the current slide. A press
// that never leaves the slop stays a click, so a slide's own controls keep
// working. Auto play pauses for the duration of the gesture.
//
// The indicator dots sit in a band under the slides rather than on top of them:
// they are painted by the parent's content pass, and the children start below
// that band, so no slide can ever cover its own controls.

#pragma once

#include <md/view/view_group.h>

#include <QList>
#include <QPointer>
#include <QRectF>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QTimer;
class QEvent;

namespace md {

class Carousel : public ViewGroup
{
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentChanged)

public:
    explicit Carousel(QWidget* parent = nullptr);

    // Replaces every slide. The removed views are detached, not deleted: the
    // caller keeps ownership, exactly as ViewGroup::removeView documents.
    void setSlides(const QList<View*>& slides);
    void addSlide(View* slide);
    void removeSlide(View* slide);
    void clearSlides();

    [[nodiscard]] QList<View*> slides() const;
    [[nodiscard]] int slideCount() const;

    [[nodiscard]] int currentIndex() const noexcept { return current_; }

    // Moves to `index`. With loop() the index wraps; without it, it is clamped.
    // The transition starts immediately and currentChanged() is emitted before
    // the animation runs.
    void setCurrentIndex(int index);

    void next();
    void previous();

    [[nodiscard]] bool loop() const noexcept { return loop_; }
    void setLoop(bool loop);

    [[nodiscard]] bool autoPlay() const noexcept { return autoPlay_; }
    void setAutoPlay(bool enabled);

    [[nodiscard]] int interval() const noexcept { return intervalMs_; }

    // Auto-play period in milliseconds. Non-positive values are ignored.
    void setInterval(int milliseconds);

    [[nodiscard]] int duration() const noexcept { return durationMs_; }

    // Transition length in milliseconds; zero means "switch instantly", which
    // is also what a reduced-motion theme resolves to.
    void setDuration(int milliseconds);

    // The transition, expressed as 0 (the previous slide is still in place) to
    // 1 (the current slide is in place).
    [[nodiscard]] qreal transitionProgress() const noexcept { return progress_; }
    [[nodiscard]] bool isAnimating() const;

    // Ends a running transition at its final state. A test uses it to assert a
    // settled layout without waiting for the timer.
    void finishTransition();

    // Ends a running transition, a spring-back or a drag-commit at its final
    // state. Same contract as finishTransition(), and the one a gesture test
    // uses to assert where the slides came to rest.
    void finishAnimation();

    // The gesture, exposed so a test can assert it without reaching into the
    // private arrangement. dragOffset() is the displacement, in pixels, that
    // the slides are pulled from their resting position: negative while the
    // next slide is being pulled in, positive while the previous one is.
    [[nodiscard]] bool isDragging() const noexcept { return dragging_; }
    [[nodiscard]] qreal dragOffset() const noexcept { return dragOffset_; }

    // Geometry, in widget coordinates. The slide area excludes the indicator
    // band; the band is empty when there is nothing to indicate.
    [[nodiscard]] QRectF slideArea() const;
    [[nodiscard]] QRectF indicatorBarRect() const;

    // The hit target of one indicator dot, in widget coordinates.
    [[nodiscard]] QRectF indicatorRect(int index) const;

signals:
    void currentChanged(int index);

    // Emitted when a mouse gesture starts and stops dragging the slides. A
    // click that never crosses the touch slop emits nothing.
    void dragStateChanged(bool dragging);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void onShown() override;
    void onHidden() override;

private:
    [[nodiscard]] int wrapIndex(int index) const;
    [[nodiscard]] bool indicatorsVisible() const;
    [[nodiscard]] int transitionDuration() const;
    // The signed distance, in slides, that the running transition travels.
    // Loop wraps take the short way round the cycle.
    [[nodiscard]] qreal transitionDelta() const;
    // Whether auto play has somewhere to go: more than one slide, and either
    // looping or not already sitting on the last slide.
    [[nodiscard]] bool canAutoAdvance() const;
    void syncVisibility();
    void syncAutoPlay();
    void stopTransitionTimer();
    void tickTransition();
    void handleActivation(int index);
    void pruneSlide(View* slide);

    // The drag gesture. canDragForward()/canDragBackward() are the elastic
    // limits: with loop off, the first and last slides damp instead of
    // travelling. The snap duration and curve come from the theme's medium1
    // motion spec, so a reduced-motion theme settles in one step.
    [[nodiscard]] bool draggingEnabled() const;
    [[nodiscard]] bool canDragForward() const;
    [[nodiscard]] bool canDragBackward() const;
    [[nodiscard]] qreal dampedDragOffset(qreal raw) const;
    [[nodiscard]] int neighborIndex(qreal offset) const;
    [[nodiscard]] int snapDurationMs() const;
    [[nodiscard]] qreal snapEasedProgress(qreal linear) const;
    void beginDragPress(const QPointF& position, quint64 timestamp);
    void beginDrag();
    void updateDrag(const QPointF& position, quint64 timestamp);
    void endDrag(const QPointF& position, quint64 timestamp);
    void beginSnap(int target);
    void resetDrag();

    QList<View*> slides_;
    int current_ = 0;
    int previous_ = 0;
    qreal progress_ = 1.0;
    bool loop_ = true;
    bool autoPlay_ = false;
    int intervalMs_ = 3000;
    int durationMs_ = 300;

    QTimer* transitionTimer_ = nullptr;
    QTimer* autoTimer_ = nullptr;

    QPointF pressed_{-1.0, -1.0};

    bool dragging_ = false;
    bool dragPressed_ = false;
    bool snapping_ = false;
    QPointF dragPressPosition_;
    QPointer<View> dragSlide_;
    qreal dragOffset_ = 0.0;
    qreal dragFromOffset_ = 0.0;
    qreal dragVelocity_ = 0.0;
    qreal dragLastX_ = 0.0;
    quint64 dragLastTimestamp_ = 0;
    int dragNeighbor_ = -1;
    qreal snapLinear_ = 0.0;
};

} // namespace md
