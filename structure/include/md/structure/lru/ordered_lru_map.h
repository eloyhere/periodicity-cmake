// Periodicity — ordered LRU map (structure).
//
// OrderedLRUMap<Key, Value, Compare> has the same cache contract as
// UnorderedLRUMap — fixed capacity, LRU eviction, promotion on access — but
// its index is a std::map, so iteration (begin()/end()/forEach()) walks the
// live entries in **key order** (ascending by Compare). Eviction still removes
// the least-recently-used entry, not the smallest key. keys() deliberately
// stays in recency order (oldest -> newest) because it reports the eviction
// view; the ordered view is the iterator.
//
// Every operation on the index is O(log n); promotion is O(1) (the recency
// list splices the existing node). Iterating in key order is O(n).
//
// The implementation is detail::LruCore from unordered_lru_map.h. The Order
// policy defaults to LinkedLRUList<Key>; ArrayLRUList<Key> is also accepted:
//
//     OrderedLRUMap<Key, Value, Compare, ArrayLRUList<Key>>
//
// Iterator invalidation: an iterator stays valid across insertions, promotions
// and erasure of other entries; erasing its own entry invalidates it. Pointers
// and references to mapped values are stable until their key is erased. Moving
// the map invalidates iterators.
//
// The eviction handler runs after the victim has been removed from both the
// index and the recency list; explicit erase()/take()/clear() and the
// destructor never call it.
//
// No exceptions are thrown on the happy path.

#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <md/structure/lru/unordered_lru_map.h>

namespace md::structure {

template <class Key, class Value, class Compare = std::less<Key>,
          class Order = LinkedLRUList<Key>>
class OrderedLRUMap
{
public:
    using key_type = Key;
    using mapped_type = Value;
    using size_type = std::size_t;
    using key_compare = Compare;
    using order_type = Order;
    using handle_type = typename order_type::handle;
    using entry_type = detail::LruEntry<Value, handle_type>;
    using index_type = std::map<Key, entry_type, Compare>;
    using eviction_handler = std::function<void(const Key&, const Value&)>;

    class const_iterator
    {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = std::pair<const Key, Value>;
        using reference = std::pair<const Key&, const Value&>;
        using pointer = void;
        using difference_type = std::ptrdiff_t;

        const_iterator() = default;

        reference operator*() const
        {
            return reference(position_->first, position_->second.value);
        }

        // Lets `it->first` / `it->second` work with the reference-pair result.
        struct arrow_proxy
        {
            reference value;

            const reference* operator->() const noexcept { return &value; }
        };

        arrow_proxy operator->() const { return arrow_proxy{**this}; }

        const_iterator& operator++()
        {
            ++position_;
            return *this;
        }

        const_iterator operator++(int)
        {
            const_iterator copy = *this;
            ++position_;
            return copy;
        }

        const_iterator& operator--()
        {
            --position_;
            return *this;
        }

        const_iterator operator--(int)
        {
            const_iterator copy = *this;
            --position_;
            return copy;
        }

        friend bool operator==(const const_iterator& lhs, const const_iterator& rhs)
        {
            return lhs.position_ == rhs.position_;
        }

        friend bool operator!=(const const_iterator& lhs, const const_iterator& rhs)
        {
            return !(lhs == rhs);
        }

    private:
        explicit const_iterator(typename index_type::const_iterator position)
            : position_(position)
        {
        }

        typename index_type::const_iterator position_{};

        friend class OrderedLRUMap<Key, Value, Compare, Order>;
    };

    // `capacity` is the maximum number of live entries; 0 means unbounded.
    explicit OrderedLRUMap(size_type capacity = 1024)
        : core_(capacity, index_type(Compare{}))
    {
    }

    // Same, with an explicit comparator (for stateful comparisons).
    OrderedLRUMap(size_type capacity, Compare compare)
        : core_(capacity, index_type(std::move(compare)))
    {
    }

    // ---- Capacity ---------------------------------------------------------

    [[nodiscard]] size_type capacity() const noexcept { return core_.capacity(); }

    // Shrinking evicts oldest entries immediately (handler included).
    void setCapacity(size_type capacity) { core_.setCapacity(capacity); }

    [[nodiscard]] size_type size() const noexcept { return core_.size(); }
    [[nodiscard]] bool empty() const noexcept { return core_.empty(); }

    // Pre-allocates recency-list storage; capacity() is unaffected.
    void reserve(size_type count) { core_.reserve(count); }

    void clear() noexcept { core_.clear(); }

    // ---- Lookup -----------------------------------------------------------

    // Copies the value into *out on a hit, promoting the entry.
    bool tryGet(const Key& key, Value* out)
    {
        if (out == nullptr)
            return false;
        const Value* value = core_.find(key);
        if (value == nullptr)
            return false;
        *out = *value;
        return true;
    }

    // Copies the value into `out` on a hit, promoting the entry.
    bool get(const Key& key, Value& out)
    {
        const Value* value = core_.find(key);
        if (value == nullptr)
            return false;
        out = *value;
        return true;
    }

    // Promotes on hit; returns a pointer into the entry or nullptr.
    [[nodiscard]] Value* find(const Key& key) { return core_.find(key); }

    // Const lookup, no promotion.
    [[nodiscard]] const Value* peek(const Key& key) const { return core_.peek(key); }

    // Const membership test, no promotion.
    [[nodiscard]] bool contains(const Key& key) const { return core_.contains(key); }

    // ---- Modifiers --------------------------------------------------------

    void put(Key key, Value value) { core_.put(std::move(key), std::move(value)); }

    void insert_or_assign(Key key, Value value)
    {
        core_.put(std::move(key), std::move(value));
    }

    // Removes and moves the value out.
    std::optional<Value> take(const Key& key) { return core_.take(key); }

    bool erase(const Key& key) { return core_.erase(key); }

    void touch(const Key& key) { core_.touch(key); }

    // ---- Views ------------------------------------------------------------

    // Keys from oldest to newest (eviction order, not key order).
    [[nodiscard]] std::vector<Key> keys() const { return core_.keys(); }

    // Visits (key, value) in key order, matching begin()/end().
    template <class Function>
    void forEach(Function&& function) const
    {
        for (const auto& entry : core_.index())
            function(entry.first, entry.second.value);
    }

    // Called after an entry has been evicted (never by clear()/erase()/take()
    // or the destructor).
    void setEvictionHandler(eviction_handler handler)
    {
        core_.setEvictionHandler(std::move(handler));
    }

    // Key order.
    [[nodiscard]] const_iterator begin() const
    {
        return const_iterator(core_.index().begin());
    }

    [[nodiscard]] const_iterator end() const
    {
        return const_iterator(core_.index().end());
    }

    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const { return end(); }

private:
    detail::LruCore<Key, Value, index_type, Order> core_;
};

} // namespace md::structure
