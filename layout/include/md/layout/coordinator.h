// Periodicity — Coordinator (framework.md §6.3, docs/backlog.md batch 4).
//
// Coordinator is a stacking container with one addition: a child may carry a
// CoordinatorBehaviour, an object that can react to a nested scroll. The
// container owns the behaviours, dispatches the four nested-scroll callbacks in
// registration order, and leaves the layout itself to the stacking rule the
// other containers use.
//
// The protocol is deliberately the Android one, minus the arguments Periodicity
// does not have yet:
//
//   startNestedScroll(axis)     -> any behaviour accepts?  (once per gesture)
//   dispatchNestedPreScroll()   -> behaviours consume before the content moves
//   dispatchNestedScroll()      -> behaviours observe what was left over
//   stopNestedScroll()          -> every behaviour is told, once
//
// Registration order *is* the dependency order: a behaviour that depends on
// another one must be registered after it. A behaviour also knows the child it
// was attached to and the coordinator that owns it, which is what lets it drive
// an AppBar or a Collapse without a global lookup.
//
// Behaviours are heap objects owned by the coordinator; a behaviour is never
// shared between children, and removing the child (or destroying it) removes
// the behaviour with it.

#pragma once

#include <md/view/view_group.h>

#include <QList>
#include <Qt>

#include <memory>
#include <vector>

namespace md {

class Coordinator;

// Base class for a scroll behaviour attached to one coordinator child. The
// defaults accept nothing and do nothing, so a behaviour implements only the
// callbacks it cares about.
class CoordinatorBehaviour
{
public:
    CoordinatorBehaviour() = default;
    virtual ~CoordinatorBehaviour();

    CoordinatorBehaviour(const CoordinatorBehaviour&) = delete;
    CoordinatorBehaviour& operator=(const CoordinatorBehaviour&) = delete;

    // Whether this behaviour takes part in a nested scroll on `axis`. Returning
    // true for at least one behaviour is what starts the gesture.
    virtual bool onStartNestedScroll(Qt::Orientation axis)
    {
        Q_UNUSED(axis);
        return false;
    }

    // Called before the scrolling content consumes `delta`. A behaviour adds
    // what it took to `consumed`; the remainder scrolls the content.
    virtual void onNestedPreScroll(const QPointF& delta, QPointF* consumed)
    {
        Q_UNUSED(delta);
        Q_UNUSED(consumed);
    }

    // Called with what the content consumed and what it could not consume, so
    // a behaviour can react to an overscroll.
    virtual void onNestedScroll(const QPointF& consumed, const QPointF& unconsumed)
    {
        Q_UNUSED(consumed);
        Q_UNUSED(unconsumed);
    }

    // Called once when the gesture ends.
    virtual void onStopNestedScroll() {}

    // Gives the behaviour a chance to place `child` inside `content`. The
    // default leaves the full-content rectangle alone, which is the stacking
    // rule; a collapsing app bar narrows it to the bar's current height.
    virtual void onLayoutChild(View* child, const QRectF& content, QRectF* geometry)
    {
        Q_UNUSED(child);
        Q_UNUSED(content);
        Q_UNUSED(geometry);
    }

    // The child this behaviour is attached to, or null when it is not attached.
    [[nodiscard]] View* child() const noexcept { return child_; }

    // The coordinator that owns this behaviour, or null while detached.
    [[nodiscard]] Coordinator* coordinator() const noexcept { return coordinator_; }

private:
    friend class Coordinator;

    View* child_ = nullptr;
    Coordinator* coordinator_ = nullptr;
};

// A stacking container that dispatches nested scrolls to its children's
// behaviours.
class Coordinator : public ViewGroup
{
    Q_OBJECT

public:
    explicit Coordinator(QWidget* parent = nullptr);
    ~Coordinator() override;

    // Attaches `behaviour` to `child` and returns it. The child must be a
    // member of this coordinator; a child that already has a behaviour keeps
    // its old one unless it is removed first.
    CoordinatorBehaviour* addBehaviour(View* child,
                                       std::unique_ptr<CoordinatorBehaviour> behaviour);

    [[nodiscard]] CoordinatorBehaviour* behaviour(View* child) const;

    // Detaches and destroys the behaviour of `child`. The child itself stays.
    bool removeBehaviour(View* child);

    // The behaviours in registration order, which is also dispatch order.
    [[nodiscard]] QList<CoordinatorBehaviour*> behaviours() const;

    // Starts a nested scroll. Returns true when at least one behaviour accepted
    // the axis, in which case the gesture is in progress and the dispatch
    // methods below do anything.
    bool startNestedScroll(Qt::Orientation axis);

    // Asks every behaviour to consume part of `delta` before the content
    // scrolls. `consumed` receives the sum and may be null.
    void dispatchNestedPreScroll(const QPointF& delta, QPointF* consumed);

    // Tells every behaviour what the content consumed and what it could not.
    void dispatchNestedScroll(const QPointF& consumed, const QPointF& unconsumed);

    void stopNestedScroll();

    // Re-places every child now. A behaviour whose child changed its size (a
    // collapsing app bar, for example) calls this instead of waiting for a
    // parent layout pass that may never come.
    void requestLayout();

    [[nodiscard]] bool isNestedScrollInProgress() const noexcept { return scrolling_; }

    // The axis of the gesture in progress; horizontal when none is.
    [[nodiscard]] Qt::Orientation nestedScrollAxis() const noexcept { return axis_; }

signals:
    void nestedScrollStarted(Qt::Orientation axis);
    void nestedScrollStopped();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    bool event(QEvent* event) override;

private:
    struct Entry
    {
        View* child = nullptr;
        std::unique_ptr<CoordinatorBehaviour> behaviour;
    };

    void forgetGoneChildren();

    std::vector<Entry> entries_;
    bool scrolling_ = false;
    bool rearranging_ = false;
    Qt::Orientation axis_ = Qt::Horizontal;
};

} // namespace md
