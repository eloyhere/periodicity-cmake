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
#include <functional>
#include <stdexcept>
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
        if (m_wheelPositions != nullptr) {
            if (const auto* wheel = dynamic_cast<const md::WheelEvent*>(&event)) {
                m_wheelPositions->append(wheel->position());
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

        if (m_handler) {
            // Copy before calling: the handler may delete this target (and with
            // it m_handler), so no member may be touched once it returns.
            const std::function<void(md::Event&)> handler = m_handler;
            handler(event);
        }

        return event.isAccepted();
    }

    bool onInterceptEvent(md::Event& event) override
    {
        if (m_interceptLog != nullptr) {
            m_interceptLog->append(m_name);
        }
        if (m_interceptHandler) {
            return m_interceptHandler(event);
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
    QList<QPointF>* m_wheelPositions = nullptr;
    QList<md::EventTarget*>* m_targets = nullptr;
    std::function<void(md::Event&)> m_handler;
    std::function<bool(md::Event&)> m_interceptHandler;
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
    void wheelTranslationPerNode();
    void cycleGuard();
    void eventReuse();
    void reentrantDispatchKeepsOuterState();
    void recursiveDispatchIsCapped();
    void destroyedTargetAbortsDispatch();
    void throwingHandlerDoesNotBreakBubble();
    void gestureTap();
    void gestureLongPressFromMove();
    void gestureLongPressFromUp();
    void gestureDragSequence();
    void gestureSlopInDp();
    void gestureReset();
    void gestureCancelOnFocusLoss();
    void gestureStalePressCancels();
    void gestureSecondPressKeepsDrag();
    void gestureThrowingCallbackDoesNotWedge();
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

    // The dispatcher detaches the target when a top-level pass returns, so a
    // retained event cannot name a target that may have been destroyed; the
    // phase still records the last pass.
    QVERIFY(event.target() == nullptr);
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
    // The target is detached when dispatch returns; the recorded list below
    // still shows which target every handler saw.
    QVERIFY(event.target() == nullptr);
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

void TestEvent::wheelTranslationPerNode()
{
    FakeTarget root(QStringLiteral("root"));
    FakeTarget middle(QStringLiteral("middle"), &root);
    FakeTarget leaf(QStringLiteral("leaf"), &middle);
    root.m_mappingOffset = QPointF(10.0, 20.0);
    middle.m_mappingOffset = QPointF(100.0, 200.0);

    QList<QPointF> positions;
    for (FakeTarget* node : {&root, &middle, &leaf}) {
        node->m_wheelPositions = &positions;
    }

    md::WheelEvent event(QPoint(0, 120),
                         QPoint(0, 0),
                         QPointF(5.0, 5.0),
                         Qt::NoModifier,
                         10ms);

    QVERIFY(!md::EventDispatcher::dispatch(event, leaf));

    // Same order and mapping contract as a pointer event: the wheel position is
    // expressed in each receiving target's frame, and the original target keeps
    // the caller's position.
    QCOMPARE(static_cast<int>(positions.size()), 5);
    QCOMPARE(positions.at(0), QPointF(15.0, 25.0));
    QCOMPARE(positions.at(1), QPointF(105.0, 205.0));
    QCOMPARE(positions.at(2), QPointF(5.0, 5.0));
    QCOMPARE(positions.at(3), QPointF(105.0, 205.0));
    QCOMPARE(positions.at(4), QPointF(15.0, 25.0));

    // The caller's frame is restored so the event can be reused.
    QCOMPARE(event.position(), QPointF(5.0, 5.0));
    QVERIFY(event.target() == nullptr);

    // setPosition is the dispatcher's translation hook; it behaves like the
    // constructor argument.
    event.setPosition(QPointF(7.0, 9.0));
    QCOMPARE(event.position(), QPointF(7.0, 9.0));
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
    QVERIFY(event.target() == nullptr);

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

void TestEvent::reentrantDispatchKeepsOuterState()
{
    FakeTarget root(QStringLiteral("root"));
    FakeTarget middle(QStringLiteral("middle"), &root);
    FakeTarget leaf(QStringLiteral("leaf"), &middle);
    FakeTarget other(QStringLiteral("other"));

    QStringList order;
    root.m_log = &order;
    middle.m_log = &order;
    leaf.m_log = &order;

    bool innerRan = false;
    bool innerSawCleanState = false;
    bool innerResult = true;
    md::EventPhase phaseAfterInner = md::EventPhase::Capture;
    md::EventTarget* targetAfterInner = nullptr;
    bool acceptedAfterInner = false;
    bool stoppedAfterInner = false;

    other.m_handler = [&](md::Event& event) {
        innerRan = true;
        innerSawCleanState = event.target() == &other && !event.isAccepted()
                             && !event.isPropagationStopped();
    };

    leaf.m_handler = [&](md::Event& event) {
        // The outer pass settles its own outcome first ...
        event.accept();
        event.stopPropagation();
        // ... then dispatches the event it is handling to another target.
        innerResult = md::EventDispatcher::dispatch(event, other);
        phaseAfterInner = event.phase();
        targetAfterInner = event.target();
        acceptedAfterInner = event.isAccepted();
        stoppedAfterInner = event.isPropagationStopped();
    };

    md::Event event(md::EventType::PointerDown);
    QVERIFY(md::EventDispatcher::dispatch(event, leaf));

    QVERIFY(innerRan);
    QVERIFY(innerSawCleanState);
    QVERIFY(!innerResult);

    // The inner pass reset the flags and retargeted the event; the outer pass's
    // phase, target and both flags were restored before the handler resumed.
    QCOMPARE(phaseAfterInner, md::EventPhase::Target);
    QVERIFY(targetAfterInner == &leaf);
    QVERIFY(acceptedAfterInner);
    QVERIFY(stoppedAfterInner);

    const QStringList expected{
        QStringLiteral("root:capture"),
        QStringLiteral("middle:capture"),
        QStringLiteral("leaf:target"),
    };
    QCOMPARE(order, expected);
    QVERIFY(event.isAccepted());
    QVERIFY(event.isPropagationStopped());
    QVERIFY(event.target() == nullptr);
}

void TestEvent::recursiveDispatchIsCapped()
{
    FakeTarget node(QStringLiteral("node"));
    int deliveries = 0;

    node.m_handler = [&](md::Event& event) {
        ++deliveries;
        static_cast<void>(md::EventDispatcher::dispatch(event, node));
    };

    md::Event event(md::EventType::PointerDown);
    QVERIFY(!md::EventDispatcher::dispatch(event, node));

    // Every re-entry adds one delivery; the first pass beyond the nesting cap
    // is refused, which unwinds the recursion instead of overflowing the stack.
    QVERIFY(deliveries >= 2);
    QVERIFY(deliveries <= 32);
    QVERIFY(event.target() == nullptr);
    QVERIFY(!event.isAccepted());
    QVERIFY(!event.isPropagationStopped());
}

void TestEvent::destroyedTargetAbortsDispatch()
{
    // (a) The target handler destroys an ancestor. The bubble must not call the
    // freed node: dispatch aborts at the next boundary.
    auto* root = new FakeTarget(QStringLiteral("root"));
    auto* middle = new FakeTarget(QStringLiteral("middle"), root);
    auto* leaf = new FakeTarget(QStringLiteral("leaf"), middle);

    QStringList order;
    root->m_log = &order;
    middle->m_log = &order;
    leaf->m_log = &order;

    leaf->m_handler = [&](md::Event&) {
        delete middle;
        middle = nullptr;
    };

    md::Event event(md::EventType::PointerDown);
    QVERIFY(!md::EventDispatcher::dispatch(event, *leaf));

    const QStringList expected{
        QStringLiteral("root:capture"),
        QStringLiteral("middle:capture"),
        QStringLiteral("leaf:target"),
    };
    QCOMPARE(order, expected);
    QCOMPARE(root->m_eventCount, 1); // captured once; never reached by the bubble
    QVERIFY(event.target() == nullptr);

    delete leaf;
    delete root;

    // (b) The delivery target destroys itself. The chain's liveness token lets
    // the dispatcher stop without touching the freed target, and the event no
    // longer names it afterwards.
    auto* root2 = new FakeTarget(QStringLiteral("root"));
    auto* leaf2 = new FakeTarget(QStringLiteral("leaf"), root2);

    QStringList order2;
    root2->m_log = &order2;
    leaf2->m_log = &order2;

    leaf2->m_handler = [&](md::Event&) {
        delete leaf2;
        leaf2 = nullptr;
    };

    md::Event second(md::EventType::PointerUp);
    QVERIFY(!md::EventDispatcher::dispatch(second, *leaf2));

    const QStringList expected2{
        QStringLiteral("root:capture"),
        QStringLiteral("leaf:target"),
    };
    QCOMPARE(order2, expected2);
    QCOMPARE(root2->m_eventCount, 1);
    QVERIFY(second.target() == nullptr);

    delete root2;
}

void TestEvent::throwingHandlerDoesNotBreakBubble()
{
    FakeTarget root(QStringLiteral("root"));
    FakeTarget middle(QStringLiteral("middle"), &root);
    FakeTarget leaf(QStringLiteral("leaf"), &middle);

    QStringList order;
    for (FakeTarget* node : {&root, &middle, &leaf}) {
        node->m_log = &order;
    }

    const QStringList expected{
        QStringLiteral("root:capture"),
        QStringLiteral("middle:capture"),
        QStringLiteral("leaf:target"),
        QStringLiteral("middle:bubble"),
        QStringLiteral("root:bubble"),
    };

    // A throwing onEvent is contained and treated as an unhandled delivery, so
    // the bubble still reaches every ancestor.
    leaf.m_handler = [](md::Event&) { throw std::runtime_error("boom"); };

    md::Event event(md::EventType::PointerDown);
    QVERIFY(!md::EventDispatcher::dispatch(event, leaf));
    QCOMPARE(order, expected);
    QVERIFY(!event.isAccepted());
    QVERIFY(!event.isPropagationStopped());
    QVERIFY(event.target() == nullptr);

    // The same containment applies to onInterceptEvent: a throwing interceptor
    // counts as "does not claim the gesture".
    leaf.m_handler = nullptr;
    middle.m_interceptHandler = [](md::Event&) -> bool {
        throw std::runtime_error("intercept");
    };

    order.clear();
    md::Event second(md::EventType::PointerDown);
    QVERIFY(!md::EventDispatcher::dispatch(second, leaf));
    QCOMPARE(order, expected);
    QVERIFY(!second.isAccepted());
    QVERIFY(second.target() == nullptr);
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

void TestEvent::gestureCancelOnFocusLoss()
{
    int cancels = 0;
    int taps = 0;
    int dragStarts = 0;
    int drags = 0;
    int dragEnds = 0;
    md::EventType cancelType = md::EventType::None;

    md::GestureDetector::Callbacks callbacks;
    callbacks.onTap = [&taps](const md::PointerEvent&) { ++taps; };
    callbacks.onDragStart = [&dragStarts](const md::PointerEvent&) { ++dragStarts; };
    callbacks.onDrag = [&drags](const md::PointerEvent&, QPointF) { ++drags; };
    callbacks.onDragEnd = [&dragEnds](const md::PointerEvent&) { ++dragEnds; };
    callbacks.onCancel = [&cancels, &cancelType](const md::Event& event) {
        ++cancels;
        cancelType = event.type();
    };

    md::GestureDetector detector(callbacks);

    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(0.0, 0.0), 0ms)));
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(20.0, 0.0), 10ms)));
    QCOMPARE(dragStarts, 1);
    QCOMPARE(drags, 1);
    QVERIFY(detector.isDragging());

    // Losing focus abandons the drag: onCancel runs, onDragEnd does not, and
    // the notification itself is not consumed.
    QVERIFY(!detector.handle(md::FocusEvent(false, 20ms)));
    QCOMPARE(cancels, 1);
    QCOMPARE(cancelType, md::EventType::FocusOut);
    QVERIFY(!detector.isDragging());
    QCOMPARE(dragEnds, 0);

    // The abandoned gesture no longer consumes the pointer stream.
    QVERIFY(!detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(40.0, 0.0), 30ms)));
    QVERIFY(!detector.handle(makePointerEvent(md::EventType::PointerUp, QPointF(40.0, 0.0), 40ms)));
    QCOMPARE(drags, 1);
    QCOMPARE(cancels, 1);
    QCOMPARE(taps, 0);

    // PointerLeave cancels a press that has not become a drag yet.
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(5.0, 5.0), 100ms)));
    QVERIFY(!detector.handle(makePointerEvent(md::EventType::PointerLeave, QPointF(5.0, 5.0), 110ms)));
    QCOMPARE(cancels, 2);
    QCOMPARE(cancelType, md::EventType::PointerLeave);
    QVERIFY(!detector.isDragging());

    // A focus loss without a gesture is a no-op.
    QVERIFY(!detector.handle(md::FocusEvent(false, 120ms)));
    QCOMPARE(cancels, 2);
}

void TestEvent::gestureStalePressCancels()
{
    int cancels = 0;
    int taps = 0;
    int longPresses = 0;
    int dragStarts = 0;
    int dragEnds = 0;
    md::EventType cancelType = md::EventType::None;

    md::GestureDetector::Callbacks callbacks;
    callbacks.onTap = [&taps](const md::PointerEvent&) { ++taps; };
    callbacks.onLongPress = [&longPresses](const md::PointerEvent&) { ++longPresses; };
    callbacks.onDragStart = [&dragStarts](const md::PointerEvent&) { ++dragStarts; };
    callbacks.onDrag = [](const md::PointerEvent&, QPointF) {};
    callbacks.onDragEnd = [&dragEnds](const md::PointerEvent&) { ++dragEnds; };
    callbacks.onCancel = [&cancels, &cancelType](const md::Event& event) {
        ++cancels;
        cancelType = event.type();
    };

    md::GestureDetector detector(callbacks);

    // A move that arrives long after the press belongs to a different
    // interaction: the lost press is cancelled and the move is not consumed.
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(0.0, 0.0), 0ms)));
    QVERIFY(!detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(20.0, 0.0), 20000ms)));
    QCOMPARE(cancels, 1);
    QCOMPARE(cancelType, md::EventType::PointerMove);
    QVERIFY(!detector.isDragging());
    QVERIFY(!detector.handle(makePointerEvent(md::EventType::PointerUp, QPointF(20.0, 0.0), 20010ms)));
    QCOMPARE(cancels, 1);
    QCOMPARE(taps, 0);
    QCOMPARE(dragStarts, 0);

    // The deadline measures inactivity, not the total press duration: a slow
    // drag that keeps sending moves survives.
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(0.0, 0.0), 30000ms)));
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(20.0, 0.0), 30010ms)));
    QCOMPARE(dragStarts, 1);
    QVERIFY(detector.isDragging());
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(25.0, 0.0), 34000ms)));
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerUp, QPointF(25.0, 0.0), 38000ms)));
    QCOMPARE(dragEnds, 1);
    QCOMPARE(cancels, 1);

    // A reported long press followed by a stalled stream is stale as well.
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(0.0, 0.0), 50000ms)));
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(2.0, 0.0), 50600ms)));
    QCOMPARE(longPresses, 1);
    QVERIFY(!detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(4.0, 0.0), 60000ms)));
    QCOMPARE(cancels, 2);
    QCOMPARE(cancelType, md::EventType::PointerMove);
    QVERIFY(!detector.isDragging());
}

void TestEvent::gestureSecondPressKeepsDrag()
{
    int cancels = 0;
    int taps = 0;
    int dragStarts = 0;
    int drags = 0;
    int dragEnds = 0;
    QList<QPointF> deltas;

    md::GestureDetector::Callbacks callbacks;
    callbacks.onTap = [&taps](const md::PointerEvent&) { ++taps; };
    callbacks.onDragStart = [&dragStarts](const md::PointerEvent&) { ++dragStarts; };
    callbacks.onDrag = [&drags, &deltas](const md::PointerEvent&, QPointF delta) {
        ++drags;
        deltas.append(delta);
    };
    callbacks.onDragEnd = [&dragEnds](const md::PointerEvent&) { ++dragEnds; };
    callbacks.onCancel = [&cancels](const md::Event&) { ++cancels; };

    md::GestureDetector detector(callbacks);

    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(0.0, 0.0), 0ms)));
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(20.0, 0.0), 10ms)));
    QCOMPARE(dragStarts, 1);
    QCOMPARE(drags, 1);
    QVERIFY(detector.isDragging());

    // A second press inside the stale deadline is ignored: the drag keeps its
    // press position, so the next delta is still measured from (0, 0).
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(100.0, 100.0), 20ms)));
    QCOMPARE(cancels, 0);
    QCOMPARE(dragStarts, 1);
    QVERIFY(detector.isDragging());

    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(30.0, 5.0), 30ms)));
    QCOMPARE(drags, 2);
    QCOMPARE(static_cast<int>(deltas.size()), 2);
    QCOMPARE(deltas.at(1), QPointF(30.0, 5.0));

    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerUp, QPointF(30.0, 5.0), 40ms)));
    QCOMPARE(dragEnds, 1);

    // Past the deadline the old press is lost: the replacing press cancels it
    // and starts a new gesture from the new position.
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(0.0, 0.0), 50000ms)));
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(20.0, 0.0), 50010ms)));
    QVERIFY(detector.isDragging());
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(5.0, 5.0), 60000ms)));
    QCOMPARE(cancels, 1);
    QVERIFY(!detector.isDragging());

    // The new press is the active one: a small move and a release are a tap,
    // not the continuation of the old drag.
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(6.0, 5.0), 60010ms)));
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerUp, QPointF(6.0, 5.0), 60020ms)));
    QCOMPARE(taps, 1);
    QCOMPARE(dragEnds, 1);
    QCOMPARE(drags, 3);
}

void TestEvent::gestureThrowingCallbackDoesNotWedge()
{
    int taps = 0;

    md::GestureDetector::Callbacks callbacks;
    callbacks.onDragStart = [](const md::PointerEvent&) { throw std::runtime_error("drag"); };
    callbacks.onTap = [&taps](const md::PointerEvent&) { ++taps; };

    md::GestureDetector detector(callbacks);

    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(0.0, 0.0), 0ms)));
    // The throwing drag callback is contained: the gesture is abandoned, the
    // event is not consumed and the detector stays usable.
    QVERIFY(!detector.handle(makePointerEvent(md::EventType::PointerMove, QPointF(20.0, 0.0), 10ms)));
    QVERIFY(!detector.isDragging());

    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerDown, QPointF(0.0, 0.0), 100ms)));
    QVERIFY(detector.handle(makePointerEvent(md::EventType::PointerUp, QPointF(1.0, 1.0), 110ms)));
    QCOMPARE(taps, 1);
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
