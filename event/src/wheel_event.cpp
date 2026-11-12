// Periodicity — WheelEvent implementation (framework.md §5.10).

#include <md/event/wheel_event.h>

#include <utility>

namespace md {

WheelEvent::WheelEvent(QPoint angleDelta,
                       QPoint pixelDelta,
                       QPointF position,
                       Qt::KeyboardModifiers modifiers,
                       std::chrono::milliseconds timestamp)
    : Event(EventType::Wheel, timestamp)
    , m_angleDelta(angleDelta)
    , m_pixelDelta(pixelDelta)
    , m_position(std::move(position))
    , m_modifiers(modifiers)
{
}

QPoint WheelEvent::angleDelta() const noexcept
{
    return m_angleDelta;
}

QPoint WheelEvent::pixelDelta() const noexcept
{
    return m_pixelDelta;
}

QPointF WheelEvent::position() const noexcept
{
    return m_position;
}

void WheelEvent::setPosition(QPointF position) noexcept
{
    m_position = std::move(position);
}

Qt::KeyboardModifiers WheelEvent::modifiers() const noexcept
{
    return m_modifiers;
}

} // namespace md
