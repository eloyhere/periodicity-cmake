// Periodicity — task priority queue implementation (framework.md §5.12).

#include <md/schedule/task_queue.h>

#include <QMetaObject>

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

TaskQueue::~TaskQueue() = default;

TaskQueue& TaskQueue::main()
{
    static TaskQueue queue;
    return queue;
}

void TaskQueue::post(Priority priority, std::function<void()> task)
{
    if (!task)
        return;

    queues_[indexOf(priority)].push_back(Entry{nextId_++, std::move(task)});
    scheduleFlush();
}

void TaskQueue::postCoalesced(Priority priority, quint64 key, std::function<void()> task)
{
    if (!task)
        return;

    // The previous pending run for this key is superseded, not removed: marking
    // it retired keeps every queue operation O(1) and lets the drain skip it.
    if (const auto existing = coalesced_.find(key); existing != coalesced_.end())
        retired_.insert(existing->second);

    const quint64 id = nextId_++;
    queues_[indexOf(priority)].push_back(Entry{id, std::move(task)});
    coalesced_[key] = id;
    scheduleFlush();
}

void TaskQueue::cancel(quint64 key)
{
    const auto existing = coalesced_.find(key);
    if (existing == coalesced_.end())
        return;

    retired_.insert(existing->second);
    coalesced_.erase(existing);
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

void TaskQueue::setBatchTimeBudget(std::chrono::microseconds budget)
{
    budget_ = budget;
}

int TaskQueue::flush(int maxTasks)
{
    // A task that posts and then pumps must not re-enter the drain loop; the
    // outer flush will pick the new work up on its next iteration anyway.
    if (flushing_)
        return 0;

    flushing_ = true;
    const auto started = std::chrono::steady_clock::now();
    int ran = 0;
    Entry entry;

    while (ran < maxTasks && popNext(entry)) {
        entry.task();
        ++ran;

        if (budget_.count() > 0
            && std::chrono::steady_clock::now() - started >= budget_) {
            break;
        }
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
            Entry candidate = std::move(queue.front());
            queue.pop_front();

            // Superseded by a later post with the same key, or cancelled.
            if (retired_.erase(candidate.id) > 0)
                continue;

            entry = std::move(candidate);
            return true;
        }
    }
    return false;
}

void TaskQueue::scheduleFlush()
{
    if (flushScheduled_ || flushing_)
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

} // namespace md::schedule
