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
// Positions: dispatch begins with the event position expressed in the
// original target's coordinate system — PointerEvent::localPosition() or
// WheelEvent::position(). Before delivering to each handler the dispatcher
// recomputes it with `node.mapPointFrom(originalTarget, originalPosition)`, so
// every handler sees the position in its own coordinates and the capture and
// bubble passes agree. The original target itself receives the caller's
// position unchanged, and the caller's position is restored when dispatch ends
// so the same event can be reused.
//
// Event::resetForDispatch() is called at the start of every admitted run; a
// re-entrant pass saves the outer state first (see below).
// Re-entrant dispatch is supported: a handler may dispatch the event it is
// handling, even to a different target. The outer pass saves its phase, target
// and flags before the inner pass resets them and restores them when the inner
// pass returns, so neither pass observes the other's outcome. Nesting is
// capped at 16 passes; a dispatch beyond that cap is refused without touching
// the event and returns the flags the event already carries.
//
// Lifetime: EventTarget is not a QObject, so the chain cannot hold a QPointer.
// Every target carries a shared liveness token instead (event_target.h), and
// the chain keeps a copy. When a handler destroys a node — including the
// delivery target or the original target — dispatch aborts before the next
// delivery, and Event::target() is detached from the destroyed node. A
// top-level dispatch clears Event::target() when it returns, so a retained
// Event never names a target that may since have been destroyed.
//
// Handlers may throw. Every onEvent()/onInterceptEvent() invocation is wrapped
// in a catch-all: a throwing handler is treated as if it had returned false
// without accepting or stopping anything, and the remaining passes still run.
// A throwing handler therefore cannot break the chain or leave the dispatcher
// wedged.
//
// The dispatcher is not thread-safe; targets and dispatch belong to the GUI
// thread.

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
