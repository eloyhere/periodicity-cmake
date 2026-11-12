// Periodicity — fixed-arity n-ary tree (structure).
//
// A tree whose nodes have a fixed child capacity: MultiNode<T, N>. It is the
// generalisation of the binary tree in the direction of arity rather than of
// order — ternary trees, quadtrees and octrees all have this shape.
//
//   MultiTree<T, 2>   a binary tree with an indexable child array
//   MultiTree<T, 3>   a ternary tree
//   MultiTree<T, 4>   a quadtree
//
// Children are kept packed at the front of the node's array, so childCount is
// the number of real children and a traversal never skips holes. Appending is
// O(1); removing a child shifts the later ones down, which is O(N) — for large
// N a child list would be the better structure, but a fixed arity is precisely
// the case where the array wins.
//
// There is no ordering rule and no balance rule here; the tree contributes
// shape, traversal and the arity questions (height, fullness). Ordering would
// be the business of a search variant built on the same node.

#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

#include <md/structure/node/multi_node.h>

namespace md::structure {

template <class T, std::size_t N>
class MultiTree
{
public:
    using value_type = T;
    using node_type = MultiNode<T, N>;
    using size_type = std::size_t;

    static constexpr size_type arity = N;

    MultiTree() noexcept = default;

    MultiTree(const MultiTree& other) : root_(cloneSubtree(other.root_.get())) {}

    MultiTree& operator=(const MultiTree& other)
    {
        if (this != &other) {
            MultiTree copy(other);
            root_.swap(copy.root_);
        }
        return *this;
    }

    MultiTree(MultiTree&&) noexcept = default;
    MultiTree& operator=(MultiTree&&) noexcept = default;
    ~MultiTree() = default;

    [[nodiscard]] bool empty() const noexcept { return root_ == nullptr; }
    [[nodiscard]] size_type size() const noexcept { return sizeOf(root_.get()); }
    [[nodiscard]] int height() const noexcept { return heightOf(root_.get()); }

    [[nodiscard]] node_type* root() noexcept { return root_.get(); }
    [[nodiscard]] const node_type* root() const noexcept { return root_.get(); }

    void setRoot(std::unique_ptr<node_type> root) noexcept { root_ = std::move(root); }
    [[nodiscard]] std::unique_ptr<node_type> releaseRoot() noexcept { return std::move(root_); }
    void clear() noexcept { root_.reset(); }

    node_type& insertRoot(T value)
    {
        assert(root_ == nullptr && "insertRoot requires an empty tree");
        root_ = std::make_unique<node_type>(std::move(value));
        return *root_;
    }

    // Appends a child to `parent`. Precondition: the parent is not full.
    node_type& appendChild(node_type* parent, T value)
    {
        assert(parent != nullptr && "appendChild needs a parent");
        assert(!parent->isFull() && "parent has no free child slot");
        auto child = std::make_unique<node_type>(std::move(value));
        node_type* raw = child.get();
        parent->appendChild(std::move(child));
        return *raw;
    }

    // Removes the child at `index`, shifting the later children down.
    std::unique_ptr<node_type> removeChild(node_type* parent, size_type index)
    {
        assert(parent != nullptr && "removeChild needs a parent");
        return parent->removeChild(index);
    }

    // ---- Measurements -----------------------------------------------------

    [[nodiscard]] static size_type sizeOf(const node_type* node) noexcept
    {
        if (node == nullptr)
            return 0;
        size_type count = 1;
        for (size_type index = 0; index < node->childCount; ++index)
            count += sizeOf(node->child(index));
        return count;
    }

    [[nodiscard]] static int heightOf(const node_type* node) noexcept
    {
        if (node == nullptr)
            return 0;
        int tallest = 0;
        for (size_type index = 0; index < node->childCount; ++index) {
            const int childHeight = heightOf(node->child(index));
            if (childHeight > tallest)
                tallest = childHeight;
        }
        return 1 + tallest;
    }

    // ---- Traversal --------------------------------------------------------

    template <class Function>
    void forEachPreorder(Function&& function) const
    {
        preorder(root_.get(), function);
    }

    template <class Function>
    void forEachLevelOrder(Function&& function) const
    {
        levelOrder(root_.get(), function);
    }

    // Node visitor receives (const node_type&, depth).
    template <class Function>
    void forEachNodePreorder(Function&& function) const
    {
        preorderNodes(root_.get(), 0, function);
    }

    // ---- Queries ----------------------------------------------------------

    [[nodiscard]] size_type countLeaves() const noexcept { return countLeaves(root_.get()); }

    // True when every internal node uses its full arity. A ternary tree of one
    // node is saturated; a ternary tree with a node holding two children is
    // not.
    [[nodiscard]] bool isSaturated() const noexcept { return isSaturatedNode(root_.get()); }

    [[nodiscard]] bool equals(const MultiTree& other) const noexcept
    {
        return equalsNode(root_.get(), other.root_.get());
    }

    [[nodiscard]] std::vector<T> toPreorder() const
    {
        std::vector<T> values;
        forEachPreorder([&values](const T& value) { values.push_back(value); });
        return values;
    }

private:
    static std::unique_ptr<node_type> cloneSubtree(const node_type* node)
    {
        if (node == nullptr)
            return nullptr;
        auto copy = std::make_unique<node_type>(node->value);
        for (size_type index = 0; index < node->childCount; ++index) {
            std::unique_ptr<node_type> child = cloneSubtree(node->child(index));
            copy->children[index] = std::move(child);
        }
        copy->childCount = node->childCount;
        return copy;
    }

    template <class Function>
    static void preorder(const node_type* node, Function& function)
    {
        if (node == nullptr)
            return;
        function(node->value);
        for (size_type index = 0; index < node->childCount; ++index)
            preorder(node->child(index), function);
    }

    template <class Function>
    static void preorderNodes(const node_type* node, int depth, Function& function)
    {
        if (node == nullptr)
            return;
        function(*node, depth);
        for (size_type index = 0; index < node->childCount; ++index)
            preorderNodes(node->child(index), depth + 1, function);
    }

    template <class Function>
    static void levelOrder(const node_type* node, Function& function)
    {
        if (node == nullptr)
            return;
        std::queue<const node_type*> pending;
        pending.push(node);
        while (!pending.empty()) {
            const node_type* current = pending.front();
            pending.pop();
            function(current->value);
            for (size_type index = 0; index < current->childCount; ++index)
                pending.push(current->child(index));
        }
    }

    static size_type countLeaves(const node_type* node) noexcept
    {
        if (node == nullptr)
            return 0;
        if (node->childCount == 0)
            return 1;
        size_type count = 0;
        for (size_type index = 0; index < node->childCount; ++index)
            count += countLeaves(node->child(index));
        return count;
    }

    static bool isSaturatedNode(const node_type* node) noexcept
    {
        if (node == nullptr)
            return true;
        if (node->childCount != 0 && node->childCount != N)
            return false;
        for (size_type index = 0; index < node->childCount; ++index) {
            if (!isSaturatedNode(node->child(index)))
                return false;
        }
        return true;
    }

    static bool equalsNode(const node_type* lhs, const node_type* rhs) noexcept
    {
        if (lhs == nullptr || rhs == nullptr)
            return lhs == rhs;
        if (!(lhs->value == rhs->value) || lhs->childCount != rhs->childCount)
            return false;
        for (size_type index = 0; index < lhs->childCount; ++index) {
            if (!equalsNode(lhs->child(index), rhs->child(index)))
                return false;
        }
        return true;
    }

    std::unique_ptr<node_type> root_;
};

} // namespace md::structure
