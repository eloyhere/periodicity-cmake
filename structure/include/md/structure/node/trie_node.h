// Periodicity — trie node (structure).
//
// A node of a prefix tree: one slot per symbol of the alphabet, plus the value
// that makes the node the end of a key. Two properties matter:
//
//   * lookup is an array index, not a comparison, so the cost of a search
//     depends on the length of the key and not on the number of entries;
//   * a slot is allocated only when a key passes through that symbol, so the
//     memory follows the shape of the key set rather than N per node.
//
// N is the size of the alphabet the caller maps symbols onto: 26 for lower-
// case Latin letters, 2 for a binary alphabet, 256 for bytes. The mapping
// itself belongs to the tree (Trie), not to the node.
//
// A node holds std::optional<T> rather than a T plus a flag, so a payload is
// constructed only at the end of an inserted key and no default constructor is
// required of T.

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>

namespace md::structure {

template <class T, std::size_t N>
struct TrieNode
{
    static_assert(N >= 1, "a trie node needs a non-empty alphabet");

    using value_type = T;
    using node_type = TrieNode;
    using size_type = std::size_t;

    static constexpr size_type alphabetSize = N;

    std::array<std::unique_ptr<node_type>, N> children{};
    std::optional<T> value;

    [[nodiscard]] bool isTerminal() const noexcept { return value.has_value(); }
    [[nodiscard]] bool isLeaf() const noexcept
    {
        for (const auto& child : children) {
            if (child != nullptr)
                return false;
        }
        return true;
    }

    [[nodiscard]] node_type* child(size_type symbol) noexcept
    {
        return symbol < N ? children[symbol].get() : nullptr;
    }

    [[nodiscard]] const node_type* child(size_type symbol) const noexcept
    {
        return symbol < N ? children[symbol].get() : nullptr;
    }

    // Returns the child for `symbol`, creating it when absent. The caller is
    // responsible for `symbol < N`, which the symbol mapper guarantees.
    node_type& ensureChild(size_type symbol)
    {
        if (children[symbol] == nullptr)
            children[symbol] = std::make_unique<node_type>();
        return *children[symbol];
    }

    // Number of occupied child slots; O(N).
    [[nodiscard]] size_type childCount() const noexcept
    {
        size_type count = 0;
        for (const auto& child : children) {
            if (child != nullptr)
                ++count;
        }
        return count;
    }
};

} // namespace md::structure
