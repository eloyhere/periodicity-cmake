// Periodicity — segment tree (structure).
//
// A segment tree answers range queries over an array: "combine the elements
// between i and j" in O(log n), and "replace element i" in O(log n). It is the
// tree for sums, minima, maxima, gcds and any other associative combination
// that has an identity element.
//
// The layout is implicit — the same idea as the heap, one level up:
//
//   leaf i                 lives at index capacity + i
//   internal node k        combines children 2k and 2k + 1
//   root                   index 1
//
// capacity is the smallest power of two at least the array length, which makes
// the index arithmetic a pair of shifts and wastes at most half the array.
// The padding leaves hold the identity element, so a query never has to
// special-case the tail.
//
// Two requirements the Combine policy must satisfy:
//
//   associativity   combine(combine(a, b), c) == combine(a, combine(b, c))
//   identity        combine(identity, a) == combine(a, identity) == a
//
// Commutativity is not required: the implementation keeps the left and right
// partial results separate and joins them in order, so a non-commutative
// combination (string concatenation, matrix product) still comes out right.
//
// Combine is invoked from const methods, so its call operator must be const —
// std::plus, std::min and the like all are.

#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace md::structure {

template <class T, class Combine = std::plus<T>>
class SegmentTree
{
public:
    using value_type = T;
    using size_type = std::size_t;

    SegmentTree() = default;

    SegmentTree(std::vector<T> values, T identity)
        : identity_(std::move(identity))
    {
        build(std::move(values));
    }

    SegmentTree(std::vector<T> values, T identity, Combine combine)
        : identity_(std::move(identity)), combine_(std::move(combine))
    {
        build(std::move(values));
    }

    // ---- Capacity ---------------------------------------------------------

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] size_type size() const noexcept { return size_; }
    [[nodiscard]] const T& identity() const noexcept { return identity_; }

    // ---- Build and update -------------------------------------------------

    // Replaces the whole array; O(n).
    void assign(std::vector<T> values) { build(std::move(values)); }

    // Replaces element `index`; O(log n).
    void set(size_type index, T value)
    {
        assert(index < size_ && "set() index out of range");
        size_type position = capacity_ + index;
        nodes_[position] = std::move(value);
        while (position > 1) {
            position >>= 1;
            nodes_[position] = combine_(nodes_[position * 2], nodes_[position * 2 + 1]);
        }
    }

    // The stored value at `index`; O(1).
    [[nodiscard]] const T& at(size_type index) const
    {
        assert(index < size_ && "at() index out of range");
        return nodes_[capacity_ + index];
    }

    // ---- Queries ----------------------------------------------------------

    // Combines elements [begin, end). An empty range yields the identity.
    // O(log n).
    [[nodiscard]] T query(size_type begin, size_type end) const
    {
        assert(begin <= end && "query() needs begin <= end");
        assert(end <= size_ && "query() range out of bounds");

        T leftResult = identity_;
        T rightResult = identity_;
        size_type left = begin + capacity_;
        size_type right = end + capacity_;

        while (left < right) {
            if ((left & 1u) != 0u)
                leftResult = combine_(leftResult, nodes_[left++]);
            if ((right & 1u) != 0u)
                rightResult = combine_(nodes_[--right], rightResult);
            left >>= 1;
            right >>= 1;
        }
        return combine_(leftResult, rightResult);
    }

    // Combines the whole array; O(1) because the root caches it.
    [[nodiscard]] T queryAll() const noexcept
    {
        return size_ == 0 ? identity_ : nodes_[1];
    }

    void clear() noexcept
    {
        nodes_.clear();
        capacity_ = 0;
        size_ = 0;
    }

    // Number of nodes the layout allocates; useful when sizing a tree up front.
    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }

private:
    void build(std::vector<T> values)
    {
        size_ = values.size();
        capacity_ = 1;
        while (capacity_ < size_)
            capacity_ *= 2;

        nodes_.assign(capacity_ * 2, identity_);
        for (size_type index = 0; index < size_; ++index)
            nodes_[capacity_ + index] = std::move(values[index]);

        // Internal nodes 1 .. capacity - 1, built bottom up.
        for (size_type index = capacity_; index-- > 1;)
            nodes_[index] = combine_(nodes_[index * 2], nodes_[index * 2 + 1]);
    }

    std::vector<T> nodes_;
    size_type capacity_ = 0;
    size_type size_ = 0;
    T identity_{};
    Combine combine_{};
};

} // namespace md::structure
