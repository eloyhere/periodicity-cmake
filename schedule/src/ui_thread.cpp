// Periodicity — the UI thread and its frame pipeline.

#include <md/schedule/frame_scheduler.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace md::schedule {
namespace {

constexpr std::size_t indexOf(Priority priority) noexcept
{
    const int raw = static_cast<int>(priority);
    return static_cast<std::size_t>(std::clamp(raw, 0, kPriorityCount - 1));
}

// The phase a priority's work runs in. PreFrame and Paint are reserved for
// callbacks; Idle tasks posted through postTask() still belong to the
// post-frame tail, while postIdleTask() work is handled between frames.

UiThread* g_instance = nullptr;

} // namespace

UiThread::UiThread(QObject* parent) : QObject(parent) {}

UiThread::~UiThread()
{
    // The destructor may run with work still queued. Callables are destroyed,
    // not invoked: running them now would touch objects that are already dying.
    std::lock_guard lock(mutex_);
    queues_ = {};
    idle_.clear();
    frameCallbacks_.clear();
    paintCallbacks_.clear();
    resizeRequests_.clear();
}

UiThread* UiThread::instance() noexcept
{
    return g_instance;
}

UiThread& UiThread::install()
{
    Q_ASSERT_X(!QCoreApplication::instance()
                   || QThread::currentThread() == QCoreApplication::instance()->thread(),
               "UiThread::install", "the UI runtime belongs to the GUI thread");
    if (g_instance == nullptr) {
        g_instance = new UiThread;
        g_instance->installed_ = true;
    }
    return *g_instance;
}

void UiThread::uninstall()
{
    if (g_instance == nullptr)
        return;

    UiThread* dying = g_instance;
    g_instance = nullptr;
    dying->installed_ = false;
    delete dying;
}

bool UiThread::isInstalled() const noexcept
{
    return installed_;
}

Phase UiThread::phase() const noexcept
{
    return phase_.load(std::memory_order_acquire);
}

quint64 UiThread::frameNumber() const noexcept
{
    return frameNumber_.load(std::memory_order_acquire);
}

FrameRequest UiThread::lastFrameRequest() const noexcept
{
    std::lock_guard lock(mutex_);
    return lastFrameRequest_;
}

void UiThread::requestFrame(FrameRequest reason)
{
    {
        std::lock_guard lock(mutex_);
        lastFrameRequest_ = reason;
    }
    framePending_.store(true, std::memory_order_release);
    scheduleFrame();
}

void UiThread::scheduleFrame()
{
    if (frameScheduled_.exchange(true, std::memory_order_acq_rel))
        return;

    const bool invoked = QMetaObject::invokeMethod(
        this,
        [this] {
            frameScheduled_.store(false, std::memory_order_release);
            if (framePending_.load(std::memory_order_acquire))
                runFrame();
        },
        Qt::QueuedConnection);

    if (!invoked)
        frameScheduled_.store(false, std::memory_order_release);
}

void UiThread::addFrameCallback(std::function<void()> callback)
{
    if (!callback)
        return;

    std::lock_guard lock(mutex_);
    frameCallbacks_.push_back(std::move(callback));
}

void UiThread::addPaintCallback(std::function<void()> callback)
{
    if (!callback)
        return;

    std::lock_guard lock(mutex_);
    paintCallbacks_.push_back(std::move(callback));
}

void UiThread::postTask(Priority priority, std::function<void()> task)
{
    postTask(priority, std::move(task), kDefaultTaskTtl);
}

void UiThread::postTask(Priority priority, std::function<void()> task, Duration ttl)
{
    if (!task)
        return;

    const Duration lifetime = std::max(ttl, Duration::zero());
    postTaskUntil(priority, std::move(task), now() + lifetime);
}

void UiThread::postTaskUntil(Priority priority, std::function<void()> task,
                             Clock::time_point deadline)
{
    if (!task)
        return;

    std::lock_guard lock(mutex_);
    queues_[indexOf(priority)].push_back(Task{nextTaskId_++, std::move(task), deadline});
}

void UiThread::postIdleTask(std::function<void()> task)
{
    postIdleTask(std::move(task), kDefaultTaskTtl);
}

void UiThread::postIdleTask(std::function<void()> task, Duration ttl)
{
    if (!task)
        return;

    {
        std::lock_guard lock(mutex_);
        idle_.push_back(Task{nextTaskId_++, std::move(task),
                             now() + std::max(ttl, Duration::zero())});
    }

    if (!framePending_.load(std::memory_order_acquire) && !inFrame_.load())
        scheduleIdle();
}

void UiThread::scheduleIdle()
{
    if (idleScheduled_.exchange(true, std::memory_order_acq_rel))
        return;

    const bool invoked = QMetaObject::invokeMethod(
        this,
        [this] {
            idleScheduled_.store(false, std::memory_order_release);
            if (!framePending_.load(std::memory_order_acquire) && !inFrame_.load())
                runIdle();
        },
        Qt::QueuedConnection);

    if (!invoked)
        idleScheduled_.store(false, std::memory_order_release);
}

void UiThread::setFrameBudget(Duration budget)
{
    frameBudget_ = std::max(budget, Duration::zero());
}

UiThread::Duration UiThread::frameBudget() const noexcept
{
    return frameBudget_;
}

bool UiThread::runFrame()
{
    bool expected = false;
    if (!inFrame_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;

    framePending_.store(false, std::memory_order_release);
    const quint64 number = frameNumber_.fetch_add(1, std::memory_order_acq_rel) + 1;
    {
        std::lock_guard lock(mutex_);
        ++stats_.frames;
        stats_.frameNumber = number;
    }

    const Clock::time_point frameStarted = now();
    bool overBudget = false;

    // 1. Pre-frame: the frame boundary. One-shot callbacks registered before
    // the frame started; a callback registered now belongs to the next frame.
    setPhase(Phase::PreFrame);
    std::vector<std::function<void()>> frameCallbacks;
    {
        std::lock_guard lock(mutex_);
        frameCallbacks.swap(frameCallbacks_);
    }
    for (const auto& callback : frameCallbacks) {
        try {
            callback();
        } catch (...) {
            // A broken boundary callback must not abort the frame phases.
        }
    }

    // 2. Build: input and invalidation work.
    setPhase(Phase::Build);
    drainPhase(Priority::UserInput, frameStarted, overBudget, 256);
    drainPhase(Priority::UiUpdate, frameStarted, overBudget, 256);

    // 3. Layout: UI events and animation ticks.
    setPhase(Phase::Layout);
    drainPhase(Priority::UiEvent, frameStarted, overBudget, 256);

    // 4. Paint: measured, and never skipped because Build overran; the frame
    // has already been committed to producing a result.
    setPhase(Phase::Paint);
    runPaint();

    // 5. Post-frame: background work and low-priority cleanup. Over budget
    // means "start nothing else", but the phase transition still happens.
    setPhase(Phase::PostFrame);
    if (!overBudget)
        drainPhase(Priority::Background, frameStarted, overBudget, 256);
    if (!overBudget)
        drainPhase(Priority::Idle, frameStarted, overBudget, 256);

    const bool overrun = overBudget
        || (frameBudget_.count() > 0 && now() - frameStarted >= frameBudget_);
    {
        std::lock_guard lock(mutex_);
        if (overrun) {
            ++stats_.overruns;
            ++stats_.droppedFrames;
        }
    }

    setPhase(Phase::Idle);
    inFrame_.store(false, std::memory_order_release);

    // A request made while this frame ran starts the next event-loop turn; it
    // must never be folded into this one. Idle work waits for the frame queue
    // to be empty.
    if (framePending_.load(std::memory_order_acquire))
        scheduleFrame();
    else
        scheduleIdle();

    return true;
}

void UiThread::runPaint()
{
    std::vector<std::function<void()>> callbacks;
    {
        std::lock_guard lock(mutex_);
        callbacks.swap(paintCallbacks_);
    }

    const Clock::time_point started = now();
    for (const auto& callback : callbacks) {
        try {
            callback();
        } catch (...) {
            // A paint hook that throws is contained; the next frame still runs.
        }
    }
    recordPaint(std::chrono::duration_cast<Duration>(now() - started));
}

void UiThread::setPhase(Phase phase) noexcept
{
    phase_.store(phase, std::memory_order_release);
}

int UiThread::drainPhase(Priority priority, Clock::time_point frameStarted,
                         bool& overBudget, int maxTasks)
{
    int ran = 0;
    int attempts = 0;
    while (ran < maxTasks && attempts < maxTasks * 2) {
        if (overBudget)
            break;

        if (!overBudget && frameBudget_.count() > 0
            && now() - frameStarted >= frameBudget_) {
            overBudget = true;
            break;
        }

        Task task;
        bool dropped = false;
        if (!popTask(queues_[indexOf(priority)], task, dropped)) {
            if (dropped) {
                ++attempts;
                continue;
            }
            break;
        }

        try {
            task.task();
        } catch (...) {
            // Contain a throwing task exactly as TaskQueue does.
        }
        ++ran;
        ++attempts;
    }
    return ran;
}

int UiThread::runIdle(int maxTasks)
{
    if (inFrame_.load(std::memory_order_acquire)
        || framePending_.load(std::memory_order_acquire)) {
        return 0;
    }

    return drainIdle(maxTasks);
}

int UiThread::drainIdle(int maxTasks)
{
    int ran = 0;
    int attempts = 0;
    const Clock::time_point started = now();
    while (ran < maxTasks && attempts < maxTasks * 2) {
        if (framePending_.load(std::memory_order_acquire)
            || inFrame_.load(std::memory_order_acquire)) {
            break;
        }
        if (frameBudget_.count() > 0 && now() - started >= frameBudget_)
            break;

        Task task;
        bool dropped = false;
        if (!popTask(idle_, task, dropped)) {
            if (dropped) {
                ++attempts;
                continue;
            }
            break;
        }

        try {
            task.task();
        } catch (...) {
            // Idle cleanup is best effort.
        }
        ++ran;
        ++attempts;
    }

    std::size_t remaining = 0;
    {
        std::lock_guard lock(mutex_);
        remaining = idle_.size();
    }
    if (remaining > 0 && !framePending_.load(std::memory_order_acquire) && !inFrame_.load())
        scheduleIdle();

    return ran;
}

bool UiThread::popTask(std::deque<Task>& queue, Task& out, bool& expiredTask)
{
    expiredTask = false;
    std::lock_guard lock(mutex_);

    while (!queue.empty()) {
        Task candidate = std::move(queue.front());
        queue.pop_front();

        if (expired(candidate)) {
            ++droppedTasks_;
            ++stats_.droppedTasks;
            expiredTask = true;
            continue;
        }

        out = std::move(candidate);
        return true;
    }
    return false;
}

bool UiThread::expired(const Task& task) const
{
    return task.deadline != Clock::time_point{} && now() > task.deadline;
}

UiThread::Clock::time_point UiThread::now() const
{
    return clockForTesting_ ? clockForTesting_() : Clock::now();
}

void UiThread::requestResize(quint64 target, QSizeF size)
{
    {
        std::lock_guard lock(mutex_);
        resizeRequests_[target] = size; // last size wins
    }
    requestFrame(FrameRequest::Resize);
}

QSizeF UiThread::pendingResize(quint64 target) const
{
    std::lock_guard lock(mutex_);
    const auto found = resizeRequests_.find(target);
    return found != resizeRequests_.end() ? found->second : QSizeF();
}

int UiThread::pendingResizeCount() const
{
    std::lock_guard lock(mutex_);
    return static_cast<int>(resizeRequests_.size());
}

std::vector<std::pair<quint64, QSizeF>> UiThread::takeResizeRequests()
{
    std::vector<std::pair<quint64, QSizeF>> result;
    std::lock_guard lock(mutex_);
    result.reserve(resizeRequests_.size());
    for (const auto& [target, size] : resizeRequests_)
        result.emplace_back(target, size);
    resizeRequests_.clear();
    return result;
}

void UiThread::recordPaint(Duration paintTime)
{
    const double micros = static_cast<double>(std::max(paintTime, Duration::zero()).count());
    std::lock_guard lock(mutex_);
    ++paintSamples_;
    totalPaintUs_ += micros;
    stats_.maxPaintUs = std::max(stats_.maxPaintUs, micros);
}

UiThread::FrameStats UiThread::stats() const
{
    std::lock_guard lock(mutex_);
    FrameStats result = stats_;
    if (paintSamples_ > 0)
        result.meanPaintUs = totalPaintUs_ / static_cast<double>(paintSamples_);
    result.phase = phase_.load(std::memory_order_acquire);
    result.frameNumber = frameNumber_.load(std::memory_order_acquire);
    result.pendingTasks = static_cast<std::size_t>(pendingLocked());
    return result;
}

quint64 UiThread::droppedTasks() const noexcept
{
    std::lock_guard lock(mutex_);
    return droppedTasks_;
}

int UiThread::pending(Priority priority) const noexcept
{
    std::lock_guard lock(mutex_);
    return static_cast<int>(queues_[indexOf(priority)].size());
}

int UiThread::pending() const noexcept
{
    std::lock_guard lock(mutex_);
    return pendingLocked();
}

int UiThread::pendingLocked() const noexcept
{
    int count = static_cast<int>(idle_.size());
    for (const auto& queue : queues_)
        count += static_cast<int>(queue.size());
    return count;
}

bool UiThread::framePending() const noexcept
{
    return framePending_.load(std::memory_order_acquire);
}

void UiThread::setClockForTesting(std::function<Clock::time_point()> clock)
{
    clockForTesting_ = std::move(clock);
}

void UiThread::resetClockForTesting()
{
    clockForTesting_ = {};
}

} // namespace md::schedule
