#include <md/pool/mailbox.h>

#include <QMetaObject>
#include <QThread>

#include <utility>

namespace md::pool {

Mailbox::Mailbox(QObject* parent, std::size_t capacity)
    : QObject(parent)
    , state_(std::make_shared<State>())
{
    state_->capacity = capacity;
}

Mailbox::~Mailbox()
{
    close();
}

void Mailbox::post(std::function<void()> fn)
{
    if (!fn)
        return;

    // Keep the state alive for the whole call: the delivery holds its own
    // reference, and a poster that races the owning thread's teardown must not
    // read the state after the last reference dropped.
    const std::shared_ptr<State> state = state_;

    if (state->closed.load(std::memory_order_acquire))
        return;

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->closed.load(std::memory_order_relaxed))
            return;
        if (state->capacity != 0 && state->queue.size() >= state->capacity) {
            state->dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        state->queue.push_back(std::move(fn));
    }

    // One queued meta-call per backlog: whoever wins the exchange owns the
    // wake-up, and the drain loop empties the queue before it resets the flag.
    bool expected = false;
    if (!state->wakeup.compare_exchange_strong(expected, true,
                                               std::memory_order_acq_rel,
                                               std::memory_order_relaxed)) {
        return;
    }

    if (!QMetaObject::invokeMethod(this, [this, state] { drain(state); },
                                   Qt::QueuedConnection)) {
        // Nobody took the wake-up (the object is being destroyed, or it has no
        // event loop to post to). Leaving the flag set would make every later
        // post() return early and strand the backlog, so release it; the next
        // post() will try again and the closure stays queued.
        state->wakeup.store(false, std::memory_order_release);
    }
}

std::size_t Mailbox::pending() const
{
    const std::shared_ptr<State> state = state_;
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->queue.size();
}

std::uint64_t Mailbox::delivered() const
{
    const std::shared_ptr<State> state = state_;
    return state->delivered.load(std::memory_order_relaxed);
}

std::uint64_t Mailbox::dropped() const
{
    const std::shared_ptr<State> state = state_;
    return state->dropped.load(std::memory_order_relaxed);
}

bool Mailbox::isCurrentThread() const
{
    return QThread::currentThread() == thread();
}

void Mailbox::close()
{
    const std::shared_ptr<State> state = state_;

    std::size_t discarded = 0;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->closed.store(true, std::memory_order_release);
        discarded = state->queue.size();
        state->queue.clear();
    }

    state->dropped.fetch_add(static_cast<std::uint64_t>(discarded),
                             std::memory_order_relaxed);
    state->wakeup.store(false, std::memory_order_release);
}

void Mailbox::drain(const std::shared_ptr<State>& state)
{
    state->wakeup.store(false, std::memory_order_release);

    for (;;) {
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->closed.load(std::memory_order_relaxed)) {
                state->queue.clear();
                break;
            }
            if (state->queue.empty())
                break;
            fn = std::move(state->queue.front());
            state->queue.pop_front();
        }

        try {
            fn();
        } catch (...) {
            // A throwing closure must not strand the rest of the batch.
        }

        state->delivered.fetch_add(1, std::memory_order_relaxed);
    }

    emit drained();
}

} // namespace md::pool
