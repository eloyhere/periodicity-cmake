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
// sibling (see SiblingNode). Measurements, traversals, copies and teardown are
// iterative: both the depth of the hierarchy and the length of a sibling chain
// may be tens of thousands of nodes, so neither may translate into recursive
// call-stack depth. The teardown splices children into a pending list built
// out of the nextSibling links themselves, which needs O(1) extra space.

#pragma once

#include <cassert>
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
            clear();
            firstRoot_ = std::move(other.firstRoot_);
            lastRoot_ = other.lastRoot_;
            other.lastRoot_ = nullptr;
        }
        return *this;
    }

    ~SiblingForest() { clear(); }

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
    //
    // Returns nullptr without touching the forest when `node` is null or is
    // not part of this forest (a node of another forest, or a subtree that was
    // already removed). A debug build asserts for a non-null foreign node:
    // unlinking it would corrupt its real owner and walk past the end of a
    // sibling chain.
    std::unique_ptr<node_type> removeSubtree(node_type* node)
    {
        if (node == nullptr)
            return nullptr;

        const bool owned = isInForest(node);
        assert(owned && "removeSubtree requires a node of this forest");
        if (!owned)
            return nullptr;

        std::unique_ptr<node_type> siblings = std::move(node->nextSibling);
        std::unique_ptr<node_type>& slot = slotOf(node);
        const bool wasRoot = node->parent == nullptr;
        node_type* parent = node->parent;

        std::unique_ptr<node_type> ownedNode = std::move(slot);
        slot = std::move(siblings);
        if (slot != nullptr)
            slot->parent = parent;

        ownedNode->parent = nullptr;
        if (wasRoot)
            refreshLastRoot();
        return ownedNode;
    }

    // Releases the whole forest iteratively. Each node's child chain is
    // spliced onto a pending list built out of the nextSibling links before
    // the node is deleted, so neither a deep hierarchy nor a long sibling
    // chain recurses; the time is O(n) and the extra space O(1).
    void clear() noexcept
    {
        node_type* pending = firstRoot_.release();
        lastRoot_ = nullptr;
        while (pending != nullptr) {
            node_type* node = pending;
            pending = node->nextSibling.release();

            if (node->firstChild != nullptr) {
                node_type* children = node->firstChild.release();
                node_type* lastChild = children;
                while (lastChild->nextSibling != nullptr)
                    lastChild = lastChild->nextSibling.get();
                lastChild->nextSibling.reset(pending);
                pending = children;
            }
            delete node;
        }
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
        std::vector<const node_type*> pending;
        if (node != nullptr)
            pending.push_back(node);
        while (!pending.empty()) {
            const node_type* current = pending.back();
            pending.pop_back();
            ++total;
            if (current->firstChild != nullptr)
                pending.push_back(current->firstChild.get());
            if (current->nextSibling != nullptr)
                pending.push_back(current->nextSibling.get());
        }
        return total;
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
            if (current.first->firstChild != nullptr)
                pending.emplace_back(current.first->firstChild.get(), current.second + 1);
            if (current.first->nextSibling != nullptr)
                pending.emplace_back(current.first->nextSibling.get(), current.second);
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
    // True when `node` is reachable from this forest's root chain. A node from
    // another forest, or a subtree that was already removed, has a root that
    // is not in the chain and is rejected before any link is touched.
    [[nodiscard]] bool isInForest(const node_type* node) const noexcept
    {
        if (node == nullptr)
            return false;

        const node_type* root = node;
        while (root->parent != nullptr)
            root = root->parent;
        for (const node_type* current = firstRoot_.get(); current != nullptr;
             current = current->nextSibling.get()) {
            if (current == root)
                return true;
        }
        return false;
    }

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
        std::vector<const node_type*> pending;
        if (node != nullptr)
            pending.push_back(node);
        while (!pending.empty()) {
            const node_type* current = pending.back();
            pending.pop_back();
            if (current->isLeaf()) {
                ++count;
                continue;
            }
            if (current->firstChild != nullptr)
                pending.push_back(current->firstChild.get());
            if (current->nextSibling != nullptr)
                pending.push_back(current->nextSibling.get());
        }
        return count;
    }

    static bool equalsSiblings(const node_type* lhs, const node_type* rhs) noexcept
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
            pending.emplace_back(left->firstChild.get(), right->firstChild.get());
            pending.emplace_back(left->nextSibling.get(), right->nextSibling.get());
        }
        return true;
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
            // The sibling is visited after the node's whole subtree, so it is
            // pushed first and the child is popped first.
            if (current->nextSibling != nullptr)
                pending.push_back(current->nextSibling.get());
            if (current->firstChild != nullptr)
                pending.push_back(current->firstChild.get());
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
            if (current.first->nextSibling != nullptr)
                pending.emplace_back(current.first->nextSibling.get(), current.second);
            if (current.first->firstChild != nullptr)
                pending.emplace_back(current.first->firstChild.get(), current.second + 1);
        }
    }

    static std::unique_ptr<node_type> cloneSiblings(const node_type* node, node_type* parent)
    {
        if (node == nullptr)
            return nullptr;

        auto copy = std::make_unique<node_type>(node->value);
        copy->parent = parent;
        std::vector<std::pair<const node_type*, node_type*>> pending;
        pending.emplace_back(node, copy.get());
        while (!pending.empty()) {
            const std::pair<const node_type*, node_type*> current = pending.back();
            pending.pop_back();
            const node_type* source = current.first;
            node_type* target = current.second;
            if (source->firstChild != nullptr) {
                target->firstChild = std::make_unique<node_type>(source->firstChild->value);
                target->firstChild->parent = target;
                pending.emplace_back(source->firstChild.get(), target->firstChild.get());
            }
            if (source->nextSibling != nullptr) {
                target->nextSibling = std::make_unique<node_type>(source->nextSibling->value);
                target->nextSibling->parent = target->parent;
                pending.emplace_back(source->nextSibling.get(), target->nextSibling.get());
            }
        }
        return copy;
    }

    std::unique_ptr<node_type> firstRoot_;
    node_type* lastRoot_ = nullptr;
};

} // namespace md::structure
