// Periodicity — event values (framework.md §5.10).
//
// An Event is a first-class value rather than a raw Qt event. It carries a
// type, the phase at which a handler sees it, an accepted flag, a
// stop-propagation flag and a timestamp. The dispatcher (event_dispatcher.h)
// owns the phase and target fields and calls resetForDispatch() before every
// admitted run, so a single instance can be reused for successive dispatches.
// A handler may re-dispatch the event it is handling: the dispatcher saves the
// outer pass's phase, target and flags and restores them when the inner pass
// returns, so neither pass observes the other's outcome. The target is cleared
// when the top-level dispatch returns, so a retained Event never names a
// target that may have been destroyed.
//
// Events are deliberately not QObjects: the module stays headless — no
// QtWidgets and no painting — so dispatch and gesture recognition can be
// tested without a window system.

#pragma once

#include <chrono>

namespace md {

// The stage of dispatch at which a handler sees the event. Capture runs from
// the root towards the target, Target delivers to the target itself and Bubble
// runs from the target back towards the root.
enum class EventPhase {
    Capture,
    Target,
    Bubble,
};

// The payload kind. Pointer, wheel, key and focus events share this one
// enumeration; the subclasses in pointer_event.h, wheel_event.h and
// key_event.h add the payload data.
enum class EventType {
    None,
    PointerDown,
    PointerUp,
    PointerMove,
    PointerEnter,
    PointerLeave,
    Wheel,
    KeyDown,
    KeyUp,
    FocusIn,
    FocusOut,
};

class EventTarget;

// Base class for every event value. An event starts unaccepted and with
// propagation running; handlers opt in to either flag.
class Event
{
public:
    explicit Event(EventType type, std::chrono::milliseconds timestamp = {});

    // Virtual so that events may be destroyed through an Event pointer.
    virtual ~Event();

    [[nodiscard]] EventType type() const noexcept;
    [[nodiscard]] EventPhase phase() const noexcept;

    // The node the dispatcher is delivering to as the dispatch target: the
    // end of the original chain, or the intercepting ancestor after a
    // retarget. Set by the dispatcher only; nullptr outside a dispatch and
    // nullptr again once the top-level dispatch returns.
    [[nodiscard]] EventTarget* target() const noexcept;

    [[nodiscard]] std::chrono::milliseconds timestamp() const noexcept;

    // Marks the event as handled. Acceptance suppresses the bubble but neither
    // the capture pass nor the delivery to the target.
    void accept() noexcept;
    void ignore() noexcept;
    [[nodiscard]] bool isAccepted() const noexcept;

    // Stops the dispatch immediately, whichever phase is running.
    void stopPropagation() noexcept;
    [[nodiscard]] bool isPropagationStopped() const noexcept;

    // Set by EventDispatcher only; public so that the dispatcher does not need
    // friendship. Application code must not call these.
    void setPhase(EventPhase phase) noexcept;
    void setTarget(EventTarget* target) noexcept;

    // Restores the state saved when a dispatch of this event re-entered
    // itself, so the outer pass resumes with its own phase, target and flags
    // instead of the inner pass's. Set by EventDispatcher only.
    void restoreDispatchState(EventPhase phase,
                              EventTarget* target,
                              bool accepted,
                              bool propagationStopped) noexcept;

protected:
    // Clears the forwarding state — accepted, propagation-stopped, phase and
    // target — before a new dispatch. Derived events override this to clear
    // their own bookkeeping and must call the base implementation first. A
    // re-entrant dispatch saves the outer state before calling this and
    // restores it when the inner pass ends.
    virtual void resetForDispatch();

private:
    // resetForDispatch() is protected, yet the dispatcher is the one component
    // that must call it; the friendship keeps the operation off the public API.
    friend class EventDispatcher;

    EventType m_type;
    EventPhase m_phase = EventPhase::Target;
    EventTarget* m_target = nullptr;
    std::chrono::milliseconds m_timestamp;
    bool m_accepted = false;
    bool m_propagationStopped = false;
};

} // namespace md
