// Periodicity — gesture recognition implementation (framework.md §5.10).
//
// All timing comes from the event timestamps; the detector never reads a
// clock. A long press is therefore reported lazily, from the next move or
// release, and a lost press is only noticed when the next event arrives. Both
// rules are documented in gesture_detector.h.

#include <md/event/gesture_detector.h>

#include <QPointF>

#include <cmath>
#include <utility>

namespace md {
namespace {

std::chrono::milliseconds elapsedSince(std::chrono::milliseconds start,
                                       std::chrono::milliseconds now) noexcept
{
    // Timestamps are expected to be monotonic; a misordered pair is treated as
    // no elapsed time rather than as a negative duration.
    return now > start ? now - start : std::chrono::milliseconds{0};
}

qreal distanceBetween(const QPointF& from, const QPointF& to) noexcept
{
    const qreal dx = to.x() - from.x();
    const qreal dy = to.y() - from.y();
    return std::hypot(dx, dy);
}

} // namespace

GestureDetector::GestureDetector(Callbacks callbacks, GestureConfig config, Density density)
    : m_callbacks(std::move(callbacks))
    , m_config(config)
    , m_slop(density.dp(config.touchSlopDp))
{
}

bool GestureDetector::handle(const Event& event)
{
    try {
        switch (event.type()) {
        case EventType::PointerDown:
            return handleDown(event);
        case EventType::PointerMove:
            return handleMoveEvent(event);
        case EventType::PointerUp:
            return handleUpEvent(event);
        case EventType::PointerLeave:
        case EventType::FocusOut:
            // Losing the pointer or the focus abandons the gesture; the
            // notification itself is not part of it and is not consumed.
            if (m_pressed) {
                cancel(event);
            }
            return false;
        default:
            return false;
        }
    } catch (...) {
        // A throwing callback must not leave a half-updated gesture behind.
        reset();
        return false;
    }
}

bool GestureDetector::handleDown(const Event& event)
{
    const auto* pointer = dynamic_cast<const PointerEvent*>(&event);
    if (pointer == nullptr) {
        return false;
    }

    if (m_pressed) {
        if (!isStale(pointer->timestamp())) {
            // Single-pointer detector: a second press must not overwrite the
            // press position or the drag state of the gesture in progress.
            // Consume it so the caller does not start a competing gesture.
            return true;
        }
        cancel(event); // A lost release: report it before starting again.
    }

    begin(*pointer);
    return true;
}

bool GestureDetector::handleMoveEvent(const Event& event)
{
    const auto* pointer = dynamic_cast<const PointerEvent*>(&event);
    if (pointer == nullptr || !m_pressed) {
        return false;
    }

    if (isStale(pointer->timestamp())) {
        cancel(event);
        return false; // A hover move, not the tail of the lost gesture.
    }

    m_lastTimestamp = pointer->timestamp();
    return handleMove(*pointer);
}

bool GestureDetector::handleUpEvent(const Event& event)
{
    const auto* pointer = dynamic_cast<const PointerEvent*>(&event);
    if (pointer == nullptr || !m_pressed) {
        return false;
    }

    if (isStale(pointer->timestamp())) {
        cancel(event);
        return false;
    }

    m_lastTimestamp = pointer->timestamp();
    return handleUp(*pointer);
}

bool GestureDetector::handleMove(const PointerEvent& event)
{
    const QPointF delta = event.localPosition() - m_pressPosition;

    if (m_dragging) {
        if (m_callbacks.onDrag) {
            m_callbacks.onDrag(event, delta);
        }
        return true;
    }

    if (m_longPressReported) {
        return true;
    }

    const auto held = elapsedSince(m_pressTimestamp, event.timestamp());

    if (distanceBetween(m_pressPosition, event.localPosition()) > m_slop) {
        // The press survived until the long-press deadline before leaving the
        // slop, so it is a long press rather than a late drag.
        if (held >= m_config.longPressTimeout) {
            reportLongPress(event);
        } else {
            m_dragging = true;
            if (m_callbacks.onDragStart) {
                m_callbacks.onDragStart(event);
            }
            if (m_callbacks.onDrag) {
                m_callbacks.onDrag(event, delta);
            }
        }
        return true;
    }

    if (held >= m_config.longPressTimeout) {
        reportLongPress(event);
    }
    return true;
}

bool GestureDetector::handleUp(const PointerEvent& event)
{
    const QPointF delta = event.localPosition() - m_pressPosition;

    if (m_dragging) {
        if (m_callbacks.onDragEnd) {
            m_callbacks.onDragEnd(event);
        }
        reset();
        return true;
    }

    if (m_longPressReported) {
        reset();
        return true;
    }

    const auto held = elapsedSince(m_pressTimestamp, event.timestamp());
    const qreal distance = distanceBetween(m_pressPosition, event.localPosition());

    if (distance > m_slop && held < m_config.longPressTimeout) {
        // A release outside the slop without an intervening move is still a
        // drag: report start, travel and end in order.
        m_dragging = true;
        if (m_callbacks.onDragStart) {
            m_callbacks.onDragStart(event);
        }
        if (m_callbacks.onDrag) {
            m_callbacks.onDrag(event, delta);
        }
        m_dragging = false;
        if (m_callbacks.onDragEnd) {
            m_callbacks.onDragEnd(event);
        }
        reset();
        return true;
    }

    if (held >= m_config.longPressTimeout) {
        reportLongPress(event);
    } else if (distance <= m_slop) {
        if (m_callbacks.onTap) {
            m_callbacks.onTap(event);
        }
    }

    reset();
    return true;
}

void GestureDetector::begin(const PointerEvent& event)
{
    m_pressed = true;
    m_dragging = false;
    m_longPressReported = false;
    m_pressPosition = event.localPosition();
    m_pressTimestamp = event.timestamp();
    m_lastTimestamp = event.timestamp();
}

void GestureDetector::reportLongPress(const PointerEvent& event)
{
    m_longPressReported = true;
    if (m_callbacks.onLongPress) {
        m_callbacks.onLongPress(event);
    }
}

void GestureDetector::cancel(const Event& event)
{
    const bool wasPressed = m_pressed;
    // Clear the state before the callback so a callback may feed the detector
    // again without observing a half-cancelled gesture.
    reset();
    if (wasPressed && m_callbacks.onCancel) {
        m_callbacks.onCancel(event);
    }
}

bool GestureDetector::isStale(std::chrono::milliseconds now) const noexcept
{
    if (!m_pressed) {
        return false;
    }
    if (m_config.stalePressTimeout <= std::chrono::milliseconds{0}) {
        return false; // Disabled.
    }
    return elapsedSince(m_lastTimestamp, now) >= m_config.stalePressTimeout;
}

void GestureDetector::reset()
{
    m_pressed = false;
    m_dragging = false;
    m_longPressReported = false;
    m_pressPosition = QPointF();
    m_pressTimestamp = std::chrono::milliseconds{};
    m_lastTimestamp = std::chrono::milliseconds{};
}

bool GestureDetector::isDragging() const noexcept
{
    return m_dragging;
}

} // namespace md
