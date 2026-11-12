// Periodicity — wheel event payload (framework.md §5.10).
//
// Angle deltas are in eighths of a degree, exactly as Qt reports them; pixel
// deltas are non-zero only on platforms with high-resolution wheels. A wheel
// event is always of type EventType::Wheel, so the constructor does not take a
// type.

#pragma once

#include <md/event/event.h>

#include <QPoint>
#include <QPointF>
#include <Qt>

#include <chrono>

namespace md {

class WheelEvent : public Event
{
public:
    WheelEvent(QPoint angleDelta,
               QPoint pixelDelta,
               QPointF position,
               Qt::KeyboardModifiers modifiers,
               std::chrono::milliseconds timestamp = {});

    [[nodiscard]] QPoint angleDelta() const noexcept;
    [[nodiscard]] QPoint pixelDelta() const noexcept;

    // The wheel position in the receiving target's coordinate system. The
    // dispatcher translates it per node, exactly as it does
    // PointerEvent::localPosition(), and restores the caller's position when
    // dispatch ends.
    [[nodiscard]] QPointF position() const noexcept;
    [[nodiscard]] Qt::KeyboardModifiers modifiers() const noexcept;

    // Set by the dispatcher when it translates the event between targets.
    void setPosition(QPointF position) noexcept;

private:
    QPoint m_angleDelta;
    QPoint m_pixelDelta;
    QPointF m_position;
    Qt::KeyboardModifiers m_modifiers;
};

} // namespace md
