// Periodicity — the IO thread (pool).
//
// File reads and delayed work should not run on the thread that paints. This
// class owns one thread and a deadline-ordered timer set:
//
//   post(task)                 run as soon as the thread is free
//   postAfter(delay, task)     run once, no earlier than `delay` from now
//   postEvery(period, task)    run now and then every `period`; returns an id
//   cancel(id)                 drop a timer that has not fired yet
//   readFile(path, done)       read a whole file here, report (bytes, error)
//
// One thread and one ordering is the guarantee: tasks run in the order they
// became due, and a due timer never waits behind a task that is not due yet.
// The loop sleeps on the earliest deadline, so an idle IO thread costs nothing.
//
// The latency statistics are measured from submit to execution. They are the
// number that tells a UI whether the IO thread is keeping up, which is why they
// are part of the public surface rather than a debug counter.

#pragma once

#include <QByteArray>
#include <QString>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace md::pool {

class IoThread
{
public:
    struct Stats
    {
        std::uint64_t posted = 0;    // tasks and timers accepted
        std::uint64_t executed = 0;  // tasks and timer ticks that ran
        std::uint64_t cancelled = 0; // timers dropped by cancel()/stop()
        std::size_t pending = 0;     // tasks + timers waiting
        double avgLatencyUs = 0.0;
        double maxLatencyUs = 0.0;
        bool running = false;
    };

    explicit IoThread(QString name = QStringLiteral("md-io"));
    ~IoThread();

    IoThread(const IoThread&) = delete;
    IoThread& operator=(const IoThread&) = delete;
    IoThread(IoThread&&) = delete;
    IoThread& operator=(IoThread&&) = delete;

    void start();
    void stop(); // drops pending work, joins the thread

    bool post(std::function<void()> task);
    bool postAfter(std::chrono::milliseconds delay, std::function<void()> task);

    // Runs `task` immediately and then every `period`. Returns 0 when `period`
    // is not positive or the thread is not running.
    std::uint64_t postEvery(std::chrono::milliseconds period, std::function<void()> task);

    // Best effort: a timer that is already executing cannot be stopped.
    bool cancel(std::uint64_t timerId);

    // Reads the file on the IO thread and hands the result to `done`, also on
    // the IO thread. The callback owns its copies; no reference outlives it.
    void readFile(const QString& path, std::function<void(QByteArray, QString)> done);

    [[nodiscard]] Stats stats() const;
    [[nodiscard]] QString name() const { return name_; }

private:
    using Clock = std::chrono::steady_clock;

    struct Timer
    {
        std::uint64_t id = 0;
        std::function<void()> fn;
        Clock::time_point due{};
        std::chrono::milliseconds period{0};
        Clock::time_point posted{};
        std::atomic<bool> cancelled{false};
    };

    struct Immediate
    {
        std::function<void()> fn;
        Clock::time_point posted{};
    };

    using TimerMap = std::multimap<Clock::time_point, std::shared_ptr<Timer>>;

    void loop();
    void scheduleLocked(const std::shared_ptr<Timer>& timer);
    void recordExecution(Clock::time_point posted, Clock::time_point finished);

    QString name_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Immediate> immediate_;
    TimerMap timers_;
    std::unordered_map<std::uint64_t, TimerMap::iterator> timerIndex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<Timer>> timersById_;

    std::thread thread_;
    std::uint64_t nextTimerId_ = 1;

    std::uint64_t posted_ = 0;
    std::uint64_t executed_ = 0;
    std::uint64_t cancelled_ = 0;
    double totalLatencyUs_ = 0.0;
    double maxLatencyUs_ = 0.0;

    bool running_ = false;
    bool stopping_ = false;
};

} // namespace md::pool
