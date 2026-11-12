// Periodicity — AVL tree (structure).
//
// A binary search tree that keeps itself height-balanced: after every insertion
// and removal, each node on the affected path is refreshed and, where the two
// subtree heights differ by more than one, rotated back into shape. The height
// difference of an AVL tree is at most one at every node, which bounds the
// height at about 1.44 log2(n) and therefore every search, insertion and
// removal at O(log n).
//
// The balance rule is expressed entirely through the two hooks
// BinarySearchTree already calls (onInserted/onErased), so the ordering logic
// is not duplicated: this class adds a rebalance walk and nothing else.
//
// The rotations are the substrate's (BinaryPlusTree::rotateLeft/rotateRight);
// the subtree heights and sizes they refresh are what makes the balance
// factor O(1) to read. There is no separate colour or rank field, because the
// height is already stored.
//
//     Balance factor, used throughout the module:
//         balanceFactor() = height(right) - height(left)
//         > +1  right-heavy     < -1  left-heavy
//
// A double rotation is required when the heavy child leans the other way: a
// right-heavy node whose right child is left-heavy needs a right rotation on
// the child first, and symmetrically for the left side.

#pragma once

#include <utility>

#include <md/structure/tree/binary_search_tree.h>

namespace md::structure {

template <class T, class Compare = std::less<T>>
class AvlTree : public BinarySearchTree<T, Compare>
{
public:
    using Base = BinarySearchTree<T, Compare>;
    using typename Base::node_type;
    using typename Base::size_type;

    AvlTree() = default;
    explicit AvlTree(Compare compare) : Base(std::move(compare)) {}

    AvlTree(const AvlTree&) = default;
    AvlTree& operator=(const AvlTree&) = default;
    AvlTree(AvlTree&&) noexcept = default;
    AvlTree& operator=(AvlTree&&) noexcept = default;
    ~AvlTree() override = default;

    // The invariant, read from the cached heights. O(n).
    [[nodiscard]] bool isAvlBalanced() const { return this->isBalanced(); }

    // Largest height an AVL tree with `nodeCount` nodes can have. It follows
    // the minimal node count of an AVL tree of height h, which is Fibonacci:
    //     N(0) = 0, N(1) = 1, N(h) = N(h-1) + N(h-2) + 1
    // so the inverse is O(log_phi n), the constant of the balance rule.
    [[nodiscard]] static int maximumHeightFor(size_type nodeCount) noexcept
    {
        int height = 0;
        size_type previous = 0; // N(0)
        size_type current = 1;  // N(1)
        while (current <= nodeCount) {
            const size_type next = previous + current + 1;
            previous = current;
            current = next;
            ++height;
        }
        return height;
    }

protected:
    void onInserted(node_type* node) override { rebalanceFrom(node); }
    void onErased(node_type* parent) override { rebalanceFrom(parent); }

private:
    // Walks from `start` to the root, refreshing each node and repairing any
    // node whose subtrees differ in height by more than one. After a rotation
    // the walk continues above the new subtree root, because the rotation has
    // already refreshed the pair it touched.
    void rebalanceFrom(node_type* start)
    {
        node_type* current = start;
        while (current != nullptr) {
            current->refresh();
            const int balance = current->balanceFactor();
            node_type* next = current->parent;

            if (balance > 1) {
                node_type* right = current->right.get();
                if (right->balanceFactor() < 0)
                    this->rotateRight(right);
                this->rotateLeft(current);
                node_type* pivot = current->parent;
                next = pivot != nullptr ? pivot->parent : nullptr;
            } else if (balance < -1) {
                node_type* left = current->left.get();
                if (left->balanceFactor() > 0)
                    this->rotateLeft(left);
                this->rotateRight(current);
                node_type* pivot = current->parent;
                next = pivot != nullptr ? pivot->parent : nullptr;
            }

            current = next;
        }
    }
};

} // namespace md::structure
