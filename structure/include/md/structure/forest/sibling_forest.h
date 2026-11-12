// Periodicity — left-child/right-sibling forest (structure).
//
// A forest in the binary-tree representation: every tree in it uses the
// first-child / next-sibling encoding, and the roots are themselves a sibling
// chain. That is what makes this the general n-ary container of the module —
// a tree is a forest with a single root, so one class covers both:
//
//     forest ── root1 ── root2 ── root3        (root chain, younger siblings)
//                 │
//               child1 ── child2 ── child3      (child chain)
//
// Navigation is the LCRS contract: a node's children are its firstChild and
// that child's successive nextSibling links. There is deliberately no
// children() vector — iterating the sibling chain is the representation, and
// materialising a child list would throw away its memory advantage.
//
// Removal is the one operation that is not O(1) in the width of the node: a
// node is unlinked from its sibling chain, and finding the predecessor in that
// chain is O(siblings). Depth-first traversal, appending a child, appending a
// root and computing sizes are all linear in what they touch.
//
// The forest owns the root chain; each node owns its first child and its next
// sibling (see SiblingNode).

#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <md/structure/node/sibling_node.h>

namespace md::structure {

template <class T>
class SiblingForest
{
public:
    using value_type = T;
    using node_type = SiblingNode<T>;
    using size_type = std::size_t;

    SiblingForest() = default;

    SiblingForest(const SiblingForest& other)
        : firstRoot_(cloneSiblings(other.firstRoot_.get(), nullptr))
    {
        refreshLastRoot();
    }

    SiblingForest& operator=(const SiblingForest& other)
    {
        if (this != &other) {
            SiblingForest copy(other);
            firstRoot_.swap(copy.firstRoot_);
            lastRoot_ = copy.lastRoot_;
            copy.lastRoot_ = nullptr;
        }
        return *this;
    }

    SiblingForest(SiblingForest&& other) noexcept
        : firstRoot_(std::move(other.firstRoot_)), lastRoot_(other.lastRoot_)
    {
        other.lastRoot_ = nullptr;
    }

    SiblingForest& operator=(SiblingForest&& other) noexcept
    {
        if (this != &other) {
            firstRoot_ = std::move(other.firstRoot_);
            lastRoot_ = other.lastRoot_;
            other.lastRoot_ = nullptr;
        }
        return *this;
    }

    ~SiblingForest() = default;

    // ---- Capacity ---------------------------------------------------------

    [[nodiscard]] bool empty() const noexcept { return firstRoot_ == nullptr; }
    [[nodiscard]] size_type rootCount() const noexcept { return countSiblings(firstRoot_.get()); }

    // Total number of nodes in every tree; O(total).
    [[nodiscard]] size_type size() const noexcept { return sizeOf(firstRoot_.get()); }

    // Depth of the deepest node; 0 for an empty forest, 1 for roots only.
    [[nodiscard]] int height() const noexcept { return heightOf(firstRoot_.get()); }

    // ---- Roots ------------------------------------------------------------

    [[nodiscard]] node_type* firstRoot() noexcept { return firstRoot_.get(); }
    [[nodiscard]] const node_type* firstRoot() const noexcept { return firstRoot_.get(); }

    // The root at `index`, or nullptr.
    [[nodiscard]] node_type* root(size_type index) noexcept
    {
        return const_cast<node_type*>(static_cast<const SiblingForest*>(this)->root(index));
    }

    [[nodiscard]] const node_type* root(size_type index) const noexcept
    {
        const node_type* node = firstRoot_.get();
        while (node != nullptr && index > 0) {
            node = node->nextSibling.get();
            --index;
        }
        return node;
    }

    // Appends a root at the end of the root chain; O(1) because the last root
    // is cached.
    node_type& addRoot(T value)
    {
        auto node = std::make_unique<node_type>(std::move(value));
        node_type* raw = node.get();
        if (lastRoot_ != nullptr)
            lastRoot_->nextSibling = std::move(node);
        else
            firstRoot_ = std::move(node);
        lastRoot_ = raw;
        return *raw;
    }

    // Appends a child at the end of `parent`'s child chain; O(children).
    node_type& appendChild(node_type* parent, T value)
    {
        auto node = std::make_unique<node_type>(std::move(value));
        node_type* raw = node.get();
        raw->parent = parent;

        if (parent->firstChild == nullptr) {
            parent->firstChild = std::move(node);
        } else {
            node_type* last = parent->lastChild();
            last->nextSibling = std::move(node);
        }
        return *raw;
    }

    // Unlinks `node` and its subtree and returns it. The node's younger
    // siblings are promoted into its place, because a sibling is not part of
    // the subtree.
    std::unique_ptr<node_type> removeSubtree(node_type* node)
    {
        std::unique_ptr<node_type> siblings = std::move(node->nextSibling);
        std::unique_ptr<node_type>& slot = slotOf(node);
        const bool wasRoot = node->parent == nullptr;
        node_type* parent = node->parent;

        std::unique_ptr<node_type> owned = std::move(slot);
        slot = std::move(siblings);
        if (slot != nullptr)
            slot->parent = parent;

        owned->parent = nullptr;
        if (wasRoot)
            refreshLastRoot();
        return owned;
    }

    void clear() noexcept
    {
        firstRoot_.reset();
        lastRoot_ = nullptr;
    }

    // ---- Traversal --------------------------------------------------------

    // Depth-first, preorder, across every tree in root order.
    template <class Function>
    void forEachPreorder(Function&& function) const
    {
        preorder(firstRoot_.get(), function);
    }

    // Node visitor receives (const node_type&, depth); roots are depth 0.
    template <class Function>
    void forEachNodePreorder(Function&& function) const
    {
        preorderNodes(firstRoot_.get(), 0, function);
    }

    // ---- Queries ----------------------------------------------------------

    [[nodiscard]] static size_type sizeOf(const node_type* node) noexcept
    {
        size_type total = 0;
        for (const node_type* current = node; current != nullptr;
             current = current->nextSibling.get()) {
            total += 1 + sizeOf(current->firstChild.get());
        }
        return total;
    }

    [[nodiscard]] static int heightOf(const node_type* node) noexcept
    {
        int tallest = 0;
        for (const node_type* current = node; current != nullptr;
             current = current->nextSibling.get()) {
            const int childHeight = heightOf(current->firstChild.get());
            if (1 + childHeight > tallest)
                tallest = 1 + childHeight;
        }
        return tallest;
    }

    [[nodiscard]] size_type countLeaves() const noexcept { return countLeaves(firstRoot_.get()); }

    [[nodiscard]] bool equals(const SiblingForest& other) const noexcept
    {
        return equalsSiblings(firstRoot_.get(), other.firstRoot_.get());
    }

    [[nodiscard]] friend bool operator==(const SiblingForest& lhs,
                                         const SiblingForest& rhs) noexcept
    {
        return lhs.equals(rhs);
    }

    [[nodiscard]] friend bool operator!=(const SiblingForest& lhs,
                                         const SiblingForest& rhs) noexcept
    {
        return !lhs.equals(rhs);
    }

    [[nodiscard]] std::vector<T> toPreorder() const
    {
        std::vector<T> values;
        forEachPreorder([&values](const T& value) { values.push_back(value); });
        return values;
    }

private:
    // The owning slot that holds `node`: its previous sibling's nextSibling,
    // its parent's firstChild, or the forest's root pointer. O(siblings).
    [[nodiscard]] std::unique_ptr<node_type>& slotOf(node_type* node)
    {
        if (node->parent == nullptr) {
            if (firstRoot_.get() == node)
                return firstRoot_;
            node_type* previous = firstRoot_.get();
            while (previous->nextSibling.get() != node)
                previous = previous->nextSibling.get();
            return previous->nextSibling;
        }

        node_type* parent = node->parent;
        if (parent->firstChild.get() == node)
            return parent->firstChild;

        node_type* previous = parent->firstChild.get();
        while (previous->nextSibling.get() != node)
            previous = previous->nextSibling.get();
        return previous->nextSibling;
    }

    void refreshLastRoot() noexcept
    {
        lastRoot_ = nullptr;
        for (node_type* node = firstRoot_.get(); node != nullptr;
             node = node->nextSibling.get()) {
            lastRoot_ = node;
        }
    }

    static size_type countSiblings(const node_type* node) noexcept
    {
        size_type count = 0;
        for (const node_type* current = node; current != nullptr;
             current = current->nextSibling.get())
            ++count;
        return count;
    }

    static size_type countLeaves(const node_type* node) noexcept
    {
        size_type count = 0;
        for (const node_type* current = node; current != nullptr;
             current = current->nextSibling.get()) {
            if (current->isLeaf())
                ++count;
            else
                count += countLeaves(current->firstChild.get());
        }
        return count;
    }

    static bool equalsSiblings(const node_type* lhs, const node_type* rhs) noexcept
    {
        if (lhs == nullptr || rhs == nullptr)
            return lhs == rhs;
        if (!(lhs->value == rhs->value))
            return false;
        return equalsSiblings(lhs->firstChild.get(), rhs->firstChild.get())
               && equalsSiblings(lhs->nextSibling.get(), rhs->nextSibling.get());
    }

    template <class Function>
    static void preorder(const node_type* node, Function& function)
    {
        if (node == nullptr)
            return;
        function(node->value);
        preorder(node->firstChild.get(), function);
        preorder(node->nextSibling.get(), function);
    }

    template <class Function>
    static void preorderNodes(const node_type* node, int depth, Function& function)
    {
        if (node == nullptr)
            return;
        function(*node, depth);
        preorderNodes(node->firstChild.get(), depth + 1, function);
        preorderNodes(node->nextSibling.get(), depth, function);
    }

    static std::unique_ptr<node_type> cloneSiblings(const node_type* node, node_type* parent)
    {
        if (node == nullptr)
            return nullptr;
        auto copy = std::make_unique<node_type>(node->value);
        copy->parent = parent;
        copy->firstChild = cloneSiblings(node->firstChild.get(), copy.get());
        copy->nextSibling = cloneSiblings(node->nextSibling.get(), parent);
        return copy;
    }

    std::unique_ptr<node_type> firstRoot_;
    node_type* lastRoot_ = nullptr;
};

} // namespace md::structure
