// Periodicity — event target contract (framework.md §5.10).
//
// Anything that can receive a dispatched event implements EventTarget. The
// two graph queries let the dispatcher build the chain without knowing the
// visual tree: parentTarget() names the next node towards the root and
// mapPointFrom() converts pointer positions between nodes.

#pragma once

#include <QPointF>

namespace md {

class Event;

class EventTarget
{
public:
    virtual ~EventTarget() = default;

    // The next target towards the root, or nullptr for a root. The dispatcher
    // caps the resulting chain, so a malformed graph cannot loop forever.
    [[nodiscard]] virtual EventTarget* parentTarget() const noexcept = 0;

    // Translates `point`, expressed in `source`'s coordinate system, into this
    // target's coordinate system. The dispatcher calls it before delivering a
    // PointerEvent, always with the original dispatch target as `source`.
    [[nodiscard]] virtual QPointF mapPointFrom(const EventTarget& source,
                                               const QPointF& point) const = 0;

    // Gives an ancestor the chance to claim a gesture before its descendants
    // see it, mirroring Android's onInterceptTouchEvent. Returning true makes
    // this target the delivery target. The default does not intercept.
    virtual bool onInterceptEvent(Event& /*event*/) { return false; }

    // Handles the event in the current phase. The return value is advisory;
    // acceptance is expressed through Event::accept().
    virtual bool onEvent(Event& event) = 0;
};

} // namespace md
