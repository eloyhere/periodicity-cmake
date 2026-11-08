// Unit tests for the event module (L3): dispatch order, propagation flags,
// interception, pointer translation, cycle guarding, gesture recognition and
// the Qt event bridge.
//
// The suite is headless: it creates no widgets. The bridge tests construct
// QMouseEvent, QWheelEvent and QKeyEvent values directly; that needs a
// QGuiApplication (supplied by QTEST_MAIN) but no window system — CMake runs
// the executable on Qt's offscreen platform.

#include <md/event/event_dispatcher.h>
#include <md/event/event_target.h>
#include <md/event/gesture_detector.h>
#include <md/event/key_event.h>
#include <md/event/pointer_event.h>
#include <md/event/qt_bridge.h>
#include <md/event/wheel_event.h>

#include <QKeyEvent>
#include <QList>
#include <QMouseEvent>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QWheelEvent>

#include <chrono>
#include <utility>

using namespace std::chrono_literals;

namespace {

QString phaseName(md::EventPhase phase)
{
    switch (phase) {
    case md::EventPhase::Capture:
        return QStringLiteral("capture");
    case md::EventPhase::Target:
        return QStringLiteral("target");
    case md::EventPhase::Bubble:
        return QStringLiteral("bubble");
    }
    return QStringLiteral("unknown");
}

md::PointerEvent makePointerEvent(md::EventType type,
                                  const QPointF& position,
                                  std::chrono::milliseconds timestamp)
{
    return md::PointerEvent(type,
                            position,
                            position,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier,
                            timestamp);
}

// A configurable EventTarget for the dispatch tests. It records the phase and
// target of every delivery, can accept or stop an event, and intercepts on
// demand. mapPointFrom adds a fixed offset, which makes the per-node pointer
// translation observable without any real geometry.
class FakeTarget : public md::EventTarget
{
public:
    explicit FakeTarget(QString name, FakeTarget* parent = nullptr)
        : m_name(std::move(name))
        , m_parent(parent)
    {
    }

    [[nodiscard]] md::EventTarget* parentTarget() const noexcept override
    {
        return m_parent;
    }

    [[nodiscard]] QPointF mapPointFrom(const md::EventTarget& source,
                                       const QPointF& point) const override
    {
        m_lastMapSource = &source;
        return point + m_mappingOffset;
    }

    bool onEvent(md::Event& event) override
    {
        ++m_eventCount;

        if (m_log != nullptr) {
            m_log->append(QStringLiteral("%1:%2").arg(m_name, phaseName(event.phase())));
        }
        if (m_positions != nullptr) {
            if (const auto* pointer = dynamic_cast<const md::PointerEvent*>(&event)) {
                m_positions->append(pointer->localPosition());
            }
        }
        if (m_targets != nullptr) {
            m_targets->append(event.target());
        }

        if (m_stop) {
            event.stopPropagation();
        }
        if (m_accept) {
            event.accept();
        }

        return event.isAccepted();
    }

    bool onInterceptEvent(md::Event&) override
    {
        if (m_interceptLog != nullptr) {
            m_interceptLog->append(m_name);
        }
        return m_intercept;
    }

    // Test-controlled state.
    QString m_name;
    FakeTarget* m_parent = nullptr;
    QPointF m_mappingOffset;
    bool m_intercept = false;
    bool m_accept = false;
    bool m_stop = false;
    QStringList* m_log = nullptr;
    QStringList* m_interceptLog = nullptr;
    QList<QPointF>* m_positions = nullptr;
    QList<md::EventTarget*>* m_targets = nullptr;
    int m_eventCount = 0;

private:
    mutable const md::EventTarget* m_lastMapSource = nullptr;

public:
    [[nodiscard]] const md::EventTarget* lastMapSource() const noexcept
    {
        return m_lastMapSource;
    }
};

} // namespace

class TestEvent : public QObject
{
    Q_OBJECT

private slots:
    void eventValueDefaults();
    void dispatchOrderThreeNodeChain();
    void stopPropagationDuringCapture();
    void acceptOnTargetStopsBubble();
    void interceptionRetargets();
    void pointerTranslationPerNode();
    void cycleGuard();
    void eventReuse();
    void gestureTap();
    void gestureLongPressFromMove();
    void gestureLongPressFromUp();
    void gestureDragSequence();
    void gestureSlopInDp();
    void gestureReset();
    void mouseBridge();
    void keyBridge();
    void wheelBridge();
    void focusEvent();
};

void TestEvent::eventValueDefaults()
{
    md::Event event(md::EventType::Wheel, 15ms);
    QCOMPARE(event.type(), md::EventType::Wheel);
    QCOMPARE(event.phase(), md::EventPhase::Target);
    QVERIFY(event.target() == nullptr);
    QCOMPARE(event.timestamp().count(), qint64(15));
    QVERIFY(!event.isAccepted());
    QVERIFY(!event.isPropagationStopped());

    event.accept();
    QVERIFY(event.isAccepted());
    event.ignore();
    QVERIFY(!event.isAccepted());

    event.stopPropagation();
    QVERIFY(event.isPropagationStopped());

    FakeTarget node(QStringLiteral("node"));
    event.setPhase(md::EventPhase::Capture);
    event.setTarget(&node);
    QCOMPARE(event.phase(), md::EventPhase::Capture);
    QVERIFY(event.target() == &node);

    // A missing timestamp defaults to zero, so synthetic events stay
    // deterministic.
    const md::Event defaulted(md::EventType::None);
    QCOMPARE(defaulted.timestamp().count(), qint64(0));
}

void TestEvent::dispatchOrderThreeNodeChain()
{
    FakeTarget root(QStringLiteral("root"));
    FakeTarget middle(QStringLiteral("middle"), &root);
    FakeTarget leaf(QStringLiteral("leaf"), &middle);

    QStringList order;
    root.m_log = &order;
    middle.m_log = &order;
    leaf.m_log = &order;

    md::Event event(md::EventType::PointerDown, 3ms);
    QVERIFY(!md::EventDispatcher::dispatch(event, leaf));

    const QStringList expected{
        QStringLiteral("root:capture"),
        QStringLiteral("middle:capture"),
        QStringLiteral("leaf:target"),
        QStringLiteral("middle:bubble"),
        QStringLiteral("root:bubble"),
    };
    QCOMPARE(order, expected);

    // The event ends on the target it was dispatched to, in the last phase.
    QVERIFY(event.target() == &leaf);
    QCOMPARE(event.phase(), md::EventPhase::Bubble);
    QVERIFY(!event.isAccepted());
    QCOMPARE(root.m_eventCount, 2);
    QCOMPARE(middle.m_eventCount, 2);
    QCOMPARE(leaf.m_eventCount, 1);
}

void TestEvent::stopPropagationDuringCapture()
{
    FakeTarget root(QStringLiteral("root"));
    FakeTarget middle(QStringLiteral("middle"), &root);
    FakeTarget leaf(QStringLiteral("leaf"), &middle);

    QStringList order;
    root.m_log = &order;
    middle.m_log = &order;
    leaf.m_log = &order;
    root.m_stop = true;

    md::Event event(md::EventType::PointerDown);
    QVERIFY(md::EventDispatcher::dispatch(event, leaf));

    // Stopping at the root's capture ends the dispatch before the target and
    // before any bubble.
    QCOMPARE(order, QStringList{QStringLiteral("root:capture")});
    QVERIFY(event.isPropagationStopped());
    QCOMPARE(middle.m_eventCount, 0);
    QCOMPARE(leaf.m_eventCount, 0);
}

void TestEvent::acceptOnTargetStopsBubble()
{
    FakeTarget root(QStringLiteral("root"));
    FakeTarget middle(QStringLiteral("middle"), &root);
    FakeTarget leaf(QStringLiteral("leaf"), &middle);

    QStringList order;
    root.m_log = &order;
    middle.m_log = &order;
    leaf.m_log = &order;
    leaf.m_accept = true;

    md::Event event(md::EventType::PointerDown);
    QVERIFY(md::EventDispatcher::dispatch(event, leaf));

    // Both capture handlers ran before the target accepted; no bubble ran.
    const QStringList expected{
        QStringLiteral("root:capture"),
        QStringLiteral("middle:capture"),
        QStringLiteral("leaf:target"),
    };
    QCOMPARE(order, expected);
    QVERIFY(event.isAccepted());
    QCOMPARE(root.m_eventCount, 1);
    QCOMPARE(middle.m_eventCount, 1);
    QCOMPARE(leaf.m_eventCount, 1);
}

void TestEvent::interceptionRetargets()
{
    FakeTarget root(QStringLiteral("root"));
    FakeTarget middle(QStringLiteral("middle"), &root);
    FakeTarget leaf(QStringLiteral("leaf"), &middle);

    QStringList order;
    QStringList intercepts;
    QList<md::EventTarget*> targets;
    for (FakeTarget* node : {&root, &middle, &leaf}) {
        node->m_log = &order;
        node->m_interceptLog = &intercepts;
        node->m_targets = &targets;
    }
    middle.m_intercept = true;

    md::Event event(md::EventType::PointerDown);
    QVERIFY(!md::EventDispatcher::dispatch(event, leaf));

    // Middle claims the gesture during interception: it receives capture and
    // then the target phase, leaf receives nothing, and the bubble still
    // reaches the root.
    const QStringList expected{
        QStringLiteral("root:capture"),
        QStringLiteral("middle:capture"),
        QStringLiteral("middle:target"),
        QStringLiteral("root:bubble"),
    };
    QCOMPARE(order, expected);
    QCOMPARE(intercepts, QStringList({QStringLiteral("root"), QStringLiteral("middle")}));
    QVERIFY(event.target() == &middle);
    QCOMPARE(leaf.m_eventCount, 0);
    QCOMPARE(middle.m_eventCount, 2);

    // Capture saw the original target; the target phase and bubble saw the
    // interceptor.
    QCOMPARE(static_cast<int>(targets.size()), 4);
    QVERIFY(targets.at(0) == &leaf);
    QVERIFY(targets.at(1) == &leaf);
    QVERIFY(targets.at(2) == &middle);
    QVERIFY(targets.at(3) == &middle);
}

void TestEvent::pointerTranslationPerNode()
{
    FakeTarget root(QStringLiteral("root"));
    FakeTarget middle(QStringLiteral("middle"), &root);
    FakeTarget leaf(QStringLiteral("leaf"), &middle);
    root.m_mappingOffset = QPointF(10.0, 20.0);
    middle.m_mappingOffset = QPointF(100.0, 200.0);

    QList<QPointF> positions;
    for (FakeTarget* node : {&root, &middle, &leaf}) {
        node->m_positions = &positions;
    }

    md::PointerEvent event(md::EventType::PointerDown,
                           QPointF(5.0, 5.0),
                           QPointF(1000.0, 1000.0),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier,
                           10ms);

    QVERIFY(!md::EventDispatcher::dispatch(event, leaf));

    // Order is root capture, middle capture, leaf target, middle bubble, root
    // bubble. Each ancestor sees the point in its own frame; the target keeps
    // the caller's position.
    QCOMPARE(static_cast<int>(positions.size()), 5);
    QCOMPARE(positions.at(0), QPointF(15.0, 25.0));
    QCOMPARE(positions.at(1), QPointF(105.0, 205.0));
    QCOMPARE(positions.at(2), QPointF(5.0, 5.0));
    QCOMPARE(positions.at(3), QPointF(105.0, 205.0));
    QCOMPARE(positions.at(4), QPointF(15.0, 25.0));

    // Every mapping is anchored to the original target, not to the previously
    // visited node.
    QVERIFY(root.lastMapSource() == &leaf);
    QVERIFY(middle.lastMapSource() == &leaf);
    QVERIFY(leaf.lastMapSource() == nullptr);

    // Dispatch restores the caller's coordinate frame, so the same pointer
    // event can be reused without inheriting the last handler's translation.
    QCOMPARE(event.localPosition(), QPointF(5.0, 5.0));
    positions.clear();
    QVERIFY(!md::EventDispatcher::dispatch(event, leaf));
    QCOMPARE(static_cast<int>(positions.size()), 5);
    QCOMPARE(positions.at(0), QPointF(15.0, 25.0));
    QCOMPARE(positions.at(2), QPointF(5.0, 5.0));
    QCOMPARE(positions.at(4), QPointF(15.0, 25.0));
}

void TestEvent::cycleGuard()
{
    // A target that is its own parent must terminate: the revisit guard turns
    // the chain into a single root/target node.
    FakeTarget selfish(QStringLiteral("selfish"));
    selfish.m_parent = &selfish;
    QStringList order;
    selfish.m_log = &order;

    md::Event event(md::EventType::PointerDown);
    QVERIFY(!md::EventDispatcher::dispatch(event, selfish));
    QCOMPARE(order, QStringList{QStringLiteral("selfish:target")});
    QCOMPARE(selfish.m_eventCount, 1);
    QVERIFY(event.target() == &selfish);

    // A two-node cycle terminates as well, at the first revisited node.
    FakeTarget first(QStringLiteral("first"));
    FakeTarget second(QStringLiteral("second"));
    first.m_parent = &second;
    second.m_parent = &first;
    QStringList loop;
    first.m_log = &loop;
    second.m_log = &loop;

    md::Event loopEvent(md::EventType::PointerUp);
    QVERIFY(!md::EventDispatcher::dispatch(loopEvent, first));
    const QStringList expected{
        QStringLiteral("second:capture"),
        QStringLiteral("first:target"),
        QStringLiteral("second:bubble"),
    };
    QCOMPARE(loop, expected);
}

void TestEvent::eventReuse()
{
    FakeTarget root(QStringLiteral("root"));
    FakeTarget leaf(QStringLiteral("leaf"), &root);

    QStringList order;
    root.m_log = &order;
    leaf.m_log = &order;

    md::Event event(md::EventType::KeyDown, 1ms);

    // First run stops in capture, leaving both flags dirty.
    root.m_stop = true;
    QVERIFY(md::EventDispatcher::dispatch(event, leaf));
    QCOMPARE(order, QStringList{QStringLiteral("root:capture")});
    QVERIFY(event.isPropagationStopped());

    // resetForDispatch clears the stopped flag, so the second run completes.
    root.m_stop = false;
    QVERIFY(!md::EventDispatcher::dispatch(event, leaf));
    const QStringList afterSecond{
        QStringLiteral("root:capture"),
        QStringLiteral("root:capture"),
        QStringLiteral("leaf:target"),
        QStringLiteral("root:bubble"),
    };
    QCOMPARE(order, afterSecond);
    QVERIFY(!event.isPropagationStopped());

    // The accepted flag is cleared the same way.
    leaf.m_accept = true;
    QVERIFY(md::EventDispatcher::dispatch(event, leaf));
    QVERIFY(event.isAccepted());

    leaf.m_accept = false;
    QVERIFY(!md::EventDispatcher::dispatch(event, leaf));
    QVERIFY(!event.isAccepted());
    QCOMPARE(leaf.m_eventCount, 3);
}

void TestEvent::gestureTap()
{
    struct Counts
    {
        int taps = 0;
        int longPresses = 0;
        int dragStarts = 0;
        int drags = 0;
        int dragEnds = 0;
    } counts;

    md::GestureDetector::Callbacks callbacks;
    callbacks.onTap = [&counts](const md::PointerEvent&) { ++counts.taps; };
    callbacks.onLongPress = [&counts](const md::PointerEvent&) { ++counts.longPresses; };
    callbacks.onDragStart = [&counts](const md::PointerEvent&) { ++counts.dragStarts; };
    callbacks.onDrag = [&counts](const md::PointerEvent&, QPointF) { ++counts.drags; };
    callbacks.onDragEnd = [&counts](const md::PointerEvent&) { ++counts.dragEnds; };

    md::GestureDetector detector(callbacks);

    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(10.0, 10.0), 0ms)));
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(12.0, 10.0), 20ms)));
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerUp, QPointF(12.0, 10.0), 40ms)));

    QCOMPARE(counts.taps, 1);
    QCOMPARE(counts.longPresses, 0);
    QCOMPARE(counts.dragStarts, 0);
    QCOMPARE(counts.drags, 0);
    QCOMPARE(counts.dragEnds, 0);
    QVERIFY(!detector.isDragging());

    // A slow release that is still inside the slop and before the long-press
    // deadline is a tap: the single-tap rule is defined by those two bounds,
    // not by the reserved tapTimeout.
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(0.0, 0.0), 1000ms)));
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerUp, QPointF(2.0, 2.0), 1300ms)));
    QCOMPARE(counts.taps, 2);
    QCOMPARE(counts.longPresses, 0);

    // Events outside a gesture are not consumed.
    detector.reset();
    QVERIFY(!detector.handle(makePointerEvent(md::EventType::PointerEnter, QPointF(1.0, 1.0), 2000ms)));
    QVERIFY(!detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(1.0, 1.0), 2010ms)));
    QVERIFY(!detector.handle(makePointerEvent(md::EventType::PointerUp, QPointF(1.0, 1.0), 2020ms)));
    QCOMPARE(counts.taps, 2);
}

void TestEvent::gestureLongPressFromMove()
{
    int longPresses = 0;
    int taps = 0;
    int drags = 0;

    md::GestureDetector::Callbacks callbacks;
    callbacks.onTap = [&taps](const md::PointerEvent&) { ++taps; };
    callbacks.onLongPress = [&longPresses](const md::PointerEvent&) { ++longPresses; };
    callbacks.onDrag = [&drags](const md::PointerEvent&, QPointF) { ++drags; };

    md::GestureDetector detector(callbacks);

    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(0.0, 0.0), 0ms)));

    // The press was held past the 500 ms deadline; the move that reveals this
    // is still inside the slop and reports the long press.
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(3.0, 0.0), 550ms)));
    QCOMPARE(longPresses, 1);
    QCOMPARE(taps, 0);
    QCOMPARE(drags, 0);
    QVERIFY(!detector.isDragging());

    // Later moves are consumed but do not repeat the callback.
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(4.0, 0.0), 600ms)));
    QCOMPARE(longPresses, 1);
    QCOMPARE(drags, 0);

    // The release does not turn a long press into a tap.
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerUp, QPointF(4.0, 0.0), 700ms)));
    QCOMPARE(taps, 0);
    QCOMPARE(longPresses, 1);
}

void TestEvent::gestureLongPressFromUp()
{
    int longPresses = 0;
    int taps = 0;

    md::GestureDetector::Callbacks callbacks;
    callbacks.onTap = [&taps](const md::PointerEvent&) { ++taps; };
    callbacks.onLongPress = [&longPresses](const md::PointerEvent&) { ++longPresses; };

    md::GestureDetector detector(callbacks);

    // Exactly at the deadline counts as a long press, which pins the inclusive
    // comparison.
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(0.0, 0.0), 0ms)));
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerUp, QPointF(1.0, 1.0), 500ms)));
    QCOMPARE(longPresses, 1);
    QCOMPARE(taps, 0);

    // A release just before the deadline is still a tap.
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(0.0, 0.0), 1000ms)));
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerUp, QPointF(0.0, 0.0), 1499ms)));
    QCOMPARE(taps, 1);
    QCOMPARE(longPresses, 1);
}

void TestEvent::gestureDragSequence()
{
    int dragStarts = 0;
    int drags = 0;
    int dragEnds = 0;
    int taps = 0;
    QList<QPointF> deltas;

    md::GestureDetector::Callbacks callbacks;
    callbacks.onTap = [&taps](const md::PointerEvent&) { ++taps; };
    callbacks.onDragStart = [&dragStarts](const md::PointerEvent&) { ++dragStarts; };
    callbacks.onDrag = [&drags, &deltas](const md::PointerEvent&, QPointF delta) {
        ++drags;
        deltas.append(delta);
    };
    callbacks.onDragEnd = [&dragEnds](const md::PointerEvent&) { ++dragEnds; };

    md::GestureDetector detector(callbacks);

    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(0.0, 0.0), 0ms)));

    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(20.0, 0.0), 10ms)));
    QCOMPARE(dragStarts, 1);
    QCOMPARE(drags, 1);
    QVERIFY(detector.isDragging());

    // The drag callback reports the total delta from the press position, not
    // the step since the previous move.
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(30.0, 5.0), 20ms)));
    QCOMPARE(drags, 2);
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(30.0, 10.0), 30ms)));
    QCOMPARE(drags, 3);
    QCOMPARE(dragStarts, 1);

    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerUp, QPointF(30.0, 10.0), 40ms)));
    QCOMPARE(dragEnds, 1);
    QCOMPARE(taps, 0);
    QVERIFY(!detector.isDragging());

    QCOMPARE(static_cast<int>(deltas.size()), 3);
    QCOMPARE(deltas.at(0), QPointF(20.0, 0.0));
    QCOMPARE(deltas.at(1), QPointF(30.0, 5.0));
    QCOMPARE(deltas.at(2), QPointF(30.0, 10.0));

    QVERIFY(!detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(50.0, 50.0), 50ms)));
    QVERIFY(!detector.handle(makePointerEvent(md::EventType::PointerUp, QPointF(50.0, 50.0), 60ms)));
}

void TestEvent::gestureSlopInDp()
{
    int dragStarts = 0;

    md::GestureDetector::Callbacks callbacks;
    callbacks.onDragStart = [&dragStarts](const md::PointerEvent&) { ++dragStarts; };
    callbacks.onDrag = [](const md::PointerEvent&, QPointF) {};

    md::GestureConfig config;
    config.touchSlopDp = 8.0;

    md::Density density;
    density.scale = 2.0; // 8 dp becomes 16 device-independent pixels

    md::GestureDetector detector(callbacks, config, density);

    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(0.0, 0.0), 0ms)));

    // Exactly on the threshold is still a press...
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(16.0, 0.0), 10ms)));
    QCOMPARE(dragStarts, 0);
    QVERIFY(!detector.isDragging());

    // ... one pixel further starts the drag.
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(16.5, 0.0), 20ms)));
    QCOMPARE(dragStarts, 1);
    QVERIFY(detector.isDragging());
}

void TestEvent::gestureReset()
{
    int dragStarts = 0;
    int drags = 0;
    int dragEnds = 0;
    int taps = 0;

    md::GestureDetector::Callbacks callbacks;
    callbacks.onTap = [&taps](const md::PointerEvent&) { ++taps; };
    callbacks.onDragStart = [&dragStarts](const md::PointerEvent&) { ++dragStarts; };
    callbacks.onDrag = [&drags](const md::PointerEvent&, QPointF) { ++drags; };
    callbacks.onDragEnd = [&dragEnds](const md::PointerEvent&) { ++dragEnds; };

    md::GestureDetector detector(callbacks);

    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(0.0, 0.0), 0ms)));
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(20.0, 0.0), 10ms)));
    QCOMPARE(dragStarts, 1);
    QCOMPARE(drags, 1);
    QVERIFY(detector.isDragging());

    // reset() abandons the drag without firing onDragEnd...
    detector.reset();
    QVERIFY(!detector.isDragging());

    // ... and the following events no longer belong to a gesture.
    QVERIFY(!detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(40.0, 0.0), 20ms)));
    QVERIFY(!detector.handle(makePointerEvent(md::EventType::PointerUp, QPointF(40.0, 0.0), 30ms)));
    QCOMPARE(dragStarts, 1);
    QCOMPARE(drags, 1);
    QCOMPARE(dragEnds, 0);
    QCOMPARE(taps, 0);

    detector.reset(); // idempotent
}

void TestEvent::mouseBridge()
{
    const QPointF localPosition(3.0, 4.0);
    const QPointF globalPosition(100.0, 200.0);

    QMouseEvent press(QEvent::MouseButtonPress,
                      localPosition,
                      globalPosition,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::ShiftModifier);
    const md::PointerEvent down = md::pointerEventFromMouse(press, localPosition, 42ms);
    QCOMPARE(down.type(), md::EventType::PointerDown);
    QCOMPARE(down.localPosition(), localPosition);
    QCOMPARE(down.globalPosition(), globalPosition);
    QCOMPARE(down.button(), Qt::LeftButton);
    QCOMPARE(down.buttons().toInt(), static_cast<int>(Qt::LeftButton));
    QCOMPARE(down.modifiers().toInt(), static_cast<int>(Qt::ShiftModifier));
    QCOMPARE(down.timestamp().count(), qint64(42));

    QMouseEvent release(QEvent::MouseButtonRelease,
                        localPosition,
                        globalPosition,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    const md::PointerEvent up = md::pointerEventFromMouse(release, localPosition, 43ms);
    QCOMPARE(up.type(), md::EventType::PointerUp);
    QCOMPARE(up.button(), Qt::LeftButton);
    QCOMPARE(up.buttons().toInt(), 0);

    QMouseEvent move(QEvent::MouseMove,
                     localPosition,
                     globalPosition,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    const md::PointerEvent motion = md::pointerEventFromMouse(move, localPosition, 44ms);
    QCOMPARE(motion.type(), md::EventType::PointerMove);
    QCOMPARE(motion.button(), Qt::NoButton);
    QCOMPARE(motion.buttons().toInt(), static_cast<int>(Qt::LeftButton));

    // A boundary crossing is reported without a causing button, even if Qt
    // still carries a pressed button in the event.
    QMouseEvent enter(QEvent::Enter,
                      localPosition,
                      globalPosition,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    const md::PointerEvent entered = md::pointerEventFromMouse(enter, localPosition, 45ms);
    QCOMPARE(entered.type(), md::EventType::PointerEnter);
    QCOMPARE(entered.button(), Qt::NoButton);

    QMouseEvent leave(QEvent::Leave,
                      localPosition,
                      globalPosition,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    const md::PointerEvent left = md::pointerEventFromMouse(leave, localPosition, 46ms);
    QCOMPARE(left.type(), md::EventType::PointerLeave);
    QCOMPARE(left.button(), Qt::NoButton);

    // Unsupported Qt types are mapped to None instead of being guessed.
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick,
                            localPosition,
                            globalPosition,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    const md::PointerEvent other = md::pointerEventFromMouse(doubleClick, localPosition, 47ms);
    QCOMPARE(other.type(), md::EventType::None);
}

void TestEvent::keyBridge()
{
    QKeyEvent press(QEvent::KeyPress,
                    Qt::Key_A,
                    Qt::ShiftModifier,
                    QStringLiteral("A"));
    const md::KeyEvent down = md::keyEventFrom(press, 7ms);
    QCOMPARE(down.type(), md::EventType::KeyDown);
    QCOMPARE(down.key(), static_cast<int>(Qt::Key_A));
    QCOMPARE(down.text(), QStringLiteral("A"));
    QCOMPARE(down.modifiers().toInt(), static_cast<int>(Qt::ShiftModifier));
    QVERIFY(!down.isAutoRepeat());
    QCOMPARE(down.timestamp().count(), qint64(7));

    QKeyEvent repeat(QEvent::KeyPress,
                     Qt::Key_B,
                     Qt::NoModifier,
                     QStringLiteral("b"),
                     true);
    const md::KeyEvent repeated = md::keyEventFrom(repeat, 8ms);
    QCOMPARE(repeated.type(), md::EventType::KeyDown);
    QCOMPARE(repeated.key(), static_cast<int>(Qt::Key_B));
    QCOMPARE(repeated.text(), QStringLiteral("b"));
    QVERIFY(repeated.isAutoRepeat());

    QKeyEvent release(QEvent::KeyRelease,
                      Qt::Key_A,
                      Qt::NoModifier,
                      QStringLiteral("A"));
    const md::KeyEvent up = md::keyEventFrom(release, 9ms);
    QCOMPARE(up.type(), md::EventType::KeyUp);
    QCOMPARE(up.key(), static_cast<int>(Qt::Key_A));
}

void TestEvent::wheelBridge()
{
    QWheelEvent wheel(QPointF(12.0, 18.0),
                      QPointF(120.0, 180.0),
                      QPoint(1, 2),
                      QPoint(3, 4),
                      Qt::NoButton,
                      Qt::ControlModifier,
                      Qt::ScrollUpdate,
                      false);

    const md::WheelEvent converted = md::wheelEventFrom(wheel, 9ms);
    QCOMPARE(converted.type(), md::EventType::Wheel);
    QCOMPARE(converted.angleDelta(), QPoint(3, 4));
    QCOMPARE(converted.pixelDelta(), QPoint(1, 2));
    QCOMPARE(converted.position(), QPointF(12.0, 18.0));
    QCOMPARE(converted.modifiers().toInt(), static_cast<int>(Qt::ControlModifier));
    QCOMPARE(converted.timestamp().count(), qint64(9));
}

void TestEvent::focusEvent()
{
    const md::FocusEvent gained(true, 5ms);
    QCOMPARE(gained.type(), md::EventType::FocusIn);
    QVERIFY(gained.isFocused());
    QCOMPARE(gained.timestamp().count(), qint64(5));

    const md::FocusEvent lost(false);
    QCOMPARE(lost.type(), md::EventType::FocusOut);
    QVERIFY(!lost.isFocused());
}

QTEST_MAIN(TestEvent)

#include "tst_event.moc"
