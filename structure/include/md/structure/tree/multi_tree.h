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
//
// Every traversal, measurement, copy and teardown is iterative, for the same
// reason as in the binary trees: a chain of single-child nodes is a legal
// shape of any depth, so no operation may recurse once per level.

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

    // Clears the current contents through the iterative teardown rather than
    // letting the defaulted move assignment release the old root recursively.
    MultiTree& operator=(MultiTree&& other) noexcept
    {
        if (this != &other) {
            clear();
            root_ = std::move(other.root_);
        }
        return *this;
    }

    ~MultiTree() { clear(); }

    [[nodiscard]] bool empty() const noexcept { return root_ == nullptr; }
    [[nodiscard]] size_type size() const noexcept { return sizeOf(root_.get()); }
    [[nodiscard]] int height() const noexcept { return heightOf(root_.get()); }

    [[nodiscard]] node_type* root() noexcept { return root_.get(); }
    [[nodiscard]] const node_type* root() const noexcept { return root_.get(); }

    void setRoot(std::unique_ptr<node_type> root) noexcept
    {
        clear();
        root_ = std::move(root);
    }

    [[nodiscard]] std::unique_ptr<node_type> releaseRoot() noexcept { return std::move(root_); }

    // Removes every node. Children are moved onto an explicit pending stack
    // before their parent is deleted, so neither a deep hierarchy nor a wide
    // one recurses: O(n) time and O(nodes still pending) extra space.
    void clear() noexcept
    {
        std::vector<std::unique_ptr<node_type>> pending;
        if (root_ != nullptr)
            pending.push_back(std::move(root_));
        while (!pending.empty()) {
            std::unique_ptr<node_type> node = std::move(pending.back());
            pending.pop_back();
            for (size_type index = 0; index < node->childCount; ++index)
                pending.push_back(std::move(node->children[index]));
            // `node` no longer owns a child, so its destructor is O(1).
        }
    }

    node_type& insertRoot(T value)
    {
        assert(root_ == nullptr && "insertRoot requires an empty tree");
        root_ = std::make_unique<node_type>(std::move(value));
        return *root_;
    }

    // Appends a child to `parent` and returns it, or nullptr when `parent` is
    // null or already at capacity. A full parent is a recoverable failure: the
    // value is destroyed and the tree is unchanged, so the result must be
    // checked before it is dereferenced.
    node_type* appendChild(node_type* parent, T value)
    {
        assert(parent != nullptr && "appendChild needs a parent");
        if (parent == nullptr || parent->isFull())
            return nullptr;

        auto child = std::make_unique<node_type>(std::move(value));
        node_type* raw = child.get();
        const size_type index = parent->appendChild(std::move(child));
        return index < N ? raw : nullptr;
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
        size_type count = 0;
        std::vector<const node_type*> pending;
        if (node != nullptr)
            pending.push_back(node);
        while (!pending.empty()) {
            const node_type* current = pending.back();
            pending.pop_back();
            ++count;
            for (size_type index = 0; index < current->childCount; ++index)
                pending.push_back(current->children[index].get());
        }
        return count;
    }

    [[nodiscard]] static int heightOf(const node_type* node) noexcept
    {
        int tallest = 0;
        std::vector<std::pair<const node_type*, int>> pending;
        if (node != nullptr)
            pending.emplace_back(node, 1);
        while (!pending.empty()) {
            const std::pair<const node_type*, int> current = pending.back();
            pending.pop_back();
            if (current.second > tallest)
                tallest = current.second;
            for (size_type index = 0; index < current.first->childCount; ++index)
                pending.emplace_back(current.first->children[index].get(),
                                     current.second + 1);
        }
        return tallest;
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
        std::vector<std::pair<const node_type*, node_type*>> pending;
        pending.emplace_back(node, copy.get());
        while (!pending.empty()) {
            const std::pair<const node_type*, node_type*> current = pending.back();
            pending.pop_back();
            const node_type* source = current.first;
            node_type* target = current.second;
            target->childCount = source->childCount;
            for (size_type index = 0; index < source->childCount; ++index) {
                target->children[index] =
                    std::make_unique<node_type>(source->children[index]->value);
                pending.emplace_back(source->children[index].get(),
                                     target->children[index].get());
            }
        }
        return copy;
    }

    template <class Function>
    static void preorder(const node_type* node, Function& function)
    {
        std::vector<const node_type*> pending;
        if (node != nullptr)
            pending.push_back(node);
        while (!pending.empty()) {
            const node_type* current = pending.back();
            pending.pop_back();
            function(current->value);
            // Push in reverse so the first child is popped (and visited) first.
            for (size_type index = current->childCount; index > 0; --index)
                pending.push_back(current->children[index - 1].get());
        }
    }

    template <class Function>
    static void preorderNodes(const node_type* node, int depth, Function& function)
    {
        std::vector<std::pair<const node_type*, int>> pending;
        if (node != nullptr)
            pending.emplace_back(node, depth);
        while (!pending.empty()) {
            const std::pair<const node_type*, int> current = pending.back();
            pending.pop_back();
            function(*current.first, current.second);
            for (size_type index = current.first->childCount; index > 0; --index)
                pending.emplace_back(current.first->children[index - 1].get(),
                                     current.second + 1);
        }
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
                pending.push(current->children[index].get());
        }
    }

    static size_type countLeaves(const node_type* node) noexcept
    {
        size_type leaves = 0;
        std::vector<const node_type*> pending;
        if (node != nullptr)
            pending.push_back(node);
        while (!pending.empty()) {
            const node_type* current = pending.back();
            pending.pop_back();
            if (current->childCount == 0) {
                ++leaves;
                continue;
            }
            for (size_type index = 0; index < current->childCount; ++index)
                pending.push_back(current->children[index].get());
        }
        return leaves;
    }

    static bool isSaturatedNode(const node_type* node) noexcept
    {
        std::vector<const node_type*> pending;
        if (node != nullptr)
            pending.push_back(node);
        while (!pending.empty()) {
            const node_type* current = pending.back();
            pending.pop_back();
            if (current->childCount != 0 && current->childCount != N)
                return false;
            for (size_type index = 0; index < current->childCount; ++index)
                pending.push_back(current->children[index].get());
        }
        return true;
    }

    static bool equalsNode(const node_type* lhs, const node_type* rhs) noexcept
    {
        std::vector<std::pair<const node_type*, const node_type*>> pending;
        pending.emplace_back(lhs, rhs);
        while (!pending.empty()) {
            const std::pair<const node_type*, const node_type*> current = pending.back();
            pending.pop_back();
            const node_type* left = current.first;
            const node_type* right = current.second;
            if (left == nullptr || right == nullptr) {
                if (left != right)
                    return false;
                continue;
            }
            if (!(left->value == right->value) || left->childCount != right->childCount)
                return false;
            for (size_type index = 0; index < left->childCount; ++index)
                pending.emplace_back(left->children[index].get(),
                                     right->children[index].get());
        }
        return true;
    }

    std::unique_ptr<node_type> root_;
};

} // namespace md::structure
