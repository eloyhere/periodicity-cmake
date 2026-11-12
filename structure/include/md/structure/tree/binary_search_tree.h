// Periodicity — binary search tree (structure).
//
// BinaryPlusTree plus one rule: for every node, everything in its left subtree
// compares less and everything in its right subtree compares greater. The
// ordering is supplied by a Compare policy (std::less by default), so a tree
// can be ordered by a projection or a custom predicate without a wrapper type.
//
// Because the substrate keeps subtree sizes, the ordered tree also answers the
// order-statistic questions that make a search tree worth having beyond
// lookup:
//
//     inOrder(i)  the i-th smallest element        O(height)
//     rank(node)  how many elements are before it  O(height)
//     min/max     the extremes                     O(height)
//
// Both are inherited: BinaryPlusTree::selectInorder() and inorderIndex() are
// already the order statistics of a search tree once the ordering holds.
//
// Duplicate keys are not stored. insert() returns the existing node when an
// equivalent key is already present, which keeps size() equal to the number of
// distinct keys and makes the tree usable as a set.
//
// Balance is a policy layered on top: the protected onInserted()/onErased()
// hooks are where AvlTree rebalances, so the search and removal logic is
// written once.

#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <utility>

#include <md/structure/tree/binary_plus_tree.h>

namespace md::structure {

template <class T, class Compare = std::less<T>>
class BinarySearchTree : public BinaryPlusTree<T>
{
public:
    using Base = BinaryPlusTree<T>;
    using typename Base::node_type;
    using typename Base::size_type;
    using value_type = T;
    using key_compare = Compare;

    BinarySearchTree() = default;
    explicit BinarySearchTree(Compare compare) : compare_(std::move(compare)) {}

    BinarySearchTree(const BinarySearchTree&) = default;
    BinarySearchTree& operator=(const BinarySearchTree&) = default;
    BinarySearchTree(BinarySearchTree&&) noexcept = default;
    BinarySearchTree& operator=(BinarySearchTree&&) noexcept = default;

    // The tree is a balance policy host: a subclass overrides the hooks and is
    // destroyed through this type from a caller that holds a pointer to it.
    virtual ~BinarySearchTree() = default;

    [[nodiscard]] const Compare& keyCompare() const noexcept { return compare_; }

    // ---- Lookup -----------------------------------------------------------

    [[nodiscard]] node_type* find(const T& value) const
    {
        node_type* current = rootNode();
        while (current != nullptr) {
            if (compare_(value, current->value))
                current = current->left.get();
            else if (compare_(current->value, value))
                current = current->right.get();
            else
                return current;
        }
        return nullptr;
    }

    [[nodiscard]] bool contains(const T& value) const { return find(value) != nullptr; }

    // First node whose key is not less than `value`, or nullptr.
    [[nodiscard]] node_type* lowerBound(const T& value) const
    {
        node_type* result = nullptr;
        node_type* current = rootNode();
        while (current != nullptr) {
            if (!compare_(current->value, value)) {
                result = current;
                current = current->left.get();
            } else {
                current = current->right.get();
            }
        }
        return result;
    }

    // First node whose key is strictly greater than `value`, or nullptr.
    [[nodiscard]] node_type* upperBound(const T& value) const
    {
        node_type* result = nullptr;
        node_type* current = rootNode();
        while (current != nullptr) {
            if (compare_(value, current->value)) {
                result = current;
                current = current->left.get();
            } else {
                current = current->right.get();
            }
        }
        return result;
    }

    [[nodiscard]] node_type* minNode() const { return leftmost(rootNode()); }
    [[nodiscard]] node_type* maxNode() const { return rightmost(rootNode()); }

    // Precondition: the tree is not empty.
    [[nodiscard]] const T& min() const
    {
        assert(this->root() != nullptr && "min() needs a non-empty tree");
        return minNode()->value;
    }

    [[nodiscard]] const T& max() const
    {
        assert(this->root() != nullptr && "max() needs a non-empty tree");
        return maxNode()->value;
    }

    // Smallest key greater than `value` (the in-order successor of the key),
    // or nullptr when `value` is the maximum or absent and larger than all.
    [[nodiscard]] node_type* successor(const T& value) const { return upperBound(value); }

    // Greatest key less than `value`, or nullptr.
    [[nodiscard]] node_type* predecessor(const T& value) const
    {
        node_type* result = nullptr;
        node_type* current = rootNode();
        while (current != nullptr) {
            if (compare_(current->value, value)) {
                result = current;
                current = current->right.get();
            } else {
                current = current->left.get();
            }
        }
        return result;
    }

    // ---- Modifiers --------------------------------------------------------

    // Inserts `value` and returns the node that holds it; when an equivalent
    // key is already present the existing node is returned and nothing is
    // inserted.
    node_type* insert(T value)
    {
        if (this->root() == nullptr) {
            node_type* node = &this->insertRoot(std::move(value));
            onInserted(node);
            return node;
        }

        node_type* current = this->root();
        while (true) {
            if (compare_(value, current->value)) {
                if (current->left == nullptr) {
                    node_type* node = &this->insertLeft(current, std::move(value));
                    onInserted(node);
                    return node;
                }
                current = current->left.get();
            } else if (compare_(current->value, value)) {
                if (current->right == nullptr) {
                    node_type* node = &this->insertRight(current, std::move(value));
                    onInserted(node);
                    return node;
                }
                current = current->right.get();
            } else {
                return current;
            }
        }
    }

    // Removes the node holding a key equivalent to `value`. Returns whether
    // one was removed.
    bool erase(const T& value)
    {
        node_type* node = find(value);
        if (node == nullptr)
            return false;
        eraseNode(node);
        return true;
    }

    // ---- Invariants -------------------------------------------------------

    // Debug check: every node lies within the open interval (lower, upper)
    // implied by its ancestors.
    [[nodiscard]] bool isValidOrder() const
    {
        return isValidOrderNode(this->root(), nullptr, nullptr);
    }

protected:
    // Const-safe root access for the read-only queries. They hand out
    // non-owning node pointers on purpose: the caller pairs them with
    // inorderIndex()/selectInorder(), whose arithmetic is const but whose
    // argument is a mutable handle.
    [[nodiscard]] node_type* rootNode() const noexcept
    {
        return const_cast<BinarySearchTree*>(this)->root();
    }

    // Called after a node is linked and the path above it refreshed.
    virtual void onInserted(node_type* node) { (void)node; }

    // Called after a node is removed, with the parent of the removed slot
    // (nullptr when the tree became empty) and the path above it refreshed.
    virtual void onErased(node_type* parent) { (void)parent; }

    // Unlinks `node`, promoting its single child (a two-child node first has
    // itself replaced in value by its in-order successor). O(height).
    void eraseNode(node_type* node)
    {
        node_type* parent = node->parent;

        if (node->left != nullptr && node->right != nullptr) {
            node_type* successor = leftmost(node->right.get());
            node->value = std::move(successor->value);
            node = successor;
            parent = node->parent;
        }

        std::unique_ptr<node_type>& slot = this->slotOf(node);
        std::unique_ptr<node_type> onlyChild =
            node->left != nullptr ? std::move(node->left) : std::move(node->right);

        std::unique_ptr<node_type> removed = std::move(slot);
        slot = std::move(onlyChild);
        if (slot != nullptr)
            slot->parent = parent;
        removed->parent = nullptr;

        if (parent != nullptr)
            this->updateToRoot(parent);

        onErased(parent);
    }

    [[nodiscard]] static node_type* leftmost(node_type* node) noexcept
    {
        if (node == nullptr)
            return nullptr;
        while (node->left != nullptr)
            node = node->left.get();
        return node;
    }

    [[nodiscard]] static node_type* rightmost(node_type* node) noexcept
    {
        if (node == nullptr)
            return nullptr;
        while (node->right != nullptr)
            node = node->right.get();
        return node;
    }

private:
    [[nodiscard]] bool isValidOrderNode(const node_type* node, const T* lower,
                                        const T* upper) const
    {
        if (node == nullptr)
            return true;
        if (lower != nullptr && !compare_(*lower, node->value))
            return false;
        if (upper != nullptr && !compare_(node->value, *upper))
            return false;
        return isValidOrderNode(node->left.get(), lower, &node->value)
               && isValidOrderNode(node->right.get(), &node->value, upper);
    }

    Compare compare_{};
};

} // namespace md::structure
