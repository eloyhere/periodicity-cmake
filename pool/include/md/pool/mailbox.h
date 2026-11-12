// Periodicity — the cross-thread mailbox (pool).
//
// A worker cannot touch a widget, and a signal connection is not always the
// shape the work needs: a job often wants to hand back one closure. Mailbox is
// that hand-off. It lives on the UI thread, any thread may post a closure, and
// one queued meta-call delivers the whole backlog:
//
//   worker:  mailbox->post([this, result] { label->setText(result); });
//
// The wake-up is coalesced: a burst of a thousand posts costs one event-loop
// wake-up, not a thousand. Closures run in post order, on the thread that owns
// the mailbox.
//
// Lifetime is the part worth stating: the queue lives in a shared state object
// that the queue itself keeps alive, and the queued meta-call is addressed to
// the mailbox as its context object. If the mailbox dies first, Qt drops the
// meta-call and the shared state dies with the last poster — a late post() is
// dropped rather than touching freed memory. The mailbox itself must still
// outlive the threads that call post(): reading the member after the destructor
// started would race with it, exactly as for any QObject.
//
// The backlog is bounded: `capacity` (0 = unbounded) caps how many closures may
// wait, and a post that would exceed it is dropped and counted instead of
// growing memory without limit.

#pragma once

#include <QObject>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace md::pool {

class Mailbox : public QObject
{
    Q_OBJECT

public:
    // `capacity` bounds the queue; 0 means unbounded. The bound is fixed for
    // the mailbox's lifetime, so it must be chosen before the first post().
    explicit Mailbox(QObject* parent = nullptr, std::size_t capacity = 0);
    ~Mailbox() override;

    Mailbox(const Mailbox&) = delete;
    Mailbox& operator=(const Mailbox&) = delete;

    // Safe from any thread, including the owning one. A closed mailbox drops
    // the closure; a full one drops it too and counts it.
    void post(std::function<void()> fn);

    [[nodiscard]] std::size_t pending() const;

    // Closures delivered on the owning thread so far.
    [[nodiscard]] std::uint64_t delivered() const;

    // Closures dropped instead of queued: the backlog was full at post(), or
    // close() discarded what was still queued.
    [[nodiscard]] std::uint64_t dropped() const;

    // True when called on the thread the mailbox lives on.
    [[nodiscard]] bool isCurrentThread() const;

    // Drops what is queued and refuses later posts. The destructor closes.
    void close();

signals:
    // Emitted on the owning thread after a delivery batch, whether or not more
    // work arrived meanwhile.
    void drained();

private:
    struct State
    {
        std::mutex mutex;
        std::deque<std::function<void()>> queue;
        std::size_t capacity = 0;
        std::atomic<bool> wakeup{false};
        std::atomic<bool> closed{false};
        std::atomic<std::uint64_t> delivered{0};
        std::atomic<std::uint64_t> dropped{0};
    };

    void drain(const std::shared_ptr<State>& state);

    std::shared_ptr<State> state_;
};

} // namespace md::pool
