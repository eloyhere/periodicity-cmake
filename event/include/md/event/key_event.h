// Periodicity — key and focus event payloads (framework.md §5.10).
//
// KeyEvent carries the Qt key code, the produced text (empty for non-printing
// keys), the modifiers and the auto-repeat flag. FocusEvent is the minimal
// focus change notification: the event type follows from the bool — FocusIn
// when the target gained focus, FocusOut when it lost it.

#pragma once

#include <md/event/event.h>

#include <QString>
#include <Qt>

#include <chrono>

namespace md {

class KeyEvent : public Event
{
public:
    KeyEvent(EventType type,
             int key,
             QString text,
             Qt::KeyboardModifiers modifiers,
             bool autoRepeat,
             std::chrono::milliseconds timestamp = {});

    [[nodiscard]] int key() const noexcept;
    [[nodiscard]] QString text() const;
    [[nodiscard]] Qt::KeyboardModifiers modifiers() const noexcept;
    [[nodiscard]] bool isAutoRepeat() const noexcept;

private:
    int m_key = 0;
    QString m_text;
    Qt::KeyboardModifiers m_modifiers;
    bool m_autoRepeat = false;
};

class FocusEvent : public Event
{
public:
    explicit FocusEvent(bool isFocused, std::chrono::milliseconds timestamp = {});

    [[nodiscard]] bool isFocused() const noexcept;

private:
    bool m_focused = false;
};

} // namespace md
