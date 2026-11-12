// Periodicity — event thread input pipeline tests.
//
// Everything is deterministic: batches are taken synchronously, and the frame
// hand-off test drives UiThread::runFrame() directly. No test sleeps.

#include <md/schedule/event_thread.h>
#include <md/schedule/frame_scheduler.h>

#include <QTest>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

using md::schedule::EventThread;
using md::schedule::InputEvent;
using md::schedule::InputKind;
using md::schedule::InputPayload;
using md::schedule::Phase;
using md::schedule::Priority;
using md::schedule::UiThread;

namespace {

InputPayload pointer(quint64 target, qreal x, qreal y,
                     Qt::MouseButton button = Qt::NoButton)
{
    InputPayload payload;
    payload.target = target;
    payload.position = QPointF(x, y);
    payload.globalPosition = QPointF(x + 100.0, y + 100.0);
    payload.button = button;
    return payload;
}

} // namespace

class TestEventThread : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void movesCoalesceToLastPositionButDiscreteEventsSurvive();
    void batchesAreOrderedByInputPriority();
    void wheelCoalescesPerTarget();
    void movesCoalescePerTarget();
    void keysAreNeverCoalesced();
    void frameBoundaryHandsOneBatchToLayout();
    void emptyQueueDoesNotRequestAHandoff();
    void producerThreadCanPostWithoutBlocking();
    void destroyingUiThreadDetachesSafely();
    void priorityTableIsStable();
};

void TestEventThread::init()
{
    md::schedule::EventThread::uninstall();
    md::schedule::UiThread::uninstall();
}

void TestEventThread::cleanup()
{
    md::schedule::EventThread::uninstall();
    md::schedule::UiThread::uninstall();
}

void TestEventThread::movesCoalesceToLastPositionButDiscreteEventsSurvive()
{
    EventThread events;
    events.postInput(InputKind::PointerDown, pointer(7, 1.0, 1.0, Qt::LeftButton));
    events.postInput(InputKind::PointerMove, pointer(7, 2.0, 2.0));
    events.postInput(InputKind::PointerMove, pointer(7, 3.0, 3.0));
    events.postInput(InputKind::PointerUp, pointer(7, 3.0, 3.0, Qt::LeftButton));

    const auto batch = events.takeBatch();
    QCOMPARE(static_cast<int>(batch.size()), 3);
    QVERIFY(batch.at(0).kind == InputKind::PointerDown);
    QVERIFY(batch.at(1).kind == InputKind::PointerUp);
    QVERIFY(batch.at(2).kind == InputKind::PointerMove);
    QVERIFY(batch.at(2).payload.position == QPointF(3.0, 3.0));
    QCOMPARE(events.stats().coalesced, quint64(1));
}

void TestEventThread::batchesAreOrderedByInputPriority()
{
    EventThread events;
    events.postInput(InputKind::PointerDown, pointer(7, 0.0, 0.0));
    events.postInput(InputKind::PointerMove, pointer(7, 4.0, 4.0));
    events.postInput(InputKind::PointerUp, pointer(7, 4.0, 4.0));
    events.postInput(InputKind::Wheel, pointer(7, 4.0, 4.0));
    events.postInput(InputKind::Key, pointer(7, 0.0, 0.0));
    events.postInput(InputKind::Gesture, pointer(7, 0.0, 0.0));

    const auto batch = events.takeBatch();
    QCOMPARE(static_cast<int>(batch.size()), 6);
    QVERIFY(batch.at(0).kind == InputKind::PointerDown);
    QVERIFY(batch.at(1).kind == InputKind::PointerUp);
    QVERIFY(batch.at(2).kind == InputKind::Key);
    QVERIFY(batch.at(3).kind == InputKind::Wheel);
    QVERIFY(batch.at(4).kind == InputKind::PointerMove);
    QVERIFY(batch.at(5).kind == InputKind::Gesture);
}

void TestEventThread::wheelCoalescesPerTarget()
{
    EventThread events;
    InputPayload first = pointer(7, 0.0, 0.0);
    first.angleDelta = QPoint(0, 120);
    InputPayload second = pointer(7, 0.0, 0.0);
    second.angleDelta = QPoint(0, 240);

    events.postInput(InputKind::Wheel, first);
    events.postInput(InputKind::Wheel, second);

    const auto batch = events.takeBatch();
    QCOMPARE(static_cast<int>(batch.size()), 1);
    QVERIFY(batch.front().kind == InputKind::Wheel);
    QVERIFY(batch.front().payload.angleDelta == QPoint(0, 240));
}

void TestEventThread::movesCoalescePerTarget()
{
    EventThread events;
    events.postInput(InputKind::PointerMove, pointer(7, 1.0, 1.0));
    events.postInput(InputKind::PointerMove, pointer(8, 2.0, 2.0));
    events.postInput(InputKind::PointerMove, pointer(7, 3.0, 3.0));
    events.postInput(InputKind::PointerMove, pointer(8, 4.0, 4.0));

    const auto batch = events.takeBatch();
    QCOMPARE(static_cast<int>(batch.size()), 2);
    QCOMPARE(batch.at(0).payload.target, quint64(7));
    QVERIFY(batch.at(0).payload.position == QPointF(3.0, 3.0));
    QCOMPARE(batch.at(1).payload.target, quint64(8));
    QVERIFY(batch.at(1).payload.position == QPointF(4.0, 4.0));
}

void TestEventThread::keysAreNeverCoalesced()
{
    EventThread events;
    for (int key = 0; key < 4; ++key) {
        InputPayload payload;
        payload.target = 1;
        payload.key = key;
        events.postInput(InputKind::Key, payload);
    }

    const auto batch = events.takeBatch();
    QCOMPARE(static_cast<int>(batch.size()), 4);
    for (int key = 0; key < 4; ++key)
        QCOMPARE(batch.at(static_cast<std::size_t>(key)).payload.key, key);
}

void TestEventThread::frameBoundaryHandsOneBatchToLayout()
{
    UiThread ui;
    EventThread events;
    events.attach(&ui);

    int deliveries = 0;
    QStringList kinds;
    events.setBatchHandler([&](const EventThread::Batch& batch) {
        QVERIFY(ui.phase() == Phase::Layout);
        ++deliveries;
        for (const InputEvent& event : batch) {
            switch (event.kind) {
            case InputKind::PointerDown:
                kinds << QStringLiteral("down");
                break;
            case InputKind::PointerMove:
                kinds << QStringLiteral("move");
                break;
            case InputKind::PointerUp:
                kinds << QStringLiteral("up");
                break;
            default:
                kinds << QStringLiteral("other");
                break;
            }
        }
    });

    events.postInput(InputKind::PointerDown, pointer(7, 1.0, 1.0));
    events.postInput(InputKind::PointerMove, pointer(7, 2.0, 2.0));
    events.postInput(InputKind::PointerMove, pointer(7, 3.0, 3.0));
    events.postInput(InputKind::PointerUp, pointer(7, 3.0, 3.0));

    QCOMPARE(ui.stats().frames, quint64(0));
    QCOMPARE(events.depth(), std::size_t(3));
    QVERIFY(ui.framePending());

    QVERIFY(ui.runFrame());

    QCOMPARE(deliveries, 1);
    QCOMPARE(kinds, (QStringList{QStringLiteral("down"), QStringLiteral("up"),
                                 QStringLiteral("move")}));
    QCOMPARE(events.depth(), std::size_t(0));
    QCOMPARE(events.stats().batches, quint64(1));
    QCOMPARE(events.stats().handoffs, quint64(1));
    QCOMPARE(events.stats().delivered, quint64(3));
}

void TestEventThread::emptyQueueDoesNotRequestAHandoff()
{
    UiThread ui;
    EventThread events;
    events.attach(&ui);

    int deliveries = 0;
    events.setBatchHandler([&deliveries](const EventThread::Batch&) { ++deliveries; });

    QVERIFY(ui.runFrame());
    QCOMPARE(deliveries, 0);
    QCOMPARE(events.stats().handoffs, quint64(0));
}

void TestEventThread::producerThreadCanPostWithoutBlocking()
{
    EventThread events;
    constexpr int samples = 1000;
    std::thread producer([&events, samples] {
        for (int sample = 0; sample < samples; ++sample)
            events.postInput(InputKind::PointerMove, pointer(7, sample, sample));
    });
    producer.join();

    QCOMPARE(events.depth(), std::size_t(1));
    const auto batch = events.takeBatch();
    QCOMPARE(static_cast<int>(batch.size()), 1);
    QVERIFY(batch.front().payload.position == QPointF(samples - 1, samples - 1));
    QCOMPARE(events.stats().coalesced, quint64(samples - 1));
}

void TestEventThread::destroyingUiThreadDetachesSafely()
{
    auto ui = std::make_unique<UiThread>();
    EventThread events;
    events.attach(ui.get());
    events.postInput(InputKind::PointerMove, pointer(7, 1.0, 1.0));

    ui.reset();
    QVERIFY(events.uiThread() == nullptr);

    // A producer that was already in flight must not touch the destroyed
    // runtime; the queue keeps accepting work.
    events.postInput(InputKind::PointerMove, pointer(7, 2.0, 2.0));
    QCOMPARE(events.depth(), std::size_t(1));
}

void TestEventThread::priorityTableIsStable()
{
    QVERIFY(EventThread::priorityOf(InputKind::PointerDown) == 0);
    QVERIFY(EventThread::priorityOf(InputKind::PointerUp) == 0);
    QVERIFY(EventThread::priorityOf(InputKind::Key) == 0);
    QVERIFY(EventThread::priorityOf(InputKind::Wheel) == 1);
    QVERIFY(EventThread::priorityOf(InputKind::PointerMove) == 2);
    QVERIFY(EventThread::priorityOf(InputKind::Gesture) == 3);
}

QTEST_MAIN(TestEventThread)
#include "tst_event_thread.moc"
