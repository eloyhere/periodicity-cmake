// Periodicity — augmented binary tree (structure).
//
// BinaryPlusTree is BinaryTree plus the three fields BinaryPlusNode carries
// (parent, height, size) and the operations those fields make cheap. It is the
// substrate the ordered trees are built on:
//
//     BinaryTree       shape only;  size() and height() are O(n)
//     BinaryPlusTree   shape + bookkeeping;  size() and height() are O(1),
//                      depth(), rank and select are O(height)
//     BinarySearchTree ordering on top of the bookkeeping
//     AvlTree          balance on top of the ordering
//
// Two invariants hold after every public operation, and a debug build asserts
// them where a check is affordable:
//
//   parent   child->parent points back at its owner, root->parent is null
//   metrics  node->height and node->size equal what refresh() would compute
//
// Positional queries are defined against the traversal they name, so there is
// no ambiguity about what "index 3" means:
//
//   inorderIndex(node)    index in the inorder reading   (sorted order in a
//                         binary search tree, which is what order statistics
//                         are usually about)
//   preorderIndex(node)   index in the preorder reading  (useful for stable
//                         identifiers and for layout serialisation)
//
// Both are O(height), because a node knows its subtree sizes and its parent
// chain but nothing else.

#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

#include <md/structure/node/binary_plus_node.h>

namespace md::structure {

template <class T>
class BinaryPlusTree
{
public:
    using value_type = T;
    using node_type = BinaryPlusNode<T>;
    using size_type = std::size_t;

    BinaryPlusTree() noexcept = default;

    BinaryPlusTree(const BinaryPlusTree& other)
        : root_(cloneSubtree(other.root_.get(), nullptr))
    {
    }

    BinaryPlusTree& operator=(const BinaryPlusTree& other)
    {
        if (this != &other) {
            BinaryPlusTree copy(other);
            root_.swap(copy.root_);
        }
        return *this;
    }

    BinaryPlusTree(BinaryPlusTree&&) noexcept = default;
    BinaryPlusTree& operator=(BinaryPlusTree&&) noexcept = default;
    ~BinaryPlusTree() = default;

    void swap(BinaryPlusTree& other) noexcept { root_.swap(other.root_); }

    // ---- Capacity (all O(1)) ---------------------------------------------

    [[nodiscard]] bool empty() const noexcept { return root_ == nullptr; }
    [[nodiscard]] size_type size() const noexcept { return node_type::sizeOf(root_.get()); }
    [[nodiscard]] int height() const noexcept { return node_type::heightOf(root_.get()); }

    // ---- Structure --------------------------------------------------------

    [[nodiscard]] node_type* root() noexcept { return root_.get(); }
    [[nodiscard]] const node_type* root() const noexcept { return root_.get(); }

    // Adopts `root` and rebuilds every derived field, so a subtree assembled
    // by hand can be handed over safely.
    void setRoot(std::unique_ptr<node_type> root)
    {
        root_ = std::move(root);
        refresh();
    }

    [[nodiscard]] std::unique_ptr<node_type> releaseRoot() noexcept
    {
        return std::move(root_);
    }

    void clear() noexcept { root_.reset(); }

    node_type& insertRoot(T value)
    {
        assert(root_ == nullptr && "insertRoot requires an empty tree");
        root_ = std::make_unique<node_type>(std::move(value));
        return *root_;
    }

    node_type& insertLeft(node_type* parent, T value)
    {
        assert(parent != nullptr && "insertLeft needs a parent");
        assert(parent->left == nullptr && "left slot is occupied");
        parent->left = std::make_unique<node_type>(std::move(value));
        parent->left->parent = parent;
        updateToRoot(parent);
        return *parent->left;
    }

    node_type& insertRight(node_type* parent, T value)
    {
        assert(parent != nullptr && "insertRight needs a parent");
        assert(parent->right == nullptr && "right slot is occupied");
        parent->right = std::make_unique<node_type>(std::move(value));
        parent->right->parent = parent;
        updateToRoot(parent);
        return *parent->right;
    }

    // Replaces the child on `rightSide` and returns the previous occupant.
    std::unique_ptr<node_type> setChild(node_type* parent, bool rightSide,
                                        std::unique_ptr<node_type> child)
    {
        assert(parent != nullptr && "setChild needs a parent");
        std::unique_ptr<node_type>& slot = rightSide ? parent->right : parent->left;
        std::unique_ptr<node_type> previous = std::move(slot);
        if (previous != nullptr)
            previous->parent = nullptr;
        slot = std::move(child);
        if (slot != nullptr)
            slot->parent = parent;
        updateToRoot(parent);
        return previous;
    }

    // Detaches the child on `rightSide` and returns it, leaving the node's
    // derived fields correct.
    std::unique_ptr<node_type> takeChild(node_type* parent, bool rightSide)
    {
        assert(parent != nullptr && "takeChild needs a parent");
        std::unique_ptr<node_type>& slot = rightSide ? parent->right : parent->left;
        std::unique_ptr<node_type> taken = std::move(slot);
        if (taken != nullptr)
            taken->parent = nullptr;
        updateToRoot(parent);
        return taken;
    }

    // ---- Derived-field maintenance ---------------------------------------

    // Recomputes parent links, heights and sizes for the whole tree, bottom up.
    void refresh() noexcept { refreshSubtree(root_.get(), nullptr); }

    // Recomputes `node` and every ancestor. Call after changing a subtree's
    // shape through raw node access.
    void updateToRoot(node_type* node) noexcept
    {
        for (node_type* current = node; current != nullptr; current = current->parent)
            current->refresh();
    }

    // Distance from the root; the root itself is 0.
    [[nodiscard]] int depth(const node_type* node) const noexcept
    {
        int depth = 0;
        for (const node_type* current = node; current->parent != nullptr;
             current = current->parent)
            ++depth;
        return depth;
    }

    // ---- Positional queries ----------------------------------------------

    // Index of `node` in the inorder reading; O(height).
    [[nodiscard]] size_type inorderIndex(const node_type* node) const noexcept
    {
        size_type index = node_type::sizeOf(node->left.get());
        const node_type* child = node;
        for (const node_type* parent = node->parent; parent != nullptr;
             child = parent, parent = parent->parent) {
            if (parent->right.get() == child)
                index += node_type::sizeOf(parent->left.get()) + 1;
        }
        return index;
    }

    // The node at inorder position `index`, or nullptr when out of range.
    [[nodiscard]] node_type* selectInorder(size_type index) const noexcept
    {
        node_type* current = root_.get();
        while (current != nullptr) {
            const size_type leftSize = node_type::sizeOf(current->left.get());
            if (index < leftSize) {
                current = current->left.get();
            } else if (index == leftSize) {
                return current;
            } else {
                index -= leftSize + 1;
                current = current->right.get();
            }
        }
        return nullptr;
    }

    // Index of `node` in the preorder reading; O(height).
    [[nodiscard]] size_type preorderIndex(const node_type* node) const noexcept
    {
        size_type index = 0;
        const node_type* child = node;
        for (const node_type* parent = node->parent; parent != nullptr;
             child = parent, parent = parent->parent) {
            index += parent->right.get() == child
                         ? node_type::sizeOf(parent->left.get()) + 1
                         : 1;
        }
        return index;
    }

    // The node at preorder position `index`, or nullptr when out of range.
    [[nodiscard]] node_type* selectPreorder(size_type index) const noexcept
    {
        node_type* current = root_.get();
        while (current != nullptr) {
            if (index == 0)
                return current;
            const size_type leftSize = node_type::sizeOf(current->left.get());
            if (index <= leftSize) {
                current = current->left.get();
                --index;
            } else {
                index -= leftSize + 1;
                current = current->right.get();
            }
        }
        return nullptr;
    }

    // ---- Traversal --------------------------------------------------------

    template <class Function>
    void forEachPreorder(Function&& function) const
    {
        preorder(root_.get(), function);
    }

    template <class Function>
    void forEachInorder(Function&& function) const
    {
        inorder(root_.get(), function);
    }

    template <class Function>
    void forEachPostorder(Function&& function) const
    {
        postorder(root_.get(), function);
    }

    template <class Function>
    void forEachLevelOrder(Function&& function) const
    {
        levelOrder(root_.get(), function);
    }

    // Node-level visitors receive (const node_type&, depth).
    template <class Function>
    void forEachNodePreorder(Function&& function) const
    {
        preorderNodes(root_.get(), 0, function);
    }

    template <class Function>
    void forEachNodeInorder(Function&& function) const
    {
        inorderNodes(root_.get(), 0, function);
    }

    template <class Function>
    void forEachNodePostorder(Function&& function) const
    {
        postorderNodes(root_.get(), 0, function);
    }

    template <class Function>
    void forEachNodeLevelOrder(Function&& function) const
    {
        levelOrderNodes(root_.get(), function);
    }

    // ---- Queries ----------------------------------------------------------

    [[nodiscard]] size_type countLeaves() const noexcept { return countLeaves(root_.get()); }
    [[nodiscard]] size_type countNodesWithOneChild() const noexcept
    {
        return countNodesWithOneChild(root_.get());
    }

    template <class Predicate>
    [[nodiscard]] const node_type* findIf(Predicate&& predicate) const noexcept
    {
        return findIfNode(root_.get(), predicate);
    }

    template <class Predicate>
    [[nodiscard]] size_type countIf(Predicate&& predicate) const noexcept
    {
        return countIfNode(root_.get(), predicate);
    }

    [[nodiscard]] bool isFull() const noexcept { return isFullNode(root_.get()); }

    // AVL balance criterion, read from the cached heights, so this is O(n)
    // rather than O(n log n).
    [[nodiscard]] bool isBalanced() const noexcept { return isBalancedNode(root_.get()); }

    [[nodiscard]] int diameter() const noexcept
    {
        int best = 0;
        (void)diameterOf(root_.get(), best);
        return best;
    }

    void mirror() noexcept { mirrorNode(root_.get()); }

    [[nodiscard]] bool equals(const BinaryPlusTree& other) const noexcept
    {
        return equalsNode(root_.get(), other.root_.get());
    }

    [[nodiscard]] friend bool operator==(const BinaryPlusTree& lhs,
                                         const BinaryPlusTree& rhs) noexcept
    {
        return lhs.equals(rhs);
    }

    [[nodiscard]] friend bool operator!=(const BinaryPlusTree& lhs,
                                         const BinaryPlusTree& rhs) noexcept
    {
        return !lhs.equals(rhs);
    }

    // ---- Builders and exports --------------------------------------------

    [[nodiscard]] static BinaryPlusTree fromLevelOrder(const std::vector<T>& values)
    {
        BinaryPlusTree tree;
        tree.root_ = buildComplete(values, 0, nullptr);
        return tree;
    }

    [[nodiscard]] std::vector<T> toLevelOrder() const
    {
        std::vector<T> values;
        forEachLevelOrder([&values](const T& value) { values.push_back(value); });
        return values;
    }

    [[nodiscard]] std::vector<T> toPreorder() const
    {
        std::vector<T> values;
        forEachPreorder([&values](const T& value) { values.push_back(value); });
        return values;
    }

    [[nodiscard]] std::vector<T> toInorder() const
    {
        std::vector<T> values;
        forEachInorder([&values](const T& value) { values.push_back(value); });
        return values;
    }

    [[nodiscard]] std::vector<T> toPostorder() const
    {
        std::vector<T> values;
        forEachPostorder([&values](const T& value) { values.push_back(value); });
        return values;
    }

protected:
    // The owning slot that holds `node`: its parent's left or right pointer,
    // or the tree's root pointer. Rotations and removals are expressed by
    // moving through this slot.
    [[nodiscard]] std::unique_ptr<node_type>& slotOf(node_type* node) noexcept
    {
        assert(node != nullptr && "slotOf needs a node");
        if (node->parent == nullptr)
            return root_;
        return node->parent->left.get() == node ? node->parent->left : node->parent->right;
    }

    // Unlinks `node` from its parent and returns ownership. The node keeps its
    // children; the caller relinks them and then calls updateToRoot().
    [[nodiscard]] std::unique_ptr<node_type> unlink(node_type* node) noexcept
    {
        std::unique_ptr<node_type>& slot = slotOf(node);
        std::unique_ptr<node_type> owned = std::move(slot);
        owned->parent = nullptr;
        return owned;
    }

    // Replaces `node` in its parent's slot with `replacement`, keeping parent
    // links and refreshing from the old parent upwards. `node` itself must
    // have been released by the caller.
    void replaceNode(node_type* node, std::unique_ptr<node_type> replacement) noexcept
    {
        node_type* parent = node->parent;
        std::unique_ptr<node_type>& slot = slotOf(node);
        slot = std::move(replacement);
        if (slot != nullptr)
            slot->parent = parent;
        if (parent != nullptr)
            updateToRoot(parent);
    }

    // Rotates `node` towards the right subtree: its right child takes its
    // place and `node` becomes that child's left subtree. The tree's shape
    // changes; heights and sizes are refreshed on both nodes, but not above,
    // because the subtree's height may have changed for the parent to see —
    // callers on a root-to-leaf path therefore refresh upwards themselves.
    void rotateLeft(node_type* node) noexcept
    {
        node_type* pivot = node->right.get();
        assert(pivot != nullptr && "rotateLeft needs a right child");

        node_type* parent = node->parent;
        std::unique_ptr<node_type>& slot = slotOf(node);

        std::unique_ptr<node_type> pivotOwned = std::move(node->right);
        node->right = std::move(pivotOwned->left);
        if (node->right != nullptr)
            node->right->parent = node;

        pivotOwned->left = std::move(slot);
        pivotOwned->left->parent = pivotOwned.get();

        pivotOwned->parent = parent;
        slot = std::move(pivotOwned);

        node->refresh();
        slot.get()->refresh();
    }

    // Mirror image of rotateLeft.
    void rotateRight(node_type* node) noexcept
    {
        node_type* pivot = node->left.get();
        assert(pivot != nullptr && "rotateRight needs a left child");

        node_type* parent = node->parent;
        std::unique_ptr<node_type>& slot = slotOf(node);

        std::unique_ptr<node_type> pivotOwned = std::move(node->left);
        node->left = std::move(pivotOwned->right);
        if (node->left != nullptr)
            node->left->parent = node;

        pivotOwned->right = std::move(slot);
        pivotOwned->right->parent = pivotOwned.get();

        pivotOwned->parent = parent;
        slot = std::move(pivotOwned);

        node->refresh();
        slot.get()->refresh();
    }

private:
    static void refreshSubtree(node_type* node, node_type* parent) noexcept
    {
        if (node == nullptr)
            return;
        node->parent = parent;
        refreshSubtree(node->left.get(), node);
        refreshSubtree(node->right.get(), node);
        node->refresh();
    }

    static std::unique_ptr<node_type> cloneSubtree(const node_type* node, node_type* parent)
    {
        if (node == nullptr)
            return nullptr;
        auto copy = std::make_unique<node_type>(node->value);
        copy->parent = parent;
        copy->left = cloneSubtree(node->left.get(), copy.get());
        copy->right = cloneSubtree(node->right.get(), copy.get());
        copy->height = node->height;
        copy->size = node->size;
        return copy;
    }

    static std::unique_ptr<node_type> buildComplete(const std::vector<T>& values,
                                                    size_type index, node_type* parent)
    {
        if (index >= values.size())
            return nullptr;
        auto node = std::make_unique<node_type>(values[index]);
        node->parent = parent;
        node->left = buildComplete(values, index * 2 + 1, node.get());
        node->right = buildComplete(values, index * 2 + 2, node.get());
        node->refresh();
        return node;
    }

    template <class Function>
    static void preorder(const node_type* node, Function& function)
    {
        if (node == nullptr)
            return;
        function(node->value);
        preorder(node->left.get(), function);
        preorder(node->right.get(), function);
    }

    template <class Function>
    static void inorder(const node_type* node, Function& function)
    {
        if (node == nullptr)
            return;
        inorder(node->left.get(), function);
        function(node->value);
        inorder(node->right.get(), function);
    }

    template <class Function>
    static void postorder(const node_type* node, Function& function)
    {
        if (node == nullptr)
            return;
        postorder(node->left.get(), function);
        postorder(node->right.get(), function);
        function(node->value);
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
            if (current->left != nullptr)
                pending.push(current->left.get());
            if (current->right != nullptr)
                pending.push(current->right.get());
        }
    }

    template <class Function>
    static void preorderNodes(const node_type* node, int depth, Function& function)
    {
        if (node == nullptr)
            return;
        function(*node, depth);
        preorderNodes(node->left.get(), depth + 1, function);
        preorderNodes(node->right.get(), depth + 1, function);
    }

    template <class Function>
    static void inorderNodes(const node_type* node, int depth, Function& function)
    {
        if (node == nullptr)
            return;
        inorderNodes(node->left.get(), depth + 1, function);
        function(*node, depth);
        inorderNodes(node->right.get(), depth + 1, function);
    }

    template <class Function>
    static void postorderNodes(const node_type* node, int depth, Function& function)
    {
        if (node == nullptr)
            return;
        postorderNodes(node->left.get(), depth + 1, function);
        postorderNodes(node->right.get(), depth + 1, function);
        function(*node, depth);
    }

    template <class Function>
    static void levelOrderNodes(const node_type* node, Function& function)
    {
        if (node == nullptr)
            return;
        std::queue<std::pair<const node_type*, int>> pending;
        pending.push({node, 0});
        while (!pending.empty()) {
            const std::pair<const node_type*, int> current = pending.front();
            pending.pop();
            function(*current.first, current.second);
            if (current.first->left != nullptr)
                pending.push({current.first->left.get(), current.second + 1});
            if (current.first->right != nullptr)
                pending.push({current.first->right.get(), current.second + 1});
        }
    }

    static size_type countLeaves(const node_type* node) noexcept
    {
        if (node == nullptr)
            return 0;
        if (node->isLeaf())
            return 1;
        return countLeaves(node->left.get()) + countLeaves(node->right.get());
    }

    static size_type countNodesWithOneChild(const node_type* node) noexcept
    {
        if (node == nullptr)
            return 0;
        const size_type self = (node->left == nullptr) != (node->right == nullptr) ? 1 : 0;
        return self + countNodesWithOneChild(node->left.get())
               + countNodesWithOneChild(node->right.get());
    }

    template <class Predicate>
    static const node_type* findIfNode(const node_type* node, Predicate& predicate) noexcept
    {
        if (node == nullptr)
            return nullptr;
        if (predicate(node->value))
            return node;
        if (const node_type* found = findIfNode(node->left.get(), predicate))
            return found;
        return findIfNode(node->right.get(), predicate);
    }

    template <class Predicate>
    static size_type countIfNode(const node_type* node, Predicate& predicate) noexcept
    {
        if (node == nullptr)
            return 0;
        const size_type self = predicate(node->value) ? 1 : 0;
        return self + countIfNode(node->left.get(), predicate)
               + countIfNode(node->right.get(), predicate);
    }

    static bool isFullNode(const node_type* node) noexcept
    {
        if (node == nullptr)
            return true;
        if ((node->left == nullptr) != (node->right == nullptr))
            return false;
        return isFullNode(node->left.get()) && isFullNode(node->right.get());
    }

    static bool isBalancedNode(const node_type* node) noexcept
    {
        if (node == nullptr)
            return true;
        const int balance = node->balanceFactor();
        if (balance < -1 || balance > 1)
            return false;
        return isBalancedNode(node->left.get()) && isBalancedNode(node->right.get());
    }

    static int diameterOf(const node_type* node, int& best) noexcept
    {
        if (node == nullptr)
            return 0;
        const int left = diameterOf(node->left.get(), best);
        const int right = diameterOf(node->right.get(), best);
        const int through = left + right + 1;
        if (through > best)
            best = through;
        return 1 + (left > right ? left : right);
    }

    static void mirrorNode(node_type* node) noexcept
    {
        if (node == nullptr)
            return;
        node->left.swap(node->right);
        mirrorNode(node->left.get());
        mirrorNode(node->right.get());
    }

    static bool equalsNode(const node_type* lhs, const node_type* rhs) noexcept
    {
        if (lhs == nullptr || rhs == nullptr)
            return lhs == rhs;
        if (!(lhs->value == rhs->value))
            return false;
        return equalsNode(lhs->left.get(), rhs->left.get())
               && equalsNode(lhs->right.get(), rhs->right.get());
    }

    std::unique_ptr<node_type> root_;
};

} // namespace md::structure
