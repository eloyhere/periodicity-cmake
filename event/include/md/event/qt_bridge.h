// Periodicity — Qt event bridge (framework.md §5.10).
//
// This is the only place in the library that knows Qt's concrete event
// classes. The view layer receives Qt events from its widgets and converts
// them here, keeping the rest of the dispatch code independent of QtGui.
//
// The timestamp is supplied by the caller rather than read from the Qt event:
// it keeps the conversion a pure function and lets tests and tools drive
// gestures deterministically.

#pragma once

#include <md/event/key_event.h>
#include <md/event/pointer_event.h>
#include <md/event/wheel_event.h>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <chrono>

namespace md {

// Converts a mouse event to a pointer event. `localPosition` is the position
// in the receiving target's coordinate system; callers translate it when the
// Qt event's widget differs from the dispatch target. Mouse-enter and
// mouse-leave become PointerEnter and PointerLeave with Qt::NoButton as the
// causing button; other types keep Qt's button. Unsupported Qt types become
// EventType::None.
PointerEvent pointerEventFromMouse(const QMouseEvent& event,
                                   const QPointF& localPosition,
                                   std::chrono::milliseconds timestamp);

// Converts a wheel event. The position is Qt's local position
// (QWheelEvent::position()).
WheelEvent wheelEventFrom(const QWheelEvent& event,
                          std::chrono::milliseconds timestamp);

// Converts a key event. QEvent::KeyPress becomes KeyDown and
// QEvent::KeyRelease becomes KeyUp; other types become EventType::None.
KeyEvent keyEventFrom(const QKeyEvent& event,
                      std::chrono::milliseconds timestamp);

} // namespace md
