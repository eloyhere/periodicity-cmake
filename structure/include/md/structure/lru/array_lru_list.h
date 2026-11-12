// Periodicity — array-backed LRU order list (structure).
//
// ArrayLRUList<T> stores the recency order in a growable array of slots rather
// than in one allocation per element. An insert takes a slot from an intrusive
// free list (or appends one when the array is full), writes the value there,
// and links the slot at the newest end through slot indices:
//
//     slots = [ value | next | previous ] [ value | next | previous ] ...
//
// Because the link fields are indices, the handle returned by insert() is a
// stable slot index: it stays valid until that slot is erased, even when the
// array grows and the values themselves are moved. References and iterators
// into the array are invalidated by growth; handles are not.
//
//     insert / touch / erase / popOldest     O(1)
//     iteration                              O(n), oldest -> newest
//     memory                                 one contiguous allocation that
//                                            grows geometrically, no per-node
//                                            allocation at all
//
// reserve(n) pre-sizes the slot array for n slots; after it, inserting and
// erasing up to n elements does not grow the array (capacity() stays put).
// Unlike LinkedLRUList there is no node pool: the free list is just a chain of
// unused slots inside the array, so clear() keeps every slot allocated.
//
// This is an *order* container. Mapping values to keys and rejecting duplicates
// is the job of the LRU faces, which optionally take this type as their Order
// policy: UnorderedLRUMap<Key, Value, Hash, Equal, ArrayLRUList<Key>>.
//
// Iterators are invalidated only when their own slot is erased (or by reserve/
// growth); insertions, promotions and erasures of other slots keep iteration
// valid and ++ follows the current links.

#pragma once

#include <cassert>
#include <cstddef>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace md::structure {

template <class T>
class ArrayLRUList
{
public:
    using value_type = T;
    using size_type = std::size_t;
    using handle = size_type;

    static constexpr handle npos = static_cast<handle>(-1);

    class const_iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using reference = const T&;
        using pointer = const T*;
        using difference_type = std::ptrdiff_t;

        const_iterator() = default;

        reference operator*() const
        {
            assert(owner_ != nullptr && slot_ != npos);
            return *owner_->slots_[slot_].value;
        }

        pointer operator->() const { return &**this; }

        const_iterator& operator++()
        {
            assert(owner_ != nullptr && slot_ != npos);
            slot_ = owner_->slots_[slot_].next;
            return *this;
        }

        const_iterator operator++(int)
        {
            const_iterator copy = *this;
            ++*this;
            return copy;
        }

        friend bool operator==(const const_iterator& lhs, const const_iterator& rhs) noexcept
        {
            return lhs.owner_ == rhs.owner_ && lhs.slot_ == rhs.slot_;
        }

        friend bool operator!=(const const_iterator& lhs, const const_iterator& rhs) noexcept
        {
            return !(lhs == rhs);
        }

    private:
        const_iterator(const ArrayLRUList* owner, handle slot) noexcept
            : owner_(owner), slot_(slot)
        {
        }

        const ArrayLRUList* owner_ = nullptr;
        handle slot_ = npos;

        friend class ArrayLRUList<T>;
    };

    ArrayLRUList() = default;

    explicit ArrayLRUList(size_type slots) { reserve(slots); }

    ArrayLRUList(const ArrayLRUList&) = default;
    ArrayLRUList& operator=(const ArrayLRUList&) = default;

    ArrayLRUList(ArrayLRUList&& other) noexcept
        : slots_(std::move(other.slots_)), freeHead_(other.freeHead_), head_(other.head_),
          tail_(other.tail_), size_(other.size_)
    {
        other.freeHead_ = npos;
        other.head_ = npos;
        other.tail_ = npos;
        other.size_ = 0;
    }

    ArrayLRUList& operator=(ArrayLRUList&& other) noexcept
    {
        if (this != &other) {
            slots_ = std::move(other.slots_);
            freeHead_ = other.freeHead_;
            head_ = other.head_;
            tail_ = other.tail_;
            size_ = other.size_;

            other.freeHead_ = npos;
            other.head_ = npos;
            other.tail_ = npos;
            other.size_ = 0;
        }
        return *this;
    }

    ~ArrayLRUList() = default;

    // ---- Capacity ---------------------------------------------------------

    // Pre-sizes the slot array for `slots` slots. Existing content is kept; the
    // new slots join the free list. Handles stay valid, references may move.
    void reserve(size_type slots)
    {
        if (slots <= slots_.size())
            return;

        const size_type first = slots_.size();
        slots_.resize(slots);
        for (size_type index = slots; index-- > first;) {
            slots_[index].next = freeHead_;
            freeHead_ = index;
        }
    }

    [[nodiscard]] size_type size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    // Number of slots currently owned (allocated), live or free.
    [[nodiscard]] size_type capacity() const noexcept { return slots_.size(); }

    // ---- Modifiers --------------------------------------------------------

    // Appends `value` as the newest element and returns its stable slot handle.
    handle insert(T value)
    {
        const handle slot = acquireSlot();
        try {
            slots_[slot].value.emplace(std::move(value));
        } catch (...) {
            releaseSlot(slot);
            throw;
        }

        slots_[slot].previous = tail_;
        slots_[slot].next = npos;
        if (tail_ != npos)
            slots_[tail_].next = slot;
        else
            head_ = slot;
        tail_ = slot;
        ++size_;
        return slot;
    }

    // Moves the element to the newest end. Unknown/free handles are ignored.
    void touch(handle slot)
    {
        if (!isLive(slot) || slot == tail_)
            return;

        unlink(slot);
        slots_[slot].previous = tail_;
        slots_[slot].next = npos;
        if (tail_ != npos)
            slots_[tail_].next = slot;
        else
            head_ = slot;
        tail_ = slot;
    }

    // Removes the element; the handle and iterators to it become invalid.
    void erase(handle slot)
    {
        if (!isLive(slot))
            return;

        unlink(slot);
        releaseSlot(slot);
        --size_;
    }

    // Removes and returns the oldest value, or nullopt when empty.
    std::optional<T> popOldest()
    {
        if (head_ == npos)
            return std::nullopt;

        const handle slot = head_;
        std::optional<T> value(std::move(*slots_[slot].value));
        unlink(slot);
        releaseSlot(slot);
        --size_;
        return value;
    }

    // Destroys every value and returns all slots to the free list. The slot
    // array keeps its capacity.
    void clear() noexcept
    {
        for (Slot& slot : slots_) {
            slot.value.reset();
            slot.next = npos;
            slot.previous = npos;
        }

        size_ = 0;
        head_ = npos;
        tail_ = npos;
        freeHead_ = npos;
        for (size_type index = slots_.size(); index-- > 0;) {
            slots_[index].next = freeHead_;
            freeHead_ = index;
        }
    }

    // Releases the slot array once the list is empty.
    void shrinkToFit()
    {
        if (size_ != 0)
            return;
        slots_.clear();
        slots_.shrink_to_fit();
        freeHead_ = npos;
        head_ = npos;
        tail_ = npos;
    }

    // ---- Access -----------------------------------------------------------

    [[nodiscard]] bool isLive(handle slot) const noexcept
    {
        return slot < slots_.size() && slots_[slot].value.has_value();
    }

    T& at(handle slot)
    {
        assert(isLive(slot));
        return *slots_[slot].value;
    }

    const T& at(handle slot) const
    {
        assert(isLive(slot));
        return *slots_[slot].value;
    }

    // ---- Traversal --------------------------------------------------------

    [[nodiscard]] const_iterator begin() const noexcept
    {
        return const_iterator(this, head_);
    }

    [[nodiscard]] const_iterator end() const noexcept
    {
        return const_iterator(this, npos);
    }

    [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

private:
    struct Slot
    {
        std::optional<T> value;
        handle next = npos;
        handle previous = npos;
    };

    handle acquireSlot()
    {
        if (freeHead_ != npos) {
            const handle slot = freeHead_;
            freeHead_ = slots_[slot].next;
            slots_[slot].next = npos;
            slots_[slot].previous = npos;
            return slot;
        }

        slots_.emplace_back();
        return slots_.size() - 1;
    }

    void releaseSlot(handle slot) noexcept
    {
        slots_[slot].value.reset();
        slots_[slot].previous = npos;
        slots_[slot].next = freeHead_;
        freeHead_ = slot;
    }

    void unlink(handle slot) noexcept
    {
        const handle previous = slots_[slot].previous;
        const handle next = slots_[slot].next;

        if (previous != npos)
            slots_[previous].next = next;
        else
            head_ = next;

        if (next != npos)
            slots_[next].previous = previous;
        else
            tail_ = previous;
    }

    std::vector<Slot> slots_;
    handle freeHead_ = npos;
    handle head_ = npos; // oldest
    handle tail_ = npos; // newest
    size_type size_ = 0;
};

} // namespace md::structure
