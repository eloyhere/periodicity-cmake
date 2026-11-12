// Periodicity — multi-producer / multi-consumer channel (pool).
//
// The ring buffer is the fast path between exactly two threads. A channel is the
// general one: any number of senders, any number of receivers, an optional
// bound, and a close() that unblocks everyone waiting on it.
//
//   Channel<T> channel;              // unbounded
//   Channel<T> bounded(64);          // send() blocks while full
//   producer: channel.send(value)    // false once closed
//   consumer: channel.receive()      // nullopt once closed and drained
//
// Close is the termination signal of the whole design: after close() every
// blocked sender and receiver wakes up, sends fail, and receives drain what was
// already queued before returning nullopt. That is what lets a consumer thread
// end its loop with a plain `while (auto value = channel.receive())`.
//
// The implementation is a mutex and two condition variables. It is not the
// lock-free path; it is the correct, closeable one that a UI can own.

#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace md::pool {

template <class T>
class Channel
{
public:
    // `capacity` of 0 means unbounded.
    explicit Channel(std::size_t capacity = 0)
        : capacity_(capacity)
    {
    }

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    Channel(Channel&&) = delete;
    Channel& operator=(Channel&&) = delete;

    ~Channel()
    {
        // Waiters must be gone before destruction; close() only makes that
        // easier, it cannot make concurrent destruction safe.
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }

    // Blocks while a bounded channel is full. Returns false when the channel is
    // closed, in which case the value is not stored.
    bool send(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_)
            return false;

        if (capacity_ != 0) {
            notFull_.wait(lock, [this] { return closed_ || queue_.size() < capacity_; });
        }
        if (closed_)
            return false;

        queue_.push_back(std::move(value));
        ++sent_;
        notEmpty_.notify_one();
        return true;
    }

    // Never blocks. A full bounded channel drops the value and counts it.
    bool trySend(T value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_)
            return false;
        if (capacity_ != 0 && queue_.size() >= capacity_) {
            ++dropped_;
            return false;
        }

        queue_.push_back(std::move(value));
        ++sent_;
        notEmpty_.notify_one();
        return true;
    }

    // Blocks until a value is available or the channel is closed.
    std::optional<T> receive()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        notEmpty_.wait(lock, [this] { return closed_ || !queue_.empty(); });

        if (queue_.empty())
            return std::nullopt;

        T value = std::move(queue_.front());
        queue_.pop_front();
        ++received_;
        notFull_.notify_one();
        return value;
    }

    std::optional<T> tryReceive()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty())
            return std::nullopt;

        T value = std::move(queue_.front());
        queue_.pop_front();
        ++received_;
        notFull_.notify_one();
        return value;
    }

    // Idempotent. Wakes every blocked sender and receiver; queued values remain
    // available to receive() until they are drained.
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_)
                return;
            closed_ = true;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    [[nodiscard]] bool isClosed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    [[nodiscard]] std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    [[nodiscard]] std::uint64_t sent() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sent_;
    }

    [[nodiscard]] std::uint64_t received() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return received_;
    }

    [[nodiscard]] std::uint64_t dropped() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::deque<T> queue_;
    std::size_t capacity_ = 0;
    bool closed_ = false;
    std::uint64_t sent_ = 0;
    std::uint64_t received_ = 0;
    std::uint64_t dropped_ = 0;
};

} // namespace md::pool
