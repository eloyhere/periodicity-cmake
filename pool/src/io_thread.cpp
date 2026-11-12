#include <md/pool/io_thread.h>

#include <QByteArray>
#include <QFile>

#include <algorithm>
#include <cstring>
#include <utility>

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
    ::pthread_setname_np(::pthread_self(), utf8.constData());
#else
    Q_UNUSED(name);
#endif
}

} // namespace

IoThread::IoThread(QString name)
    : name_(std::move(name))
{
    start();
}

IoThread::~IoThread()
{
    stop();
}

void IoThread::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_)
        return;

    stopping_ = false;
    running_ = true;
    thread_ = std::thread([this] { loop(); });
}

void IoThread::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ && !thread_.joinable())
            return;

        stopping_ = true;
        running_ = false;

        cancelled_ += static_cast<std::uint64_t>(timers_.size());
        timers_.clear();
        timerIndex_.clear();
        timersById_.clear();
        immediate_.clear();
    }

    cv_.notify_all();

    if (thread_.joinable())
        thread_.join();
}

bool IoThread::post(std::function<void()> task)
{
    if (!task)
        return false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || stopping_)
            return false;

        immediate_.push_back(Immediate{std::move(task), Clock::now()});
        ++posted_;
    }

    cv_.notify_one();
    return true;
}

bool IoThread::postAfter(std::chrono::milliseconds delay, std::function<void()> task)
{
    if (!task)
        return false;
    if (delay <= std::chrono::milliseconds::zero())
        return post(std::move(task));

    std::shared_ptr<Timer> timer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || stopping_)
            return false;

        timer = std::make_shared<Timer>();
        timer->id = nextTimerId_++;
        timer->fn = std::move(task);
        timer->due = Clock::now() + delay;
        timer->posted = Clock::now();
        scheduleLocked(timer);
        ++posted_;
    }

    cv_.notify_one();
    return true;
}

std::uint64_t IoThread::postEvery(std::chrono::milliseconds period, std::function<void()> task)
{
    if (!task || period <= std::chrono::milliseconds::zero())
        return 0;

    std::shared_ptr<Timer> timer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || stopping_)
            return 0;

        timer = std::make_shared<Timer>();
        timer->id = nextTimerId_++;
        timer->fn = std::move(task);
        timer->period = period;
        timer->due = Clock::now();
        timer->posted = Clock::now();
        scheduleLocked(timer);
        ++posted_;
    }

    cv_.notify_one();
    return timer->id;
}

bool IoThread::cancel(std::uint64_t timerId)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const auto byId = timersById_.find(timerId);
    if (byId == timersById_.end())
        return false;

    byId->second->cancelled.store(true, std::memory_order_release);

    const auto scheduled = timerIndex_.find(timerId);
    if (scheduled != timerIndex_.end()) {
        timers_.erase(scheduled->second);
        timerIndex_.erase(scheduled);
    }

    timersById_.erase(byId);
    ++cancelled_;
    return true;
}

void IoThread::readFile(const QString& path, std::function<void(QByteArray, QString)> done)
{
    if (!done)
        return;

    // The original is moved into the queued task, so the rejection path needs
    // its own copy to report the failure at all.
    const std::function<void(QByteArray, QString)> onRejected = done;

    const bool queued = post([path, done = std::move(done)] {
        if (!done)
            return;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            done(QByteArray{}, file.errorString());
            return;
        }

        const QByteArray bytes = file.readAll();
        done(bytes, QString());
    });

    if (!queued) {
        // There is no IO thread left to run the callback on, so the error is
        // reported here, on the caller: the documented exception to the
        // on-the-IO-thread rule in the header.
        onRejected(QByteArray{}, QStringLiteral("IO thread is not running"));
    }
}

IoThread::Stats IoThread::stats() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    Stats stats;
    stats.posted = posted_;
    stats.executed = executed_;
    stats.cancelled = cancelled_;
    stats.failed = failed_;
    stats.pending = immediate_.size() + timers_.size();
    stats.avgLatencyUs = executed_ == 0 ? 0.0 : totalLatencyUs_ / static_cast<double>(executed_);
    stats.maxLatencyUs = maxLatencyUs_;
    stats.running = running_ && !stopping_;
    return stats;
}

void IoThread::loop()
{
    setCurrentThreadName(name_);

    std::unique_lock<std::mutex> lock(mutex_);

    for (;;) {
        if (stopping_ && immediate_.empty() && timers_.empty())
            break;

        const Clock::time_point now = Clock::now();

        if (!timers_.empty() && timers_.begin()->first <= now) {
            auto due = timers_.begin();
            const std::shared_ptr<Timer> timer = due->second;
            timers_.erase(due);
            timerIndex_.erase(timer->id);

            // The lateness of this tick is measured from the deadline that made
            // it due, captured before the callback runs. Overwriting `posted`
            // with the next deadline (as this used to) makes every periodic
            // sample negative, which the clamp below turns into a zero.
            timer->posted = timer->due;

            lock.unlock();
            bool callbackFailed = false;
            if (!timer->cancelled.load(std::memory_order_acquire) && timer->fn) {
                try {
                    timer->fn();
                } catch (...) {
                    // A throwing timer must not kill the thread: it counts as
                    // a failed execution and the timer is re-armed below.
                    callbackFailed = true;
                }
            }
            const Clock::time_point finished = Clock::now();
            lock.lock();

            if (callbackFailed)
                ++failed_;

            if (!timer->cancelled.load(std::memory_order_relaxed)
                && timer->period > std::chrono::milliseconds::zero() && !stopping_) {
                // Re-arm from the moment it ran, so a slow callback cannot
                // queue a burst of overdue ticks.
                timer->due = finished + timer->period;
                scheduleLocked(timer);
            } else {
                timersById_.erase(timer->id);
            }
            recordExecution(timer->posted, finished);
            continue;
        }

        if (!immediate_.empty()) {
            Immediate item = std::move(immediate_.front());
            immediate_.pop_front();

            lock.unlock();
            bool callbackFailed = false;
            if (item.fn) {
                try {
                    item.fn();
                } catch (...) {
                    // One failing task cannot take the IO thread down.
                    callbackFailed = true;
                }
            }
            const Clock::time_point finished = Clock::now();
            lock.lock();

            if (callbackFailed)
                ++failed_;
            recordExecution(item.posted, finished);
            continue;
        }

        if (!running_ && !stopping_)
            break;

        if (timers_.empty())
            cv_.wait(lock);
        else
            cv_.wait_until(lock, timers_.begin()->first);
    }
}

void IoThread::scheduleLocked(const std::shared_ptr<Timer>& timer)
{
    const auto it = timers_.insert(TimerMap::value_type(timer->due, timer));
    timerIndex_[timer->id] = it;
    timersById_[timer->id] = timer;
}

void IoThread::recordExecution(Clock::time_point posted, Clock::time_point finished)
{
    ++executed_;
    const double micros =
        static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(finished - posted)
                                .count());
    totalLatencyUs_ += std::max(0.0, micros);
    maxLatencyUs_ = std::max(maxLatencyUs_, micros);
}

} // namespace md::pool
