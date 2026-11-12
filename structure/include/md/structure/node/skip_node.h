// Periodicity — skip node (structure).
//
// A node with one forward pointer per level it participates in. Level 0 is the
// sorted base list; level k skips over about 2^k entries. The node owns no
// link at all — the owning SkipList keeps its nodes in an arena of
// std::unique_ptr and splices them with raw pointers — so the forward vector
// is deliberately a vector of non-owning pointers.
//
// The level is fixed when the node is created, because a skip list decides the
// height from its probability distribution at insertion time and never
// resizes a node afterwards.

#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace md::structure {

template <class T>
struct SkipNode
{
    using value_type = T;
    using node_type = SkipNode;
    using level_type = std::size_t;

    // `level` is the number of forward pointers, so a level-1 node is a plain
    // list node and every node has an index-based link at forward[0].
    template <class U>
    SkipNode(U&& nodeValue, level_type level)
        : value(std::forward<U>(nodeValue)), forward(level, nullptr)
    {
    }

    SkipNode(const SkipNode&) = delete;
    SkipNode& operator=(const SkipNode&) = delete;
    SkipNode(SkipNode&&) = default;
    SkipNode& operator=(SkipNode&&) = default;

    T value;
    std::vector<node_type*> forward;

    [[nodiscard]] level_type level() const noexcept { return forward.size(); }

    [[nodiscard]] node_type* next(level_type at) const noexcept
    {
        return at < forward.size() ? forward[at] : nullptr;
    }
};

} // namespace md::structure
