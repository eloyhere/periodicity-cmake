// Periodicity — pointer event payload (framework.md §5.10).
//
// A pointer event carries the position twice: `local` is expressed in the
// coordinate system of the target that is currently handling the event and is
// recomputed by the dispatcher for every node, while `global` is the position
// in the window's global coordinate system and never changes.

#pragma once

#include <md/event/event.h>

#include <QPointF>
#include <Qt>

#include <chrono>

namespace md {

class PointerEvent : public Event
{
public:
    PointerEvent(EventType type,
                 QPointF localPosition,
                 QPointF globalPosition,
                 Qt::MouseButton button,
                 Qt::MouseButtons buttons,
                 Qt::KeyboardModifiers modifiers,
                 std::chrono::milliseconds timestamp = {});

    [[nodiscard]] QPointF localPosition() const noexcept;
    [[nodiscard]] QPointF globalPosition() const noexcept;

    // The button that caused the event. Pointer moves and enter/leave events
    // report Qt::NoButton; `buttons()` carries the currently pressed set.
    [[nodiscard]] Qt::MouseButton button() const noexcept;
    [[nodiscard]] Qt::MouseButtons buttons() const noexcept;
    [[nodiscard]] Qt::KeyboardModifiers modifiers() const noexcept;

    // Set by the dispatcher when it translates the event between targets.
    void setLocalPosition(QPointF position) noexcept;

private:
    QPointF m_localPosition;
    QPointF m_globalPosition;
    Qt::MouseButton m_button = Qt::NoButton;
    Qt::MouseButtons m_buttons;
    Qt::KeyboardModifiers m_modifiers;
};

} // namespace md
