// Periodicity — forest tests.
//
// Three different things are called a forest in this module, and each has a
// different contract: a bag of independent trees, a single left-child /
// right-sibling hierarchy, and the disjoint-set forest behind union-find.

#include <md/structure/forest/disjoint_set.h>
#include <md/structure/forest/forest.h>
#include <md/structure/forest/sibling_forest.h>
#include <md/structure/tree/binary_tree.h>

#include <QtTest/QtTest>

#include <vector>

using md::structure::BinaryTree;
using md::structure::DisjointSet;
using md::structure::Forest;
using md::structure::SiblingForest;
using md::structure::UnionFind;

class TestForest : public QObject
{
    Q_OBJECT

private slots:
    void forestCollectsTrees();
    void forestAggregates();
    void siblingForestBuildsNaryTrees();
    void siblingForestRemovesSubtrees();
    void siblingForestCopies();
    void disjointSetMerges();
    void disjointSetGrows();
};

void TestForest::forestCollectsTrees()
{
    Forest<int> forest;
    QVERIFY(forest.empty());
    QCOMPARE(forest.treeCount(), std::size_t(0));

    forest.addTree(BinaryTree<int>::fromLevelOrder({1, 2, 3}));
    forest.addTree(BinaryTree<int>::fromLevelOrder({4, 5}));

    QCOMPARE(forest.treeCount(), std::size_t(2));
    QCOMPARE(forest.tree(0).root()->value, 1);
    QCOMPARE(forest.tree(1).root()->value, 4);

    std::vector<int> values;
    forest.forEachPreorder([&values](int value) { values.push_back(value); });
    QVERIFY((values == std::vector<int>{1, 2, 3, 4, 5}));

    QVERIFY(forest.removeTree(0));
    QVERIFY(!forest.removeTree(5));
    QCOMPARE(forest.treeCount(), std::size_t(1));
    QCOMPARE(forest.tree(0).root()->value, 4);

    forest.clear();
    QVERIFY(forest.empty());
}

void TestForest::forestAggregates()
{
    Forest<int> forest;
    QCOMPARE(forest.size(), std::size_t(0));
    QCOMPARE(forest.height(), 0);

    forest.addTree(BinaryTree<int>::fromLevelOrder({1, 2, 3}));
    forest.addTree(BinaryTree<int>::fromLevelOrder({4, 5, 6, 7}));

    QCOMPARE(forest.size(), std::size_t(7));
    QCOMPARE(forest.height(), 3);

    int trees = 0;
    forest.forEachTree([&trees](const BinaryTree<int>& tree) {
        QVERIFY(!tree.empty());
        ++trees;
    });
    QCOMPARE(trees, 2);
}

void TestForest::siblingForestBuildsNaryTrees()
{
    SiblingForest<int> forest;
    QVERIFY(forest.empty());
    QCOMPARE(forest.height(), 0);

    auto& rootA = forest.addRoot(1);
    auto& rootB = forest.addRoot(2);
    auto& childA1 = forest.appendChild(&rootA, 10);
    forest.appendChild(&rootA, 11);
    forest.appendChild(&childA1, 100);
    forest.appendChild(&rootB, 20);

    QCOMPARE(forest.rootCount(), std::size_t(2));
    QCOMPARE(forest.size(), std::size_t(6));
    QCOMPARE(forest.height(), 3);
    QCOMPARE(forest.countLeaves(), std::size_t(3));

    QCOMPARE(forest.root(0)->value, 1);
    QCOMPARE(forest.root(1)->value, 2);
    QCOMPARE(forest.root(2), nullptr);

    QCOMPARE(rootA.childCount(), std::size_t(2));
    QCOMPARE(rootA.childAt(0)->value, 10);
    QCOMPARE(rootA.childAt(1)->value, 11);
    QVERIFY(childA1.isLeaf() == false);
    QCOMPARE(childA1.siblingCount(), std::size_t(2));
    QCOMPARE(childA1.parent, &rootA);

    std::vector<int> preorder;
    forest.forEachPreorder([&preorder](int value) { preorder.push_back(value); });
    QVERIFY((preorder == std::vector<int>{1, 10, 100, 11, 2, 20}));

    std::vector<int> depths;
    forest.forEachNodePreorder([&depths](const SiblingForest<int>::node_type& node, int depth) {
        depths.push_back(node.value + depth);
    });
    QVERIFY((depths == std::vector<int>{1, 11, 102, 12, 2, 21}));
}

void TestForest::siblingForestRemovesSubtrees()
{
    SiblingForest<int> forest;
    auto& root = forest.addRoot(1);
    auto& childA = forest.appendChild(&root, 2);
    auto& childB = forest.appendChild(&root, 3);
    forest.appendChild(&childA, 4);

    // Removing a middle child promotes its younger siblings.
    auto removed = forest.removeSubtree(&childA);
    QVERIFY(removed != nullptr);
    QCOMPARE(removed->value, 2);
    QCOMPARE(removed->firstChild->value, 4);
    QVERIFY(removed->nextSibling == nullptr);
    QCOMPARE(forest.size(), std::size_t(2));
    QCOMPARE(root.firstChild->value, 3);
    QCOMPARE(childB.value, 3);

    // Removing the only root empties the forest: the root's child was not a
    // sibling, so it leaves with the subtree.
    forest.removeSubtree(&root);
    QCOMPARE(forest.rootCount(), std::size_t(0));
    QVERIFY(forest.empty());

    auto& added = forest.addRoot(9);
    QVERIFY(&added == forest.root(0));
    QCOMPARE(added.value, 9);
}

void TestForest::siblingForestCopies()
{
    SiblingForest<int> forest;
    auto& root = forest.addRoot(1);
    forest.appendChild(&root, 2);
    forest.appendChild(&root, 3);

    SiblingForest<int> copy = forest;
    QVERIFY(copy == forest);
    QCOMPARE(copy.size(), std::size_t(3));
    QCOMPARE(copy.root(0)->firstChild->parent, copy.root(0));

    copy.appendChild(copy.root(0), 4);
    QCOMPARE(copy.size(), std::size_t(4));
    QCOMPARE(forest.size(), std::size_t(3));
    QVERIFY(!(copy == forest));
}

void TestForest::disjointSetMerges()
{
    static_assert(std::is_same_v<UnionFind, DisjointSet>);

    DisjointSet sets(10);
    QCOMPARE(sets.size(), std::size_t(10));
    QCOMPARE(sets.componentCount(), std::size_t(10));

    QVERIFY(sets.unite(0, 1));
    QVERIFY(sets.unite(2, 3));
    QVERIFY(sets.unite(1, 3));
    QVERIFY(!sets.unite(0, 2));
    QCOMPARE(sets.componentCount(), std::size_t(7));

    QVERIFY(sets.connected(0, 2));
    QVERIFY(!sets.connected(0, 4));
    QCOMPARE(sets.componentSize(3), std::size_t(4));
    QCOMPARE(sets.componentSize(4), std::size_t(1));

    // Path compression changes the shape, never the answers.
    const std::size_t root = sets.find(2);
    QCOMPARE(sets.find(0), root);
    QCOMPARE(sets.rootOf(0), root);
    QCOMPARE(sets.componentSize(0), std::size_t(4));
}

void TestForest::disjointSetGrows()
{
    DisjointSet sets;
    QVERIFY(sets.empty());

    QCOMPARE(sets.addSet(), std::size_t(0));
    QCOMPARE(sets.addSet(), std::size_t(1));
    QCOMPARE(sets.addSet(), std::size_t(2));
    QCOMPARE(sets.size(), std::size_t(3));

    QVERIFY(sets.unite(0, 2));
    QCOMPARE(sets.componentCount(), std::size_t(2));
    QCOMPARE(sets.componentSize(2), std::size_t(2));

    sets.reset(4);
    QCOMPARE(sets.size(), std::size_t(4));
    QCOMPARE(sets.componentCount(), std::size_t(4));
    QVERIFY(!sets.connected(0, 1));

    sets.clear();
    QVERIFY(sets.empty());
    QCOMPARE(sets.componentCount(), std::size_t(0));
}

QTEST_APPLESS_MAIN(TestForest)
#include "tst_forest.moc"
