// Periodicity — linear node (structure).
//
// A linear structure is a chain: each node carries one value and one
// successor. Two properties make this the cheapest node in the catalogue and
// the one every singly linked structure shares:
//
//   * one outgoing link, so the node is one pointer plus its payload;
//   * the successor is owned, so a chain frees itself when its head goes away.
//
// Structures that must walk backwards use parent pointers (BinaryPlusNode) or
// an explicit predecessor link rather than widening this node.

#pragma once

#include <cstddef>
#include <memory>
#include <utility>

namespace md::structure {

template <class T>
struct LinearNode
{
    using value_type = T;
    using node_type = LinearNode;

    // Constructs the payload in place; the node then owns its successor.
    template <class... Args>
    explicit LinearNode(Args&&... args) : value(std::forward<Args>(args)...) {}

    LinearNode(const LinearNode&) = delete;
    LinearNode& operator=(const LinearNode&) = delete;
    LinearNode(LinearNode&&) = default;
    LinearNode& operator=(LinearNode&&) = default;

    T value;
    std::unique_ptr<node_type> next;

    [[nodiscard]] bool hasNext() const noexcept { return next != nullptr; }

    // Length of the chain that starts at this node; O(n) and iterative, so a
    // very long list cannot exhaust the stack the way a recursive count can.
    [[nodiscard]] std::size_t length() const noexcept
    {
        std::size_t count = 0;
        for (const node_type* node = this; node != nullptr; node = node->next.get())
            ++count;
        return count;
    }
};

} // namespace md::structure
