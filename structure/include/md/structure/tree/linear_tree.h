// Periodicity — linear tree (structure).
//
// A linear tree is a chain of LinearNodes: one successor per node, a head and a
// tail, and no branching. It is the degenerate tree of the catalogue and, as a
// sequence container, the one that most code actually wants: push/pop at either
// end, insert after a known node, splice two chains in O(1).
//
//     LinearTree is the same structure as LinearLink
//
// The two names are kept because the tree vocabulary (LinearTree, BinaryTree,
// BinaryPlusTree) is what the catalogue is indexed by, while "link" is the
// shape of the node. LinearLink<T> is an alias for LinearTree<T>; there is no
// second implementation to drift out of sync.
//
// Costs, stated once:
//
//   pushFront, popFront, insertAfter, spliceAfter   O(1)
//   pushBack, back                                  O(1) — the tail is cached
//   popBack                                         O(n) — a singly linked
//                                                   chain cannot find the
//                                                   predecessor of its tail
//   size                                            O(1)
//
// The list owns its nodes, so it is movable but not copyable: duplicating a
// chain would have to duplicate every payload, and a structure module should
// not quietly deep-copy what a caller passed by value.

#pragma once

#include <cstddef>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

#include <md/structure/node/linear_node.h>

namespace md::structure {

template <class T>
class LinearTree
{
public:
    using value_type = T;
    using node_type = LinearNode<T>;
    using size_type = std::size_t;

    template <bool IsConst>
    class basic_iterator
    {
    public:
        using node_pointer = std::conditional_t<IsConst, const node_type*, node_type*>;
        using reference = std::conditional_t<IsConst, const T&, T&>;
        using pointer = std::conditional_t<IsConst, const T*, T*>;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        basic_iterator() noexcept = default;
        explicit basic_iterator(node_pointer node) noexcept : node_(node) {}

        [[nodiscard]] reference operator*() const noexcept { return node_->value; }
        [[nodiscard]] pointer operator->() const noexcept { return &node_->value; }

        basic_iterator& operator++() noexcept
        {
            node_ = node_->next.get();
            return *this;
        }

        basic_iterator operator++(int) noexcept
        {
            basic_iterator copy = *this;
            ++(*this);
            return copy;
        }

        [[nodiscard]] friend bool operator==(const basic_iterator& lhs,
                                             const basic_iterator& rhs) noexcept
        {
            return lhs.node_ == rhs.node_;
        }

        [[nodiscard]] friend bool operator!=(const basic_iterator& lhs,
                                             const basic_iterator& rhs) noexcept
        {
            return lhs.node_ != rhs.node_;
        }

        [[nodiscard]] node_pointer node() const noexcept { return node_; }

    private:
        node_pointer node_ = nullptr;
    };

    using iterator = basic_iterator<false>;
    using const_iterator = basic_iterator<true>;

    LinearTree() noexcept = default;
    ~LinearTree() = default;

    LinearTree(const LinearTree&) = delete;
    LinearTree& operator=(const LinearTree&) = delete;
    LinearTree(LinearTree&& other) noexcept
        : head_(std::move(other.head_)), tail_(other.tail_), size_(other.size_)
    {
        other.tail_ = nullptr;
        other.size_ = 0;
    }

    LinearTree& operator=(LinearTree&& other) noexcept
    {
        if (this != &other) {
            clear();
            head_ = std::move(other.head_);
            tail_ = other.tail_;
            size_ = other.size_;
            other.tail_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    // ---- Capacity ---------------------------------------------------------

    [[nodiscard]] bool empty() const noexcept { return head_ == nullptr; }
    [[nodiscard]] size_type size() const noexcept { return size_; }

    // ---- Element access ---------------------------------------------------

    [[nodiscard]] T& front() noexcept { return head_->value; }
    [[nodiscard]] const T& front() const noexcept { return head_->value; }
    [[nodiscard]] T& back() noexcept { return tail_->value; }
    [[nodiscard]] const T& back() const noexcept { return tail_->value; }

    [[nodiscard]] node_type* head() noexcept { return head_.get(); }
    [[nodiscard]] const node_type* head() const noexcept { return head_.get(); }
    [[nodiscard]] node_type* tail() noexcept { return tail_; }
    [[nodiscard]] const node_type* tail() const noexcept { return tail_; }

    // ---- Modifiers --------------------------------------------------------

    // Inserts `value` at the front and returns the stored value. O(1).
    T& pushFront(T value)
    {
        auto node = std::make_unique<node_type>(std::move(value));
        node->next = std::move(head_);
        head_ = std::move(node);
        if (tail_ == nullptr)
            tail_ = head_.get();
        ++size_;
        return head_->value;
    }

    // Inserts `value` at the back and returns the stored value. O(1).
    T& pushBack(T value)
    {
        auto node = std::make_unique<node_type>(std::move(value));
        node_type* raw = node.get();
        if (tail_ != nullptr) {
            tail_->next = std::move(node);
        } else {
            head_ = std::move(node);
        }
        tail_ = raw;
        ++size_;
        return tail_->value;
    }

    // Inserts `value` after `position`, or at the front when `position` is
    // null. O(1). `position` must belong to this list.
    T& insertAfter(node_type* position, T value)
    {
        if (position == nullptr)
            return pushFront(std::move(value));

        auto node = std::make_unique<node_type>(std::move(value));
        node_type* raw = node.get();
        node->next = std::move(position->next);
        position->next = std::move(node);
        if (tail_ == position)
            tail_ = raw;
        ++size_;
        return raw->value;
    }

    // Removes the first element. Undefined when empty.
    void popFront() noexcept
    {
        head_ = std::move(head_->next);
        if (head_ == nullptr)
            tail_ = nullptr;
        --size_;
    }

    // Removes the last element in O(n), because the predecessor of the tail is
    // only reachable by walking the chain. Undefined when empty.
    void popBack() noexcept
    {
        if (head_.get() == tail_) {
            head_.reset();
            tail_ = nullptr;
            size_ = 0;
            return;
        }

        node_type* node = head_.get();
        while (node->next.get() != tail_)
            node = node->next.get();
        node->next.reset();
        tail_ = node;
        --size_;
    }

    // Removes the node after `position`, or the head when `position` is null.
    // Returns whether a node was removed; `position` must belong to this list.
    bool eraseAfter(node_type* position) noexcept
    {
        node_type* victim = position == nullptr ? head_.get() : position->next.get();
        if (victim == nullptr)
            return false;

        if (victim == head_.get()) {
            popFront();
        } else {
            position->next = std::move(victim->next);
            if (victim == tail_)
                tail_ = position;
            --size_;
        }
        return true;
    }

    // Removes the first element equal to `value`. Returns whether one was
    // removed. O(n).
    bool remove(const T& value)
    {
        node_type* previous = nullptr;
        for (node_type* node = head_.get(); node != nullptr; node = node->next.get()) {
            if (node->value == value) {
                eraseAfter(previous);
                return true;
            }
            previous = node;
        }
        return false;
    }

    // Removes every element for which `predicate` returns true and returns how
    // many were removed. O(n).
    template <class Predicate>
    size_type removeIf(Predicate&& predicate)
    {
        size_type removed = 0;
        node_type* previous = nullptr;
        node_type* node = head_.get();
        while (node != nullptr) {
            node_type* next = node->next.get();
            if (predicate(node->value)) {
                eraseAfter(previous);
                ++removed;
            } else {
                previous = node;
            }
            node = next;
        }
        return removed;
    }

    // Removes every element. The chain is released iteratively so that a long
    // list cannot exhaust the stack through recursive unique_ptr destruction.
    void clear() noexcept
    {
        while (head_ != nullptr)
            head_ = std::move(head_->next);
        tail_ = nullptr;
        size_ = 0;
    }

    // Reverses the chain in place in O(n) time and O(1) space. The links are
    // rewired through raw pointers and re-owned at the end, which keeps the
    // loop free of temporaries that would have to be released by hand.
    void reverse() noexcept
    {
        if (head_ == nullptr)
            return;

        node_type* previous = nullptr;
        node_type* node = head_.release();
        tail_ = node;
        while (node != nullptr) {
            node_type* next = node->next.release();
            node->next.reset(previous);
            previous = node;
            node = next;
        }
        head_.reset(previous);
    }

    // Moves every element of `other` after `position` (or to the front when
    // `position` is null), leaving `other` empty. O(1). `position` must belong
    // to this list, and `other` must not be this list.
    void spliceAfter(node_type* position, LinearTree&& other) noexcept
    {
        if (this == &other || other.head_ == nullptr)
            return;

        if (position == nullptr) {
            other.tail_->next = std::move(head_);
            head_ = std::move(other.head_);
            if (tail_ == nullptr)
                tail_ = other.tail_;
        } else {
            other.tail_->next = std::move(position->next);
            position->next = std::move(other.head_);
            if (tail_ == position)
                tail_ = other.tail_;
        }

        size_ += other.size_;
        other.tail_ = nullptr;
        other.size_ = 0;
    }

    // Moves the whole of `other` to the back of this list. O(1).
    void append(LinearTree&& other) noexcept { spliceAfter(tail_, std::move(other)); }

    // ---- Lookup -----------------------------------------------------------

    [[nodiscard]] node_type* find(const T& value) noexcept
    {
        for (node_type* node = head_.get(); node != nullptr; node = node->next.get()) {
            if (node->value == value)
                return node;
        }
        return nullptr;
    }

    [[nodiscard]] const node_type* find(const T& value) const noexcept
    {
        for (const node_type* node = head_.get(); node != nullptr; node = node->next.get()) {
            if (node->value == value)
                return node;
        }
        return nullptr;
    }

    [[nodiscard]] bool contains(const T& value) const noexcept
    {
        return find(value) != nullptr;
    }

    // First element satisfying `predicate`, or nullptr.
    template <class Predicate>
    [[nodiscard]] node_type* findIf(Predicate&& predicate) noexcept
    {
        for (node_type* node = head_.get(); node != nullptr; node = node->next.get()) {
            if (predicate(node->value))
                return node;
        }
        return nullptr;
    }

    template <class Predicate>
    [[nodiscard]] const node_type* findIf(Predicate&& predicate) const noexcept
    {
        for (const node_type* node = head_.get(); node != nullptr; node = node->next.get()) {
            if (predicate(node->value))
                return node;
        }
        return nullptr;
    }

    // ---- Traversal --------------------------------------------------------

    template <class Function>
    void forEach(Function&& function)
    {
        for (node_type* node = head_.get(); node != nullptr; node = node->next.get())
            function(node->value);
    }

    template <class Function>
    void forEach(Function&& function) const
    {
        for (const node_type* node = head_.get(); node != nullptr; node = node->next.get())
            function(node->value);
    }

    [[nodiscard]] iterator begin() noexcept { return iterator(head_.get()); }
    [[nodiscard]] iterator end() noexcept { return iterator(nullptr); }
    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(head_.get()); }
    [[nodiscard]] const_iterator end() const noexcept { return const_iterator(nullptr); }

private:
    std::unique_ptr<node_type> head_;
    node_type* tail_ = nullptr;
    size_type size_ = 0;
};

// The alias exists so that both the tree vocabulary and the link vocabulary
// name the same type (see the header comment).
template <class T>
using LinearLink = LinearTree<T>;

} // namespace md::structure
