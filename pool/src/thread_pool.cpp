#include <md/pool/thread_pool.h>

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <latch>
#include <string>
#include <system_error>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#elif defined(Q_OS_UNIX)
#  include <pthread.h>
#endif

namespace md::pool {
namespace {

constexpr std::size_t kMaxWorkers = 32;

// True only on a thread owned by a ThreadPool. parallelFor() asserts on it:
// waiting for work while holding a worker would deadlock the whole pool.
thread_local bool t_isPoolWorker = false;

void setCurrentThreadName(const QString& name)
{
#ifdef Q_OS_WIN
    using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);

    static const SetThreadDescriptionFn setThreadDescription = [] {
        const HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
        if (kernel == nullptr)
            return static_cast<SetThreadDescriptionFn>(nullptr);

        const auto raw = ::GetProcAddress(kernel, "SetThreadDescription");
        if (raw == nullptr)
            return static_cast<SetThreadDescriptionFn>(nullptr);

        // SetThreadDescription is declared only for recent SDKs; resolving it
        // by name keeps the module compiling against the whole Windows 10 range.
        SetThreadDescriptionFn fn = nullptr;
        static_assert(sizeof(fn) == sizeof(raw), "function pointers share a size");
        std::memcpy(&fn, &raw, sizeof(fn));
        return fn;
    }();

    if (setThreadDescription != nullptr) {
        const std::wstring wide = name.toStdWString();
        setThreadDescription(::GetCurrentThread(), wide.c_str());
    }
#elif defined(Q_OS_UNIX)
    const QByteArray utf8 = name.toUtf8();
    // POSIX truncates at 15 characters, which is enough to tell workers apart.
    ::pthread_setname_np(::pthread_self(), utf8.constData());
#else
    Q_UNUSED(name);
#endif
}

std::size_t resolveWorkerCount(std::size_t requested)
{
    if (requested == 0) {
        const unsigned int hardware = std::thread::hardware_concurrency();
        requested = hardware == 0 ? 4 : static_cast<std::size_t>(hardware);
    }
    return std::clamp<std::size_t>(requested, 1, kMaxWorkers);
}

} // namespace

ThreadPool::ThreadPool(ThreadPoolOptions options)
    : options_(std::move(options))
    , workerCount_(resolveWorkerCount(options_.workers))
    , queueCapacity_(options_.queueCapacity)
    , nodes_(md::pool::ObjectPool<TaskNode>::Options{
          .chunkBlocks = 64,
          .maxBlocks = 0,
          .initialBlocks = 16})
{
    start();
}

ThreadPool::~ThreadPool()
{
    stop(true);
}

void ThreadPool::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_)
        return;

    // A previous stop() joined its threads but left the vector populated.
    workers_.clear();
    workers_.reserve(workerCount_);

    stopping_ = false;
    drainOnStop_ = true;
    paused_ = false;
    running_ = true;

    for (std::size_t index = 0; index < workerCount_; ++index)
        workers_.emplace_back([this, index] { workerLoop(index); });
}

void ThreadPool::stop(bool drain)
{
    std::vector<std::thread> workers;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ && workers_.empty() && ready_.empty())
            return;

        stopping_ = true;
        drainOnStop_ = drain;
        running_ = false;

        if (!drain) {
            // The queue is about to disappear: destroy the nodes now so the
            // ObjectPool the nodes came from sees nothing live at destruction.
            discardQueuedLocked();
        }

        workers.swap(workers_);
    }

    workReady_.notify_all();
    idle_.notify_all();

    for (std::thread& worker : workers) {
        if (worker.joinable())
            worker.join();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        // A drain stop can still leave nodes behind if a task was queued while
        // the workers were already leaving; dropping them here is the last word.
        discardQueuedLocked();
        stopping_ = false;
        paused_ = false;
    }
}

void ThreadPool::pause()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_)
        paused_ = true;
}

void ThreadPool::resume()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        paused_ = false;
    }
    workReady_.notify_all();
}

bool ThreadPool::drain(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mutex_);
    return idle_.wait_for(lock, timeout, [this] { return ready_.empty() && active_ == 0; });
}

bool ThreadPool::post(std::function<void()> task)
{
    if (!task)
        return false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || stopping_) {
            ++rejected_;
            return false;
        }
        if (queueCapacity_ != 0 && ready_.size() >= queueCapacity_) {
            ++rejected_;
            return false;
        }

        TaskNode* node = nodes_.create();
        if (node == nullptr) {
            ++rejected_;
            return false;
        }

        node->fn = std::move(task);
        ready_.push_back(node);
        ++submitted_;
    }

    workReady_.notify_one();
    return true;
}

void ThreadPool::parallelFor(std::size_t count, std::function<void(std::size_t)> body)
{
    if (count == 0 || !body)
        return;

    assert(!t_isPoolWorker && "ThreadPool::parallelFor must not run on a worker");

    bool canQueue = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        canQueue = running_ && !paused_ && !stopping_ && !workers_.empty();
    }

    if (!canQueue) {
        for (std::size_t index = 0; index < count; ++index)
            body(index);
        return;
    }

    const std::size_t chunkCount =
        std::min(count, std::max<std::size_t>(1, workerCount_ * 4));

    std::atomic<std::size_t> next{0};
    std::latch pending(static_cast<std::ptrdiff_t>(chunkCount));
    std::size_t queuedChunks = 0;

    for (std::size_t chunk = 0; chunk < chunkCount; ++chunk) {
        const bool queued = post([&next, &body, &pending, count] {
            for (;;) {
                const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= count)
                    break;
                body(index);
            }
            pending.count_down();
        });

        if (queued)
            ++queuedChunks;
        else
            pending.count_down();
    }

    if (queuedChunks == 0) {
        for (std::size_t index = 0; index < count; ++index)
            body(index);
        return;
    }

    pending.wait();
}

ThreadPool::Stats ThreadPool::stats() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    Stats stats;
    stats.workers = workers_.size();
    stats.queued = ready_.size();
    stats.active = active_;
    stats.submitted = submitted_;
    stats.completed = completed_;
    stats.rejected = rejected_;
    stats.dropped = dropped_;
    stats.failed = failed_;
    stats.running = running_ && !stopping_;
    stats.paused = paused_;
    stats.tasks = PoolStats{
        .capacity = nodes_.capacity(),
        .available = nodes_.available(),
        .live = nodes_.liveCount(),
        .highWaterMark = nodes_.highWaterMark(),
        .chunks = nodes_.chunkCount()};
    return stats;
}

ThreadPool& ThreadPool::global()
{
    static ThreadPool pool;
    return pool;
}

void ThreadPool::workerLoop(std::size_t index)
{
    t_isPoolWorker = true;
    setCurrentThreadName(QStringLiteral("%1-%2").arg(options_.name).arg(index));

    for (;;) {
        TaskNode* node = nullptr;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            workReady_.wait(lock, [this] {
                return stopping_ || (!paused_ && !ready_.empty());
            });

            if (stopping_ && (ready_.empty() || !drainOnStop_))
                break;

            if (ready_.empty())
                continue;

            node = ready_.front();
            ready_.pop_front();
            ++active_;
        }

        try {
            node->fn();
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++failed_;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_;
            nodes_.destroy(node);
            ++completed_;
        }

        idle_.notify_all();
    }

    t_isPoolWorker = false;
}

void ThreadPool::discardQueuedLocked()
{
    while (!ready_.empty()) {
        TaskNode* node = ready_.front();
        ready_.pop_front();
        nodes_.destroy(node);
        ++dropped_;
    }
}

} // namespace md::pool
