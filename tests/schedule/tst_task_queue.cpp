// Periodicity — task priority queue tests (framework.md §5.12).
//
// The contract under test is the ordering guarantee: user input overtakes UI
// work, UI work overtakes UI events, and a flush never lets a lower priority
// delay a higher one that arrived while it was draining. The failure and
// lifetime edges are covered too: a throwing task must not wedge the queue, a
// coalesced key must stay bounded and position-stable across reposts, cancel
// must report whether anything was pending, and a task may destroy the queue.

#include <md/schedule/task_queue.h>

#include <QPointer>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>

#include <chrono>
#include <stdexcept>

using md::schedule::Priority;
using md::schedule::TaskQueue;

class TestTaskQueue : public QObject
{
    Q_OBJECT

private slots:
    void runsInPriorityOrder();
    void preservesFifoWithinPriority();
    void userInputJumpsAheadOfRunningBatch();
    void coalescesByKey();
    void cancelDropsPendingTask();
    void reentrantPostRunsInSameFlush();
    void flushBudgetLeavesWorkAndReschedules();
    void pendingCountsIgnoreRetiredEntries();
    void throwingTaskDoesNotWedgeQueue();
    void coalescedRepostsKeepRetiredIdsBounded();
    void cancelReportsWhetherTaskWasPending();
    void coalescedRepostKeepsPriorityAndPosition();
    void negativeBudgetMeansNoBudget();
    void destructionFromTaskIsSafe();
};

void TestTaskQueue::runsInPriorityOrder()
{
    TaskQueue queue;
    QStringList order;
    queue.setBatchTimeBudget(std::chrono::microseconds(0));

    queue.post(Priority::Idle, [&order] { order << QStringLiteral("idle"); });
    queue.post(Priority::Background, [&order] { order << QStringLiteral("background"); });
    queue.post(Priority::UiEvent, [&order] { order << QStringLiteral("event"); });
    queue.post(Priority::UiUpdate, [&order] { order << QStringLiteral("update"); });
    queue.post(Priority::UserInput, [&order] { order << QStringLiteral("user"); });

    QCOMPARE(queue.flush(), 5);
    QCOMPARE(order, (QStringList{QStringLiteral("user"), QStringLiteral("update"),
                                QStringLiteral("event"), QStringLiteral("background"),
                                QStringLiteral("idle")}));
}

void TestTaskQueue::preservesFifoWithinPriority()
{
    TaskQueue queue;
    QStringList order;

    queue.post(Priority::UiUpdate, [&order] { order << QStringLiteral("a"); });
    queue.post(Priority::UiUpdate, [&order] { order << QStringLiteral("b"); });
    queue.post(Priority::UiUpdate, [&order] { order << QStringLiteral("c"); });

    queue.flush();
    QCOMPARE(order, (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
}

void TestTaskQueue::userInputJumpsAheadOfRunningBatch()
{
    TaskQueue queue;
    QStringList order;

    queue.post(Priority::UiEvent, [&queue, &order] {
        order << QStringLiteral("event-1");
        queue.post(Priority::UserInput, [&order] { order << QStringLiteral("user"); });
    });
    queue.post(Priority::UiEvent, [&order] { order << QStringLiteral("event-2"); });

    queue.flush();

    QCOMPARE(order.size(), 3);
    QCOMPARE(order.at(0), QStringLiteral("event-1"));
    QCOMPARE(order.at(1), QStringLiteral("user"));
    QCOMPARE(order.at(2), QStringLiteral("event-2"));
}

void TestTaskQueue::coalescesByKey()
{
    TaskQueue queue;
    QStringList order;

    queue.postCoalesced(Priority::UiUpdate, 7, [&order] { order << QStringLiteral("first"); });
    queue.postCoalesced(Priority::UiUpdate, 7, [&order] { order << QStringLiteral("second"); });

    queue.flush();

    QCOMPARE(order, QStringList{QStringLiteral("second")});
}

void TestTaskQueue::cancelDropsPendingTask()
{
    TaskQueue queue;
    QStringList order;

    queue.postCoalesced(Priority::Background, 42, [&order] { order << QStringLiteral("never"); });
    queue.cancel(42);

    QVERIFY(queue.isEmpty());
    queue.flush();
    QVERIFY(order.isEmpty());
}

void TestTaskQueue::reentrantPostRunsInSameFlush()
{
    TaskQueue queue;
    QStringList order;

    queue.post(Priority::UserInput, [&queue, &order] {
        order << QStringLiteral("outer");
        queue.post(Priority::UiUpdate, [&order] { order << QStringLiteral("inner"); });
    });

    queue.flush();

    QCOMPARE(order, (QStringList{QStringLiteral("outer"), QStringLiteral("inner")}));
}

void TestTaskQueue::flushBudgetLeavesWorkAndReschedules()
{
    TaskQueue queue;
    QStringList order;

    for (int index = 0; index < 4; ++index)
        queue.post(Priority::UiEvent, [&order, index] { order << QString::number(index); });

    QCOMPARE(queue.flush(2), 2);
    QVERIFY(!queue.isEmpty());

    // The remaining work is rescheduled on the event loop, not lost.
    QTRY_VERIFY_WITH_TIMEOUT(queue.isEmpty(), 2000);
    QCOMPARE(order.size(), 4);
}

void TestTaskQueue::pendingCountsIgnoreRetiredEntries()
{
    TaskQueue queue;

    queue.post(Priority::UserInput, [] {});
    queue.post(Priority::UserInput, [] {});
    queue.post(Priority::Idle, [] {});

    queue.postCoalesced(Priority::UiUpdate, 5, [] {});
    queue.postCoalesced(Priority::UiUpdate, 5, [] {});

    QCOMPARE(queue.pending(Priority::UserInput), 2);
    QCOMPARE(queue.pending(Priority::UiUpdate), 1);
    QCOMPARE(queue.pending(Priority::Idle), 1);
    QCOMPARE(queue.pending(), 4);
    QVERIFY(!queue.isEmpty());
}

void TestTaskQueue::throwingTaskDoesNotWedgeQueue()
{
    TaskQueue queue;
    queue.setBatchTimeBudget(std::chrono::microseconds(0));
    QSignalSpy drained(&queue, &TaskQueue::drained);
    QStringList order;

    queue.post(Priority::UserInput, [] { throw std::runtime_error("task failure"); });
    queue.post(Priority::UiUpdate, [&order] { order << QStringLiteral("after-throw"); });

    QCOMPARE(queue.flush(), 2);
    QCOMPARE(order, QStringList{QStringLiteral("after-throw")});
    QVERIFY(queue.isEmpty());
    QCOMPARE(drained.count(), 1);

    // flushing_ was reset with the drain, so a later post still runs and a
    // later flush still accepts work.
    queue.post(Priority::UiEvent, [&order] { order << QStringLiteral("later"); });
    QCOMPARE(queue.flush(), 1);
    QCOMPARE(order.size(), 2);
}

void TestTaskQueue::coalescedRepostsKeepRetiredIdsBounded()
{
    TaskQueue queue;
    queue.setBatchTimeBudget(std::chrono::microseconds(0));
    int runs = 0;

    // The leak path from the issue: drain, then repost the same key, repeated.
    // Each cycle must leave neither a retired id nor a coalescing entry behind.
    for (int cycle = 0; cycle < 1000; ++cycle) {
        queue.postCoalesced(Priority::UiUpdate, 11, [&runs] { ++runs; });
        QCOMPARE(queue.flush(), 1);
        QCOMPARE(queue.retiredCount(), 0);
        QVERIFY(queue.isEmpty());
    }
    QCOMPARE(runs, 1000);

    // A burst of reposts before one flush still collapses to a single run.
    for (int repost = 0; repost < 1000; ++repost)
        queue.postCoalesced(Priority::UiUpdate, 12, [&runs] { ++runs; });

    QCOMPARE(queue.retiredCount(), 0);
    QCOMPARE(queue.pending(Priority::UiUpdate), 1);
    QCOMPARE(queue.flush(), 1);
    QCOMPARE(runs, 1001);
    QCOMPARE(queue.retiredCount(), 0);
}

void TestTaskQueue::cancelReportsWhetherTaskWasPending()
{
    TaskQueue queue;
    queue.setBatchTimeBudget(std::chrono::microseconds(0));

    QVERIFY(!queue.cancel(1234));

    bool cancelledFromTask = true;
    queue.postCoalesced(Priority::Background, 7, [&queue, &cancelledFromTask] {
        // The entry has already left the queue, so a running task cannot be
        // cancelled and the caller can tell.
        cancelledFromTask = queue.cancel(7);
    });

    QCOMPARE(queue.flush(), 1);
    QVERIFY(!cancelledFromTask);
    QVERIFY(!queue.cancel(7));

    queue.postCoalesced(Priority::Idle, 8, [] {});
    QVERIFY(queue.cancel(8));
    QVERIFY(!queue.cancel(8)); // Already cancelled.
    QVERIFY(queue.isEmpty());
}

void TestTaskQueue::coalescedRepostKeepsPriorityAndPosition()
{
    TaskQueue queue;
    queue.setBatchTimeBudget(std::chrono::microseconds(0));
    QStringList order;

    queue.postCoalesced(Priority::UiEvent, 1,
                        [&order] { order << QStringLiteral("key-1-first"); });
    queue.postCoalesced(Priority::UiEvent, 2,
                        [&order] { order << QStringLiteral("key-2"); });

    // The repost asks for UserInput, but the pending entry keeps the priority
    // and position of its first post: it stays behind key 2 in UiEvent.
    queue.postCoalesced(Priority::UserInput, 1,
                        [&order] { order << QStringLiteral("key-1-second"); });

    QCOMPARE(queue.pending(Priority::UserInput), 0);
    QCOMPARE(queue.pending(Priority::UiEvent), 2);
    QCOMPARE(queue.flush(), 2);
    QCOMPARE(order, (QStringList{QStringLiteral("key-1-second"), QStringLiteral("key-2")}));
}

void TestTaskQueue::negativeBudgetMeansNoBudget()
{
    TaskQueue queue;
    queue.setBatchTimeBudget(std::chrono::microseconds(-1000));
    int ran = 0;

    for (int index = 0; index < 3; ++index)
        queue.post(Priority::Idle, [&ran] { ++ran; });

    QCOMPARE(queue.flush(), 3);
    QCOMPARE(ran, 3);
}

void TestTaskQueue::destructionFromTaskIsSafe()
{
    TaskQueue* queue = new TaskQueue;
    queue->setBatchTimeBudget(std::chrono::microseconds(0));
    QPointer<TaskQueue> guard(queue);

    queue->post(Priority::UserInput, [queue] { delete queue; });
    queue->flush();

    QVERIFY(guard.isNull());
}

QTEST_MAIN(TestTaskQueue)
#include "tst_task_queue.moc"
