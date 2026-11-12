// Periodicity — Scheduler panel tests.
//
// The panel is exercised headlessly: every widget is rendered into an
// explicitly transparent QImage, and the queue assertions are made between
// synchronous calls, so the TaskQueue's event-loop auto-flush never races a
// pending() check. The lifetime cases are the point of the binding: a queue is
// never owned by the panel, rebinding must stop listening to the old queue,
// and a queue destroyed while bound must leave the panel in the unbound state.
//
// The throwing-paint case is contained by View's error guard; the test asserts
// that the Scheduler override does not leak the exception into Qt and that the
// diagnostic reaches lastError().

#include <md/components/scheduler.h>
#include <md/schedule/task_queue.h>

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QRegion>
#include <QTest>

#include <chrono>
#include <stdexcept>

using md::Scheduler;
using md::schedule::Priority;
using md::schedule::TaskQueue;

namespace {

// Rendering into a cleared image keeps unpainted pixels deterministic; the
// pattern matches the other component tests.
QImage renderWidget(QWidget& widget)
{
    const qreal ratio = widget.devicePixelRatioF();
    QImage image(widget.size() * ratio, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    widget.render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();

    return image;
}

bool hasPaintedPixels(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) != 0)
                return true;
        }
    }
    return false;
}

// A panel whose content paint always throws. View's paintEvent guard must
// contain it; the test never expects the exception to escape render().
class ThrowingScheduler : public Scheduler
{
public:
    ThrowingScheduler() = default;

protected:
    void paintContent(QPainter&, const QRectF&) override
    {
        throw std::runtime_error("scheduler paint boom");
    }
};

} // namespace

class TestScheduler : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void defaultBinding();
    void unboundPanelPaintsAndDisables();
    void postingTasksRefreshes();
    void flushQueueEmpties();
    void drainQueueEmptiesCoalescedBacklog();
    void rebindingDisconnectsOldQueue();
    void destroyingSchedulerKeepsQueue();
    void queueDestructionUnbinds();
    void throwingPaintIsContained();
};

void TestScheduler::init()
{
    // Tests that use TaskQueue::main() must not inherit work (or an auto-flush
    // scheduled by it) from an earlier test. Flushing here is synchronous and
    // leaves the queue empty for the next test's pending() assertions.
    TaskQueue::main().flush();
}

void TestScheduler::defaultBinding()
{
    Scheduler scheduler;

    QCOMPARE(scheduler.queue(), &TaskQueue::main());
    QCOMPARE(scheduler.refreshInterval(), 250);
    QCOMPARE(scheduler.batchBudgetMs(), 8);

    scheduler.setRefreshInterval(0);
    QCOMPARE(scheduler.refreshInterval(), 0);

    scheduler.setRefreshInterval(-25);
    QCOMPARE(scheduler.refreshInterval(), 0);

    scheduler.setRefreshInterval(100);
    QCOMPARE(scheduler.refreshInterval(), 100);
}

void TestScheduler::unboundPanelPaintsAndDisables()
{
    Scheduler scheduler;
    scheduler.setQueue(nullptr);
    QVERIFY(scheduler.queue() == nullptr);

    // The actions are no-ops and must not dereference the missing queue.
    scheduler.flushQueue();
    scheduler.drainQueue();

    QVERIFY(scheduler.accessibleName().contains(QStringLiteral("Scheduler")));
    QVERIFY(scheduler.accessibleDescription().contains(QStringLiteral("unbound")));
    QVERIFY(scheduler.accessibleDescription().contains(QStringLiteral("0 tasks pending")));

    scheduler.resize(scheduler.sizeHint());
    const QImage image = renderWidget(scheduler);
    QVERIFY(hasPaintedPixels(image));
}

void TestScheduler::postingTasksRefreshes()
{
    Scheduler scheduler;
    TaskQueue& queue = TaskQueue::main();

    // View's constructor-time setShape()/setElevation()/setInteractionFeedback
    // calls post one coalesced repaint for this view into the shared queue.
    // Run that task first so the assertion below counts only the test's work.
    queue.flush();
    QCOMPARE(queue.pending(), 0);

    queue.post(Priority::UserInput, [] {});
    queue.post(Priority::UiUpdate, [] {});
    queue.post(Priority::Idle, [] {});
    QCOMPARE(queue.pending(), 3);

    scheduler.refresh();
    QCOMPARE(queue.pending(), 3);
    QVERIFY(scheduler.accessibleDescription().contains(QStringLiteral("3 tasks pending")));

    scheduler.flushQueue();
    QCOMPARE(queue.pending(), 0);
    QVERIFY(queue.isEmpty());
}

void TestScheduler::flushQueueEmpties()
{
    int ran = 0;
    TaskQueue queue;
    Scheduler scheduler;
    scheduler.setQueue(&queue);
    queue.setBatchTimeBudget(std::chrono::microseconds(0));

    for (int index = 0; index < 3; ++index)
        queue.post(Priority::UiEvent, [&ran] { ++ran; });

    QCOMPARE(queue.pending(), 3);
    scheduler.refresh();

    scheduler.flushQueue();

    QCOMPARE(ran, 3);
    QVERIFY(queue.isEmpty());
    QVERIFY(scheduler.accessibleDescription().contains(QStringLiteral("0 tasks pending")));
}

void TestScheduler::drainQueueEmptiesCoalescedBacklog()
{
    TaskQueue queue;
    Scheduler scheduler;
    scheduler.setQueue(&queue);
    queue.setBatchTimeBudget(std::chrono::microseconds(0));

    // A burst for one key collapses to one task; then a backlog larger than a
    // single default flush proves Drain keeps pumping until the queue is empty.
    for (int repost = 0; repost < 3; ++repost)
        queue.postCoalesced(Priority::UiUpdate, 17, [] {});
    QCOMPARE(queue.pending(), 1);

    for (int task = 0; task < 300; ++task)
        queue.post(Priority::Background, [] {});

    scheduler.drainQueue();

    QVERIFY(queue.isEmpty());
    QCOMPARE(queue.pending(), 0);
    QVERIFY(queue.retiredCount() == 0);
}

void TestScheduler::rebindingDisconnectsOldQueue()
{
    auto* first = new TaskQueue;
    auto* second = new TaskQueue;
    int changes = 0;

    Scheduler scheduler;
    QObject::connect(&scheduler, &Scheduler::queueChanged, &scheduler,
                     [&changes](TaskQueue*) { ++changes; });

    scheduler.setQueue(first);
    QCOMPARE(scheduler.queue(), first);

    scheduler.setQueue(second);
    QCOMPARE(scheduler.queue(), second);
    QCOMPARE(changes, 2);

    // The old queue must no longer be able to unbind the panel. Deleting it
    // would emit queueChanged(nullptr) if the destroyed() connection survived.
    QPointer<TaskQueue> firstGuard(first);
    delete first;
    QVERIFY(firstGuard.isNull());
    QCOMPARE(changes, 2);
    QCOMPARE(scheduler.queue(), second);

    delete second;
    QCOMPARE(changes, 3);
    QVERIFY(scheduler.queue() == nullptr);
}

void TestScheduler::destroyingSchedulerKeepsQueue()
{
    auto* queue = new TaskQueue;
    QPointer<TaskQueue> guard(queue);

    {
        Scheduler scheduler;
        scheduler.setQueue(queue);
        scheduler.refresh();
        scheduler.resize(scheduler.sizeHint());
        const QImage image = renderWidget(scheduler);
        QVERIFY(hasPaintedPixels(image));
    }

    // The panel was the only thing holding a pointer to the queue; it must not
    // have deleted it on the way out.
    QVERIFY(!guard.isNull());
    delete queue;
}

void TestScheduler::queueDestructionUnbinds()
{
    auto* queue = new TaskQueue;
    int changes = 0;

    Scheduler scheduler;
    scheduler.setQueue(queue);

    QObject::connect(&scheduler, &Scheduler::queueChanged, &scheduler,
                     [&changes](TaskQueue*) { ++changes; });

    delete queue;

    QVERIFY(scheduler.queue() == nullptr);
    QCOMPARE(changes, 1);
    QVERIFY(scheduler.accessibleDescription().contains(QStringLiteral("unbound")));

    scheduler.resize(scheduler.sizeHint());
    const QImage image = renderWidget(scheduler);
    QVERIFY(hasPaintedPixels(image));
}

void TestScheduler::throwingPaintIsContained()
{
    ThrowingScheduler scheduler;
    scheduler.resize(scheduler.sizeHint());

    QImage image(scheduler.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    scheduler.render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();

    QVERIFY(!scheduler.lastError().isEmpty());
    QVERIFY(scheduler.lastError().contains(QStringLiteral("scheduler paint boom")));
}

QTEST_MAIN(TestScheduler)
#include "tst_scheduler.moc"
