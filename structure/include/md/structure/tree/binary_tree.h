// Periodicity — binary tree (structure).
//
// The unrestricted binary tree: no ordering rule, no balance rule and no
// bookkeeping beyond the links themselves. Values may sit anywhere; the tree
// contributes shape, traversal and the structural questions that any binary
// tree can be asked (height, fullness, completeness, balance, diameter).
//
// Ordering belongs to BinarySearchTree and balance to AvlTree, both built on
// BinaryPlusNode, which is why this tree keeps no height, size or parent field
// and answers size() and height() by traversal. A caller that needs those
// answers in O(1) wants BinaryPlusTree instead; a caller that wants the
// smallest possible node and its own traversal wants this one.
//
// Traversal order, stated once and used everywhere:
//
//   preorder    node, left subtree, right subtree
//   inorder     left subtree, node, right subtree
//   postorder   left subtree, right subtree, node
//   level order breadth first, left before right within a level
//
// The node-level visitors also receive the depth of the node, because without
// parent links (this tree has none) depth is only known during a traversal.
//
// The tree owns its nodes. It is copyable — a copy duplicates every node and
// payload — and movable.

#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

#include <md/structure/node/binary_node.h>

namespace md::structure {

template <class T>
class BinaryTree
{
public:
    using value_type = T;
    using node_type = BinaryNode<T>;
    using size_type = std::size_t;

    BinaryTree() noexcept = default;

    // Deep copy: the shape and every payload are duplicated.
    BinaryTree(const BinaryTree& other) : root_(cloneSubtree(other.root_.get())) {}

    BinaryTree& operator=(const BinaryTree& other)
    {
        if (this != &other) {
            BinaryTree copy(other);
            root_.swap(copy.root_);
        }
        return *this;
    }

    BinaryTree(BinaryTree&&) noexcept = default;
    BinaryTree& operator=(BinaryTree&&) noexcept = default;
    ~BinaryTree() = default;

    void swap(BinaryTree& other) noexcept { root_.swap(other.root_); }

    // ---- Capacity ---------------------------------------------------------

    [[nodiscard]] bool empty() const noexcept { return root_ == nullptr; }

    // O(n): this tree does not cache its size.
    [[nodiscard]] size_type size() const noexcept { return sizeOf(root_.get()); }

    // O(n). Empty tree = 0, leaf = 1.
    [[nodiscard]] int height() const noexcept { return heightOf(root_.get()); }

    // O(n): the shortest distance from the root to a missing child.
    [[nodiscard]] int minimumHeight() const noexcept { return minimumHeightOf(root_.get()); }

    // ---- Structure --------------------------------------------------------

    [[nodiscard]] node_type* root() noexcept { return root_.get(); }
    [[nodiscard]] const node_type* root() const noexcept { return root_.get(); }

    // Takes ownership of `root`, replacing the current contents.
    void setRoot(std::unique_ptr<node_type> root) noexcept { root_ = std::move(root); }

    // Gives up ownership of the root; the tree is left empty.
    [[nodiscard]] std::unique_ptr<node_type> releaseRoot() noexcept { return std::move(root_); }

    void clear() noexcept { root_.reset(); }

    // Creates the root of an empty tree. Throws away nothing: assert if the
    // tree is not empty.
    node_type& insertRoot(T value)
    {
        assert(root_ == nullptr && "insertRoot requires an empty tree");
        root_ = std::make_unique<node_type>(std::move(value));
        return *root_;
    }

    // Creates a left child of `parent`. The slot must be empty; replacing a
    // subtree is spelled takeChild() + setChild() so that no subtree is ever
    // destroyed by accident.
    node_type& insertLeft(node_type* parent, T value)
    {
        assert(parent != nullptr && "insertLeft needs a parent");
        assert(parent->left == nullptr && "left slot is occupied");
        parent->left = std::make_unique<node_type>(std::move(value));
        return *parent->left;
    }

    node_type& insertRight(node_type* parent, T value)
    {
        assert(parent != nullptr && "insertRight needs a parent");
        assert(parent->right == nullptr && "right slot is occupied");
        parent->right = std::make_unique<node_type>(std::move(value));
        return *parent->right;
    }

    // Replaces the child on `rightSide` of `parent` and returns the previous
    // occupant (nullptr when the slot was empty).
    std::unique_ptr<node_type> setChild(node_type* parent, bool rightSide,
                                        std::unique_ptr<node_type> child) noexcept
    {
        assert(parent != nullptr && "setChild needs a parent");
        std::unique_ptr<node_type> previous =
            std::move(rightSide ? parent->right : parent->left);
        (rightSide ? parent->right : parent->left) = std::move(child);
        return previous;
    }

    // Detaches and returns the child on `rightSide` of `parent`.
    std::unique_ptr<node_type> takeChild(node_type* parent, bool rightSide) noexcept
    {
        assert(parent != nullptr && "takeChild needs a parent");
        return std::move(rightSide ? parent->right : parent->left);
    }

    // ---- Static measurements ---------------------------------------------

    [[nodiscard]] static size_type sizeOf(const node_type* node) noexcept
    {
        if (node == nullptr)
            return 0;
        return 1 + sizeOf(node->left.get()) + sizeOf(node->right.get());
    }

    [[nodiscard]] static int heightOf(const node_type* node) noexcept
    {
        if (node == nullptr)
            return 0;
        const int left = heightOf(node->left.get());
        const int right = heightOf(node->right.get());
        return 1 + (left > right ? left : right);
    }

    [[nodiscard]] static int minimumHeightOf(const node_type* node) noexcept
    {
        if (node == nullptr)
            return 0;
        const int left = minimumHeightOf(node->left.get());
        const int right = minimumHeightOf(node->right.get());
        return 1 + (left < right ? left : right);
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

    // Node-level visitors receive (const node_type&, depth), depth starting at
    // 0 for the root.
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

    [[nodiscard]] size_type countLeaves() const noexcept
    {
        return countLeaves(root_.get());
    }

    [[nodiscard]] size_type countNodesWithOneChild() const noexcept
    {
        return countNodesWithOneChild(root_.get());
    }

    // First node whose value satisfies `predicate`, or nullptr.
    template <class Predicate>
    [[nodiscard]] const node_type* findIf(Predicate&& predicate) const noexcept
    {
        return findIf(root_.get(), predicate);
    }

    template <class Predicate>
    [[nodiscard]] size_type countIf(Predicate&& predicate) const noexcept
    {
        return countIf(root_.get(), predicate);
    }

    // True when every node has zero or two children; a full tree has no node
    // with a single child.
    [[nodiscard]] bool isFull() const noexcept { return isFullNode(root_.get()); }

    // True when every level except possibly the last is full and the last
    // level is filled from left to right.
    [[nodiscard]] bool isComplete() const noexcept
    {
        if (root_ == nullptr)
            return true;

        std::queue<const node_type*> pending;
        pending.push(root_.get());
        bool sawGap = false;

        while (!pending.empty()) {
            const node_type* node = pending.front();
            pending.pop();
            if (node == nullptr) {
                sawGap = true;
                continue;
            }
            if (sawGap)
                return false;
            pending.push(node->left.get());
            pending.push(node->right.get());
        }
        return true;
    }

    // True when the tree is both full and complete: a perfect tree of height h
    // has exactly 2^h - 1 nodes and every leaf is at depth h - 1.
    [[nodiscard]] bool isPerfect() const noexcept
    {
        const int depth = height();
        if (depth == 0)
            return true;
        // 2^depth - 1, guarding against shift overflow on absurd heights.
        if (depth >= static_cast<int>(sizeof(size_type) * 8))
            return false;
        return size() == (static_cast<size_type>(1) << depth) - 1 && isFull();
    }

    // Height balance in the AVL sense: at every node the heights of the two
    // subtrees differ by at most one.
    [[nodiscard]] bool isBalanced() const noexcept { return isBalancedNode(root_.get()); }

    // Number of nodes on the longest path between two leaves; a single node
    // has diameter 1 and an empty tree 0.
    [[nodiscard]] int diameter() const noexcept
    {
        int best = 0;
        (void)diameterOf(root_.get(), best);
        return best;
    }

    // ---- Transformations --------------------------------------------------

    // Swaps left and right at every node.
    void mirror() noexcept { mirrorNode(root_.get()); }

    // Structural equality: same shape and equal payloads position by position.
    [[nodiscard]] bool equals(const BinaryTree& other) const noexcept
    {
        return equalsNode(root_.get(), other.root_.get());
    }

    [[nodiscard]] friend bool operator==(const BinaryTree& lhs,
                                         const BinaryTree& rhs) noexcept
    {
        return lhs.equals(rhs);
    }

    [[nodiscard]] friend bool operator!=(const BinaryTree& lhs,
                                         const BinaryTree& rhs) noexcept
    {
        return !lhs.equals(rhs);
    }

    // ---- Builders and exports --------------------------------------------

    // Builds the complete tree whose level-order reading is `values`
    // (child i has children 2i + 1 and 2i + 2). Useful for tests and for
    // turning an array layout into a linked one.
    [[nodiscard]] static BinaryTree fromLevelOrder(const std::vector<T>& values)
    {
        BinaryTree tree;
        tree.root_ = buildComplete(values, 0);
        return tree;
    }

    // Level-order reading of the tree; the inverse of fromLevelOrder, except
    // that missing children are skipped rather than represented as holes.
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

private:
    static std::unique_ptr<node_type> cloneSubtree(const node_type* node)
    {
        if (node == nullptr)
            return nullptr;
        auto copy = std::make_unique<node_type>(node->value);
        copy->left = cloneSubtree(node->left.get());
        copy->right = cloneSubtree(node->right.get());
        return copy;
    }

    static std::unique_ptr<node_type> buildComplete(const std::vector<T>& values,
                                                    size_type index)
    {
        if (index >= values.size())
            return nullptr;
        auto node = std::make_unique<node_type>(values[index]);
        node->left = buildComplete(values, index * 2 + 1);
        node->right = buildComplete(values, index * 2 + 2);
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
        const size_type self = node->hasOneChild() ? 1 : 0;
        return self + countNodesWithOneChild(node->left.get())
               + countNodesWithOneChild(node->right.get());
    }

    template <class Predicate>
    static const node_type* findIf(const node_type* node, Predicate& predicate) noexcept
    {
        if (node == nullptr)
            return nullptr;
        if (predicate(node->value))
            return node;
        if (const node_type* found = findIf(node->left.get(), predicate))
            return found;
        return findIf(node->right.get(), predicate);
    }

    template <class Predicate>
    static size_type countIf(const node_type* node, Predicate& predicate) noexcept
    {
        if (node == nullptr)
            return 0;
        const size_type self = predicate(node->value) ? 1 : 0;
        return self + countIf(node->left.get(), predicate)
               + countIf(node->right.get(), predicate);
    }

    static bool isFullNode(const node_type* node) noexcept
    {
        if (node == nullptr)
            return true;
        if (node->hasOneChild())
            return false;
        return isFullNode(node->left.get()) && isFullNode(node->right.get());
    }

    static bool isBalancedNode(const node_type* node) noexcept
    {
        if (node == nullptr)
            return true;
        const int left = heightOf(node->left.get());
        const int right = heightOf(node->right.get());
        if (left - right > 1 || right - left > 1)
            return false;
        return isBalancedNode(node->left.get()) && isBalancedNode(node->right.get());
    }

    // Returns the height of the subtree and updates `best` with the largest
    // left-height + right-height + 1 seen so far, which is the diameter.
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
