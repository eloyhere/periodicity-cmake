// Periodicity — ordered LRU set (structure).
//
// OrderedLRUSet<Key, Compare> is the set face of the ordered LRU family: the
// same LRU cache as OrderedLRUMap, but storing only keys. Its index is a
// std::map, so insertion/erase/contains are O(log n) and iteration
// (begin()/end()/forEach()) visits the live keys in **ascending key order**.
// Eviction still removes the least-recently-used key, not the smallest.
//
// keys() stays in recency order (oldest -> newest) because it reports the
// eviction view; the sorted view is the iterator. See ordered_lru_map.h for the
// shared implementation notes (detail::LruCore, eviction-handler timing and
// iterator invalidation).
//
// Order policy: LinkedLRUList<Key> by default, ArrayLRUList<Key> optionally:
//
//     OrderedLRUSet<Key, Compare, ArrayLRUList<Key>>
//
// capacity() == 0 means unbounded. Explicit erase() never promotes; contains()
// never promotes; touch() does.

#pragma once

#include <cstddef>
#include <functional>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

#include <md/structure/lru/ordered_lru_map.h>

namespace md::structure {

template <class Key, class Compare = std::less<Key>, class Order = LinkedLRUList<Key>>
class OrderedLRUSet
{
public:
    using key_type = Key;
    using size_type = std::size_t;
    using key_compare = Compare;
    using order_type = Order;
    using handle_type = typename order_type::handle;
    using entry_type = detail::LruEntry<detail::LruUnit, handle_type>;
    using index_type = std::map<Key, entry_type, Compare>;
    using eviction_handler = std::function<void(const Key&)>;

    class const_iterator
    {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = Key;
        using reference = const Key&;
        using pointer = const Key*;
        using difference_type = std::ptrdiff_t;

        const_iterator() = default;

        reference operator*() const { return position_->first; }
        pointer operator->() const { return &position_->first; }

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

        friend class OrderedLRUSet<Key, Compare, Order>;
    };

    // `capacity` is the maximum number of live keys; 0 means unbounded.
    explicit OrderedLRUSet(size_type capacity = 1024)
        : core_(capacity, index_type(Compare{}))
    {
    }

    // Same, with an explicit comparator (for stateful comparisons).
    OrderedLRUSet(size_type capacity, Compare compare)
        : core_(capacity, index_type(std::move(compare)))
    {
    }

    // ---- Capacity ---------------------------------------------------------

    [[nodiscard]] size_type capacity() const noexcept { return core_.capacity(); }

    // Shrinking evicts oldest keys immediately (handler included).
    void setCapacity(size_type capacity) { core_.setCapacity(capacity); }

    [[nodiscard]] size_type size() const noexcept { return core_.size(); }
    [[nodiscard]] bool empty() const noexcept { return core_.empty(); }

    // Pre-allocates recency-list storage; capacity() is unaffected.
    void reserve(size_type count) { core_.reserve(count); }

    void clear() noexcept { core_.clear(); }

    // ---- Modifiers --------------------------------------------------------

    // Inserts `key` as the newest entry (true), or promotes it (false).
    bool insert(Key key) { return core_.put(std::move(key), detail::LruUnit{}); }

    bool erase(const Key& key) { return core_.erase(key); }

    void touch(const Key& key) { core_.touch(key); }

    // Const membership test; never promotes.
    [[nodiscard]] bool contains(const Key& key) const { return core_.contains(key); }

    // ---- Views ------------------------------------------------------------

    // Keys from oldest to newest (eviction order, not key order).
    [[nodiscard]] std::vector<Key> keys() const { return core_.keys(); }

    // Visits keys in key order, matching begin()/end().
    template <class Function>
    void forEach(Function&& function) const
    {
        for (const auto& entry : core_.index())
            function(entry.first);
    }

    // Called after a key has been evicted (never by clear()/erase() or the
    // destructor).
    void setEvictionHandler(eviction_handler handler)
    {
        if (!handler) {
            core_.setEvictionHandler(nullptr);
            return;
        }
        core_.setEvictionHandler(
            [callback = std::move(handler)](const Key& key, const detail::LruUnit&) {
                callback(key);
            });
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
    detail::LruCore<Key, detail::LruUnit, index_type, Order> core_;
};

} // namespace md::structure
