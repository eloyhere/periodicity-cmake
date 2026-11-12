// Periodicity — task priority queue tests (framework.md §5.12).
//
// The contract under test is the ordering guarantee: user input overtakes UI
// work, UI work overtakes UI events, and a flush never lets a lower priority
// delay a higher one that arrived while it was draining.

#include <md/schedule/task_queue.h>

#include <QSignalSpy>
#include <QStringList>
#include <QTest>

#include <chrono>

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

QTEST_MAIN(TestTaskQueue)
#include "tst_schedule.moc"
