// Periodicity — gesture recognition (framework.md §5.10).
//
// GestureDetector turns a pointer stream into tap, long-press, drag and cancel
// callbacks. It is deliberately timer-free: every decision is derived from the
// timestamps carried by the events, so a test can replay a whole gesture with
// arbitrary timings and get the same result every run.
//
// Consequently a long press cannot be reported at the instant its deadline
// passes. The detector notices it on the next PointerMove or PointerUp and
// reports onLongPress then; a caller that needs the callback exactly at the
// timeout must arm its own timer and feed the detector a synthetic move or up
// event (or drive a release).
//
// A press can also lose its release: the pointer leaves the window, the view
// loses focus, the platform drops events, or a second pointer appears. Such a
// gesture must not stay pressed forever, so handle() cancels it and reports
// onCancel when:
//   * a FocusOut or PointerLeave arrives while pressed;
//   * a pointer event that belongs to the gesture arrives after
//     GestureConfig::stalePressTimeout has elapsed without any pointer event
//     (a stalled stream);
//   * a second PointerDown arrives after the same stale deadline — before it,
//     the press is ignored and the gesture in progress is kept intact.
// Cancelling clears the gesture before onCancel runs, so a callback may feed
// the detector again.
//
// State machine (all timings from the event timestamps):
//   * PointerDown arms a candidate, recording its local position and time.
//   * Moving further than the slop, before the long-press deadline, starts a
//     drag: onDragStart runs once and onDrag follows with the total delta from
//     the press position.
//   * If the long-press deadline was reached before the pointer left the slop,
//     the move or release that reveals this reports onLongPress — even when
//     that event has itself moved beyond the slop, because the press had
//     already been held long enough. Later events only reset it.
//   * Releasing inside the slop before the deadline reports onTap.
//   * Releasing while dragging reports onDragEnd. A release that leaves the
//     slop without an intervening move is reported as a complete drag:
//     onDragStart, onDrag and onDragEnd in that order.
//
// handle() returns true when the event belongs to the active gesture and
// should be consumed. A PointerDown always starts a gesture (or is consumed as
// the second press of one); moves and releases are consumed only while a
// gesture is in progress. FocusOut and PointerLeave are notifications rather
// than gesture events, so they are never consumed even when they cancel a
// gesture.
//
// A callback that throws is contained: handle() abandons the gesture, resets
// the detector and returns false, so a broken callback cannot leave the state
// machine wedged or break the dispatch loop that called it.

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

    // A pressed gesture that has seen no pointer event for this long is
    // considered lost — its release was dropped. The next event cancels it
    // instead of reviving it. The deadline is measured between events, so a
    // continuous drag never goes stale; zero or a negative value disables it.
    std::chrono::milliseconds stalePressTimeout{5000};
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

        // The gesture was abandoned rather than completed. The argument is the
        // event that revealed the loss (a FocusOut, a PointerLeave, a stale
        // pointer event or a replacing press), so a caller can tell them apart.
        std::function<void(const Event&)> onCancel;
    };

    GestureDetector(Callbacks callbacks, GestureConfig config = {}, Density density = {});

    // Feeds one event to the recogniser.
    // Returns true when the event belongs to an active gesture and should be
    // consumed.
    bool handle(const Event& event);

    // Abandons the current gesture without firing a callback. Callbacks and
    // configuration are retained.
    void reset();

    [[nodiscard]] bool isDragging() const noexcept;

private:
    bool handleDown(const Event& event);
    bool handleMoveEvent(const Event& event);
    bool handleUpEvent(const Event& event);
    bool handleMove(const PointerEvent& event);
    bool handleUp(const PointerEvent& event);
    void begin(const PointerEvent& event);
    void reportLongPress(const PointerEvent& event);
    void cancel(const Event& event);
    [[nodiscard]] bool isStale(std::chrono::milliseconds now) const noexcept;

    Callbacks m_callbacks;
    GestureConfig m_config;
    qreal m_slop = 8.0;

    bool m_pressed = false;
    bool m_dragging = false;
    bool m_longPressReported = false;
    QPointF m_pressPosition;
    std::chrono::milliseconds m_pressTimestamp{};
    std::chrono::milliseconds m_lastTimestamp{};
};

} // namespace md
