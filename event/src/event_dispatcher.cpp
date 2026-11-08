// Periodicity — event dispatcher implementation (framework.md §5.10).
//
// The propagation rules and the pointer translation contract are documented in
// event_dispatcher.h; this file only implements them.

#include <md/event/event_dispatcher.h>

#include <md/event/event_target.h>
#include <md/event/pointer_event.h>

#include <QPointF>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace md {
namespace {

// A malformed parent graph must not hang a dispatch. The chain is capped at
// this many nodes and stops early when it revisits one.
constexpr std::size_t kMaxChainDepth = 1024;

bool finished(const Event& event) noexcept
{
    return event.isAccepted() || event.isPropagationStopped();
}

// Recomputes a pointer event's local position for one node. The point remains
// anchored to the original target's frame, so the capture and bubble passes
// agree for every node and repeated mapping does not accumulate error.
class PointerTranslator
{
public:
    explicit PointerTranslator(Event& event)
        : m_pointer(dynamic_cast<PointerEvent*>(&event))
    {
        if (m_pointer != nullptr) {
            m_origin = m_pointer->localPosition();
        }
    }

    // Restores the caller's coordinate frame when dispatch ends, whichever
    // exit path it takes. A reused PointerEvent therefore starts from the
    // position it was given rather than from the last handler's frame.
    ~PointerTranslator()
    {
        if (m_pointer != nullptr) {
            m_pointer->setLocalPosition(m_origin);
        }
    }

    PointerTranslator(const PointerTranslator&) = delete;
    PointerTranslator& operator=(const PointerTranslator&) = delete;

    void prepare(EventTarget& node, const EventTarget& origin) const
    {
        if (m_pointer == nullptr) {
            return;
        }

        if (&node == &origin) {
            m_pointer->setLocalPosition(m_origin);
        } else {
            m_pointer->setLocalPosition(node.mapPointFrom(origin, m_origin));
        }
    }

private:
    PointerEvent* m_pointer = nullptr;
    QPointF m_origin;
};

} // namespace

bool EventDispatcher::dispatch(Event& event, EventTarget& target)
{
    // One Event instance may be reused; forget the previous outcome first.
    event.resetForDispatch();

    // chain[0] is the delivery target and chain.back() is the root.
    std::vector<EventTarget*> chain;
    chain.reserve(16);
    for (EventTarget* node = &target; node != nullptr; node = node->parentTarget()) {
        if (chain.size() >= kMaxChainDepth) {
            break;
        }
        if (std::find(chain.begin(), chain.end(), node) != chain.end()) {
            break; // A cycle: the node is already on the chain.
        }
        chain.push_back(node);
    }

    EventTarget& origin = target;
    PointerTranslator translator(event);
    event.setTarget(&origin);

    // Capture: root -> the target's parent, excluding the target itself.
    for (std::size_t index = chain.size(); index-- > 1;) {
        EventTarget& node = *chain[index];
        event.setPhase(EventPhase::Capture);
        translator.prepare(node, origin);
        node.onEvent(event);
        if (event.isPropagationStopped()) {
            return finished(event);
        }
    }

    // Interception: root -> the target's parent. The first ancestor that
    // claims the gesture becomes the delivery target; the nodes between it and
    // the original target are skipped, but capture has already run.
    std::size_t deliveryIndex = 0;
    for (std::size_t index = chain.size(); index-- > 1;) {
        EventTarget& node = *chain[index];
        event.setPhase(EventPhase::Capture);
        translator.prepare(node, origin);
        if (node.onInterceptEvent(event)) {
            deliveryIndex = index;
            break;
        }
        if (event.isPropagationStopped()) {
            return finished(event);
        }
    }

    if (event.isPropagationStopped()) {
        return finished(event);
    }

    // Target: the original target, or the ancestor that intercepted it.
    EventTarget& deliveryTarget = *chain[deliveryIndex];
    event.setTarget(&deliveryTarget);
    event.setPhase(EventPhase::Target);
    translator.prepare(deliveryTarget, origin);
    deliveryTarget.onEvent(event);
    if (finished(event)) {
        return finished(event);
    }

    // Bubble: the delivery target's parent -> root. Stops at the first
    // accepted event or when propagation is stopped.
    for (std::size_t index = deliveryIndex + 1; index < chain.size(); ++index) {
        EventTarget& node = *chain[index];
        event.setPhase(EventPhase::Bubble);
        translator.prepare(node, origin);
        node.onEvent(event);
        if (finished(event)) {
            break;
        }
    }

    return finished(event);
}

} // namespace md
