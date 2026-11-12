// Periodicity — thread pool (pool).
//
// A fixed-size worker pool with a bounded hand-off queue. Three entry points,
// one queue:
//
//   post(task)          fire and forget; false when the pool is stopped or full
//   submit(fn)          a std::future for the result; the task runs on a worker
//   parallelFor(n, fn)  splits [0, n) across the workers and waits for all of it
//
// The queue is a mutex + condition variable around a deque of task *nodes*.
// The nodes come from md::pool::ObjectPool<TaskNode>: a task that has run
// is recycled rather than allocated again, which is why the pool's
// capacity()/liveCount()/highWaterMark() are part of the statistics the UI
// shows. ObjectPool is not thread-safe, so every create/destroy happens while
// the queue mutex is held — the pool never sees two threads at once.
//
// Shutdown contract:
//   * stop(true)  drains what is queued, then joins; running tasks finish.
//   * stop(false) rejects the queued tasks (they are destroyed, not run), joins.
//   * the destructor calls stop(true).
// After either variant the task-node pool is empty again, which is the
// invariant ObjectPool's destructor asserts.
//
// post() is the only call a UI thread makes per frame; it takes one mutex and
// pushes a pointer, so a slow worker can never block the caller for longer than
// that.

#pragma once

#include <md/pool/object_pool.h>

#include <QString>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace md::pool {

struct ThreadPoolOptions
{
    // 0 asks for hardware_concurrency(), clamped to [1, 32].
    std::size_t workers = 0;

    // Tasks that may wait for a worker before post() starts rejecting. 0 means
    // unbounded: backpressure is a choice, so the default is a finite queue.
    std::size_t queueCapacity = 4096;

    // Prefix for the worker thread names, e.g. "md-worker-2".
    QString name = QStringLiteral("md-worker");
};

class ThreadPool
{
public:
    // Mirror of ObjectPool's introspection, read under the queue lock.
    struct PoolStats
    {
        std::size_t capacity = 0;      // task nodes the pool can hold without allocating
        std::size_t available = 0;     // nodes on the free list
        std::size_t live = 0;          // queued + running tasks
        std::size_t highWaterMark = 0; // most tasks alive at once so far
        std::size_t chunks = 0;        // chunk allocations backing the pool
    };

    struct Stats
    {
        std::size_t workers = 0;
        std::size_t queued = 0;
        std::size_t active = 0;
        std::uint64_t submitted = 0;
        std::uint64_t completed = 0;
        std::uint64_t rejected = 0; // post() refused: stopped, paused or full
        std::uint64_t dropped = 0;  // queued tasks discarded by stop(false)
        std::uint64_t failed = 0;   // tasks that threw; the pool keeps running
        bool running = false;
        bool paused = false;
        PoolStats tasks;
    };

    explicit ThreadPool(ThreadPoolOptions options = ThreadPoolOptions{});
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Idempotent. start() after stop() is supported: the worker set is rebuilt
    // and the pool accepts work again.
    void start();
    void stop(bool drain = true);

    // Paused workers keep their slots but run nothing; already-running tasks
    // finish. Queued tasks stay queued.
    void pause();
    void resume();

    // Waits until the queue is empty and no task is running. Returns false on
    // timeout, so a UI can call it without risking a permanent freeze.
    [[nodiscard]] bool drain(std::chrono::milliseconds timeout = std::chrono::seconds(5));

    // Queues a task. Returns false when the pool is stopped, paused-retired or
    // the queue is full; the task is then not stored anywhere.
    bool post(std::function<void()> task);

    // Queues a callable and returns a future for its result. Throws
    // std::runtime_error when the task could not be queued (pool stopped/full).
    template <class Fn, class Result = std::invoke_result_t<std::decay_t<Fn>>>
    [[nodiscard]] std::future<Result> submit(Fn&& fn)
    {
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Fn>(fn));
        std::future<Result> future = task->get_future();
        if (!post([task] { (*task)(); }))
            throw std::runtime_error("md::pool::ThreadPool::submit: task was not accepted");
        return future;
    }

    // Runs body(0..count) across the workers and blocks until every index has
    // been visited exactly once. Must not be called from a worker: all workers
    // could be inside parallelFor and the wait would never end (asserted).
    void parallelFor(std::size_t count, std::function<void(std::size_t)> body);

    [[nodiscard]] Stats stats() const;
    [[nodiscard]] std::size_t workerCount() const noexcept { return workerCount_; }

    // Process-wide pool used when no pool is owned explicitly. Constructed on
    // first use with the default options.
    static ThreadPool& global();

private:
    struct TaskNode
    {
        std::function<void()> fn;
    };

    void startLocked();
    void workerLoop(std::size_t index);
    void discardQueuedLocked();

    ThreadPoolOptions options_;
    std::size_t workerCount_ = 1;
    std::size_t queueCapacity_ = 0;

    mutable std::mutex mutex_;
    std::condition_variable workReady_;
    std::condition_variable idle_;

    std::vector<std::thread> workers_;
    std::deque<TaskNode*> ready_;
    md::pool::ObjectPool<TaskNode> nodes_;

    std::size_t active_ = 0;
    std::uint64_t submitted_ = 0;
    std::uint64_t completed_ = 0;
    std::uint64_t rejected_ = 0;
    std::uint64_t dropped_ = 0;
    std::uint64_t failed_ = 0;
    bool running_ = false;
    bool paused_ = false;
    bool stopping_ = false;
    bool drainOnStop_ = true;
};

} // namespace md::pool
