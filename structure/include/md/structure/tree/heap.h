// Periodicity — binary heap (structure).
//
// A heap is a binary tree with two properties:
//
//   shape   complete: every level is full except the last, which fills from
//           the left, so the tree is an array with no holes;
//   order   the root compares first under Compare, and every parent compares
//           no later than its children (a min-heap under std::less).
//
// Because the shape is complete, the tree is stored implicitly: index i has
// children 2i + 1 and 2i + 2 and parent (i - 1) / 2. That is why this is the
// one tree in the catalogue with no node type — the array is the tree, and
// there is nothing to allocate or link.
//
// Operations and costs, for n elements:
//
//   top                 O(1)
//   push                O(log n)   sift up
//   pop                 O(log n)   sift down
//   replaceTop          O(log n)
//   build (from array)  O(n)       Floyd's bottom-up heapify
//   sortedValues        O(n log n) heap sort, leaving the heap empty
//
// The heap is a value type: copyable, movable and cheap to compare with
// isValidHeap() in a debug build.

#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace md::structure {

template <class T, class Compare = std::less<T>>
class BinaryHeap
{
public:
    using value_type = T;
    using size_type = std::size_t;
    using container_type = std::vector<T>;

    BinaryHeap() = default;
    explicit BinaryHeap(Compare compare) : compare_(std::move(compare)) {}

    // Builds a heap from an existing array in O(n) using Floyd's bottom-up
    // heapify: sifting from the last parent to the root is linear overall,
    // whereas pushing each element would be O(n log n).
    [[nodiscard]] static BinaryHeap from(container_type values, Compare compare = {})
    {
        BinaryHeap heap(std::move(compare));
        heap.values_ = std::move(values);
        heap.heapify();
        return heap;
    }

    // ---- Capacity ---------------------------------------------------------

    [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
    [[nodiscard]] size_type size() const noexcept { return values_.size(); }
    void reserve(size_type capacity) { values_.reserve(capacity); }
    void clear() noexcept { values_.clear(); }

    // ---- Element access ---------------------------------------------------

    // Precondition: the heap is not empty.
    [[nodiscard]] const T& top() const
    {
        assert(!values_.empty() && "top() needs a non-empty heap");
        return values_.front();
    }

    // The array layout, which is the complete tree in breadth-first order.
    [[nodiscard]] const container_type& data() const noexcept { return values_; }

    // ---- Modifiers --------------------------------------------------------

    void push(T value)
    {
        values_.push_back(std::move(value));
        siftUp(values_.size() - 1);
    }

    // Removes the first element. Precondition: the heap is not empty.
    void pop()
    {
        assert(!values_.empty() && "pop() needs a non-empty heap");
        values_.front() = std::move(values_.back());
        values_.pop_back();
        if (!values_.empty())
            siftDown(0);
    }

    // Replaces the first element in one sift, which is cheaper than pop+push
    // when a scheduler re-prioritises the earliest task.
    void replaceTop(T value)
    {
        assert(!values_.empty() && "replaceTop() needs a non-empty heap");
        values_.front() = std::move(value);
        siftDown(0);
    }

    // ---- Bulk operations --------------------------------------------------

    // Empties the heap into an array in comparison order (ascending for the
    // default std::less min-heap). This is heap sort.
    [[nodiscard]] container_type sortedValues()
    {
        container_type sorted;
        sorted.reserve(values_.size());
        while (!values_.empty()) {
            sorted.push_back(std::move(values_.front()));
            pop();
        }
        return sorted;
    }

    // Debug check: every parent compares before its children.
    [[nodiscard]] bool isValidHeap() const
    {
        for (size_type index = 1; index < values_.size(); ++index) {
            const size_type parent = (index - 1) / 2;
            if (compare_(values_[index], values_[parent]))
                return false;
        }
        return true;
    }

    [[nodiscard]] const Compare& valueCompare() const noexcept { return compare_; }

private:
    void heapify()
    {
        if (values_.size() < 2)
            return;
        // The last parent is the parent of the last element.
        for (size_type index = (values_.size() - 2) / 2 + 1; index-- > 0;)
            siftDown(index);
    }

    void siftUp(size_type index)
    {
        while (index > 0) {
            const size_type parent = (index - 1) / 2;
            if (!compare_(values_[index], values_[parent]))
                break;
            std::swap(values_[index], values_[parent]);
            index = parent;
        }
    }

    void siftDown(size_type index)
    {
        const size_type count = values_.size();
        while (true) {
            const size_type left = index * 2 + 1;
            if (left >= count)
                break;
            const size_type right = left + 1;
            size_type selected = left;
            if (right < count && compare_(values_[right], values_[left]))
                selected = right;
            if (!compare_(values_[selected], values_[index]))
                break;
            std::swap(values_[index], values_[selected]);
            index = selected;
        }
    }

    container_type values_;
    Compare compare_{};
};

} // namespace md::structure
