// Periodicity — gesture recognition (framework.md §5.10).
//
// GestureDetector turns a pointer stream into tap, long-press and drag
// callbacks. It is deliberately timer-free: every decision is derived from the
// timestamps carried by PointerEvent, so a test can replay a whole gesture
// with arbitrary timings and get the same result every run.
//
// Consequently a long press cannot be reported at the instant its deadline
// passes. The detector notices it on the next PointerMove or PointerUp and
// reports onLongPress then; a caller that needs the callback exactly at the
// timeout must arm its own timer and feed the detector a synthetic move or up
// event (or drive a release). A held press whose release never arrives
// produces no callback at all.
//
// State machine (all timings from PointerEvent::timestamp()):
//   * PointerDown arms a candidate, recording its local position and time.
//   * Moving further than the slop, before the long-press deadline, starts a
//     drag: onDragStart runs once and onDrag follows with the total delta from
//     the press position.
//   * Reaching the long-press deadline while still inside the slop reports
//     onLongPress from that move or up event; later events only reset it.
//   * Releasing inside the slop before the deadline reports onTap.
//   * Releasing while dragging reports onDragEnd.
//
// handle() returns true when the event belongs to the active gesture and
// should be consumed. PointerDown always starts a gesture; moves and releases
// are consumed only while one is in progress.

#pragma once

#include <md/core/density.h>
#include <md/event/pointer_event.h>

#include <chrono>
#include <functional>

namespace md {

struct GestureConfig
{
    // Maximum travel, in dp, that still counts as a press rather than a drag.
    qreal touchSlopDp = 8.0;

    // How long a press must be held, without leaving the slop, before it
    // becomes a long press.
    std::chrono::milliseconds longPressTimeout{500};

    // Reserved for multi-tap disambiguation. The single-tap rule is defined by
    // the slop and the long-press deadline, so this detector does not consult
    // the value yet; it is kept in the configuration for the multi-tap
    // recogniser that will extend the gesture set.
    std::chrono::milliseconds tapTimeout{100};
};

class GestureDetector
{
public:
    struct Callbacks
    {
        std::function<void(const PointerEvent&)> onTap;
        std::function<void(const PointerEvent&)> onLongPress;
        std::function<void(const PointerEvent&, QPointF totalDelta)> onDrag;
        std::function<void(const PointerEvent&)> onDragEnd;
        std::function<void(const PointerEvent&)> onDragStart;
    };

    GestureDetector(Callbacks callbacks, GestureConfig config = {}, Density density = {});

    // Feeds one pointer event to the recogniser.
    // Returns true when the event belongs to an active gesture and should be
    // consumed.
    bool handle(const PointerEvent& event);

    // Abandons the current gesture without firing a callback. Callbacks and
    // configuration are retained.
    void reset();

    [[nodiscard]] bool isDragging() const noexcept;

private:
    void begin(const PointerEvent& event);
    bool handleMove(const PointerEvent& event);
    bool handleUp(const PointerEvent& event);
    void reportLongPress(const PointerEvent& event);

    Callbacks m_callbacks;
    GestureConfig m_config;
    qreal m_slop = 8.0;

    bool m_pressed = false;
    bool m_dragging = false;
    bool m_longPressReported = false;
    QPointF m_pressPosition;
    std::chrono::milliseconds m_pressTimestamp{};
};

} // namespace md
