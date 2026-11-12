#include <md/pool/mailbox.h>

#include <QMetaObject>
#include <QThread>

#include <utility>

namespace md::pool {

Mailbox::Mailbox(QObject* parent)
    : QObject(parent)
    , state_(std::make_shared<State>())
{
}

Mailbox::~Mailbox()
{
    close();
}

void Mailbox::post(std::function<void()> fn)
{
    if (!fn)
        return;

    if (state_->closed.load(std::memory_order_acquire))
        return;

    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->closed.load(std::memory_order_relaxed))
            return;
        state_->queue.push_back(std::move(fn));
    }

    // One queued meta-call per backlog: whoever wins the exchange owns the
    // wake-up, and the drain loop empties the queue before it resets the flag.
    bool expected = false;
    if (!state_->wakeup.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel,
                                                std::memory_order_relaxed)) {
        return;
    }

    const std::shared_ptr<State> state = state_;
    QMetaObject::invokeMethod(
        this, [this, state] { drain(state); }, Qt::QueuedConnection);
}

std::size_t Mailbox::pending() const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->queue.size();
}

std::uint64_t Mailbox::delivered() const
{
    return state_->delivered.load(std::memory_order_relaxed);
}

bool Mailbox::isCurrentThread() const
{
    return QThread::currentThread() == thread();
}

void Mailbox::close()
{
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->closed.store(true, std::memory_order_release);
        state_->queue.clear();
    }
    state_->wakeup.store(false, std::memory_order_release);
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
