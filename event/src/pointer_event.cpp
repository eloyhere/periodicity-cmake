// Periodicity — PointerEvent implementation (framework.md §5.10).

#include <md/event/pointer_event.h>

#include <utility>

namespace md {

PointerEvent::PointerEvent(EventType type,
                           QPointF localPosition,
                           QPointF globalPosition,
                           Qt::MouseButton button,
                           Qt::MouseButtons buttons,
                           Qt::KeyboardModifiers modifiers,
                           std::chrono::milliseconds timestamp)
    : Event(type, timestamp)
    , m_localPosition(std::move(localPosition))
    , m_globalPosition(std::move(globalPosition))
    , m_button(button)
    , m_buttons(buttons)
    , m_modifiers(modifiers)
{
}

QPointF PointerEvent::localPosition() const noexcept
{
    return m_localPosition;
}

QPointF PointerEvent::globalPosition() const noexcept
{
    return m_globalPosition;
}

Qt::MouseButton PointerEvent::button() const noexcept
{
    return m_button;
}

Qt::MouseButtons PointerEvent::buttons() const noexcept
{
    return m_buttons;
}

Qt::KeyboardModifiers PointerEvent::modifiers() const noexcept
{
    return m_modifiers;
}

void PointerEvent::setLocalPosition(QPointF position) noexcept
{
    m_localPosition = std::move(position);
}

} // namespace md
