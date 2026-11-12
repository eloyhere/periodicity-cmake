// Periodicity — task priority queue implementation (framework.md §5.12).

#include <md/schedule/task_queue.h>

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <utility>

namespace md::schedule {
namespace {

constexpr std::size_t indexOf(Priority priority)
{
    const int raw = static_cast<int>(priority);
    return static_cast<std::size_t>(std::clamp(raw, 0, kPriorityCount - 1));
}

} // namespace

TaskQueue::TaskQueue(QObject* parent) : QObject(parent) {}

TaskQueue::~TaskQueue()
{
    // Refuse re-entrant posts from the destructors of the callables being
    // dropped, then discard the pending work. Running it here would execute
    // user code against objects that are already dying; discarding is the only
    // safe choice, but it is reported rather than silent.
    shuttingDown_ = true;

    const int dropped = pending();
    coalesced_.clear();
    for (auto& queue : queues_)
        queue.clear();
    retired_.clear();

    if (dropped > 0) {
        qWarning("TaskQueue destroyed with %d pending task(s); they were discarded",
                 dropped);
    }
}

TaskQueue& TaskQueue::main()
{
    Q_ASSERT_X(!qApp || QThread::currentThread() == qApp->thread(), "TaskQueue::main",
               "the main queue belongs to the GUI thread");

    // Intentionally leaked. A function-local static would be destroyed during
    // static destruction, when QCoreApplication is already gone, and a late
    // post() would touch freed memory. The queue instead outlives the
    // application; anything posted after the event loop stops is never drained.
    static TaskQueue* queue = new TaskQueue;
    return *queue;
}

void TaskQueue::post(Priority priority, std::function<void()> task)
{
    if (!task || shuttingDown_)
        return;

    Q_ASSERT_X(onOwnerThread(), "TaskQueue::post",
               "post from a thread that does not own the queue");

    queues_[indexOf(priority)].push_back(Entry{nextId_++, 0, false, std::move(task)});
    scheduleFlush();
}

void TaskQueue::postCoalesced(Priority priority, quint64 key, std::function<void()> task)
{
    if (!task || shuttingDown_)
        return;

    Q_ASSERT_X(onOwnerThread(), "TaskQueue::postCoalesced",
               "postCoalesced from a thread that does not own the queue");

    // A pending run keeps the priority and queue position of its first post;
    // replacing the callable in place is what makes repeated invalidation cheap
    // and order-stable. The map is cleared when the entry is popped, so a post
    // after the task has run always creates a fresh entry below.
    if (const auto found = coalesced_.find(key); found != coalesced_.end()) {
        found->second->task = std::move(task);
        return;
    }

    std::deque<Entry>& queue = queues_[indexOf(priority)];
    queue.push_back(Entry{nextId_++, key, true, std::move(task)});
    coalesced_.emplace(key, &queue.back());
    scheduleFlush();
}

bool TaskQueue::cancel(quint64 key)
{
    if (shuttingDown_)
        return false;

    Q_ASSERT_X(onOwnerThread(), "TaskQueue::cancel",
               "cancel from a thread that does not own the queue");

    const auto found = coalesced_.find(key);
    if (found == coalesced_.end())
        return false;

    // A queued entry cannot be erased from the middle of its deque in O(1), so
    // retire its id: the next drain drops it there. The id leaves retired_ when
    // the entry is finally popped, which keeps the set bounded by the entries
    // still queued.
    retired_.insert(found->second->id);
    coalesced_.erase(found);
    return true;
}

bool TaskQueue::isEmpty() const noexcept
{
    for (const auto& queue : queues_) {
        for (const Entry& entry : queue) {
            if (retired_.find(entry.id) == retired_.end())
                return false;
        }
    }
    return true;
}

int TaskQueue::pending(Priority priority) const noexcept
{
    int count = 0;
    for (const Entry& entry : queues_[indexOf(priority)]) {
        if (retired_.find(entry.id) == retired_.end())
            ++count;
    }
    return count;
}

int TaskQueue::pending() const noexcept
{
    int count = 0;
    for (const auto& queue : queues_) {
        for (const Entry& entry : queue) {
            if (retired_.find(entry.id) == retired_.end())
                ++count;
        }
    }
    return count;
}

int TaskQueue::retiredCount() const noexcept
{
    return static_cast<int>(retired_.size());
}

void TaskQueue::setBatchTimeBudget(std::chrono::microseconds budget)
{
    Q_ASSERT_X(onOwnerThread(), "TaskQueue::setBatchTimeBudget",
               "setBatchTimeBudget from a thread that does not own the queue");

    // A negative budget is meaningless; clamping it to zero makes it "no
    // budget" explicitly rather than leaving a nonsensical bound in place.
    budget_ = std::max(budget, std::chrono::microseconds::zero());
}

int TaskQueue::flush(int maxTasks)
{
    Q_ASSERT_X(onOwnerThread(), "TaskQueue::flush",
               "flush from a thread that does not own the queue");

    // A task that posts and then pumps must not re-enter the drain loop; the
    // outer flush will pick the new work up on its next iteration anyway.
    if (flushing_ || shuttingDown_)
        return 0;

    // The QPointer detects a callback that destroys the queue. Once QObject's
    // destructor has run the guard is null, and nothing below may touch `this`.
    const QPointer<TaskQueue> self(this);

    flushing_ = true;
    const auto started = std::chrono::steady_clock::now();
    int ran = 0;
    Entry entry;

    while (ran < maxTasks) {
        // The budget is checked before a task starts, so a flush never begins a
        // task it has no budget left for. A task that is already running cannot
        // be preempted, so one task may still overrun the budget. `ran > 0`
        // guarantees progress: a budget smaller than the flush overhead must
        // not livelock the reschedule loop.
        if (ran > 0 && budget_.count() > 0
            && std::chrono::steady_clock::now() - started >= budget_) {
            break;
        }

        if (!popNext(entry))
            break;

        try {
            entry.task();
        } catch (...) {
            // A throwing task must not wedge the queue: contain it, keep
            // draining, and let the flush finish and report normally. This
            // matches the policy of the cross-thread mailbox drain.
        }
        ++ran;

        if (self.isNull())
            return ran; // A task destroyed the queue; the guard stopped the drain.
    }

    flushing_ = false;

    if (!isEmpty())
        scheduleFlush();

    emit drained();
    return ran;
}

bool TaskQueue::popNext(Entry& entry)
{
    for (auto& queue : queues_) {
        while (!queue.empty()) {
            Entry& candidate = queue.front();

            // The key must stop referring to this entry before the entry is
            // moved out and destroyed. A newer entry for the same key, if any,
            // is not touched.
            if (candidate.coalesced) {
                const auto found = coalesced_.find(candidate.key);
                if (found != coalesced_.end() && found->second == &candidate)
                    coalesced_.erase(found);
            }

            // Cancelled while queued: drop the entry and its retired id.
            if (retired_.erase(candidate.id) > 0) {
                queue.pop_front();
                continue;
            }

            entry = std::move(candidate);
            queue.pop_front();
            return true;
        }
    }
    return false;
}

void TaskQueue::scheduleFlush()
{
    if (flushScheduled_ || flushing_ || shuttingDown_)
        return;

    flushScheduled_ = true;
    QMetaObject::invokeMethod(
        this,
        [this]() {
            flushScheduled_ = false;
            flush();
        },
        Qt::QueuedConnection);
}

bool TaskQueue::onOwnerThread() const noexcept
{
    return QThread::currentThread() == thread();
}

} // namespace md::schedule
