// Periodicity — Coordinator (framework.md §6.3).
//
// The container owns its behaviours and dispatches the nested-scroll protocol
// to them in registration order. Dispatch is a plain loop: a behaviour that
// depends on another one is registered after it, which is the whole dependency
// model this batch needs.

#include <md/layout/coordinator.h>

#include <md/layout/alignment.h>

#include <QEvent>
#include <QPointF>

namespace md {

CoordinatorBehaviour::~CoordinatorBehaviour() = default;

Coordinator::Coordinator(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setBackgroundColour(QColor(Qt::transparent));
}

Coordinator::~Coordinator()
{
    // Detach the behaviours before the base class tears down the widget tree, so
    // a behaviour that is being destroyed cannot reach back into a half-dead
    // coordinator.
    for (Entry& entry : entries_) {
        if (entry.behaviour != nullptr) {
            entry.behaviour->child_ = nullptr;
            entry.behaviour->coordinator_ = nullptr;
        }
    }
    entries_.clear();
}

CoordinatorBehaviour* Coordinator::addBehaviour(View* child,
                                               std::unique_ptr<CoordinatorBehaviour> owned)
{
    if (child == nullptr || owned == nullptr || !views().contains(child))
        return nullptr;

    // One behaviour per child: an already attached child keeps the one it has.
    if (CoordinatorBehaviour* existing = behaviour(child))
        return existing;

    owned->child_ = child;
    owned->coordinator_ = this;

    CoordinatorBehaviour* result = owned.get();
    entries_.push_back(Entry{child, std::move(owned)});

    // A child destroyed behind the coordinator's back must not leave a stale
    // entry. The connection is tolerant of a missing entry, which is what makes
    // removeBehaviour() able to drop the behaviour without disconnecting it.
    QObject::connect(child, &QObject::destroyed, this, [this, child] {
        std::erase_if(entries_, [child](const Entry& entry) { return entry.child == child; });
    });

    return result;
}

CoordinatorBehaviour* Coordinator::behaviour(View* child) const
{
    for (const Entry& entry : entries_) {
        if (entry.child == child)
            return entry.behaviour.get();
    }
    return nullptr;
}

bool Coordinator::removeBehaviour(View* child)
{
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->child != child)
            continue;

        it->behaviour->child_ = nullptr;
        it->behaviour->coordinator_ = nullptr;
        entries_.erase(it);
        return true;
    }
    return false;
}

QList<CoordinatorBehaviour*> Coordinator::behaviours() const
{
    QList<CoordinatorBehaviour*> result;
    result.reserve(static_cast<qsizetype>(entries_.size()));

    for (const Entry& entry : entries_) {
        if (entry.behaviour != nullptr)
            result.append(entry.behaviour.get());
    }

    return result;
}

bool Coordinator::startNestedScroll(Qt::Orientation axis)
{
    // A second start while a gesture is running belongs to the same gesture.
    if (scrolling_)
        return true;

    forgetGoneChildren();

    bool accepted = false;
    for (Entry& entry : entries_) {
        if (entry.behaviour != nullptr && entry.behaviour->onStartNestedScroll(axis))
            accepted = true;
    }

    if (!accepted)
        return false;

    scrolling_ = true;
    axis_ = axis;
    emit nestedScrollStarted(axis);
    return true;
}

void Coordinator::dispatchNestedPreScroll(const QPointF& delta, QPointF* consumed)
{
    if (!scrolling_) {
        if (consumed != nullptr)
            *consumed = QPointF();
        return;
    }

    QPointF total;
    for (Entry& entry : entries_) {
        if (entry.behaviour != nullptr)
            entry.behaviour->onNestedPreScroll(delta, &total);
    }

    if (consumed != nullptr)
        *consumed = total;
}

void Coordinator::dispatchNestedScroll(const QPointF& consumed, const QPointF& unconsumed)
{
    if (!scrolling_)
        return;

    for (Entry& entry : entries_) {
        if (entry.behaviour != nullptr)
            entry.behaviour->onNestedScroll(consumed, unconsumed);
    }
}

void Coordinator::stopNestedScroll()
{
    if (!scrolling_)
        return;

    scrolling_ = false;

    for (Entry& entry : entries_) {
        if (entry.behaviour != nullptr)
            entry.behaviour->onStopNestedScroll();
    }

    emit nestedScrollStopped();
}

void Coordinator::requestLayout()
{
    updateGeometry();
    if (rect().isValid())
        arrangeChildren(contentRect());
}

void Coordinator::measureOverride(MeasureSpec width, MeasureSpec height)
{
    forgetGoneChildren();

    qreal naturalWidth = 0.0;
    qreal naturalHeight = 0.0;

    for (View* child : views()) {
        if (child == nullptr || child->isHidden())
            continue;

        child->measure(layout_detail::capTo(width), layout_detail::capTo(height));
        naturalWidth = qMax(naturalWidth, child->measuredSize().width());
        naturalHeight = qMax(naturalHeight, child->measuredSize().height());
    }

    setMeasuredSize(QSizeF(width.resolve(naturalWidth), height.resolve(naturalHeight)));
}

void Coordinator::arrangeChildren(const QRectF& content)
{
    const qreal width = qMax(qreal(0.0), content.width());
    const qreal height = qMax(qreal(0.0), content.height());

    // A coordinator stacks its children, exactly like a Frame without gravity:
    // the app bar, the collapsing header and the scrolled content all share one
    // rectangle. A behaviour may narrow that rectangle for its own child — the
    // AppBarBehaviour uses it to keep the bar at the bar's height.
    for (View* child : views()) {
        if (child == nullptr || child->isHidden())
            continue;

        child->measure(MeasureSpec::atMost(width), MeasureSpec::atMost(height));

        QRectF geometry(content.left(), content.top(), width, height);
        if (CoordinatorBehaviour* attached = behaviour(child); attached != nullptr)
            attached->onLayoutChild(child, content, &geometry);

        geometry.setWidth(qBound(qreal(0.0), geometry.width(), width));
        geometry.setHeight(qBound(qreal(0.0), geometry.height(), height));

        child->measure(MeasureSpec::exactly(geometry.width()),
                       MeasureSpec::exactly(geometry.height()));
        child->arrange(geometry.toAlignedRect());
    }
}

bool Coordinator::event(QEvent* event)
{
    const bool accepted = ViewGroup::event(event);

    // updateGeometry() on a child posts a LayoutRequest here, which is what
    // makes a collapsing app bar change its height while the page scrolls.
    if (event->type() == QEvent::LayoutRequest && !rearranging_ && rect().isValid()) {
        rearranging_ = true;
        arrangeChildren(contentRect());
        rearranging_ = false;
    }

    return accepted;
}

void Coordinator::forgetGoneChildren()
{
    if (entries_.empty())
        return;

    const QList<View*> members = views();
    std::erase_if(entries_, [&members](const Entry& entry) {
        return !members.contains(entry.child);
    });
}

} // namespace md
