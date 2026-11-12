// Periodicity — unordered LRU set (structure).
//
// UnorderedLRUSet<Key> is UnorderedLRUMap without a mapped value: a
// fixed-capacity recency cache of keys. The index is a std::unordered_map, so
// insert/contains/erase/touch are expected O(1); iteration walks the recency
// list from the oldest key to the newest.
//
// The set reuses detail::LruCore from unordered_lru_map.h with a unit mapped
// type, so promotion, eviction, capacity handling and the eviction handler are
// exactly the map's:
//
//   * insert() of a new key appends at the newest end and returns true;
//     re-inserting an existing key promotes it and returns false.
//   * inserting into a full set evicts the least-recently-used key first.
//   * contains()/erase() never promote; touch() does.
//   * capacity() == 0 means unbounded.
//   * the eviction handler runs after the victim has been removed and never
//     runs for clear()/erase() or the destructor.
//
// Order policy: LinkedLRUList<Key> by default, ArrayLRUList<Key> optionally:
//
//     UnorderedLRUSet<Key, Hash, Equal, ArrayLRUList<Key>>
//
// Iterators are invalidated only when their own entry is erased; insertions,
// promotions and erasure of other entries leave them pointing at the same key.

#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include <md/structure/lru/unordered_lru_map.h>

namespace md::structure {

template <class Key, class Hash = std::hash<Key>, class Equal = std::equal_to<Key>,
          class Order = LinkedLRUList<Key>>
class UnorderedLRUSet
{
public:
    using key_type = Key;
    using size_type = std::size_t;
    using hasher = Hash;
    using key_equal = Equal;
    using order_type = Order;
    using handle_type = typename order_type::handle;
    using entry_type = detail::LruEntry<detail::LruUnit, handle_type>;
    using index_type = std::unordered_map<Key, entry_type, Hash, Equal>;
    using const_iterator = typename order_type::const_iterator;
    using eviction_handler = std::function<void(const Key&)>;

    // `capacity` is the maximum number of live keys; 0 means unbounded.
    explicit UnorderedLRUSet(size_type capacity = 1024)
        : core_(capacity, index_type(0, Hash{}, Equal{}))
    {
    }

    // Same, with explicit hash/equality (for stateful functors).
    UnorderedLRUSet(size_type capacity, Hash hash, Equal equal)
        : core_(capacity, index_type(0, std::move(hash), std::move(equal)))
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

    // Keys from oldest to newest.
    [[nodiscard]] std::vector<Key> keys() const { return core_.keys(); }

    // Visits keys from oldest to newest.
    template <class Function>
    void forEach(Function&& function) const
    {
        for (const Key& key : core_.order())
            function(key);
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

    // Recency order, oldest first.
    [[nodiscard]] const_iterator begin() const { return core_.order().begin(); }
    [[nodiscard]] const_iterator end() const { return core_.order().end(); }
    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const { return end(); }

private:
    detail::LruCore<Key, detail::LruUnit, index_type, Order> core_;
};

} // namespace md::structure
