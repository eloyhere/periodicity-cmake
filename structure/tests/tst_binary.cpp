// Periodicity — binary tree tests.
//
// The binary tree has no ordering rule, so what is worth testing is shape:
// the traversals, the structural predicates, and the edits that change a
// subtree without touching the rest of the tree.

#include <md/structure/tree/binary_tree.h>

#include <QtTest/QtTest>

#include <memory>
#include <vector>

using md::structure::BinaryNode;
using md::structure::BinaryTree;

namespace {

BinaryTree<int> completeTree()
{
    //          1
    //        /   \
    //       2     3
    //      / \   / \
    //     4   5 6   7
    return BinaryTree<int>::fromLevelOrder({1, 2, 3, 4, 5, 6, 7});
}

} // namespace

class TestBinary : public QObject
{
    Q_OBJECT

private slots:
    void buildsFromLevelOrder();
    void traversesInEveryOrder();
    void reportsShape();
    void copiesAndMirrors();
    void editsSubtrees();
    void visitsNodesWithDepth();
    void searchesAndCounts();
    void detectsIrregularShapes();
};

void TestBinary::buildsFromLevelOrder()
{
    const BinaryTree<int> tree = completeTree();

    QCOMPARE(tree.size(), std::size_t(7));
    QCOMPARE(tree.height(), 3);
    QCOMPARE(tree.minimumHeight(), 3);
    QVERIFY(!tree.empty());
    QCOMPARE(tree.root()->value, 1);
    QVERIFY(tree.root()->left->isLeaf() == false);
    QVERIFY(tree.root()->right->hasBothChildren());

    const BinaryTree<int> empty;
    QVERIFY(empty.empty());
    QCOMPARE(empty.size(), std::size_t(0));
    QCOMPARE(empty.height(), 0);
    QCOMPARE(empty.root(), nullptr);
}

void TestBinary::traversesInEveryOrder()
{
    const BinaryTree<int> tree = completeTree();

    QVERIFY((tree.toPreorder() == std::vector<int>{1, 2, 4, 5, 3, 6, 7}));
    QVERIFY((tree.toInorder() == std::vector<int>{4, 2, 5, 1, 6, 3, 7}));
    QVERIFY((tree.toPostorder() == std::vector<int>{4, 5, 2, 6, 7, 3, 1}));
    QVERIFY((tree.toLevelOrder() == std::vector<int>{1, 2, 3, 4, 5, 6, 7}));

    int visited = 0;
    tree.forEachLevelOrder([&visited](int) { ++visited; });
    QCOMPARE(visited, 7);
}

void TestBinary::reportsShape()
{
    const BinaryTree<int> tree = completeTree();

    QCOMPARE(tree.countLeaves(), std::size_t(4));
    QCOMPARE(tree.countNodesWithOneChild(), std::size_t(0));
    QVERIFY(tree.isFull());
    QVERIFY(tree.isComplete());
    QVERIFY(tree.isPerfect());
    QVERIFY(tree.isBalanced());
    QCOMPARE(tree.diameter(), 5);
}

void TestBinary::copiesAndMirrors()
{
    BinaryTree<int> tree = completeTree();
    BinaryTree<int> copy = tree;

    QVERIFY(copy.equals(tree));
    QVERIFY(copy == tree);
    QCOMPARE(copy.size(), tree.size());

    // The copy owns its own nodes.
    QVERIFY(copy.root() != tree.root());

    copy.mirror();
    QVERIFY(!(copy == tree));
    QCOMPARE(copy.root()->left->value, 3);
    QVERIFY((copy.toPreorder() == std::vector<int>{1, 3, 7, 6, 2, 5, 4}));

    copy.mirror();
    QVERIFY(copy == tree);
}

void TestBinary::editsSubtrees()
{
    BinaryTree<int> tree;
    auto& root = tree.insertRoot(1);
    auto& left = tree.insertLeft(&root, 2);
    tree.insertRight(&root, 3);
    tree.insertLeft(&left, 4);

    QCOMPARE(tree.size(), std::size_t(4));
    QCOMPARE(tree.height(), 3);

    // Graft a hand-built subtree into the empty slot of a leaf.
    auto graft = std::make_unique<BinaryNode<int>>(9);
    graft->left = std::make_unique<BinaryNode<int>>(10);
    QVERIFY(tree.setChild(&left, true, std::move(graft)) == nullptr);
    QCOMPARE(tree.size(), std::size_t(6));
    // 9 sits to the right of 2, and its left child 10 therefore comes before
    // it in the inorder reading.
    QVERIFY((tree.toInorder() == std::vector<int>{4, 2, 10, 9, 1, 3}));

    // Replacing a child hands the old subtree back to the caller.
    std::unique_ptr<BinaryNode<int>> previous = tree.setChild(&left, true, nullptr);
    QVERIFY(previous != nullptr);
    QCOMPARE(previous->value, 9);
    QCOMPARE(tree.size(), std::size_t(4));

    std::unique_ptr<BinaryNode<int>> detached = tree.takeChild(&root, false);
    QVERIFY(detached != nullptr);
    QCOMPARE(detached->value, 2);
    QCOMPARE(tree.size(), std::size_t(2));

    // A released root leaves an empty tree behind.
    std::unique_ptr<BinaryNode<int>> released = tree.releaseRoot();
    QVERIFY(released != nullptr);
    QVERIFY(tree.empty());
}

void TestBinary::visitsNodesWithDepth()
{
    const BinaryTree<int> tree = completeTree();

    std::vector<int> values;
    std::vector<int> depths;
    tree.forEachNodePreorder([&values, &depths](const BinaryNode<int>& node, int depth) {
        values.push_back(node.value);
        depths.push_back(depth);
    });

    QVERIFY((values == std::vector<int>{1, 2, 4, 5, 3, 6, 7}));
    QVERIFY((depths == std::vector<int>{0, 1, 2, 2, 1, 2, 2}));
}

void TestBinary::searchesAndCounts()
{
    const BinaryTree<int> tree = completeTree();

    const BinaryNode<int>* found = tree.findIf([](int value) { return value == 5; });
    QVERIFY(found != nullptr);
    QCOMPARE(found->value, 5);
    QCOMPARE(tree.findIf([](int value) { return value == 42; }), nullptr);

    QCOMPARE(tree.countIf([](int value) { return value % 2 == 0; }), std::size_t(3));
    QCOMPARE(tree.countIf([](int) { return false; }), std::size_t(0));
}

void TestBinary::detectsIrregularShapes()
{
    // A chain: every level except the last is empty, so it is neither
    // complete nor full nor perfect nor balanced.
    BinaryTree<int> chain;
    auto& first = chain.insertRoot(1);
    auto& second = chain.insertLeft(&first, 2);
    chain.insertLeft(&second, 3);

    QVERIFY(!chain.isComplete());
    QVERIFY(!chain.isFull());
    QVERIFY(!chain.isPerfect());
    QVERIFY(!chain.isBalanced());
    QCOMPARE(chain.countNodesWithOneChild(), std::size_t(2));
    QCOMPARE(chain.diameter(), 3);

    // A gap in the middle row is neither complete nor full.
    BinaryTree<int> gapped;
    auto& root = gapped.insertRoot(1);
    auto& left = gapped.insertLeft(&root, 2);
    gapped.insertRight(&root, 3);
    gapped.insertRight(&left, 5);

    QVERIFY(!gapped.isComplete());
    QVERIFY(!gapped.isFull());
    QCOMPARE(gapped.countNodesWithOneChild(), std::size_t(1));

    // A full but not perfect tree: every node has zero or two children.
    BinaryTree<int> full;
    auto& fullRoot = full.insertRoot(1);
    auto& fullLeft = full.insertLeft(&fullRoot, 2);
    full.insertRight(&fullRoot, 3);
    full.insertLeft(&fullLeft, 4);
    full.insertRight(&fullLeft, 5);

    QVERIFY(full.isFull());
    QVERIFY(!full.isPerfect());
    QVERIFY(full.isBalanced());
}

QTEST_APPLESS_MAIN(TestBinary)
#include "tst_binary.moc"
