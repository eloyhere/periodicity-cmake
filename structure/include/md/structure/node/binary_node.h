// Periodicity — binary node (structure).
//
// The node of an unordered binary tree: a value and at most two owned
// children. "Left" and "right" are structural names, not ordering guarantees —
// a BinaryNode says nothing about where a value may be stored. Ordering is the
// business of BinarySearchTree, and bookkeeping (parent, height, size) is the
// business of BinaryPlusNode; keeping those out of this node is what lets a
// plain binary tree stay two pointers wide.
//
// The children are owned through std::unique_ptr, so a node frees its whole
// subtree when it is destroyed, and re-linking a subtree is a pointer move
// rather than a traversal.

#pragma once

#include <memory>
#include <utility>

namespace md::structure {

template <class T>
struct BinaryNode
{
    using value_type = T;
    using node_type = BinaryNode;

    template <class... Args>
    explicit BinaryNode(Args&&... args) : value(std::forward<Args>(args)...) {}

    BinaryNode(const BinaryNode&) = delete;
    BinaryNode& operator=(const BinaryNode&) = delete;
    BinaryNode(BinaryNode&&) = default;
    BinaryNode& operator=(BinaryNode&&) = default;

    T value;
    std::unique_ptr<node_type> left;
    std::unique_ptr<node_type> right;

    [[nodiscard]] bool isLeaf() const noexcept
    {
        return left == nullptr && right == nullptr;
    }

    // True for a node with exactly one child, which is the shape a complete
    // tree may have at most once and a full tree never has.
    [[nodiscard]] bool hasOneChild() const noexcept
    {
        return (left == nullptr) != (right == nullptr);
    }

    [[nodiscard]] bool hasBothChildren() const noexcept
    {
        return left != nullptr && right != nullptr;
    }

    // The child on `right` side, or nullptr. Returning the owning pointer
    // would invite callers to steal a subtree by accident, so the accessor is
    // non-owning; ownership is transferred only through the owning tree.
    [[nodiscard]] node_type* child(bool rightSide) noexcept
    {
        return rightSide ? right.get() : left.get();
    }

    [[nodiscard]] const node_type* child(bool rightSide) const noexcept
    {
        return rightSide ? right.get() : left.get();
    }
};

} // namespace md::structure
