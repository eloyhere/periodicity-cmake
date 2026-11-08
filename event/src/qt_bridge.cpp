// Periodicity — Qt event bridge implementation (framework.md §5.10).

#include <md/event/qt_bridge.h>

namespace md {
namespace {

EventType pointerTypeFor(QEvent::Type type) noexcept
{
    switch (type) {
    case QEvent::MouseButtonPress:
        return EventType::PointerDown;
    case QEvent::MouseButtonRelease:
        return EventType::PointerUp;
    case QEvent::MouseMove:
        return EventType::PointerMove;
    case QEvent::Enter:
        return EventType::PointerEnter;
    case QEvent::Leave:
        return EventType::PointerLeave;
    default:
        return EventType::None;
    }
}

EventType keyTypeFor(QEvent::Type type) noexcept
{
    switch (type) {
    case QEvent::KeyPress:
        return EventType::KeyDown;
    case QEvent::KeyRelease:
        return EventType::KeyUp;
    default:
        return EventType::None;
    }
}

} // namespace

PointerEvent pointerEventFromMouse(const QMouseEvent& event,
                                   const QPointF& localPosition,
                                   std::chrono::milliseconds timestamp)
{
    const EventType type = pointerTypeFor(event.type());

    // Enter and leave are notifications about the pointer crossing a boundary,
    // not about a button; Qt may nevertheless report a held button in
    // QMouseEvent::button(), so the bridge normalises it.
    const bool boundary = type == EventType::PointerEnter || type == EventType::PointerLeave;
    const Qt::MouseButton button = boundary ? Qt::NoButton : event.button();

    return PointerEvent(type,
                        localPosition,
                        event.globalPosition(),
                        button,
                        event.buttons(),
                        event.modifiers(),
                        timestamp);
}

WheelEvent wheelEventFrom(const QWheelEvent& event,
                          std::chrono::milliseconds timestamp)
{
    return WheelEvent(event.angleDelta(),
                      event.pixelDelta(),
                      event.position(),
                      event.modifiers(),
                      timestamp);
}

KeyEvent keyEventFrom(const QKeyEvent& event,
                      std::chrono::milliseconds timestamp)
{
    return KeyEvent(keyTypeFor(event.type()),
                    event.key(),
                    event.text(),
                    event.modifiers(),
                    event.isAutoRepeat(),
                    timestamp);
}

} // namespace md
