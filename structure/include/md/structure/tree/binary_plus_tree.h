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
//
// Every traversal, structural query, refresh, copy and teardown is iterative.
// The shape can legitimately be a chain of tens of thousands of nodes — a
// BinarySearchTree on this substrate degenerates by design on sorted input —
// so no operation may recurse once per level: the work uses an explicit heap
// stack whose depth is independent of the call stack.

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

    // Clears the current contents through the iterative teardown rather than
    // letting unique_ptr release the old root recursively.
    BinaryPlusTree& operator=(BinaryPlusTree&& other) noexcept
    {
        if (this != &other) {
            clear();
            root_ = std::move(other.root_);
        }
        return *this;
    }

    ~BinaryPlusTree() { clear(); }

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
        clear();
        root_ = std::move(root);
        refresh();
    }

    [[nodiscard]] std::unique_ptr<node_type> releaseRoot() noexcept
    {
        return std::move(root_);
    }

    // Removes every node. The tree is rotated into a right-leaning vine and
    // then released along that vine, so a degenerate chain of any depth is
    // freed without recursing: O(n) time, O(1) extra space.
    void clear() noexcept
    {
        while (root_ != nullptr) {
            if (root_->left == nullptr) {
                // The root has no left child, so it can be destroyed after its
                // right subtree takes its place in the owning pointer.
                root_ = std::move(root_->right);
                continue;
            }

            // Right rotation: the left child becomes the root and the old root
            // becomes its right child. The tree-to-vine transformation performs
            // at most one rotation per node in total.
            std::unique_ptr<node_type> pivot = std::move(root_->left);
            root_->left = std::move(pivot->right);
            pivot->right = std::move(root_);
            root_ = std::move(pivot);
        }
    }

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

    // Replaces the child on `rightSide` of `parent` and returns the previous
    // occupant (nullptr when the slot was empty).
    //
    // Precondition: `parent` belongs to this tree. `child` must not be `parent`
    // or one of its ancestors: grafting a subtree that already contains the
    // parent would close the parent chain into a cycle, updateToRoot() would
    // never reach the root and teardown would release the same nodes twice.
    // Such a graft is rejected before the tree is touched — the child is
    // released, the tree is unchanged and nullptr is returned. A debug build
    // asserts instead of returning.
    std::unique_ptr<node_type> setChild(node_type* parent, bool rightSide,
                                        std::unique_ptr<node_type> child)
    {
        assert(parent != nullptr && "setChild needs a parent");
        if (parent == nullptr)
            return nullptr;
        if (isSelfOrAncestor(parent, child.get())) {
            assert(false && "setChild would create a parent cycle");
            return nullptr;
        }

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
    // One explicit-stack frame of a bottom-up height walk. `stage` is 0 before
    // the left child has been processed, 1 before the right child and 2 once
    // both subtree heights are known.
    struct HeightFrame
    {
        const node_type* node;
        int stage;
        int leftHeight;
        int rightHeight;
    };

    // True when `candidate` is `node` itself or one of its ancestors.
    [[nodiscard]] static bool isSelfOrAncestor(const node_type* node,
                                               const node_type* candidate) noexcept
    {
        for (const node_type* current = node; current != nullptr;
             current = current->parent) {
            if (current == candidate)
                return true;
        }
        return false;
    }

    static void refreshSubtree(node_type* node, node_type* parent) noexcept
    {
        if (node == nullptr)
            return;

        // Preorder pass: rebuild every parent link and record the visit order.
        // Reversing that order puts every child before its parent, so the
        // bottom-up refresh needs no recursion.
        std::vector<node_type*> order;
        node->parent = parent;
        order.push_back(node);
        for (size_type index = 0; index < order.size(); ++index) {
            node_type* current = order[index];
            if (current->left != nullptr) {
                current->left->parent = current;
                order.push_back(current->left.get());
            }
            if (current->right != nullptr) {
                current->right->parent = current;
                order.push_back(current->right.get());
            }
        }

        for (auto it = order.rbegin(); it != order.rend(); ++it)
            (*it)->refresh();
    }

    static std::unique_ptr<node_type> cloneSubtree(const node_type* node, node_type* parent)
    {
        if (node == nullptr)
            return nullptr;

        auto copy = std::make_unique<node_type>(node->value);
        copy->parent = parent;
        copy->height = node->height;
        copy->size = node->size;

        std::vector<std::pair<const node_type*, node_type*>> pending;
        pending.emplace_back(node, copy.get());
        while (!pending.empty()) {
            const std::pair<const node_type*, node_type*> current = pending.back();
            pending.pop_back();
            const node_type* source = current.first;
            node_type* target = current.second;
            if (source->left != nullptr) {
                target->left = std::make_unique<node_type>(source->left->value);
                target->left->parent = target;
                target->left->height = source->left->height;
                target->left->size = source->left->size;
                pending.emplace_back(source->left.get(), target->left.get());
            }
            if (source->right != nullptr) {
                target->right = std::make_unique<node_type>(source->right->value);
                target->right->parent = target;
                target->right->height = source->right->height;
                target->right->size = source->right->size;
                pending.emplace_back(source->right.get(), target->right.get());
            }
        }
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

        std::vector<const node_type*> pending;
        pending.push_back(node);
        while (!pending.empty()) {
            const node_type* current = pending.back();
            pending.pop_back();
            function(current->value);
            // Right first so the left subtree is visited first.
            if (current->right != nullptr)
                pending.push_back(current->right.get());
            if (current->left != nullptr)
                pending.push_back(current->left.get());
        }
    }

    template <class Function>
    static void inorder(const node_type* node, Function& function)
    {
        std::vector<const node_type*> pending;
        const node_type* current = node;
        while (current != nullptr || !pending.empty()) {
            while (current != nullptr) {
                pending.push_back(current);
                current = current->left.get();
            }
            current = pending.back();
            pending.pop_back();
            function(current->value);
            current = current->right.get();
        }
    }

    template <class Function>
    static void postorder(const node_type* node, Function& function)
    {
        std::vector<const node_type*> pending;
        const node_type* current = node;
        const node_type* lastVisited = nullptr;
        while (current != nullptr || !pending.empty()) {
            while (current != nullptr) {
                pending.push_back(current);
                current = current->left.get();
            }
            const node_type* top = pending.back();
            if (top->right != nullptr && top->right.get() != lastVisited) {
                current = top->right.get();
            } else {
                function(top->value);
                lastVisited = top;
                pending.pop_back();
            }
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

        std::vector<std::pair<const node_type*, int>> pending;
        pending.emplace_back(node, depth);
        while (!pending.empty()) {
            const std::pair<const node_type*, int> current = pending.back();
            pending.pop_back();
            function(*current.first, current.second);
            if (current.first->right != nullptr)
                pending.emplace_back(current.first->right.get(), current.second + 1);
            if (current.first->left != nullptr)
                pending.emplace_back(current.first->left.get(), current.second + 1);
        }
    }

    template <class Function>
    static void inorderNodes(const node_type* node, int depth, Function& function)
    {
        std::vector<std::pair<const node_type*, int>> pending;
        const node_type* current = node;
        int currentDepth = depth;
        while (current != nullptr || !pending.empty()) {
            while (current != nullptr) {
                pending.emplace_back(current, currentDepth);
                ++currentDepth;
                current = current->left.get();
            }
            const std::pair<const node_type*, int> visited = pending.back();
            pending.pop_back();
            function(*visited.first, visited.second);
            currentDepth = visited.second + 1;
            current = visited.first->right.get();
        }
    }

    template <class Function>
    static void postorderNodes(const node_type* node, int depth, Function& function)
    {
        std::vector<std::pair<const node_type*, int>> pending;
        const node_type* current = node;
        const node_type* lastVisited = nullptr;
        int currentDepth = depth;
        while (current != nullptr || !pending.empty()) {
            while (current != nullptr) {
                pending.emplace_back(current, currentDepth);
                ++currentDepth;
                current = current->left.get();
            }
            const std::pair<const node_type*, int>& top = pending.back();
            if (top.first->right != nullptr && top.first->right.get() != lastVisited) {
                currentDepth = top.second + 1;
                current = top.first->right.get();
            } else {
                function(*top.first, top.second);
                lastVisited = top.first;
                pending.pop_back();
            }
        }
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
        size_type leaves = 0;
        std::vector<const node_type*> pending;
        if (node != nullptr)
            pending.push_back(node);
        while (!pending.empty()) {
            const node_type* current = pending.back();
            pending.pop_back();
            if (current->isLeaf()) {
                ++leaves;
                continue;
            }
            if (current->left != nullptr)
                pending.push_back(current->left.get());
            if (current->right != nullptr)
                pending.push_back(current->right.get());
        }
        return leaves;
    }

    static size_type countNodesWithOneChild(const node_type* node) noexcept
    {
        size_type count = 0;
        std::vector<const node_type*> pending;
        if (node != nullptr)
            pending.push_back(node);
        while (!pending.empty()) {
            const node_type* current = pending.back();
            pending.pop_back();
            if ((current->left == nullptr) != (current->right == nullptr))
                ++count;
            if (current->left != nullptr)
                pending.push_back(current->left.get());
            if (current->right != nullptr)
                pending.push_back(current->right.get());
        }
        return count;
    }

    template <class Predicate>
    static const node_type* findIfNode(const node_type* node, Predicate& predicate) noexcept
    {
        std::vector<const node_type*> pending;
        if (node != nullptr)
            pending.push_back(node);
        while (!pending.empty()) {
            const node_type* current = pending.back();
            pending.pop_back();
            if (predicate(current->value))
                return current;
            if (current->right != nullptr)
                pending.push_back(current->right.get());
            if (current->left != nullptr)
                pending.push_back(current->left.get());
        }
        return nullptr;
    }

    template <class Predicate>
    static size_type countIfNode(const node_type* node, Predicate& predicate) noexcept
    {
        size_type count = 0;
        std::vector<const node_type*> pending;
        if (node != nullptr)
            pending.push_back(node);
        while (!pending.empty()) {
            const node_type* current = pending.back();
            pending.pop_back();
            if (predicate(current->value))
                ++count;
            if (current->left != nullptr)
                pending.push_back(current->left.get());
            if (current->right != nullptr)
                pending.push_back(current->right.get());
        }
        return count;
    }

    static bool isFullNode(const node_type* node) noexcept
    {
        std::vector<const node_type*> pending;
        if (node != nullptr)
            pending.push_back(node);
        while (!pending.empty()) {
            const node_type* current = pending.back();
            pending.pop_back();
            if ((current->left == nullptr) != (current->right == nullptr))
                return false;
            if (current->left != nullptr)
                pending.push_back(current->left.get());
            if (current->right != nullptr)
                pending.push_back(current->right.get());
        }
        return true;
    }

    static bool isBalancedNode(const node_type* node) noexcept
    {
        std::vector<const node_type*> pending;
        if (node != nullptr)
            pending.push_back(node);
        while (!pending.empty()) {
            const node_type* current = pending.back();
            pending.pop_back();
            const int balance = current->balanceFactor();
            if (balance < -1 || balance > 1)
                return false;
            if (current->left != nullptr)
                pending.push_back(current->left.get());
            if (current->right != nullptr)
                pending.push_back(current->right.get());
        }
        return true;
    }

    static int diameterOf(const node_type* node, int& best) noexcept
    {
        if (node == nullptr)
            return 0;

        std::vector<HeightFrame> pending;
        pending.push_back({node, 0, 0, 0});
        int completedHeight = 0;
        while (!pending.empty()) {
            HeightFrame& frame = pending.back();
            if (frame.stage == 0) {
                frame.stage = 1;
                if (frame.node->left != nullptr)
                    pending.push_back({frame.node->left.get(), 0, 0, 0});
                continue;
            }
            if (frame.stage == 1) {
                frame.stage = 2;
                frame.leftHeight = frame.node->left != nullptr ? completedHeight : 0;
                if (frame.node->right != nullptr)
                    pending.push_back({frame.node->right.get(), 0, 0, 0});
                continue;
            }

            frame.rightHeight = frame.node->right != nullptr ? completedHeight : 0;
            const int through = frame.leftHeight + frame.rightHeight + 1;
            if (through > best)
                best = through;
            completedHeight = 1 + (frame.leftHeight > frame.rightHeight
                                       ? frame.leftHeight
                                       : frame.rightHeight);
            pending.pop_back();
        }
        return completedHeight;
    }

    static void mirrorNode(node_type* node) noexcept
    {
        std::vector<node_type*> pending;
        if (node != nullptr)
            pending.push_back(node);
        while (!pending.empty()) {
            node_type* current = pending.back();
            pending.pop_back();
            current->left.swap(current->right);
            if (current->left != nullptr)
                pending.push_back(current->left.get());
            if (current->right != nullptr)
                pending.push_back(current->right.get());
        }
    }

    static bool equalsNode(const node_type* lhs, const node_type* rhs) noexcept
    {
        if (lhs == nullptr || rhs == nullptr)
            return lhs == rhs;

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
            if (!(left->value == right->value))
                return false;
            pending.emplace_back(left->left.get(), right->left.get());
            pending.emplace_back(left->right.get(), right->right.get());
        }
        return true;
    }

    std::unique_ptr<node_type> root_;
};

} // namespace md::structure
