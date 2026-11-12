// Periodicity — left-child/right-sibling node (structure).
//
// The classical representation of an unbounded n-ary tree with binary links:
// the first child is the node's "left child", the next younger sibling is its
// "right child". Every node therefore has exactly two outgoing links, which is
// what lets an arbitrary hierarchy be stored in a binary tree and walked with
// a binary-tree algorithm.
//
// The forest is the chain of roots, so a tree is a forest that happens to have
// one root — the two are the same data structure here, which is why
// SiblingForest can host both.
//
// Ownership follows the links: a node owns its first child and its next
// sibling. Removing a subtree therefore means unlinking one pointer in the
// parent's child chain (or in the forest's root chain), never a traversal.

#pragma once

#include <cstddef>
#include <memory>
#include <utility>

namespace md::structure {

template <class T>
struct SiblingNode
{
    using value_type = T;
    using node_type = SiblingNode;
    using size_type = std::size_t;

    template <class... Args>
    explicit SiblingNode(Args&&... args) : value(std::forward<Args>(args)...) {}

    SiblingNode(const SiblingNode&) = delete;
    SiblingNode& operator=(const SiblingNode&) = delete;
    SiblingNode(SiblingNode&&) = default;
    SiblingNode& operator=(SiblingNode&&) = default;

    T value;

    // Non-owning back link; null exactly for a root.
    node_type* parent = nullptr;

    // Owned links: first child and next younger sibling.
    std::unique_ptr<node_type> firstChild;
    std::unique_ptr<node_type> nextSibling;

    [[nodiscard]] bool isRoot() const noexcept { return parent == nullptr; }
    [[nodiscard]] bool isLeaf() const noexcept { return firstChild == nullptr; }

    // The next younger sibling, non-owning.
    [[nodiscard]] node_type* next() noexcept { return nextSibling.get(); }
    [[nodiscard]] const node_type* next() const noexcept { return nextSibling.get(); }

    // The eldest sibling of this node: walks up through parent links and then
    // back down to the parent's first child, so it is O(depth of the sibling
    // chain above this node) rather than O(1). Roots return themselves.
    [[nodiscard]] node_type* firstSibling() noexcept
    {
        node_type* node = this;
        while (node->parent != nullptr && node->parent->firstChild.get() != node)
            node = node->parent->firstChild.get();
        return node;
    }

    // Number of siblings including this node; O(siblings).
    [[nodiscard]] size_type siblingCount() const noexcept
    {
        size_type count = 0;
        for (const node_type* node = this; node != nullptr; node = node->nextSibling.get())
            ++count;
        return count;
    }

    // Number of children; O(children).
    [[nodiscard]] size_type childCount() const noexcept
    {
        size_type count = 0;
        for (const node_type* node = firstChild.get(); node != nullptr;
             node = node->nextSibling.get())
            ++count;
        return count;
    }

    // The child at `index`, or nullptr past the last child.
    [[nodiscard]] node_type* childAt(size_type index) noexcept
    {
        node_type* node = firstChild.get();
        while (node != nullptr && index > 0) {
            node = node->nextSibling.get();
            --index;
        }
        return node;
    }

    [[nodiscard]] const node_type* childAt(size_type index) const noexcept
    {
        const node_type* node = firstChild.get();
        while (node != nullptr && index > 0) {
            node = node->nextSibling.get();
            --index;
        }
        return node;
    }

    // The youngest child, or nullptr. Walks the whole child chain.
    [[nodiscard]] node_type* lastChild() noexcept
    {
        node_type* node = firstChild.get();
        if (node == nullptr)
            return nullptr;
        while (node->nextSibling != nullptr)
            node = node->nextSibling.get();
        return node;
    }
};

} // namespace md::structure
