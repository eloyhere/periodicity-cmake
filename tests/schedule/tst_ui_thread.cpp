// Periodicity — UI thread frame pipeline tests.
//
// The tests drive frames explicitly (runFrame/runIdle) or with one
// processEvents() pass, never with sleeps. A fake steady clock makes budget and
// deadline behaviour deterministic. The final two cases cover the View seam:
// coalescing through an installed runtime, and the exact fallback when no
// runtime exists.

#include <md/schedule/event_thread.h>
#include <md/schedule/frame_scheduler.h>
#include <md/schedule/task_queue.h>
#include <md/view/view.h>

#include <QApplication>
#include <QCoreApplication>
#include <QPointer>
#include <QTest>

#include <chrono>
#include <functional>

using md::schedule::FrameRequest;
using md::schedule::Phase;
using md::schedule::Priority;
using md::schedule::TaskQueue;
using md::schedule::UiThread;

namespace {

struct FakeClock
{
    UiThread::Clock::time_point now{};

    void advance(std::chrono::microseconds amount) { now += amount; }
};

struct RuntimeGuard
{
    RuntimeGuard() { UiThread::install(); }
    ~RuntimeGuard() { UiThread::uninstall(); }
    RuntimeGuard(const RuntimeGuard&) = delete;
    RuntimeGuard& operator=(const RuntimeGuard&) = delete;
};

// A minimal View with observable painting. It only uses the public pipeline.
class ProbeView : public md::View
{
public:
    ProbeView() = default;

    using md::View::requestUpdate;

    int paints = 0;

protected:
    void measureOverride(md::MeasureSpec width, md::MeasureSpec height) override
    {
        setMeasuredSize(QSizeF(width.resolve(40.0), height.resolve(30.0)));
    }

    void paintContent(QPainter&, const QRectF&) override { ++paints; }
};

} // namespace

class TestUiThread : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void oneFramePerRequestBurst();
    void phaseOrderIsPipelineOrder();
    void idleTasksWaitUntilNoFrameIsPending();
    void expiredTasksAreDroppedAndCounted();
    void budgetOverrunRecordedOnceAndFrameFinishes();
    void frameCallbacksAreOneShot();
    void resizeRequestsKeepTheLastSize();
    void paintStatisticsAverageRecordedIntervals();
    void installPublishesOneProcessRuntime();
    void viewRequestUpdateCoalescesToOneFrame();
    void fallbackWithoutUiThreadStillUpdates();
};

void TestUiThread::init()
{
    md::schedule::EventThread::uninstall();
    UiThread::uninstall();
    TaskQueue::main().flush();
}

void TestUiThread::oneFramePerRequestBurst()
{
    UiThread ui;
    int callbacks = 0;
    ui.addFrameCallback([&callbacks] { ++callbacks; });

    for (int request = 0; request < 10; ++request)
        ui.requestFrame(FrameRequest::Update);

    QCOMPARE(ui.stats().frames, quint64(0));
    QVERIFY(ui.framePending());

    QCoreApplication::processEvents();

    QCOMPARE(ui.stats().frames, quint64(1));
    QCOMPARE(ui.frameNumber(), quint64(1));
    QCOMPARE(callbacks, 1);

    // The queued frame was consumed; no stale invocation may produce a second
    // frame in the next turn.
    QCoreApplication::processEvents();
    QCOMPARE(ui.stats().frames, quint64(1));
}

void TestUiThread::phaseOrderIsPipelineOrder()
{
    UiThread ui;
    QStringList order;

    ui.addFrameCallback([&] {
        order << QStringLiteral("PreFrame");
        QVERIFY(ui.phase() == Phase::PreFrame);
    });
    ui.postTask(Priority::UserInput, [&] {
        order << QStringLiteral("Build");
        QVERIFY(ui.phase() == Phase::Build);
    });
    ui.postTask(Priority::UiEvent, [&] {
        order << QStringLiteral("Layout");
        QVERIFY(ui.phase() == Phase::Layout);
    });
    ui.addPaintCallback([&] {
        order << QStringLiteral("Paint");
        QVERIFY(ui.phase() == Phase::Paint);
    });
    ui.postTask(Priority::Background, [&] {
        order << QStringLiteral("PostFrame");
        QVERIFY(ui.phase() == Phase::PostFrame);
    });

    QVERIFY(ui.runFrame());

    QCOMPARE(order,
             (QStringList{QStringLiteral("PreFrame"), QStringLiteral("Build"),
                          QStringLiteral("Layout"), QStringLiteral("Paint"),
                          QStringLiteral("PostFrame")}));
    QVERIFY(ui.phase() == Phase::Idle);
}

void TestUiThread::idleTasksWaitUntilNoFrameIsPending()
{
    UiThread ui;
    int idleRuns = 0;
    ui.postIdleTask([&idleRuns] { ++idleRuns; });

    ui.requestFrame();
    QCOMPARE(ui.runIdle(), 0);
    QCOMPARE(idleRuns, 0);

    QVERIFY(ui.runFrame());
    QCOMPARE(idleRuns, 0); // A frame never drains postIdleTask() work.

    QCOMPARE(ui.runIdle(), 1);
    QCOMPARE(idleRuns, 1);
}

void TestUiThread::expiredTasksAreDroppedAndCounted()
{
    FakeClock clock;
    UiThread ui;
    ui.setClockForTesting([&clock] { return clock.now; });
    ui.setFrameBudget(std::chrono::milliseconds(100));

    int ran = 0;
    ui.postTask(Priority::UiUpdate, [&ran] { ++ran; },
                std::chrono::microseconds(500));
    ui.postTask(Priority::Background, [&ran] { ++ran; },
                std::chrono::seconds(10));

    clock.advance(std::chrono::milliseconds(1));
    ui.requestFrame();
    QVERIFY(ui.runFrame());

    QCOMPARE(ran, 1); // Only the still-valid background task ran.
    QCOMPARE(ui.droppedTasks(), quint64(1));
    QCOMPARE(ui.stats().droppedTasks, quint64(1));
}

void TestUiThread::budgetOverrunRecordedOnceAndFrameFinishes()
{
    FakeClock clock;
    UiThread ui;
    ui.setClockForTesting([&clock] { return clock.now; });
    ui.setFrameBudget(std::chrono::microseconds(100));

    QStringList phases;
    ui.postTask(Priority::UiUpdate, [&] {
        phases << QStringLiteral("Build");
        clock.advance(std::chrono::milliseconds(1));
    }, std::chrono::seconds(10));
    ui.addPaintCallback([&] { phases << QStringLiteral("Paint"); });
    ui.postTask(Priority::Background, [&] { phases << QStringLiteral("PostFrame"); },
                std::chrono::seconds(10));

    ui.requestFrame();
    QVERIFY(ui.runFrame());

    // Build overran; Paint and the phase transition still happened, but no
    // further queued work was started.
    QCOMPARE(phases, (QStringList{QStringLiteral("Build"), QStringLiteral("Paint")}));
    QCOMPARE(ui.stats().overruns, quint64(1));
    QCOMPARE(ui.stats().droppedFrames, quint64(1));
    QCOMPARE(ui.stats().frames, quint64(1));

    // A later frame has a fresh budget and does not inherit the overrun.
    QVERIFY(ui.runFrame());
    QCOMPARE(ui.stats().overruns, quint64(1));
}

void TestUiThread::frameCallbacksAreOneShot()
{
    UiThread ui;
    int callbacks = 0;
    ui.addFrameCallback([&callbacks] { ++callbacks; });

    QVERIFY(ui.runFrame());
    QVERIFY(ui.runFrame());

    QCOMPARE(callbacks, 1);
}

void TestUiThread::resizeRequestsKeepTheLastSize()
{
    UiThread ui;

    for (int size = 1; size <= 5; ++size)
        ui.requestResize(42, QSizeF(size, size * 2));

    QCOMPARE(ui.pendingResizeCount(), 1);
    QVERIFY(ui.pendingResize(42) == QSizeF(5, 10));

    ui.requestResize(43, QSizeF(7, 8));
    QCOMPARE(ui.pendingResizeCount(), 2);

    const auto requests = ui.takeResizeRequests();
    QCOMPARE(static_cast<int>(requests.size()), 2);
    QCOMPARE(ui.pendingResizeCount(), 0);
}

void TestUiThread::paintStatisticsAverageRecordedIntervals()
{
    UiThread ui;
    ui.recordPaint(std::chrono::microseconds(100));
    ui.recordPaint(std::chrono::microseconds(300));

    const UiThread::FrameStats stats = ui.stats();
    QCOMPARE(stats.meanPaintUs, 200.0);
    QCOMPARE(stats.maxPaintUs, 300.0);
}

void TestUiThread::installPublishesOneProcessRuntime()
{
    RuntimeGuard runtime;
    UiThread& installed = UiThread::install();

    QCOMPARE(UiThread::instance(), &installed);
    QCOMPARE(UiThread::instance(), &UiThread::install());
    QVERIFY(installed.isInstalled());
}

void TestUiThread::viewRequestUpdateCoalescesToOneFrame()
{
    RuntimeGuard runtime;
    UiThread* ui = UiThread::instance();
    QVERIFY(ui != nullptr);

    ProbeView view;
    view.show();
    QCoreApplication::processEvents();
    const quint64 before = ui->stats().frames;

    for (int request = 0; request < 16; ++request)
        view.requestUpdate();

    QVERIFY(ui->runFrame());
    QCOMPARE(ui->stats().frames, before + 1);
    QCOMPARE(ui->pending(Priority::UiUpdate), 0);

    QCoreApplication::processEvents();
    QCOMPARE(ui->stats().frames, before + 1);
}

void TestUiThread::fallbackWithoutUiThreadStillUpdates()
{
    QVERIFY(UiThread::instance() == nullptr);

    ProbeView view;
    view.show();
    QCoreApplication::processEvents();
    view.paints = 0;

    // The fallback is the historical path: update() directly, plus the
    // coalesced TaskQueue entry that embedders may still pump themselves.
    view.requestUpdate();
    QCOMPARE(TaskQueue::main().pending(Priority::UiUpdate), 1);

    QTRY_VERIFY_WITH_TIMEOUT(view.paints > 0, 2000);
}

QTEST_MAIN(TestUiThread)
#include "tst_ui_thread.moc"
