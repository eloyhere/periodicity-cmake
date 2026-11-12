// Periodicity — intrusive doubly-linked LRU order list (structure).
//
// LinkedLRUList<T> keeps values in birth order (oldest first, newest last).
// insert() appends a node, touch() splices an existing node to the newest end,
// popOldest() unlinks the oldest node and moves its value out. A node never
// moves, so the iterator (handle) returned by insert() stays valid and points
// at the same value until that node is erased; this is the property the LRU
// containers rely on when they store handles in their index.
//
//     insert / touch / erase / popOldest     O(1)
//     iteration                              O(n), oldest -> newest
//     clear                                  O(n), keeps the pool warm
//
// Node storage comes from md::structure::ObjectPool<Node> (allocator/
// object_pool.h), which is a chunked BlockAllocator: one `new[]` per chunk of
// nodes, never one allocation per insert. The pool is *not* md::pool::
// ObjectPool because the structure layer may depend on core only
// (tools/check_layering.cmake); this allocator is the structure-local,
// dependency-free equivalent.
//
// reserve(n) pre-allocates chunks for n live nodes; after it, inserting and
// erasing up to n nodes does not allocate chunks (capacity() and chunkCount()
// stay put). clear() destroys the values, returns the nodes to the pool and
// keeps the memory; shrinkToFit() releases it (and the destructor does too).
//
// Iterators are invalidated only when their own node is erased. Insertions,
// promotions and erasures of other nodes leave them pointing at the same
// value, and ++ follows the current links.

#pragma once

#include <cassert>
#include <cstddef>
#include <iterator>
#include <optional>
#include <utility>

#include <md/structure/allocator/object_pool.h>

namespace md::structure {

template <class T>
class LinkedLRUList
{
    struct Node
    {
        std::optional<T> value;
        Node* previous = nullptr;
        Node* next = nullptr;
    };

public:
    using value_type = T;
    using size_type = std::size_t;

    class iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using reference = T&;
        using pointer = T*;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        reference operator*() const
        {
            assert(node_ != nullptr && node_->value.has_value());
            return *node_->value;
        }

        pointer operator->() const { return &**this; }

        iterator& operator++()
        {
            assert(node_ != nullptr);
            node_ = node_->next;
            return *this;
        }

        iterator operator++(int)
        {
            iterator copy = *this;
            ++*this;
            return copy;
        }

        friend bool operator==(const iterator& lhs, const iterator& rhs) noexcept
        {
            return lhs.node_ == rhs.node_;
        }

        friend bool operator!=(const iterator& lhs, const iterator& rhs) noexcept
        {
            return lhs.node_ != rhs.node_;
        }

    private:
        explicit iterator(Node* node) noexcept : node_(node) {}

        Node* node_ = nullptr;

        friend class LinkedLRUList<T>;
    };

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
            assert(node_ != nullptr && node_->value.has_value());
            return *node_->value;
        }

        pointer operator->() const { return &**this; }

        const_iterator& operator++()
        {
            assert(node_ != nullptr);
            node_ = node_->next;
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
            return lhs.node_ == rhs.node_;
        }

        friend bool operator!=(const const_iterator& lhs, const const_iterator& rhs) noexcept
        {
            return lhs.node_ != rhs.node_;
        }

    private:
        explicit const_iterator(const Node* node) noexcept : node_(node) {}

        const Node* node_ = nullptr;

        friend class LinkedLRUList<T>;
    };

    using handle = iterator;

    // `blocksPerChunk` is the pool's allocation granularity; reserve() is how
    // many nodes are pre-allocated.
    explicit LinkedLRUList(size_type blocksPerChunk = 64)
        : pool_(blocksPerChunk)
    {
    }

    LinkedLRUList(const LinkedLRUList& other)
        : LinkedLRUList(other.pool_.blocksPerChunk())
    {
        copyFrom(other);
    }

    LinkedLRUList& operator=(const LinkedLRUList& other)
    {
        if (this != &other) {
            clear();
            copyFrom(other);
        }
        return *this;
    }

    // A move transfers the pool; node addresses do not change, so handles taken
    // from `other` remain valid and now belong to `*this`.
    LinkedLRUList(LinkedLRUList&& other) noexcept
        : pool_(std::move(other.pool_)), head_(other.head_), tail_(other.tail_),
          size_(other.size_)
    {
        other.head_ = nullptr;
        other.tail_ = nullptr;
        other.size_ = 0;
    }

    LinkedLRUList& operator=(LinkedLRUList&& other) noexcept
    {
        if (this != &other) {
            clear();
            pool_ = std::move(other.pool_);
            head_ = other.head_;
            tail_ = other.tail_;
            size_ = other.size_;

            other.head_ = nullptr;
            other.tail_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    ~LinkedLRUList() { clear(); }

    // ---- Capacity ---------------------------------------------------------

    // Pre-allocates pool chunks for `nodes` live nodes.
    void reserve(size_type nodes) { pool_.reserve(nodes); }

    [[nodiscard]] size_type size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    // Nodes currently owned by the pool (live + free).
    [[nodiscard]] size_type capacity() const noexcept { return pool_.capacity(); }

    // Live nodes; always equal to size().
    [[nodiscard]] size_type liveNodes() const noexcept { return pool_.inUse(); }

    // Free nodes waiting on the pool's free list.
    [[nodiscard]] size_type pooledNodes() const noexcept { return pool_.pooled(); }

    // Pool chunks; stays constant across insert/erase after reserve(n).
    [[nodiscard]] size_type chunkCount() const noexcept { return pool_.chunkCount(); }

    // ---- Modifiers --------------------------------------------------------

    // Appends `value` as the newest node and returns its stable handle.
    iterator insert(T value)
    {
        Node* node = pool_.create();
        try {
            node->value.emplace(std::move(value));
        } catch (...) {
            pool_.destroy(node);
            throw;
        }

        linkNewest(node);
        ++size_;
        return iterator(node);
    }

    // Moves the node to the newest end. A null/end handle is ignored.
    void touch(iterator position)
    {
        Node* node = position.node_;
        if (node == nullptr || node == tail_)
            return;
        unlink(node);
        linkNewest(node);
    }

    // Removes the node. The handle and every iterator to that value become
    // invalid; iterators to other nodes stay valid.
    void erase(iterator position)
    {
        Node* node = position.node_;
        if (node == nullptr)
            return;
        unlink(node);
        pool_.destroy(node);
        --size_;
    }

    // Removes and returns the oldest value, or nullopt when empty.
    std::optional<T> popOldest()
    {
        if (head_ == nullptr)
            return std::nullopt;

        Node* node = head_;
        std::optional<T> value(std::move(*node->value));
        unlink(node);
        pool_.destroy(node);
        --size_;
        return value;
    }

    // Destroys every value and returns the nodes to the pool. The chunks are
    // kept (unlike ObjectPool::clear), so a refill does not allocate.
    void clear() noexcept
    {
        Node* node = head_;
        while (node != nullptr) {
            Node* next = node->next;
            pool_.destroy(node);
            node = next;
        }
        head_ = nullptr;
        tail_ = nullptr;
        size_ = 0;
    }

    // Releases chunks that never handed out a node; releases everything once
    // the list is empty.
    void shrinkToFit() { pool_.shrinkToFit(); }

    // ---- Traversal --------------------------------------------------------

    [[nodiscard]] iterator begin() noexcept { return iterator(head_); }
    [[nodiscard]] iterator end() noexcept { return iterator(nullptr); }
    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(head_); }
    [[nodiscard]] const_iterator end() const noexcept { return const_iterator(nullptr); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

private:
    void copyFrom(const LinkedLRUList& other)
    {
        reserve(other.size_);
        try {
            for (const T& value : other)
                insert(value);
        } catch (...) {
            clear();
            throw;
        }
    }

    void linkNewest(Node* node) noexcept
    {
        node->previous = tail_;
        node->next = nullptr;
        if (tail_ != nullptr)
            tail_->next = node;
        else
            head_ = node;
        tail_ = node;
    }

    void unlink(Node* node) noexcept
    {
        if (node->previous != nullptr)
            node->previous->next = node->next;
        else
            head_ = node->next;

        if (node->next != nullptr)
            node->next->previous = node->previous;
        else
            tail_ = node->previous;

        node->previous = nullptr;
        node->next = nullptr;
    }

    ObjectPool<Node> pool_;
    Node* head_ = nullptr; // oldest
    Node* tail_ = nullptr; // newest
    size_type size_ = 0;
};

} // namespace md::structure
