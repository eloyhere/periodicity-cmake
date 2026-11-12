// Periodicity — augmented binary node (structure).
//
// A BinaryNode plus exactly the three fields every ordered or balanced binary
// tree ends up adding anyway:
//
//   parent   a non-owning back link, so a node can report its depth, its
//            in-order position and the root of its tree without a search;
//   height   the height of the subtree rooted here (a leaf is 1, a missing
//            child is 0), maintained by refresh();
//   size     the number of nodes in that subtree, maintained by refresh().
//
// The "plus" is deliberate: keeping these fields on the node is what turns
// order-statistic queries (rank, select) and AVL balancing into O(height)
// operations that need no second traversal. Nodes without the need for them
// use BinaryNode and stay smaller.
//
// The invariant — every field equals the value recomputed from the children —
// is maintained by the owning tree, not by the node. A detached node, or a
// node whose children were edited by hand, becomes stale until refresh() is
// called on it and on its ancestors; BinaryPlusTree::refresh() does exactly
// that for a whole tree.

#pragma once

#include <cstddef>
#include <memory>
#include <utility>

namespace md::structure {

template <class T>
struct BinaryPlusNode
{
    using value_type = T;
    using node_type = BinaryPlusNode;
    using size_type = std::size_t;

    template <class... Args>
    explicit BinaryPlusNode(Args&&... args) : value(std::forward<Args>(args)...) {}

    BinaryPlusNode(const BinaryPlusNode&) = delete;
    BinaryPlusNode& operator=(const BinaryPlusNode&) = delete;
    BinaryPlusNode(BinaryPlusNode&&) = default;
    BinaryPlusNode& operator=(BinaryPlusNode&&) = default;

    T value;
    node_type* parent = nullptr;
    std::unique_ptr<node_type> left;
    std::unique_ptr<node_type> right;

    // Height of the subtree rooted here: 1 for a leaf, 0 for an empty tree.
    int height = 1;

    // Number of nodes in the subtree rooted here: 1 for a leaf.
    size_type size = 1;

    [[nodiscard]] static constexpr int heightOf(const node_type* node) noexcept
    {
        return node != nullptr ? node->height : 0;
    }

    [[nodiscard]] static constexpr size_type sizeOf(const node_type* node) noexcept
    {
        return node != nullptr ? node->size : 0;
    }

    // Recomputes height and size from the children. O(1); the caller is
    // responsible for refreshing ancestors afterwards.
    void refresh() noexcept
    {
        const int leftHeight = heightOf(left.get());
        const int rightHeight = heightOf(right.get());
        height = 1 + (leftHeight > rightHeight ? leftHeight : rightHeight);
        size = 1 + sizeOf(left.get()) + sizeOf(right.get());
    }

    // Positive when the right subtree is taller, which is the convention
    // BinaryPlusTree::rotateLeft/rotateRight are written against.
    [[nodiscard]] int balanceFactor() const noexcept
    {
        return heightOf(right.get()) - heightOf(left.get());
    }

    [[nodiscard]] bool isLeaf() const noexcept
    {
        return left == nullptr && right == nullptr;
    }

    [[nodiscard]] bool hasBothChildren() const noexcept
    {
        return left != nullptr && right != nullptr;
    }

    [[nodiscard]] node_type* child(bool rightSide) noexcept
    {
        return rightSide ? right.get() : left.get();
    }

    [[nodiscard]] const node_type* child(bool rightSide) const noexcept
    {
        return rightSide ? right.get() : left.get();
    }

    // Non-owning sibling of this node, or nullptr.
    [[nodiscard]] node_type* sibling() noexcept
    {
        if (parent == nullptr)
            return nullptr;
        return parent->left.get() == this ? parent->right.get() : parent->left.get();
    }
};

} // namespace md::structure
