// Periodicity — task priority queue (framework.md §5.12).
//
// Deferred work does not go straight to the event loop. It is posted here with
// a priority, and the queue drains strictly in priority order, re-checking the
// head after every task:
//
//   UserInput  > UiUpdate > UiEvent > Background > Idle
//
// The consequence that matters: a press posted while a paint batch is draining
// runs before the rest of that batch. Interactivity is therefore a property of
// the framework, not a rule every component has to remember.
//
// Two further guarantees keep the queue usable in a UI:
//   * coalescing — repeated invalidation for one target collapses to one task;
//   * a time budget — a flush stops after its budget and reschedules, so a
//     background burst cannot add unbounded latency to input.

#pragma once

#include <QObject>
#include <QtGlobal>

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace md::schedule {

// Lower values run first. The values are part of the public contract: code may
// compare priorities, and a new priority is inserted only if the ladder is
// revisited deliberately.
enum class Priority : quint8 {
    UserInput = 0,  // press, click, key activation, scroll, ripple start
    UiUpdate = 1,   // layout and paint invalidation
    UiEvent = 2,    // hover, synthetic notifications, animation ticks
    Background = 3, // decoding, prefetching, cache warming
    Idle = 4,       // cleanup, metrics
};

inline constexpr int kPriorityCount = 5;

class TaskQueue : public QObject
{
    Q_OBJECT

public:
    explicit TaskQueue(QObject* parent = nullptr);
    ~TaskQueue() override;

    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    // The GUI-thread queue. Widgets post here; creating it must happen on the
    // GUI thread, which is also where it drains.
    [[nodiscard]] static TaskQueue& main();

    // Appends a task. Never runs it synchronously: a flush is scheduled on the
    // event loop, and posting from inside a task appends without recursing.
    void post(Priority priority, std::function<void()> task);

    // Posts a task identified by `key`. A second post with the same key
    // replaces the pending callable instead of queueing another run, so a burst
    // of invalidations costs one task. The entry keeps the priority and queue
    // position of its first post; only the callable is replaced.
    void postCoalesced(Priority priority, quint64 key, std::function<void()> task);

    // Drops the pending task for `key`, if any.
    void cancel(quint64 key);

    [[nodiscard]] bool isEmpty() const noexcept;
    [[nodiscard]] int pending(Priority priority) const noexcept;
    [[nodiscard]] int pending() const noexcept;

    // Wall-clock budget for one flush. Zero means "no budget" (drain until
    // empty or until maxTasks). The default is 8 ms.
    void setBatchTimeBudget(std::chrono::microseconds budget);

    // Drains in priority order, at most `maxTasks` tasks and at most the time
    // budget. Returns the number of tasks that ran. Public so that tests and
    // embedding applications can pump the queue deterministically.
    int flush(int maxTasks = 256);

signals:
    // Emitted after a flush finishes, whether or not work remains.
    void drained();

private:
    struct Entry {
        quint64 id = 0;
        std::function<void()> task;
    };

    void scheduleFlush();
    [[nodiscard]] bool popNext(Entry& entry);

    std::array<std::deque<Entry>, kPriorityCount> queues_;
    // key -> (priority, entry id) for the coalescing contract.
    std::unordered_map<quint64, quint64> coalesced_;
    // Ids superseded by a later post or cancelled; skipped when reached.
    std::unordered_set<quint64> retired_;
    quint64 nextId_ = 1;
    std::chrono::microseconds budget_{8000};
    bool flushScheduled_ = false;
    bool flushing_ = false;
};

} // namespace md::schedule
