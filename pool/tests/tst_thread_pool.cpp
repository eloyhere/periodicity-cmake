// Periodicity — thread pool tests.
//
// The pool has three promises to keep: every accepted task runs exactly once,
// a stopped pool never runs a task it rejected, and the task-node pool it
// recycles from is empty again once the workers are joined. The suite checks
// those, plus the pause/resume and parallelFor paths the UI drives.

#include <md/pool/thread_pool.h>

#include <QtTest/QtTest>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

using md::pool::ThreadPool;
using md::pool::ThreadPoolOptions;

namespace {

using namespace std::chrono_literals;

bool waitForActive(ThreadPool& pool, std::size_t expected, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (pool.stats().active == expected)
            return true;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

} // namespace

class TestThreadPool : public QObject
{
    Q_OBJECT

private slots:
    void defaultsAndWorkerCount();
    void runsEveryPostedTask();
    void submitReturnsValue();
    void rejectsWhenFull();
    void pauseHoldsWork();
    void stopDrainsQueuedWork();
    void stopDiscardsQueuedWork();
    void parallelForVisitsEveryIndex();
    void parallelForSurvivesThrowingBody();
    void parallelForSurvivesDiscard();
    void recyclesTaskNodes();
    void throwingTaskDoesNotKillPool();
    void restartAfterStop();
};

void TestThreadPool::defaultsAndWorkerCount()
{
    ThreadPool pool(ThreadPoolOptions{.workers = 3,
                                      .queueCapacity = 64,
                                      .name = QStringLiteral("tst-pool")});

    QCOMPARE(pool.workerCount(), std::size_t(3));
    QCOMPARE(pool.stats().workers, std::size_t(3));
    QVERIFY(pool.stats().running);
    QVERIFY(!pool.stats().paused);
    QCOMPARE(pool.stats().queued, std::size_t(0));
    QCOMPARE(pool.stats().active, std::size_t(0));
}

void TestThreadPool::runsEveryPostedTask()
{
    ThreadPool pool(ThreadPoolOptions{.workers = 4});

    std::atomic<int> count{0};
    for (int index = 0; index < 2000; ++index) {
        QVERIFY(pool.post([&count] { count.fetch_add(1, std::memory_order_relaxed); }));
    }

    QVERIFY(pool.drain(10s));
    QCOMPARE(count.load(), 2000);
    QCOMPARE(pool.stats().completed, std::uint64_t(2000));
    QCOMPARE(pool.stats().queued, std::size_t(0));
    QCOMPARE(pool.stats().active, std::size_t(0));
}

void TestThreadPool::submitReturnsValue()
{
    ThreadPool pool(ThreadPoolOptions{.workers = 2});

    std::future<int> answer = pool.submit([] { return 42; });
    QCOMPARE(answer.get(), 42);

    const int captured = 7;
    std::future<int> doubled = pool.submit([captured] { return captured * 2; });
    QCOMPARE(doubled.get(), 14);
}

void TestThreadPool::rejectsWhenFull()
{
    ThreadPool pool(ThreadPoolOptions{.workers = 1, .queueCapacity = 1});

    std::atomic<bool> started{false};
    std::atomic<bool> release{false};

    QVERIFY(pool.post([&started, &release] {
        started.store(true);
        while (!release.load(std::memory_order_relaxed))
            std::this_thread::yield();
    }));

    QVERIFY(waitForActive(pool, 1, 2000));

    QVERIFY(pool.post([] {}));          // the one queue slot
    QVERIFY(!pool.post([] {}));         // over capacity: rejected, not run
    QCOMPARE(pool.stats().rejected, std::uint64_t(1));

    release.store(true);
    QVERIFY(pool.drain(5s));
}

void TestThreadPool::pauseHoldsWork()
{
    ThreadPool pool(ThreadPoolOptions{.workers = 2});

    std::atomic<int> count{0};
    pool.pause();
    QVERIFY(pool.stats().paused);

    QVERIFY(pool.post([&count] { count.fetch_add(1); }));
    QTest::qWait(100);
    QCOMPARE(count.load(), 0);

    pool.resume();
    QVERIFY(pool.drain(5s));
    QCOMPARE(count.load(), 1);
}

void TestThreadPool::stopDrainsQueuedWork()
{
    ThreadPool pool(ThreadPoolOptions{.workers = 2});

    std::atomic<int> count{0};
    for (int index = 0; index < 50; ++index) {
        QVERIFY(pool.post([&count] {
            std::this_thread::sleep_for(1ms);
            count.fetch_add(1);
        }));
    }

    pool.stop(true);

    QCOMPARE(count.load(), 50);
    QCOMPARE(pool.stats().dropped, std::uint64_t(0));
    QVERIFY(!pool.stats().running);
    QCOMPARE(pool.stats().tasks.live, std::size_t(0));
}

void TestThreadPool::stopDiscardsQueuedWork()
{
    ThreadPool pool(ThreadPoolOptions{.workers = 1});

    pool.pause();
    for (int index = 0; index < 10; ++index)
        QVERIFY(pool.post([] {}));

    pool.stop(false);

    QCOMPARE(pool.stats().dropped, std::uint64_t(10));
    QCOMPARE(pool.stats().tasks.live, std::size_t(0));
    QVERIFY(!pool.post([] {}));
}

void TestThreadPool::parallelForVisitsEveryIndex()
{
    ThreadPool pool(ThreadPoolOptions{.workers = 4});

    constexpr std::size_t kCount = 5000;
    std::vector<int> hits(kCount, 0);

    // Each index is claimed by exactly one worker, so distinct elements of the
    // vector are written by distinct threads and no lock is needed.
    pool.parallelFor(kCount, [&hits](std::size_t index) { ++hits[index]; });

    for (std::size_t index = 0; index < kCount; ++index)
        QCOMPARE(hits[index], 1);
}

void TestThreadPool::parallelForSurvivesThrowingBody()
{
    ThreadPool pool(ThreadPoolOptions{.workers = 4});

    // The body throws on one index. parallelFor must still return and the pool
    // must stay usable; before the fix the chunk skipped its count_down(), so
    // this call never returned and the suite timed out instead of failing.
    bool returned = false;
    try {
        pool.parallelFor(64, [](std::size_t index) {
            if (index == 7)
                throw std::runtime_error("body failed");
        });
        returned = true;
    } catch (...) {
        returned = false;
    }

    QVERIFY(returned);
    QVERIFY(pool.stats().failed >= 1);
    QCOMPARE(pool.stats().active, std::size_t(0));
    QCOMPARE(pool.stats().tasks.live, std::size_t(0));

    std::atomic<int> ran{0};
    QVERIFY(pool.post([&ran] { ran.fetch_add(1, std::memory_order_relaxed); }));
    QVERIFY(pool.drain(5s));
    QCOMPARE(ran.load(), 1);
}

void TestThreadPool::parallelForSurvivesDiscard()
{
    ThreadPool pool(ThreadPoolOptions{.workers = 1, .queueCapacity = 64});

    // Occupy the only worker, so all four chunks of the parallelFor below stay
    // queued when stop(false) discards the queue.
    std::atomic<bool> release{false};
    QVERIFY(pool.post([&release] {
        while (!release.load(std::memory_order_acquire))
            std::this_thread::yield();
    }));
    QVERIFY(waitForActive(pool, 1, 2000));

    std::thread runner([&pool, &release] {
        pool.parallelFor(16, [](std::size_t) {});
        // Reached only after the discarded chunks released their latches; that
        // is the wake-up stop(false) below needs to finish its join.
        release.store(true, std::memory_order_release);
    });

    // Wait until parallelFor has queued all of its chunks.
    QElapsedTimer timer;
    timer.start();
    while (pool.stats().queued < 4 && timer.elapsed() < 5000)
        std::this_thread::sleep_for(1ms);
    const std::size_t queued = pool.stats().queued;

    // Always finish the shutdown, even when the expectation above fails: a
    // joinable std::thread in a test would call std::terminate instead of
    // reporting the failure.
    pool.stop(false);
    release.store(true, std::memory_order_release);
    runner.join();

    QCOMPARE(queued, std::size_t(4));
    QCOMPARE(pool.stats().dropped, std::uint64_t(4));
    QCOMPARE(pool.stats().tasks.live, std::size_t(0));
}

void TestThreadPool::recyclesTaskNodes()
{
    ThreadPool pool(ThreadPoolOptions{.workers = 4, .queueCapacity = 4096});

    std::atomic<int> count{0};
    for (int index = 0; index < 4000; ++index)
        QVERIFY(pool.post([&count] { count.fetch_add(1, std::memory_order_relaxed); }));

    QVERIFY(pool.drain(10s));

    const ThreadPool::Stats stats = pool.stats();
    QCOMPARE(stats.tasks.live, std::size_t(0));
    QVERIFY(stats.tasks.capacity > 0);
    QVERIFY(stats.tasks.capacity >= stats.tasks.highWaterMark);
    // The pool hands nodes back, so its capacity tracks the peak concurrent
    // task count rather than the 4000 submissions.
    QVERIFY(stats.tasks.capacity <= 8192);
    QVERIFY(stats.tasks.chunks > 0);
}

void TestThreadPool::throwingTaskDoesNotKillPool()
{
    ThreadPool pool(ThreadPoolOptions{.workers = 2});

    std::atomic<int> ran{0};
    QVERIFY(pool.post([] { throw std::runtime_error("boom"); }));
    QVERIFY(pool.post([&ran] { ran.fetch_add(1); }));

    QVERIFY(pool.drain(5s));
    QCOMPARE(ran.load(), 1);
    QCOMPARE(pool.stats().failed, std::uint64_t(1));
}

void TestThreadPool::restartAfterStop()
{
    ThreadPool pool(ThreadPoolOptions{.workers = 2});

    pool.stop(true);
    QVERIFY(!pool.post([] {}));

    pool.start();
    QVERIFY(pool.stats().running);

    std::atomic<int> ran{0};
    QVERIFY(pool.post([&ran] { ran.fetch_add(1); }));
    QVERIFY(pool.drain(5s));
    QCOMPARE(ran.load(), 1);
}

QTEST_GUILESS_MAIN(TestThreadPool)

#include "tst_thread_pool.moc"
