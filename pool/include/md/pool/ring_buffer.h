// Periodicity — single-producer / single-consumer ring buffer (pool).
//
// The ring is a fixed-capacity hand-off between exactly one producer thread and
// exactly one consumer thread. No mutex: the producer owns `head`, the consumer
// owns `tail`, and each publishes its own cursor with a release store, so the
// other side sees a slot only after its contents are written.
//
//   producer: push() -> slots[head & mask] = value; head.store(head + 1)
//   consumer: pop()  -> value = slots[tail & mask]; tail.store(tail + 1)
//
// Capacity is rounded up to a power of two, which turns the index arithmetic
// into a mask. A full ring rejects the push (and counts it) instead of blocking
// or overwriting: dropping is visible, overwriting is not.
//
// The cursors are free-running size_t counters, so wrap-around is handled by
// unsigned arithmetic; `head - tail` is the number of occupied slots whether or
// not either counter has wrapped.
//
// Each cursor sits on its own cache line. That is the reason for the `Cursor`
// wrapper rather than an `alignas` on a bare member: the padding is deliberate,
// and a wrapper type expresses it without asking the compiler to warn about it.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace md::pool {

#if defined(_MSC_VER)
// C4324 fires for every deliberately padded type; here the padding *is* the
// design (one cache line per cursor), so the warning is noise.
#  pragma warning(push)
#  pragma warning(disable : 4324)
#endif

template <class T>
class SpscRingBuffer
{
    static_assert(std::is_object_v<T>, "SpscRingBuffer holds objects");

public:
    // `capacity` is rounded up to the next power of two, minimum 2.
    explicit SpscRingBuffer(std::size_t capacity)
        : capacity_(roundUpToPowerOfTwo(capacity))
        , mask_(capacity_ - 1)
        , slots_(std::make_unique<std::optional<T>[]>(capacity_))
    {
    }

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;
    SpscRingBuffer(SpscRingBuffer&&) = delete;
    SpscRingBuffer& operator=(SpscRingBuffer&&) = delete;

    // Producer side only. Returns false when the ring is full; the value is
    // then not stored and dropped() grows by one.
    bool push(const T& value) { return emplace(value); }
    bool push(T&& value) { return emplace(std::move(value)); }

    template <class... Args>
    bool emplace(Args&&... args)
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);

        if (head - tail >= capacity_) {
            dropped_.fetchAdd(1, std::memory_order_relaxed);
            return false;
        }

        std::optional<T>& slot = slots_[head & mask_];
        slot.emplace(std::forward<Args>(args)...);
        head_.store(head + 1, std::memory_order_release);

        const std::size_t size = head + 1 - tail;
        std::size_t watermark = watermark_.load(std::memory_order_relaxed);
        while (size > watermark
               && !watermark_.compareExchange(watermark, size, std::memory_order_relaxed)) {
        }
        return true;
    }

    // Consumer side only.
    bool pop(T& out)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail == head)
            return false;

        std::optional<T>& slot = slots_[tail & mask_];
        out = std::move(*slot);
        slot.reset();
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> pop()
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail == head)
            return std::nullopt;

        std::optional<T>& slot = slots_[tail & mask_];
        std::optional<T> value(std::move(*slot));
        slot.reset();
        tail_.store(tail + 1, std::memory_order_release);
        return value;
    }

    // Safe to call from either side. The figure is a snapshot, not a promise.
    [[nodiscard]] std::size_t size() const noexcept
    {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return head - tail;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] bool full() const noexcept { return size() >= capacity_; }
    [[nodiscard]] std::size_t highWaterMark() const noexcept
    {
        return watermark_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t dropped() const noexcept
    {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    // One cache line per cursor: the producer never invalidates the line the
    // consumer is spinning on, and vice versa.
    struct alignas(64) Cursor
    {
        std::atomic<std::size_t> value{0};

        [[nodiscard]] std::size_t load(std::memory_order order) const noexcept
        {
            return value.load(order);
        }

        void store(std::size_t next, std::memory_order order) noexcept
        {
            value.store(next, order);
        }

        bool compareExchange(std::size_t& expected, std::size_t desired,
                             std::memory_order order) noexcept
        {
            return value.compare_exchange_weak(expected, desired, order);
        }
    };

    struct alignas(64) Counter
    {
        std::atomic<std::uint64_t> value{0};

        [[nodiscard]] std::uint64_t load(std::memory_order order) const noexcept
        {
            return value.load(order);
        }

        void fetchAdd(std::uint64_t amount, std::memory_order order) noexcept
        {
            value.fetch_add(amount, order);
        }
    };

    static std::size_t roundUpToPowerOfTwo(std::size_t value)
    {
        std::size_t result = 2;
        while (result < value && result < (std::size_t{1} << (sizeof(std::size_t) * 8 - 2)))
            result <<= 1;
        return result;
    }

    std::size_t capacity_ = 2;
    std::size_t mask_ = 1;
    std::unique_ptr<std::optional<T>[]> slots_;

    Cursor head_;
    Cursor tail_;
    Cursor watermark_;
    Counter dropped_;
};

#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

} // namespace md::pool
