// Periodicity — skip list (structure).
//
// A skip list is a sorted list with express lanes. Level 0 threads every
// element; level k threads a random subset of about every 2^k-th element. A
// search descends from the top level, taking the express lane while the next
// element is still smaller and dropping a level when it is not, which finds
// an element in expected O(log n) comparisons without a single rotation.
//
//     insert / erase / find     expected O(log n)
//     iteration                  O(n), in order
//     memory                     about 2 pointers per element at level 0,
//                                one more per promoted level, so ~4 per
//                                element on average
//
// Where the order matters, the list is compared with Compare (std::less by
// default) and duplicate keys are rejected: insert() returns false when an
// equivalent key is already present, so the list behaves as an ordered set.
//
// Randomness is seeded deterministically (0x5EED5EED), so two lists built from
// the same insertions have the same shape. That matters for reproducible tests
// and for snapshots; production callers that want independent shapes call
// setSeed(). The levels of existing nodes are never reshuffled — a node picks
// its height once, at insertion.
//
// Level 0 is the whole list, so iteration never looks above level 0. Removal is
// O(1) once the node is found: the list keeps its nodes in an arena keyed by
// pointer, and erase() splices at every level the node participates in.
//
// The search structure deliberately holds non-owning pointers (see SkipNode);
// ownership lives in the arena, never in the links.

#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <unordered_map>
#include <utility>
#include <vector>

#include <md/structure/node/skip_node.h>

namespace md::structure {

template <class T, class Compare = std::less<T>>
class SkipList
{
public:
    using value_type = T;
    using node_type = SkipNode<T>;
    using size_type = std::size_t;

    // Sixteen levels hold 2^16 elements per level-0 step, which is far beyond
    // the point where the constants of the structure stop mattering.
    static constexpr size_type maximumLevel = 16;

    SkipList() : SkipList(Compare{}) {}

    explicit SkipList(Compare compare)
        : compare_(std::move(compare)), heads_(maximumLevel, nullptr)
    {
    }

    // A copy has the same contents and order. Its level structure is rebuilt
    // from the values rather than replicated, which is consistent with the
    // level distribution being random in the first place.
    SkipList(const SkipList& other) : SkipList(other.compare_)
    {
        other.forEach([this](const T& value) { insert(value); });
    }

    SkipList& operator=(const SkipList& other)
    {
        if (this != &other) {
            clear();
            compare_ = other.compare_;
            other.forEach([this](const T& value) { insert(value); });
        }
        return *this;
    }

    SkipList(SkipList&& other) noexcept
        : compare_(std::move(other.compare_)), heads_(std::move(other.heads_)),
          arena_(std::move(other.arena_)), levelCount_(other.levelCount_),
          size_(other.size_), rng_(other.rng_)
    {
        other.heads_.assign(maximumLevel, nullptr);
        other.levelCount_ = 0;
        other.size_ = 0;
    }

    SkipList& operator=(SkipList&& other) noexcept
    {
        if (this != &other) {
            compare_ = std::move(other.compare_);
            heads_ = std::move(other.heads_);
            arena_ = std::move(other.arena_);
            levelCount_ = other.levelCount_;
            size_ = other.size_;
            rng_ = other.rng_;

            other.heads_.assign(maximumLevel, nullptr);
            other.levelCount_ = 0;
            other.size_ = 0;
        }
        return *this;
    }

    ~SkipList() = default;

    // ---- Capacity ---------------------------------------------------------

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] size_type size() const noexcept { return size_; }

    // Number of levels currently in use; 0 for an empty list.
    [[nodiscard]] size_type height() const noexcept { return levelCount_; }

    // ---- Modifiers --------------------------------------------------------

    // Inserts `value`; returns false when an equivalent key is already
    // present, in which case the list is unchanged.
    bool insert(T value)
    {
        std::array<node_type*, maximumLevel> update{};
        node_type* current = nullptr;

        for (size_type level = levelCount_; level-- > 0;) {
            node_type* candidate =
                current == nullptr ? heads_[level] : current->forward[level];
            while (candidate != nullptr && compare_(candidate->value, value)) {
                current = candidate;
                candidate = current->forward[level];
            }
            update[level] = current;
        }

        node_type* next = current == nullptr ? heads_[0] : current->forward[0];
        if (next != nullptr && !compare_(value, next->value))
            return false;

        const size_type newLevel = randomLevel();
        if (newLevel > levelCount_) {
            for (size_type level = levelCount_; level < newLevel; ++level)
                update[level] = nullptr;
            levelCount_ = newLevel;
        }

        auto node = std::make_unique<node_type>(std::move(value), newLevel);
        node_type* raw = node.get();

        for (size_type level = 0; level < newLevel; ++level) {
            if (update[level] == nullptr) {
                raw->forward[level] = heads_[level];
                heads_[level] = raw;
            } else {
                raw->forward[level] = update[level]->forward[level];
                update[level]->forward[level] = raw;
            }
        }

        arena_.emplace(raw, std::move(node));
        ++size_;
        return true;
    }

    // Removes the element equivalent to `value`; returns whether one was
    // removed.
    bool erase(const T& value)
    {
        std::array<node_type*, maximumLevel> update{};
        node_type* current = nullptr;

        for (size_type level = levelCount_; level-- > 0;) {
            node_type* candidate =
                current == nullptr ? heads_[level] : current->forward[level];
            while (candidate != nullptr && compare_(candidate->value, value)) {
                current = candidate;
                candidate = current->forward[level];
            }
            update[level] = current;
        }

        node_type* target = current == nullptr ? heads_[0] : current->forward[0];
        if (target == nullptr || compare_(value, target->value))
            return false;

        for (size_type level = 0; level < target->level(); ++level) {
            if (update[level] == nullptr)
                heads_[level] = target->forward[level];
            else
                update[level]->forward[level] = target->forward[level];
        }

        // Drop levels that no longer hold anything.
        while (levelCount_ > 0 && heads_[levelCount_ - 1] == nullptr)
            --levelCount_;

        arena_.erase(target);
        --size_;
        return true;
    }

    void clear() noexcept
    {
        arena_.clear();
        heads_.assign(maximumLevel, nullptr);
        levelCount_ = 0;
        size_ = 0;
    }

    // ---- Lookup -----------------------------------------------------------

    // The node holding an equivalent key, or nullptr.
    [[nodiscard]] const node_type* find(const T& value) const
    {
        const node_type* node = lowerBound(value);
        if (node == nullptr || compare_(value, node->value))
            return nullptr;
        return node;
    }

    // Alias kept for symmetry with the ordered trees.
    [[nodiscard]] const node_type* findNode(const T& value) const { return find(value); }

    [[nodiscard]] bool contains(const T& value) const { return find(value) != nullptr; }

    // First node whose key is not less than `value`, or nullptr.
    [[nodiscard]] const node_type* lowerBound(const T& value) const
    {
        const node_type* current = nullptr;
        for (size_type level = levelCount_; level-- > 0;) {
            const node_type* candidate =
                current == nullptr ? heads_[level] : current->forward[level];
            while (candidate != nullptr && compare_(candidate->value, value)) {
                current = candidate;
                candidate = current->forward[level];
            }
        }
        return current == nullptr ? heads_[0] : current->forward[0];
    }

    // The smallest element, or nullptr when empty.
    [[nodiscard]] const node_type* first() const noexcept { return heads_[0]; }

    // ---- Traversal --------------------------------------------------------

    template <class Function>
    void forEach(Function&& function) const
    {
        for (const node_type* node = heads_[0]; node != nullptr; node = node->forward[0])
            function(node->value);
    }

    [[nodiscard]] std::vector<T> toVector() const
    {
        std::vector<T> values;
        values.reserve(size_);
        forEach([&values](const T& value) { values.push_back(value); });
        return values;
    }

    // ---- Determinism ------------------------------------------------------

    // Reseeds the level generator. Existing levels stay as they are; only
    // future insertions are affected.
    void setSeed(std::uint32_t seed) { rng_.seed(seed); }

    // ---- Invariants -------------------------------------------------------

    // Debug check: every level is sorted, every node on level k also has a
    // level-k link, and level 0 holds exactly size() nodes.
    [[nodiscard]] bool isValidList() const
    {
        size_type total = 0;
        const node_type* previous = nullptr;
        for (const node_type* node = heads_[0]; node != nullptr; node = node->forward[0]) {
            if (previous != nullptr && !compare_(previous->value, node->value))
                return false;
            if (node->level() > levelCount_)
                return false;
            previous = node;
            ++total;
        }
        if (total != size_)
            return false;

        for (size_type level = 1; level < levelCount_; ++level) {
            const node_type* higherPrevious = nullptr;
            for (const node_type* node = heads_[level]; node != nullptr;
                 node = node->forward[level]) {
                if (higherPrevious != nullptr && !compare_(higherPrevious->value, node->value))
                    return false;
                if (node->level() <= level)
                    return false;
                higherPrevious = node;
            }
        }
        return true;
    }

private:
    // Geometric distribution with p = 1/2, capped at maximumLevel.
    size_type randomLevel()
    {
        size_type level = 1;
        while (level < maximumLevel && promotion_(rng_))
            ++level;
        return level;
    }

    Compare compare_{};
    std::vector<node_type*> heads_;
    std::unordered_map<const node_type*, std::unique_ptr<node_type>> arena_;
    size_type levelCount_ = 0;
    size_type size_ = 0;
    std::mt19937 rng_{0x5EED5EEDu};
    std::bernoulli_distribution promotion_{0.5};
};

} // namespace md::structure
