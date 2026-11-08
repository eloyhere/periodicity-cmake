// Periodicity — KeyEvent and FocusEvent implementation (framework.md §5.10).

#include <md/event/key_event.h>

#include <utility>

namespace md {

KeyEvent::KeyEvent(EventType type,
                   int key,
                   QString text,
                   Qt::KeyboardModifiers modifiers,
                   bool autoRepeat,
                   std::chrono::milliseconds timestamp)
    : Event(type, timestamp)
    , m_key(key)
    , m_text(std::move(text))
    , m_modifiers(modifiers)
    , m_autoRepeat(autoRepeat)
{
}

int KeyEvent::key() const noexcept
{
    return m_key;
}

QString KeyEvent::text() const
{
    return m_text;
}

Qt::KeyboardModifiers KeyEvent::modifiers() const noexcept
{
    return m_modifiers;
}

bool KeyEvent::isAutoRepeat() const noexcept
{
    return m_autoRepeat;
}

FocusEvent::FocusEvent(bool isFocused, std::chrono::milliseconds timestamp)
    : Event(isFocused ? EventType::FocusIn : EventType::FocusOut, timestamp)
    , m_focused(isFocused)
{
}

bool FocusEvent::isFocused() const noexcept
{
    return m_focused;
}

} // namespace md
