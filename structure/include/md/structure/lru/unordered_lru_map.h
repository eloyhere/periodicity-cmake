// Periodicity — unordered LRU map (structure).
//
// UnorderedLRUMap<Key, Value> is a fixed-capacity cache with least-recently-
// used eviction. "Unordered" describes iteration, not lookup: the index is a
// std::unordered_map, so every key operation is expected O(1), while iterating
// walks the recency list from the oldest entry to the newest.
//
// This header also holds the shared implementation of all four LRU faces:
//
//     detail::LruCore<Key, Value, Index, Order>
//
// The core owns three things: the index (Key -> { Value, order handle }), the
// order container (one key per live entry, oldest first), and the capacity.
// The four public faces differ only in the index type (unordered_map vs map)
// and in how they expose iteration:
//
//     UnorderedLRUMap/Set   iteration = recency order (oldest -> newest)
//     OrderedLRUMap/Set     iteration = key order (sorted); keys() still
//                           reports recency, because it describes eviction
//
// The order container is a policy parameter (Order). The default is
// LinkedLRUList<Key> (intrusive nodes from an internal pool); pass
// ArrayLRUList<Key> for index slots in one growable array:
//
//     UnorderedLRUMap<Key, Value, Hash, Equal, ArrayLRUList<Key>>
//
// Recency/promotion/eviction:
//   * put() of a new key appends at the newest end; if the cache is full the
//     oldest entry is evicted *before* the insertion.
//   * put() of an existing key assigns and promotes it.
//   * find()/get()/tryGet()/touch() promote; peek()/contains() never do.
//   * capacity() == 0 means unbounded: nothing is evicted until setCapacity()
//     installs a positive bound.
//   * an eviction handler (setEvictionHandler) runs *after* the entry has been
//     removed from both the index and the recency list, so the callback sees
//     the cache without the victim. Explicit erase()/take()/clear() and the
//     destructor never call it.
//
// Iterator invalidation: put(), promotion and erasure of *other* entries keep
// an iterator pointing at the same entry; erasing that entry invalidates it.
// Pointers/references to mapped values stay valid until their own key is
// erased. Moving the map invalidates iterators (they point into the old map).
//
// The map itself does not throw on the happy path; allocation failures and
// user-provided handler/constructor exceptions propagate.

#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <md/structure/lru/linked_lru_list.h>

namespace md::structure::detail {

// Marker value for the set faces: they reuse LruCore with a unit mapped type.
struct LruUnit
{
};

// One index slot: the value plus the handle of its entry in the recency list.
template <class Value, class Handle>
struct LruEntry
{
    Value value;
    Handle handle;
};

// Shared LRU machinery. Index must be std::unordered_map<Key, LruEntry<...>>
// or std::map<Key, LruEntry<...>, Compare>; Order is an order container with
// the LinkedLRUList/ArrayLRUList surface.
template <class Key, class Value, class Index, class Order>
class LruCore
{
public:
    using key_type = Key;
    using mapped_type = Value;
    using size_type = std::size_t;
    using index_type = Index;
    using order_type = Order;
    using handle_type = typename Order::handle;
    using entry_type = LruEntry<Value, handle_type>;
    using eviction_handler = std::function<void(const Key&, const Value&)>;

    static_assert(std::is_same_v<typename Index::mapped_type, entry_type>,
                  "Index must map Key to LruEntry<Value, Order::handle>");

    LruCore(size_type capacity, index_type index)
        : capacity_(capacity), index_(std::move(index))
    {
    }

    // A copy rebuilds the recency order and the handles from the source's
    // recency list; handles are never copied directly (a pool handle points at
    // the source's storage). The index's hash/equality (or comparison) state is
    // copied too, so a cache with stateful functors keeps its semantics.
    LruCore(const LruCore& other)
        : capacity_(other.capacity_), index_(copyIndex(other.index_)),
          handler_(other.handler_)
    {
        for (const Key& key : other.order_) {
            const auto found = other.index_.find(key);
            if (found != other.index_.end())
                put(key, found->second.value);
        }
    }

    LruCore& operator=(const LruCore& other)
    {
        if (this != &other) {
            LruCore copy(other);
            swap(copy);
        }
        return *this;
    }

    LruCore(LruCore&&) = default;
    LruCore& operator=(LruCore&&) = default;
    ~LruCore() = default;

    // ---- Capacity ---------------------------------------------------------

    [[nodiscard]] size_type size() const noexcept { return index_.size(); }
    [[nodiscard]] bool empty() const noexcept { return index_.empty(); }
    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }

    // Grows or shrinks the bound. Shrinking evicts oldest entries (calling the
    // eviction handler) until size() <= capacity. Zero means unbounded.
    void setCapacity(size_type capacity)
    {
        capacity_ = capacity;
        if (capacity_ == 0)
            return;
        while (index_.size() > capacity_ && !order_.empty())
            evictOldest();
    }

    // Pre-allocates recency-list storage; does not change capacity().
    void reserve(size_type count) { order_.reserve(count); }

    // ---- Lookup -----------------------------------------------------------

    // Promotes on hit and returns a pointer to the mapped value, or nullptr.
    Value* find(const Key& key)
    {
        const auto found = index_.find(key);
        if (found == index_.end())
            return nullptr;
        order_.touch(found->second.handle);
        return &found->second.value;
    }

    // Const lookup that does NOT promote.
    [[nodiscard]] const Value* peek(const Key& key) const
    {
        const auto found = index_.find(key);
        return found == index_.end() ? nullptr : &found->second.value;
    }

    // Const membership test; never promotes.
    [[nodiscard]] bool contains(const Key& key) const
    {
        return index_.find(key) != index_.end();
    }

    // ---- Modifiers --------------------------------------------------------

    // Inserts or assigns, promoting the entry in either case. Returns whether a
    // new entry was created.
    bool put(Key key, Value value)
    {
        const auto found = index_.find(key);
        if (found != index_.end()) {
            found->second.value = std::move(value);
            order_.touch(found->second.handle);
            return false;
        }

        if (capacity_ != 0 && index_.size() >= capacity_)
            evictOldest();

        const handle_type handle = order_.insert(key);
        try {
            index_.emplace(std::move(key), entry_type{std::move(value), handle});
        } catch (...) {
            order_.erase(handle);
            throw;
        }
        return true;
    }

    // Promotes an existing entry; a missing key is ignored.
    void touch(const Key& key)
    {
        const auto found = index_.find(key);
        if (found != index_.end())
            order_.touch(found->second.handle);
    }

    bool erase(const Key& key)
    {
        const auto found = index_.find(key);
        if (found == index_.end())
            return false;

        order_.erase(found->second.handle);
        index_.erase(found);
        return true;
    }

    // Removes the entry and moves its value out, or returns nullopt.
    std::optional<Value> take(const Key& key)
    {
        const auto found = index_.find(key);
        if (found == index_.end())
            return std::nullopt;

        auto node = index_.extract(found);
        order_.erase(node.mapped().handle);
        return std::optional<Value>(std::move(node.mapped().value));
    }

    // Drops every entry without calling the eviction handler; storage in the
    // index and the order container is released.
    void clear() noexcept
    {
        index_.clear();
        order_.clear();
    }

    // ---- Recency view -----------------------------------------------------

    // Keys from oldest to newest (the eviction order, for every face).
    [[nodiscard]] std::vector<Key> keys() const
    {
        std::vector<Key> result;
        result.reserve(order_.size());
        for (const Key& key : order_)
            result.push_back(key);
        return result;
    }

    // ---- Eviction ---------------------------------------------------------

    // `handler` receives the key and value of each entry evicted by put() or
    // setCapacity(). It is called after both containers have dropped the entry,
    // so it may inspect the cache without seeing the victim; it must not throw,
    // must not modify the cache and should not retain the references.
    void setEvictionHandler(eviction_handler handler)
    {
        handler_ = std::move(handler);
    }

    // ---- Storage views (used by the iteration adapters) --------------------

    [[nodiscard]] const order_type& order() const noexcept { return order_; }
    [[nodiscard]] order_type& order() noexcept { return order_; }
    [[nodiscard]] const index_type& index() const noexcept { return index_; }

private:
    // A fresh, empty index carrying the source's functor state. Unordered
    // indexes expose hash_function()/key_eq(); ordered indexes expose
    // key_comp().
    static index_type copyIndex(const index_type& source)
    {
        if constexpr (requires { source.hash_function(); source.key_eq(); }) {
            return index_type(0, source.hash_function(), source.key_eq());
        } else {
            return index_type(source.key_comp());
        }
    }

    void swap(LruCore& other)
    {
        using std::swap;
        swap(capacity_, other.capacity_);
        swap(index_, other.index_);
        swap(order_, other.order_);
        swap(handler_, other.handler_);
    }

    // Removes the oldest entry. The entry is extracted from the index first, so
    // the handler (which runs last) never observes a half-erased cache and the
    // key/value references stay alive for the duration of the call.
    void evictOldest()
    {
        if (order_.empty())
            return;

        Key victimKey = *order_.begin();
        auto node = index_.extract(victimKey);
        if (!node) {
            // Defensive: the two containers must agree; drop the order entry so
            // the invariant can recover instead of looping forever.
            order_.popOldest();
            return;
        }

        order_.erase(node.mapped().handle);
        if (handler_)
            handler_(victimKey, node.mapped().value);
    }

    size_type capacity_ = 0;
    index_type index_;
    order_type order_;
    eviction_handler handler_;
};

} // namespace md::structure::detail

namespace md::structure {

template <class Key, class Value, class Hash = std::hash<Key>,
          class Equal = std::equal_to<Key>, class Order = LinkedLRUList<Key>>
class UnorderedLRUMap
{
public:
    using key_type = Key;
    using mapped_type = Value;
    using size_type = std::size_t;
    using hasher = Hash;
    using key_equal = Equal;
    using order_type = Order;
    using handle_type = typename order_type::handle;
    using entry_type = detail::LruEntry<Value, handle_type>;
    using index_type = std::unordered_map<Key, entry_type, Hash, Equal>;
    using eviction_handler = std::function<void(const Key&, const Value&)>;

    class const_iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<const Key, Value>;
        using reference = std::pair<const Key&, const Value&>;
        using pointer = void;
        using difference_type = std::ptrdiff_t;

        const_iterator() = default;

        reference operator*() const
        {
            const Key& key = *position_;
            const auto found = index_->find(key);
            assert(found != index_->end() && "iterator outlived its entry");
            return reference(key, found->second.value);
        }

        // A small proxy so `it->first` / `it->second` work even though the
        // reference type is a pair of references rather than a stored pair.
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

        friend bool operator==(const const_iterator& lhs, const const_iterator& rhs)
        {
            return lhs.position_ == rhs.position_;
        }

        friend bool operator!=(const const_iterator& lhs, const const_iterator& rhs)
        {
            return !(lhs == rhs);
        }

    private:
        const_iterator(typename order_type::const_iterator position,
                       const index_type* index)
            : position_(position), index_(index)
        {
        }

        typename order_type::const_iterator position_{};
        const index_type* index_ = nullptr;

        friend class UnorderedLRUMap<Key, Value, Hash, Equal, Order>;
    };

    // `capacity` is the maximum number of live entries; 0 means unbounded.
    explicit UnorderedLRUMap(size_type capacity = 1024)
        : core_(capacity, index_type(0, Hash{}, Equal{}))
    {
    }

    // Same, with explicit hash/equality (for stateful functors).
    UnorderedLRUMap(size_type capacity, Hash hash, Equal equal)
        : core_(capacity, index_type(0, std::move(hash), std::move(equal)))
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

    // Keys from oldest to newest.
    [[nodiscard]] std::vector<Key> keys() const { return core_.keys(); }

    // Visits (key, value) from oldest to newest. The values are const; use
    // find() to obtain a mutable pointer.
    template <class Function>
    void forEach(Function&& function) const
    {
        for (const Key& key : core_.order()) {
            const auto found = core_.index().find(key);
            assert(found != core_.index().end() && "LRU index/order mismatch");
            function(key, found->second.value);
        }
    }

    // Called after an entry has been evicted (never by clear()/erase()/take()
    // or the destructor).
    void setEvictionHandler(eviction_handler handler)
    {
        core_.setEvictionHandler(std::move(handler));
    }

    // Recency order, oldest first.
    [[nodiscard]] const_iterator begin() const
    {
        return const_iterator(core_.order().begin(), &core_.index());
    }

    [[nodiscard]] const_iterator end() const
    {
        return const_iterator(core_.order().end(), &core_.index());
    }

    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const { return end(); }

private:
    detail::LruCore<Key, Value, index_type, Order> core_;
};

} // namespace md::structure
