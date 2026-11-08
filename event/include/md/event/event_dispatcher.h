// Periodicity — event dispatcher (framework.md §5.10).
//
// The dispatcher routes one Event through a chain of EventTargets built by
// following parentTarget() from the delivery target to the root. A single
// dispatch runs four stages:
//
//   1. Capture    — onEvent(EventPhase::Capture) from the root down to the
//                   target's parent. The target never sees this phase.
//   2. Intercept  — onInterceptEvent from the root down to the target's
//                   parent, in capture phase. The first target returning true
//                   claims the gesture: it becomes the delivery target, its
//                   onEvent(EventPhase::Target) runs, and the descendants
//                   below it are skipped.
//   3. Target     — onEvent(EventPhase::Target) on the delivery target.
//   4. Bubble     — onEvent(EventPhase::Bubble) from the delivery target's
//                   parent up to the root, stopping at the first accepted
//                   event or when propagation is stopped.
//
// stopPropagation() is absolute: it ends capture, interception, target and
// bubble alike. accept() only suppresses the bubble, so a capture handler may
// accept and the target will still receive the event; no ancestor receives it
// afterwards.
//
// A malformed parent graph is made finite: the chain walk stops after 1024
// nodes and also stops as soon as it revisits a target (a cycle).
//
// Pointer positions: dispatch begins with the PointerEvent's local position
// expressed in the original target's coordinate system. Before delivering to
// each handler the dispatcher recomputes it with
// `node.mapPointFrom(originalTarget, originalPosition)`, so every handler sees
// the point in its own coordinates and the capture and bubble passes agree.
// The original target itself receives the caller's position unchanged, and the
// caller's position is restored when dispatch ends so the same PointerEvent
// can be reused.
//
// Event::resetForDispatch() is called first, so one Event instance may be
// reused for successive dispatches.

#pragma once

#include <md/event/event.h>

namespace md {

class EventDispatcher
{
public:
    // Returns true when the event ended accepted or propagation was stopped,
    // false when it traversed the whole chain without either.
    static bool dispatch(Event& event, EventTarget& target);
};

} // namespace md
