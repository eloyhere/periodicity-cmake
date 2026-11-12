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
// Three further guarantees keep the queue usable in a UI:
//   * coalescing — repeated invalidation for one target collapses to one task;
//   * a time budget — a flush stops after its budget and reschedules, so a
//     background burst cannot add unbounded latency to input;
//   * failure containment — a task that throws cannot wedge the queue.
//
// Thread affinity: a TaskQueue is confined to the thread that creates it; the
// `main()` queue is the GUI-thread instance. post(), postCoalesced(), cancel()
// and flush() assert in debug builds that they run on that thread. A worker
// that wants work done on the GUI thread must marshal through a queued
// connection (or its own TaskQueue) instead of posting here; the queue is not a
// cross-thread hand-off.

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
    // GUI thread, which is also where it drains. The instance is intentionally
    // immortal so that a post during application teardown cannot touch a
    // destroyed object; work posted after the event loop stops is simply never
    // drained.
    [[nodiscard]] static TaskQueue& main();

    // Appends a task. Never runs it synchronously: a flush is scheduled on the
    // event loop, and posting from inside a task appends without recursing.
    // A null callable is ignored.
    void post(Priority priority, std::function<void()> task);

    // Posts a task identified by `key`. While a task for `key` is pending, a
    // second post replaces its callable in place: the entry keeps the priority
    // and queue position of its first post, so a burst of invalidations costs
    // one task and does not reorder the queue. Once the task has run or been
    // cancelled the key is free again and a later post queues a fresh entry at
    // the priority supplied then. A null callable is ignored.
    void postCoalesced(Priority priority, quint64 key, std::function<void()> task);

    // Drops the pending task for `key`, if any. Returns true when a queued task
    // was cancelled, and false when `key` is unknown, already cancelled and
    // drained, or currently running — a running task cannot be cancelled.
    bool cancel(quint64 key);

    [[nodiscard]] bool isEmpty() const noexcept;
    [[nodiscard]] int pending(Priority priority) const noexcept;
    [[nodiscard]] int pending() const noexcept;

    // Diagnostic: ids retired by cancel() that a later flush has not skipped
    // yet. Bounded by the entries still queued; a drained queue reports zero.
    [[nodiscard]] int retiredCount() const noexcept;

    // Wall-clock budget for one flush. Zero means "no budget" (drain until
    // empty or until maxTasks); a negative value is clamped to zero. The
    // default is 8 ms.
    //
    // The budget is consulted before each task starts. A task already running
    // cannot be preempted, so a flush can overrun the budget by at most that
    // task; the overshoot is bounded by the task, not by the queue. With work
    // available and maxTasks > 0 a flush always runs at least one task, so a
    // very small budget cannot livelock the reschedule loop.
    //
    // Strict priority remains the contract: a level that keeps reposting itself
    // can occupy every task within a flush. The budget bounds one flush and the
    // event loop runs between flushes; there is deliberately no aging, because
    // aging would break the "user input first" guarantee above.
    void setBatchTimeBudget(std::chrono::microseconds budget);

    // Drains in priority order, at most `maxTasks` tasks and at most the time
    // budget. Returns the number of tasks that ran. Tasks that throw are
    // counted and contained: the drain continues and `drained()` is still
    // emitted. Public so that tests and embedding applications can pump the
    // queue deterministically.
    int flush(int maxTasks = 256);

signals:
    // Emitted after a flush finishes, whether or not work remains. Not emitted
    // when a task destroyed the queue, because the emitter no longer exists.
    void drained();

private:
    struct Entry {
        quint64 id = 0;
        quint64 key = 0;          // Coalescing key; meaningful only when coalesced.
        bool coalesced = false;   // True when `key` identifies this entry.
        std::function<void()> task;
    };

    void scheduleFlush();
    [[nodiscard]] bool popNext(Entry& entry);
    [[nodiscard]] bool onOwnerThread() const noexcept;

    std::array<std::deque<Entry>, kPriorityCount> queues_;

    // key -> queued entry for the coalescing contract. The pointer stays valid
    // while the entry is queued: a deque insertion at either end invalidates
    // iterators but not references, and only the erased front element loses its
    // reference. The entry is removed from this map before it is popped, so no
    // pointer ever outlives its entry.
    std::unordered_map<quint64, Entry*> coalesced_;

    // Ids cancelled while still queued; skipped (and erased) when reached.
    std::unordered_set<quint64> retired_;

    quint64 nextId_ = 1;
    std::chrono::microseconds budget_{8000};
    bool flushScheduled_ = false;
    bool flushing_ = false;
    bool shuttingDown_ = false;
};

} // namespace md::schedule
