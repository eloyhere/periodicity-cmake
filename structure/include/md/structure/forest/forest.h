// Periodicity — forest of binary trees (structure).
//
// A forest is a set of trees. This one owns binary trees and treats them as
// peers: there is no virtual root, no linking between them, and removing one
// does not disturb the others — which is exactly what a forest is for.
//
// The motivating case is deletion in a tree: removing an internal node splits
// its tree into the node's subtrees, so "the result of a delete" is naturally a
// forest rather than a tree. Aggregates (size, height) are over the whole
// forest; the per-tree API stays available through tree(index).
//
// For a forest that shares one node chain — the left-child/right-sibling
// representation, where the root list is itself the forest — see
// SiblingForest, which is the more general n-ary structure.

#pragma once

#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

#include <md/structure/tree/binary_tree.h>

namespace md::structure {

template <class T>
class Forest
{
public:
    using tree_type = BinaryTree<T>;
    using node_type = BinaryNode<T>;
    using size_type = std::size_t;

    Forest() = default;

    // ---- Trees ------------------------------------------------------------

    // Appends a tree, taking ownership of its nodes.
    void addTree(tree_type tree) { trees_.push_back(std::move(tree)); }

    // Removes the tree at `index`; returns whether one was removed.
    bool removeTree(size_type index)
    {
        if (index >= trees_.size())
            return false;
        trees_.erase(trees_.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    [[nodiscard]] size_type treeCount() const noexcept { return trees_.size(); }
    [[nodiscard]] bool empty() const noexcept { return trees_.empty(); }

    // The tree at `index`; precondition: index < treeCount(). Out-of-range
    // access is undefined (the index addresses a vector slot), so a debug build
    // asserts instead — use removeTree(), which reports an out-of-range index
    // by returning false, when the index is not known to be valid.
    [[nodiscard]] tree_type& tree(size_type index)
    {
        assert(index < trees_.size() && "tree index out of range");
        return trees_[index];
    }

    [[nodiscard]] const tree_type& tree(size_type index) const
    {
        assert(index < trees_.size() && "tree index out of range");
        return trees_[index];
    }

    void clear() noexcept { trees_.clear(); }

    // ---- Aggregates -------------------------------------------------------

    // Total number of nodes in every tree; O(total).
    [[nodiscard]] size_type size() const
    {
        size_type total = 0;
        for (const tree_type& tree : trees_)
            total += tree.size();
        return total;
    }

    // Height of the tallest tree; 0 for an empty forest.
    [[nodiscard]] int height() const
    {
        int tallest = 0;
        for (const tree_type& tree : trees_) {
            const int treeHeight = tree.height();
            if (treeHeight > tallest)
                tallest = treeHeight;
        }
        return tallest;
    }

    // ---- Traversal --------------------------------------------------------

    template <class Function>
    void forEachTree(Function&& function) const
    {
        for (const tree_type& tree : trees_)
            function(tree);
    }

    template <class Function>
    void forEachTree(Function&& function)
    {
        for (tree_type& tree : trees_)
            function(tree);
    }

    // Visits every value in the forest: tree order, then preorder within each
    // tree.
    template <class Function>
    void forEachPreorder(Function&& function) const
    {
        for (const tree_type& tree : trees_)
            tree.forEachPreorder(function);
    }

private:
    std::vector<tree_type> trees_;
};

} // namespace md::structure
