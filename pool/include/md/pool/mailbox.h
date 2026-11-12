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
// dropped rather than touching freed memory.

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
    explicit Mailbox(QObject* parent = nullptr);
    ~Mailbox() override;

    Mailbox(const Mailbox&) = delete;
    Mailbox& operator=(const Mailbox&) = delete;

    // Safe from any thread, including the owning one. A closed mailbox drops
    // the closure.
    void post(std::function<void()> fn);

    [[nodiscard]] std::size_t pending() const;
    [[nodiscard]] std::uint64_t delivered() const;

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
        std::atomic<bool> wakeup{false};
        std::atomic<bool> closed{false};
        std::atomic<std::uint64_t> delivered{0};
    };

    void drain(const std::shared_ptr<State>& state);

    std::shared_ptr<State> state_;
};

} // namespace md::pool
