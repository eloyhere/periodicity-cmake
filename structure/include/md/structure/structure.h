// Periodicity — structure module, umbrella header.
//
// One include for the whole catalogue:
//
//   node/      the link shapes a structure is built from
//                LinearNode, BinaryNode, BinaryPlusNode, MultiNode,
//                SiblingNode, TrieNode, SkipNode
//   tree/      structures that own nodes and walk them
//                LinearTree (= LinearLink), BinaryTree, BinaryPlusTree,
//                BinarySearchTree, AvlTree, MultiTree, BinaryHeap, Trie,
//                SegmentTree
//   forest/    structures that own several trees
//                Forest, SiblingForest, DisjointSet
//   skip/      probabilistic ordered list
//                SkipList
//
// Storage reuse (BlockPool, ObjectPool) lives in the `pool` module, which the
// table and tree containers here do not depend on.
//
// See structure/README.md for the taxonomy, the cost table and the references
// each structure is drawn from.

#pragma once

#include <md/structure/node/binary_node.h>
#include <md/structure/node/binary_plus_node.h>
#include <md/structure/node/linear_node.h>
#include <md/structure/node/multi_node.h>
#include <md/structure/node/sibling_node.h>
#include <md/structure/node/skip_node.h>
#include <md/structure/node/trie_node.h>

#include <md/structure/tree/avl_tree.h>
#include <md/structure/tree/binary_plus_tree.h>
#include <md/structure/tree/binary_search_tree.h>
#include <md/structure/tree/binary_tree.h>
#include <md/structure/tree/heap.h>
#include <md/structure/tree/linear_tree.h>
#include <md/structure/tree/multi_tree.h>
#include <md/structure/tree/segment_tree.h>
#include <md/structure/tree/trie.h>

#include <md/structure/forest/disjoint_set.h>
#include <md/structure/forest/forest.h>
#include <md/structure/forest/sibling_forest.h>

#include <md/structure/skip/skip_list.h>
