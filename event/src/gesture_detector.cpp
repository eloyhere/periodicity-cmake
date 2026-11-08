// Periodicity — gesture recognition implementation (framework.md §5.10).
//
// All timing comes from PointerEvent::timestamp(); the detector never reads a
// clock. A long press is therefore reported lazily, from the next move or
// release, as documented in gesture_detector.h.

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

bool GestureDetector::handle(const PointerEvent& event)
{
    switch (event.type()) {
    case EventType::PointerDown:
        begin(event);
        return true;
    case EventType::PointerMove:
        return handleMove(event);
    case EventType::PointerUp:
        return handleUp(event);
    default:
        return false;
    }
}

bool GestureDetector::handleMove(const PointerEvent& event)
{
    if (!m_pressed) {
        return false;
    }

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
    if (!m_pressed) {
        return false;
    }

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
}

void GestureDetector::reportLongPress(const PointerEvent& event)
{
    m_longPressReported = true;
    if (m_callbacks.onLongPress) {
        m_callbacks.onLongPress(event);
    }
}

void GestureDetector::reset()
{
    m_pressed = false;
    m_dragging = false;
    m_longPressReported = false;
    m_pressPosition = QPointF();
    m_pressTimestamp = std::chrono::milliseconds{};
}

bool GestureDetector::isDragging() const noexcept
{
    return m_dragging;
}

} // namespace md
