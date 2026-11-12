// Periodicity — structure module anchor translation unit.
//
// The module is header-only: every structure is a template or a small value
// type, so there is nothing to compile into a library beyond this file. It
// exists because the build declares one static library per module, and because
// the assertions below pin down the contracts that a header change could
// otherwise break silently.
//
// If a structure ever needs out-of-line code (a large non-template algorithm,
// a lookup table), it belongs in a new src/<topic>.cpp file rather than here.

#include <md/structure/structure.h>

#include <type_traits>

namespace {

using namespace md::structure;

// LinearTree and LinearLink are the same type, not two implementations.
static_assert(std::is_same_v<LinearLink<int>, LinearTree<int>>);
static_assert(std::is_same_v<UnionFind, DisjointSet>);

// Nodes are move-only, because they own their links.
static_assert(!std::is_copy_constructible_v<LinearNode<int>>);
static_assert(!std::is_copy_constructible_v<BinaryNode<int>>);
static_assert(!std::is_copy_constructible_v<BinaryPlusNode<int>>);
static_assert(std::is_move_constructible_v<LinearNode<int>>);

// Trees are value types: movable for the node-owning ones, copyable where a
// deep copy is meaningful.
static_assert(std::is_move_constructible_v<LinearTree<int>>);
static_assert(!std::is_copy_constructible_v<LinearTree<int>>);
static_assert(std::is_copy_constructible_v<BinaryTree<int>>);
static_assert(std::is_copy_constructible_v<BinaryPlusTree<int>>);
static_assert(std::is_copy_constructible_v<BinarySearchTree<int>>);
static_assert(std::is_copy_constructible_v<AvlTree<int>>);
static_assert(std::is_copy_constructible_v<Trie<int>>);
static_assert(std::is_copy_constructible_v<SkipList<int>>);
static_assert(std::is_copy_constructible_v<SiblingForest<int>>);

// Fixed capacities are compile-time facts.
static_assert(MultiTree<int, 3>::arity == 3);
static_assert(BinaryPlusNode<int>::heightOf(nullptr) == 0);
static_assert(BinaryPlusNode<int>::sizeOf(nullptr) == 0);
static_assert(SkipList<int>::maximumLevel == 16);
static_assert(TrieNode<int, 26>::alphabetSize == 26);

} // namespace
