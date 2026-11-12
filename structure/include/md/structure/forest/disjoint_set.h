// Periodicity — disjoint-set forest (structure).
//
// The disjoint-set forest, better known as union-find: a set of elements, each
// pointing at a parent, where a root is an element that points at itself. The
// trees are therefore implicit and unnamed — a component is exactly the set of
// elements whose parent chain ends at the same root.
//
// Two optimisations make the operations effectively constant:
//
//   path compression   find() re-points every element on the path at the root,
//                      so later finds on that path are one step;
//   union by size      unite() attaches the smaller component under the larger
//                      root, which bounds the tree height by log2 n before
//                      compression even applies.
//
// Together the amortised cost of m operations on n elements is O(m α(n)),
// where α is the inverse Ackermann function and is below 5 for any input that
// fits in memory.
//
// Elements are indices into the forest, not values: no allocation per element
// and no hashing. Callers that need to associate payloads keep a parallel
// array indexed the same way.
//
// The class is not thread-safe, like every other structure in this module.

#pragma once

#include <cassert>
#include <cstddef>
#include <numeric>
#include <utility>
#include <vector>

namespace md::structure {

class DisjointSet
{
public:
    using size_type = std::size_t;

    DisjointSet() = default;

    // Creates `count` singleton sets, numbered 0 .. count - 1.
    explicit DisjointSet(size_type count) { reset(count); }

    // ---- Construction -----------------------------------------------------

    void reset(size_type count)
    {
        parent_.resize(count);
        std::iota(parent_.begin(), parent_.end(), size_type{0});
        size_.assign(count, size_type{1});
        componentCount_ = count;
    }

    // Adds one element as a singleton set and returns its index.
    size_type addSet()
    {
        const size_type index = parent_.size();
        parent_.push_back(index);
        size_.push_back(1);
        ++componentCount_;
        return index;
    }

    void clear() noexcept
    {
        parent_.clear();
        size_.clear();
        componentCount_ = 0;
    }

    // ---- Capacity ---------------------------------------------------------

    [[nodiscard]] size_type size() const noexcept { return parent_.size(); }
    [[nodiscard]] bool empty() const noexcept { return parent_.empty(); }
    [[nodiscard]] size_type componentCount() const noexcept { return componentCount_; }

    // ---- Operations -------------------------------------------------------

    // Representative of the component holding `element`, with path
    // compression. Precondition: element < size().
    [[nodiscard]] size_type find(size_type element)
    {
        assert(element < parent_.size() && "find() element out of range");

        size_type root = element;
        while (parent_[root] != root)
            root = parent_[root];

        // Second pass: compress every node on the path straight to the root.
        while (parent_[element] != root) {
            const size_type next = parent_[element];
            parent_[element] = root;
            element = next;
        }
        return root;
    }

    [[nodiscard]] bool connected(size_type lhs, size_type rhs)
    {
        return find(lhs) == find(rhs);
    }

    // Merges the components holding `lhs` and `rhs`. Returns false when they
    // were already the same component.
    bool unite(size_type lhs, size_type rhs)
    {
        size_type leftRoot = find(lhs);
        size_type rightRoot = find(rhs);
        if (leftRoot == rightRoot)
            return false;

        // Union by size: the smaller root goes under the larger one.
        if (size_[leftRoot] < size_[rightRoot])
            std::swap(leftRoot, rightRoot);
        parent_[rightRoot] = leftRoot;
        size_[leftRoot] += size_[rightRoot];
        --componentCount_;
        return true;
    }

    // Number of elements in the component holding `element`.
    [[nodiscard]] size_type componentSize(size_type element)
    {
        return size_[find(element)];
    }

    // Root of `element` without compressing the path; used to inspect the
    // forest's shape in tests and diagnostics.
    [[nodiscard]] size_type rootOf(size_type element) const
    {
        while (parent_[element] != element)
            element = parent_[element];
        return element;
    }

private:
    std::vector<size_type> parent_;
    std::vector<size_type> size_;
    size_type componentCount_ = 0;
};

using UnionFind = DisjointSet;

} // namespace md::structure
