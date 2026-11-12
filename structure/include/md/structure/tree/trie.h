// Periodicity — prefix tree (structure).
//
// A trie maps keys of symbols to values by walking one node per symbol. The
// cost of a lookup is the length of the key, independent of how many keys are
// stored, and keys that share a prefix share their nodes — which is what makes
// a trie the natural index for prefix queries, where a hash table has nothing
// to offer.
//
//     insert / find / erase   O(length of key)
//     countWithPrefix         O(length of prefix + size of the subtree)
//     forEachWithPrefix       the subtree visit, in symbol order
//
// The alphabet is a policy: Alphabet::size is the number of symbols and
// Alphabet::index(char) maps a symbol to [0, size) or to nothing when the
// symbol is outside the alphabet. The default is the 26 lower-case Latin
// letters; a byte trie uses 256, a binary trie 2. A key that contains a symbol
// outside the alphabet is rejected rather than silently truncated.
//
// The root is an implicit node for the empty prefix, so every tree has a root
// and no operation has to special-case emptiness. size() counts keys, not
// nodes: a node that only passes a prefix through carries no value and is not
// a key.

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <md/structure/node/trie_node.h>

namespace md::structure {

namespace detail {

// Default alphabet: the 26 lower-case Latin letters, so "material" is a key
// and "Material" is not.
struct LowerCaseAlphabet
{
    static constexpr std::size_t size = 26;

    [[nodiscard]] static std::optional<std::size_t> index(char symbol) noexcept
    {
        if (symbol >= 'a' && symbol <= 'z')
            return static_cast<std::size_t>(symbol - 'a');
        return std::nullopt;
    }

    [[nodiscard]] static char symbol(std::size_t index) noexcept
    {
        return static_cast<char>('a' + static_cast<int>(index));
    }
};

} // namespace detail

template <class T, class Alphabet = detail::LowerCaseAlphabet>
class Trie
{
public:
    using value_type = T;
    using node_type = TrieNode<T, Alphabet::size>;
    using size_type = std::size_t;
    using alphabet_type = Alphabet;

    Trie() : root_(std::make_unique<node_type>()) {}

    Trie(const Trie& other) : root_(cloneSubtree(other.root_.get())), size_(other.size_) {}

    Trie& operator=(const Trie& other)
    {
        if (this != &other) {
            Trie copy(other);
            root_.swap(copy.root_);
            size_ = copy.size_;
        }
        return *this;
    }

    Trie(Trie&&) noexcept = default;
    Trie& operator=(Trie&&) noexcept = default;
    ~Trie() = default;

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] size_type size() const noexcept { return size_; }

    const node_type* root() const noexcept { return root_.get(); }

    // ---- Modifiers --------------------------------------------------------

    // Inserts a key. Returns true when the key was new; an existing key keeps
    // its node and takes the new value. Returns false when the key contains a
    // symbol outside the alphabet.
    bool insert(std::string_view key, T value)
    {
        node_type* node = root_.get();
        for (const char symbol : key) {
            const std::optional<size_type> index = Alphabet::index(symbol);
            if (!index)
                return false;
            node = &node->ensureChild(*index);
        }

        const bool isNew = !node->value.has_value();
        node->value = std::move(value);
        if (isNew)
            ++size_;
        return isNew;
    }

    // Removes a key and prunes the nodes that only existed to carry it.
    // Returns false when the key is absent or contains a foreign symbol.
    bool erase(std::string_view key)
    {
        std::vector<node_type*> path;
        std::vector<size_type> symbols;
        path.reserve(key.size() + 1);
        symbols.reserve(key.size());

        node_type* node = root_.get();
        path.push_back(node);
        for (const char symbol : key) {
            const std::optional<size_type> index = Alphabet::index(symbol);
            if (!index)
                return false;
            node = node->child(*index);
            if (node == nullptr)
                return false;
            path.push_back(node);
            symbols.push_back(*index);
        }

        if (!node->value.has_value())
            return false;

        node->value.reset();
        --size_;

        // Walk back up: a node that holds no value and has no children exists
        // only on the removed key's path, so it can go.
        for (size_type depth = symbols.size(); depth > 0; --depth) {
            if (path[depth]->value.has_value() || !path[depth]->isLeaf())
                break;
            path[depth - 1]->children[symbols[depth - 1]].reset();
        }
        return true;
    }

    void clear()
    {
        root_ = std::make_unique<node_type>();
        size_ = 0;
    }

    // ---- Lookup -----------------------------------------------------------

    [[nodiscard]] const T* find(std::string_view key) const
    {
        const node_type* node = descend(key);
        if (node == nullptr || !node->value.has_value())
            return nullptr;
        return &*node->value;
    }

    [[nodiscard]] bool contains(std::string_view key) const { return find(key) != nullptr; }

    // Number of keys that start with `prefix`; a prefix that is itself a key
    // counts once.
    [[nodiscard]] size_type countWithPrefix(std::string_view prefix) const
    {
        const node_type* node = descend(prefix);
        return node != nullptr ? countKeys(node) : 0;
    }

    // ---- Traversal --------------------------------------------------------

    // Visits every key/value pair under `prefix` in alphabet order.
    template <class Function>
    void forEachWithPrefix(std::string_view prefix, Function&& function) const
    {
        const node_type* node = descend(prefix);
        if (node == nullptr)
            return;
        std::string key(prefix);
        collect(node, key, function);
    }

    template <class Function>
    void forEach(Function&& function) const
    {
        forEachWithPrefix(std::string_view{}, function);
    }

    // Number of nodes, excluding the implicit root. Exposed for capacity
    // checks; O(number of nodes).
    [[nodiscard]] size_type nodeCount() const
    {
        size_type count = 0;
        for (const auto& child : root_->children) {
            if (child != nullptr)
                count += 1 + countNodes(child.get());
        }
        return count;
    }

private:
    [[nodiscard]] const node_type* descend(std::string_view key) const
    {
        const node_type* node = root_.get();
        for (const char symbol : key) {
            const std::optional<size_type> index = Alphabet::index(symbol);
            if (!index)
                return nullptr;
            node = node->child(*index);
            if (node == nullptr)
                return nullptr;
        }
        return node;
    }

    static size_type countKeys(const node_type* node)
    {
        size_type count = node->value.has_value() ? 1 : 0;
        for (const auto& child : node->children) {
            if (child != nullptr)
                count += countKeys(child.get());
        }
        return count;
    }

    static size_type countNodes(const node_type* node)
    {
        size_type count = 0;
        for (const auto& child : node->children) {
            if (child != nullptr)
                count += 1 + countNodes(child.get());
        }
        return count;
    }

    template <class Function>
    static void collect(const node_type* node, std::string& key, Function& function)
    {
        if (node->value.has_value())
            function(std::string_view(key), *node->value);

        for (size_type index = 0; index < Alphabet::size; ++index) {
            if (node->children[index] == nullptr)
                continue;
            key.push_back(Alphabet::symbol(index));
            collect(node->children[index].get(), key, function);
            key.pop_back();
        }
    }

    static std::unique_ptr<node_type> cloneSubtree(const node_type* node)
    {
        auto copy = std::make_unique<node_type>();
        copy->value = node->value;
        for (size_type index = 0; index < Alphabet::size; ++index) {
            if (node->children[index] != nullptr)
                copy->children[index] = cloneSubtree(node->children[index].get());
        }
        return copy;
    }

    std::unique_ptr<node_type> root_;
    size_type size_ = 0;
};

} // namespace md::structure
