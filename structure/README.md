# Structure — nodes, trees and forests

The data-structure layer of Periodicity. It contains no Qt widgets, no painting
and no tokens: only the link shapes (`node/`), the structures that own and walk
them (`tree/`), the structures that own several trees (`forest/`) and one
probabilistic ordered list (`skip/`). The storage pools that used to live here
moved to the `pool` module, next to the thread pool that recycles through them.

The module exists because the framework needs the same handful of shapes in
several places — a widget hierarchy and an event target chain, a layout tree, a
scheduler queue, ordered lookup for lists and menus — and each of those has a
different access pattern. Rather than one compromised container, the catalogue
offers the smallest structure that fits each pattern, and states its cost.

    node/     the link shape          (no ownership policy beyond "own my links")
    tree/     owns nodes, walks them, answers questions about them
    forest/   owns several trees, or a set of disjoint trees
    skip/     the one probabilistic structure

Storage reuse (`BlockPool`, `ObjectPool<T>`) is in the `pool` module.

---

## 1. Nodes

A node is a struct, not a container. It stores a value and the links that place
it in a structure; the owning tree decides what the links mean. Children are
owned through `std::unique_ptr`, so a subtree frees itself and re-linking a
subtree is a pointer move rather than a traversal.

| Node | Outgoing links | Extra fields | Serves |
| --- | --- | --- | --- |
| `LinearNode<T>` | `next` (owned) | — | `LinearTree`, singly linked chains |
| `BinaryNode<T>` | `left`, `right` (owned) | — | `BinaryTree`, shape-only trees |
| `BinaryPlusNode<T>` | `left`, `right` (owned) | `parent` (non-owning), `height`, `size` | `BinaryPlusTree`, `BinarySearchTree`, `AvlTree` |
| `MultiNode<T, N>` | `children[N]` (owned, packed) | `childCount` | `MultiTree`, quadtrees and other fixed-arity trees |
| `SiblingNode<T>` | `firstChild`, `nextSibling` (owned) | `parent` (non-owning) | `SiblingForest`, unbounded n-ary hierarchies |
| `TrieNode<T, N>` | `children[N]` (owned, sparse) | `value` (`std::optional`) | `Trie`, prefix trees |
| `SkipNode<T>` | `forward[level]` (non-owning) | — | `SkipList`; ownership lives in the list's arena |

Two rules keep the nodes honest:

1. **A node owns its links, never its owner.** `next`, `left`, `right`,
   `firstChild`, `nextSibling` and the trie slots are `unique_ptr`; parent
   pointers and skip-list forward pointers are raw because the owner is
   elsewhere.
2. **Bookkeeping is per node only where a structure needs it.** A plain
   `BinaryNode` is two pointers and a payload. Height, size and parent appear in
   `BinaryPlusNode` because order statistics and AVL balancing need them; putting
   them on every binary node would make the cheap tree pay for the expensive one.

## 2. Trees

| Tree | Shape | Ordering | Balance | Key operations |
| --- | --- | --- | --- | --- |
| `LinearTree<T>` (alias `LinearLink<T>`) | chain | insertion order | none | `pushFront/pushBack`, `insertAfter`, `eraseAfter`, `spliceAfter`, `reverse` |
| `BinaryTree<T>` | binary | none | none | traversals, `height`, `size`, `isFull/isComplete/isPerfect/isBalanced`, `diameter`, `mirror` |
| `BinaryPlusTree<T>` | binary + `parent/height/size` | none | none | the above plus `depth`, `inorderIndex/selectInorder`, `preorderIndex/selectPreorder` |
| `BinarySearchTree<T, Compare>` | binary + bookkeeping | `Compare` | none | `insert`, `find`, `erase`, `lowerBound/upperBound`, `min/max`, order statistics |
| `AvlTree<T, Compare>` | binary + bookkeeping | `Compare` | height-balanced | the BST interface, guaranteed O(log n) |
| `MultiTree<T, N>` | fixed arity `N` | child order | none | `appendChild`, `removeChild`, preorder/level order |
| `BinaryHeap<T, Compare>` | complete, implicit array | heap order | — | `push`, `pop`, `top`, `replaceTop`, `from` (O(n) build) |
| `Trie<T, Alphabet>` | one node per symbol | prefix | — | `insert`, `find`, `erase`, `countWithPrefix`, `forEachWithPrefix` |
| `SegmentTree<T, Combine>` | complete, implicit array | interval | — | `query(l, r)`, `set(i, v)`, `queryAll` |

Costs, with `n` nodes and `h` height:

| Operation | LinearTree | BinaryTree | BinaryPlusTree | AVL | Heap | Trie | SegmentTree |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `size` | O(1) | O(n) | O(1) | O(1) | O(1) | O(1) | O(1) |
| `height` | — | O(n) | O(1) | O(1) | — | — | — |
| search | O(n) | O(n) | O(n) | O(log n) | O(n) | O(key length) | — |
| insert | O(1) front/back | O(1) at a known slot | O(1) at a known slot | O(log n) | O(log n) | O(key length) | O(log n) set |
| erase | O(1) after the node | O(1) at a known slot | O(1) at a known slot | O(log n) | O(log n) pop | O(key length) | — |
| range query | — | — | — | — | — | O(prefix) | O(log n) |

### Why `LinearTree` is also `LinearLink`

The same structure has two natural names: the catalogue is indexed by shapes
(`LinearTree`, `BinaryTree`, `BinaryPlusTree`), while the node is a link and a
chain of links is a link. Rather than two implementations, `LinearLink<T>` is an
alias for `LinearTree<T>` — a static assertion in `src/structure.cpp` keeps them
identical, and `tests/tst_linear.cpp` asserts it again from the customer side.

### Why the "plus"

`BinaryPlusTree` is not an algorithm; it is the bookkeeping that every ordered
binary tree grows anyway. Putting `parent`, `height` and `size` on the node once,
with `refresh()` and `updateToRoot()` as the only ways to maintain them, is what
turns AVL balancing into O(1) per step and order statistics into O(h) queries.
`AvlTree` adds no field of its own: the height it balances on is the one already
there.

### Why the implicit layouts

`BinaryHeap` and `SegmentTree` have a *complete* shape, so the array index
arithmetic (children `2i+1`/`2i+2`, or `2k`/`2k+1` over a power-of-two
capacity) replaces the links entirely. There is no node type for either, and
nothing to allocate per element. This is the deliberate exception to the
node/tree split: for these two the layout *is* the structure.

## 3. Forests

| Structure | Meaning | Key operations |
| --- | --- | --- |
| `Forest<T>` | a bag of independent `BinaryTree`s | `addTree`, `removeTree`, `tree(i)`, `size`, `height`, `forEachPreorder` |
| `SiblingForest<T>` | one left-child/right-sibling hierarchy, roots chained as siblings | `addRoot`, `appendChild`, `removeSubtree`, preorder with depth, `size`, `height` |
| `DisjointSet` (alias `UnionFind`) | the disjoint-set forest behind union-find | `unite`, `find`, `connected`, `componentSize`, `componentCount` |

The three are genuinely different things, which is why they share a directory
rather than an interface:

* **`Forest`** is what a deletion in a tree produces: removing an internal node
  splits a tree into subtrees, and a *set* of trees is the honest result type.
* **`SiblingForest`** is the general n-ary container. A tree is a forest with one
  root; children are a sibling chain, so an arbitrary hierarchy costs two
  pointers per node and one class.
* **`DisjointSet`** is a forest whose roots are components and whose links only
  ever point upwards. Path compression plus union by size gives amortised
  O(α(n)), effectively constant.

## 4. Object pools (now in `pool`)

`BlockPool` and `ObjectPool<T>` moved to the `pool` module: storage reuse and the
code that reuses storage belong in one layer. The discussion below is retained
because these containers were designed against the same allocation strategy;
include `<md/pool/block_pool.h>` or `<md/pool/object_pool.h>` and use
`md::pool::BlockPool` / `md::pool::ObjectPool<T>`.

Allocating a node, an event or a view holder per use is the one cost the
containers above cannot remove: they own their nodes, and ownership means
`new`. The pool directory recycles that storage while keeping lifetimes
explicit:

| Type | Lifetime | Hands out |
| --- | --- | --- |
| `BlockPool` | none — raw bytes | `void*` |
| `ObjectPool<T>` and its `Handle` | constructed on `create()`, destroyed on `destroy()` | `T*`, `ObjectPool<T>::Handle` |

The distinction is the point. `BlockPool` is the engine: fixed-size blocks,
carved in chunks, threaded on an intrusive free list so a free block costs no
extra memory. `ObjectPool<T>` puts an object on top of each block: `create()`
placement-constructs, `destroy()` runs the destructor, and the block is reused —
storage is recycled, but no state ever survives between users.

Lifetime, stated as the contract a caller can rely on:

* `create(args...)` starts a fresh `T`; a constructor that throws returns the
  block to the pool before the exception escapes.
* `destroy()` runs `~T` and returns the block; `destroy(nullptr)` is a no-op, so
  it composes with a capped pool returning `nullptr`.
* `Handle` ends the lifetime at scope exit, is move-only, and offers `release()`
  for handing a live object back to the caller.
* A capped pool (`maxBlocks`) makes `create()` return `nullptr` instead of
  growing, which is how a caller applies backpressure instead of allocating.
* Nothing may be live when the pool is destroyed — the storage goes with it —
  and a debug build asserts it.

Costs, with `c` the chunk size:

| Operation | Cost |
| --- | --- |
| `create` | O(1) amortised; one chunk allocation per `c` objects |
| `destroy` | O(1) plus `~T` |
| `reserve(n)` | O(n / c) allocations, no construction |
| `clear()` | O(chunks), requires nothing live |

Reuse is LIFO, so the block returned last is the one handed out next: the best
case for the cache, and the reason a warm pool allocates nothing in the steady
state.

## 5. Ownership, copying and invariants

| Structure | Movable | Copyable | Owns |
| --- | --- | --- | --- |
| `LinearTree` | yes | no (a copy would duplicate every payload) | the chain |
| `BinaryTree`, `BinaryPlusTree`, `MultiTree` | yes | yes, deep | the root |
| `BinarySearchTree`, `AvlTree` | yes | yes, deep | the root |
| `Trie`, `SkipList`, `SiblingForest`, `Forest` | yes | yes | nodes / trees |
| `DisjointSet` | yes | yes | two index arrays |
| `BinaryHeap`, `SegmentTree` | yes | yes | one array |
| `BlockPool` (in `pool`) | yes | no | its chunks |
| `ObjectPool<T>` (in `pool`) | no | no (handles point back at it) | its chunks |

Invariants that a debug build asserts:

* a child's `parent` points back at its owner; `root()->parent` is null;
* `BinaryPlusNode::height` and `size` equal what `refresh()` computes — the
  public API maintains them, and `refresh()` / `updateToRoot()` rebuild them
  after a caller edits links by hand;
* `isValidOrder()` for the search trees, `isValidHeap()` for the heap,
  `isValidList()` for the skip list;
* every node-level operation documents whether it takes a slot that must be
  empty (`insertLeft`) or replaces whatever is there (`setChild`).

Copying a `SkipList` rebuilds its level structure from the values rather than
replicating it; the level distribution is random in the first place, so a copy
is defined by its contents and order, not its shape. The generator is seeded
deterministically (`0x5EED5EED`), so two lists built from the same insertions in
the same order *do* have the same shape, which is what the tests rely on.

## 6. Choosing a structure

| If you need… | Use |
| --- | --- |
| a sequence with O(1) splice and no random access | `LinearTree` |
| a hierarchy with unbounded children | `SiblingForest` |
| a hierarchy with a fixed number of children | `MultiTree` |
| shape queries over a binary hierarchy | `BinaryTree` |
| positions ("the 5th node in order") in a tree | `BinaryPlusTree` |
| a sorted set / map | `AvlTree` |
| a sorted set with occasional degenerate shapes and no rotation cost | `BinarySearchTree` |
| the smallest / next task | `BinaryHeap` |
| string keys with prefix queries | `Trie` |
| range aggregates over an array | `SegmentTree` |
| connectivity, Kruskal, component labels | `DisjointSet` |
| ordered set without rotations, with expected O(log n) | `SkipList` |
| to stop allocating objects per use | `md::pool::ObjectPool<T>` |
| raw fixed-size blocks without a type | `md::pool::BlockPool` |

## 7. Building

The module is part of the repository build (`framework.md` §12.1): the root
`CMakeLists.txt` adds the directory and `tools/check_layering.cmake` allows the
module to depend on `core` only. Apart from the anchor translation units in
`src/`, everything is header-only.

```bash
cmake --preset x64-debug
cmake --build --preset x64-debug --target periodicity_structure
```

The test suites under `tests/` are registered by `structure/tests/CMakeLists.txt`,
which the module's `CMakeLists.txt` adds:

```bash
ctest --preset x64-debug -R tst_
```

The seven suites cover every structure in this catalogue; the pool suite lives
in `pool/tests/` now.

| Suite | Covers |
| --- | --- |
| `tst_linear.cpp` | `LinearNode`, `LinearTree`, the `LinearLink` alias |
| `tst_binary.cpp` | `BinaryNode`, `BinaryTree`: traversals, shape predicates, subtree edits |
| `tst_augmented.cpp` | `BinaryPlusNode`, `BinaryPlusTree`: cached metrics, rank/select, parent links |
| `tst_ordered.cpp` | `BinarySearchTree`, `AvlTree`: ordering, order statistics, erasure, 4000-step churn |
| `tst_heap_trie.cpp` | `BinaryHeap`, `Trie` |
| `tst_forest.cpp` | `Forest`, `SiblingForest`, `DisjointSet` |
| `tst_compound.cpp` | `MultiTree`, `SegmentTree`, `SkipList` |

## 8. References

The catalogue was assembled from, and cross-checked against, the standard
literature rather than from any single library:

1. T. H. Cormen, C. E. Leiserson, R. L. Rivest, C. Stein, *Introduction to
   Algorithms*, 4th ed. — binary search trees, red-black trees, B-trees,
   heaps, disjoint sets, segment trees.
2. R. Sedgewick, K. Wayne, *Algorithms*, 4th ed. — balanced trees, tries,
   union-find, priority queues.
3. G. M. Adelson-Velsky, E. M. Landis, "An algorithm for the organization of
   information", *Doklady Akademii Nauk SSSR*, 1962 — the AVL balance rule.
4. W. Pugh, "Skip Lists: A Probabilistic Alternative to Balanced Trees",
   *Communications of the ACM*, 1990 — the level distribution used here.
5. R. E. Tarjan, "Efficiency of a good but not linear set union algorithm",
   *Journal of the ACM*, 1975 — path compression and union by rank/size.
6. E. Fredkin, "Trie Memory", *Communications of the ACM*, 1960 — the prefix
   tree.
7. D. E. Knuth, *The Art of Computer Programming*, Vol. 1, §2.3.2 — the
   left-child/right-sibling representation of arbitrary trees.
8. J. L. Bentley, "Solutions for Klee's measure problem", 1977 — the segment
   tree's origin as a range-aggregate structure.
9. M. L. Fredman, R. E. Tarjan, "Fibonacci heaps and their uses in improved
   network optimization algorithms", 1987 — heap variants and the cost model
   for priority queues.
10. Boost.Intrusive and the Linux kernel's `list_head`/`rb_node` — the
    intrusive-node idiom that `node/` follows: a node is embedded in the
    structure that links it, and the container is a handle rather than an owner
    of every element.
11. J. Bonwick, "The Slab Allocator: An Object-Caching Kernel Memory
    Allocator", *USENIX Summer 1994* — caching constructed objects rather than
    raw memory, the distinction `ObjectPool` is built around.
12. A. Alexandrescu, *Modern C++ Design*, ch. 4 "Small-Object Allocation" —
    fixed-size free lists, chunked growth and the allocation cost of a
    per-object `new`.
13. P. R. Wilson, M. S. Johnstone, M. Neely, D. Boles, "Dynamic Storage
    Allocation: A Survey and Critical Review", 1995 — segregation, free-list
    policies and why a size-classed pool beats a general allocator for small
    blocks.

Not implemented, and deliberately so for now: B-trees (and B+ trees), red-black
trees, splay and treap variants, suffix trees, k-d trees and octrees,
persistent structures, and lock-free variants. Each is a self-contained addition
that fits the same node/tree split; `BinaryPlusNode` already carries the fields
a red-black or treap tree would need beyond its own colour or priority.
