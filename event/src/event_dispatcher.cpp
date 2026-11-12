// Periodicity — event dispatcher implementation (framework.md §5.10).
//
// The propagation rules, the pointer/wheel translation contract, the
// re-entrancy rule and the target-lifetime rule are documented in
// event_dispatcher.h; this file only implements them.

#include <md/event/event_dispatcher.h>

#include <md/event/event_target.h>
#include <md/event/pointer_event.h>
#include <md/event/wheel_event.h>

#include <QVariant>

#include <QPointF>
#include <QCoreApplication>
#include <QDebug>
#include <QThread>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

namespace md {
namespace {

// A malformed parent graph must not hang a dispatch. The chain is capped at
// this many nodes and stops early when it revisits one.
constexpr std::size_t kMaxChainDepth = 1024;

// A handler that dispatches the event it is handling nests one pass deeper.
// The cap turns an unbounded re-entry into a bounded one instead of a stack
// overflow; a dispatch beyond it is refused (see dispatch()).
constexpr std::size_t kMaxDispatchNesting = 16;

// The dispatches running on this thread, outermost first. The nesting cap and
// the re-entrancy save/restore are per-thread because dispatch is a GUI-thread
// operation.
thread_local std::vector<Event*> t_activeDispatches;

bool finished(const Event& event) noexcept
{
    return event.isAccepted() || event.isPropagationStopped();
}

// One node of the propagation chain. `target` is only dereferenced while
// `lifetime` says it is alive; the shared token is the QPointer equivalent for
// the non-QObject EventTarget hierarchy (see event_target.h). A handler may
// destroy any node, and its token then outlives it.
struct ChainNode
{
    EventTarget* target = nullptr;
    std::shared_ptr<const detail::TargetLifetime> lifetime;

    [[nodiscard]] bool alive() const noexcept
    {
        return lifetime != nullptr && lifetime->isAlive();
    }
};

// True while every node on the chain is still alive. A node that a handler
// destroyed is also detached from the event when it was the current delivery
// target, so no later handler observes a dangling pointer.
bool chainIntact(const std::vector<ChainNode>& chain, Event& event) noexcept
{
    bool intact = true;
    for (const ChainNode& node : chain) {
        if (node.alive()) {
            continue;
        }
        intact = false;
        if (event.target() == node.target) {
            event.setTarget(nullptr);
        }
    }
    return intact;
}

// Handlers may throw anything. The dispatcher contains the exception here: a
// broken handler is treated as one that returned false without accepting or
// stopping the event, so the remaining passes still run.
void deliver(EventTarget& node, Event& event) noexcept
{
    try {
        node.onEvent(event);
    } catch (...) {
        // Intentionally swallowed; see event_dispatcher.h.
    }
}

bool interceptClaimed(EventTarget& node, Event& event) noexcept
{
    try {
        return node.onInterceptEvent(event);
    } catch (...) {
        return false;
    }
}

// Recomputes the event position for one node. The point stays anchored to the
// original target's frame, so the capture and bubble passes agree for every
// node and repeated mapping does not accumulate error. Both PointerEvent and
// WheelEvent positions are per-target, so both are translated; the caller's
// position is restored when dispatch ends, whichever exit path it takes.
class PositionTranslator
{
public:
    explicit PositionTranslator(Event& event)
    {
        if (auto* pointer = dynamic_cast<PointerEvent*>(&event)) {
            m_pointer = pointer;
            m_origin = pointer->localPosition();
        } else if (auto* wheel = dynamic_cast<WheelEvent*>(&event)) {
            m_wheel = wheel;
            m_origin = wheel->position();
        }
    }

    ~PositionTranslator()
    {
        if (m_pointer != nullptr) {
            m_pointer->setLocalPosition(m_origin);
        } else if (m_wheel != nullptr) {
            m_wheel->setPosition(m_origin);
        }
    }

    PositionTranslator(const PositionTranslator&) = delete;
    PositionTranslator& operator=(const PositionTranslator&) = delete;

    void prepare(EventTarget& node, const EventTarget& origin) const
    {
        if (m_pointer == nullptr && m_wheel == nullptr) {
            return;
        }

        const QPointF position = (&node == &origin)
                                     ? m_origin
                                     : node.mapPointFrom(origin, m_origin);
        if (m_pointer != nullptr) {
            m_pointer->setLocalPosition(position);
        } else {
            m_wheel->setPosition(position);
        }
    }

private:
    PointerEvent* m_pointer = nullptr;
    WheelEvent* m_wheel = nullptr;
    QPointF m_origin;
};

// Saves the outer pass's phase, target and flags when a handler re-dispatches
// the event it is handling, and restores them when the inner pass returns. A
// top-level pass instead clears the target on exit, so a retained Event never
// names a target that may have been destroyed.
class DispatchScope
{
public:
    explicit DispatchScope(Event& event)
        : m_event(event)
        , m_reentrant(std::find(t_activeDispatches.cbegin(), t_activeDispatches.cend(), &event)
                      != t_activeDispatches.cend())
    {
        if (m_reentrant) {
            m_phase = event.phase();
            m_target = event.target();
            m_accepted = event.isAccepted();
            m_stopped = event.isPropagationStopped();
        }
        t_activeDispatches.push_back(&event);
    }

    DispatchScope(const DispatchScope&) = delete;
    DispatchScope& operator=(const DispatchScope&) = delete;

    ~DispatchScope()
    {
        t_activeDispatches.pop_back();
        if (m_reentrant) {
            m_event.restoreDispatchState(m_phase, m_target, m_accepted, m_stopped);
        } else {
            m_event.setTarget(nullptr);
        }
    }

private:
    Event& m_event;
    bool m_reentrant = false;
    EventPhase m_phase = EventPhase::Target;
    EventTarget* m_target = nullptr;
    bool m_accepted = false;
    bool m_stopped = false;
};

} // namespace

bool EventDispatcher::dispatch(Event& event, EventTarget& target)
{
    // EventThread hands its decoded input batch to the UI thread, where this
    // dispatcher runs in the layout phase. A dispatch that arrives from another
    // thread while that runtime is installed would touch widgets off the UI
    // thread, so it is refused before the event or the target chain is touched.
    // The marker is a Qt application property, which keeps this headless module
    // from depending on schedule (or on any UI type).
    if (QCoreApplication* app = QCoreApplication::instance();
        app != nullptr && app->property("md.schedule.eventThreadInstalled").toBool()
        && QThread::currentThread() != app->thread()) {
        qWarning("EventDispatcher::dispatch refused on a non-UI thread while EventThread is "
                 "installed");
        return false;
    }

    // Re-entry is allowed, but bounded: after the cap the extra pass is
    // refused without touching the event, which lets the stack unwind.
    if (t_activeDispatches.size() >= kMaxDispatchNesting) {
        return finished(event);
    }

    // Constructed before resetForDispatch() so a re-entrant pass saves the
    // outer pass's state, not the clean one.
    DispatchScope scope(event);
    event.resetForDispatch();

    // chain[0] is the delivery target and chain.back() is the root. The chain
    // is a snapshot; each node carries its target's liveness token so a
    // destruction from a handler is detected before the next delivery.
    std::vector<ChainNode> chain;
    chain.reserve(16);
    for (EventTarget* node = &target; node != nullptr; node = node->parentTarget()) {
        if (chain.size() >= kMaxChainDepth) {
            break;
        }
        const bool revisited = std::any_of(chain.cbegin(),
                                           chain.cend(),
                                           [node](const ChainNode& entry) {
                                               return entry.target == node;
                                           });
        if (revisited) {
            break; // A cycle: the node is already on the chain.
        }
        chain.push_back(ChainNode{node, node->lifetimeToken()});
    }

    EventTarget& origin = target;
    PositionTranslator translator(event);
    event.setTarget(&origin);

    // Capture: root -> the target's parent, excluding the target itself.
    for (std::size_t index = chain.size(); index-- > 1;) {
        if (!chain[index].alive()) {
            return finished(event);
        }
        EventTarget& node = *chain[index].target;
        event.setPhase(EventPhase::Capture);
        translator.prepare(node, origin);
        deliver(node, event);
        if (!chainIntact(chain, event) || event.isPropagationStopped()) {
            return finished(event);
        }
    }

    // Interception: root -> the target's parent. The first ancestor that
    // claims the gesture becomes the delivery target; the nodes between it and
    // the original target are skipped, but capture has already run.
    std::size_t deliveryIndex = 0;
    for (std::size_t index = chain.size(); index-- > 1;) {
        if (!chain[index].alive()) {
            return finished(event);
        }
        EventTarget& node = *chain[index].target;
        event.setPhase(EventPhase::Capture);
        translator.prepare(node, origin);
        if (interceptClaimed(node, event)) {
            deliveryIndex = index;
            break;
        }
        if (!chainIntact(chain, event) || event.isPropagationStopped()) {
            return finished(event);
        }
    }

    if (!chainIntact(chain, event)
        || !chain[deliveryIndex].alive()
        || event.isPropagationStopped()) {
        return finished(event);
    }

    // Target: the original target, or the ancestor that intercepted it.
    EventTarget& deliveryTarget = *chain[deliveryIndex].target;
    event.setTarget(&deliveryTarget);
    event.setPhase(EventPhase::Target);
    translator.prepare(deliveryTarget, origin);
    deliver(deliveryTarget, event);
    if (!chainIntact(chain, event) || finished(event)) {
        return finished(event);
    }

    // Bubble: the delivery target's parent -> root. Stops at the first
    // accepted event or when propagation is stopped.
    for (std::size_t index = deliveryIndex + 1; index < chain.size(); ++index) {
        if (!chain[index].alive()) {
            return finished(event);
        }
        EventTarget& node = *chain[index].target;
        event.setPhase(EventPhase::Bubble);
        translator.prepare(node, origin);
        deliver(node, event);
        if (!chainIntact(chain, event)) {
            return finished(event);
        }
        if (finished(event)) {
            break;
        }
    }

    return finished(event);
}

} // namespace md
