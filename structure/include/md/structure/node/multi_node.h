// Periodicity — multiway node (structure).
//
// A node with a fixed capacity of owned children: the step after binary that
// generalises the arity without reaching for a heap-allocated child list.
//
//   MultiNode<T, 2>   is a binary node with an indexable child array
//   MultiNode<T, 3>   a ternary node
//   MultiNode<T, 4>   a quadtree node (spatial trees use the same shape)
//
// Children are packed at the front of the array: children[0 .. childCount)
// are non-null and the remaining slots are empty. Packing is what makes
// insertChild and removeChild shift instead of leaving holes, so a traversal
// never has to skip null entries and childCount() is O(1).

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <utility>

namespace md::structure {

template <class T, std::size_t N>
struct MultiNode
{
    static_assert(N >= 1, "a multiway node needs room for at least one child");

    using value_type = T;
    using node_type = MultiNode;
    using size_type = std::size_t;

    static constexpr size_type capacity = N;

    template <class... Args>
    explicit MultiNode(Args&&... args) : value(std::forward<Args>(args)...) {}

    MultiNode(const MultiNode&) = delete;
    MultiNode& operator=(const MultiNode&) = delete;
    MultiNode(MultiNode&&) = default;
    MultiNode& operator=(MultiNode&&) = default;

    T value;
    std::array<std::unique_ptr<node_type>, N> children{};
    size_type childCount = 0;

    [[nodiscard]] bool isLeaf() const noexcept { return childCount == 0; }
    [[nodiscard]] bool isFull() const noexcept { return childCount == N; }

    // The child at `index`, or nullptr when the index is outside the packed
    // range. Callers that iterate use childCount.
    [[nodiscard]] node_type* child(size_type index) noexcept
    {
        return index < childCount ? children[index].get() : nullptr;
    }

    [[nodiscard]] const node_type* child(size_type index) const noexcept
    {
        return index < childCount ? children[index].get() : nullptr;
    }

    // Appends `node` as the last child and returns its index. Returns N
    // (capacity, an invalid index) when the node is full, leaving ownership
    // with the caller.
    size_type appendChild(std::unique_ptr<node_type> node) noexcept
    {
        if (node == nullptr || isFull())
            return N;
        children[childCount] = std::move(node);
        return childCount++;
    }

    // Removes and returns the child at `index`, shifting the later children
    // down. Returns nullptr when the index is out of range.
    std::unique_ptr<node_type> removeChild(size_type index) noexcept
    {
        if (index >= childCount)
            return nullptr;
        std::unique_ptr<node_type> removed = std::move(children[index]);
        for (size_type i = index; i + 1 < childCount; ++i)
            children[i] = std::move(children[i + 1]);
        --childCount;
        return removed;
    }

    // Replaces the child at `index`, returning the previous occupant.
    std::unique_ptr<node_type> replaceChild(size_type index,
                                             std::unique_ptr<node_type> node) noexcept
    {
        if (index >= childCount)
            return nullptr;
        std::unique_ptr<node_type> previous = std::move(children[index]);
        children[index] = std::move(node);
        return previous;
    }
};

} // namespace md::structure
