// Periodicity — augmented binary tree tests.
//
// BinaryPlusTree is judged on two things the plain tree cannot offer: the
// bookkeeping (parent, height, size) staying correct through every edit, and
// the positional queries that bookkeeping makes cheap.

#include <md/structure/tree/binary_plus_tree.h>

#include <QtTest/QtTest>

#include <memory>
#include <vector>

using md::structure::BinaryPlusNode;
using md::structure::BinaryPlusTree;

namespace {

//        10
//       /  \
//      5    15
//     / \     \
//    2   7     20
BinaryPlusTree<int> sample()
{
    BinaryPlusTree<int> tree;
    auto& root = tree.insertRoot(10);
    auto& left = tree.insertLeft(&root, 5);
    auto& right = tree.insertRight(&root, 15);
    tree.insertLeft(&left, 2);
    tree.insertRight(&left, 7);
    tree.insertRight(&right, 20);
    return tree;
}

} // namespace

class TestAugmented : public QObject
{
    Q_OBJECT

private slots:
    void cachesMetrics();
    void answersPositionalQueries();
    void editsSubtreesAndKeepsMetrics();
    void repairsAfterRawEdit();
    void refreshRebuildsEverything();
    void copiesKeepParents();
    void buildsFromLevelOrder();
};

void TestAugmented::cachesMetrics()
{
    BinaryPlusTree<int> tree = sample();

    QCOMPARE(tree.size(), std::size_t(6));
    QCOMPARE(tree.height(), 3);
    QCOMPARE(tree.root()->size, std::size_t(6));
    QCOMPARE(tree.root()->height, 3);
    QCOMPARE(tree.root()->left->size, std::size_t(3));
    QCOMPARE(tree.root()->right->size, std::size_t(2));
    QCOMPARE(tree.root()->left->left->height, 1);
    // Both sides of the root are two levels deep, so the balance factor is 0.
    QCOMPARE(tree.root()->balanceFactor(), 0);

    // Parent links point back at the owner, and the root has none.
    QCOMPARE(tree.root()->parent, nullptr);
    QCOMPARE(tree.root()->left->parent, tree.root());
    QCOMPARE(tree.root()->left->right->parent, tree.root()->left.get());

    QCOMPARE(tree.depth(tree.root()), 0);
    QCOMPARE(tree.depth(tree.root()->left->left.get()), 2);
    QVERIFY(tree.root()->sibling() == nullptr);
    QCOMPARE(tree.root()->left->right->sibling()->value, 2);
}

void TestAugmented::answersPositionalQueries()
{
    BinaryPlusTree<int> tree = sample();
    const BinaryPlusNode<int>* root = tree.root();

    QVERIFY((tree.toInorder() == std::vector<int>{2, 5, 7, 10, 15, 20}));
    QVERIFY((tree.toPreorder() == std::vector<int>{10, 5, 2, 7, 15, 20}));

    // Inorder position: 2, 5, 7, 10, 15, 20.
    QCOMPARE(tree.inorderIndex(root->left->left.get()), std::size_t(0));
    QCOMPARE(tree.inorderIndex(root->left.get()), std::size_t(1));
    QCOMPARE(tree.inorderIndex(root), std::size_t(3));
    QCOMPARE(tree.inorderIndex(root->right->right.get()), std::size_t(5));

    QCOMPARE(tree.selectInorder(0)->value, 2);
    QCOMPARE(tree.selectInorder(3)->value, 10);
    QCOMPARE(tree.selectInorder(5)->value, 20);
    QCOMPARE(tree.selectInorder(6), nullptr);

    // Preorder position: 10, 5, 2, 7, 15, 20.
    QCOMPARE(tree.preorderIndex(root), std::size_t(0));
    QCOMPARE(tree.preorderIndex(root->left.get()), std::size_t(1));
    QCOMPARE(tree.preorderIndex(root->left->left.get()), std::size_t(2));
    QCOMPARE(tree.preorderIndex(root->right.get()), std::size_t(4));

    QCOMPARE(tree.selectPreorder(4)->value, 15);
    QCOMPARE(tree.selectPreorder(5)->value, 20);
    QCOMPARE(tree.selectPreorder(99), nullptr);
}

void TestAugmented::editsSubtreesAndKeepsMetrics()
{
    BinaryPlusTree<int> tree = sample();

    // Detaching a subtree must refresh the path above it.
    std::unique_ptr<BinaryPlusNode<int>> detached = tree.takeChild(tree.root(), true);
    QCOMPARE(detached->value, 15);
    QCOMPARE(detached->parent, nullptr);
    QCOMPARE(tree.size(), std::size_t(4));
    QCOMPARE(tree.height(), 3);
    QCOMPARE(tree.root()->size, std::size_t(4));

    // Putting it back restores the original shape.
    QVERIFY(tree.setChild(tree.root(), true, std::move(detached)) == nullptr);
    QCOMPARE(tree.size(), std::size_t(6));
    QCOMPARE(tree.height(), 3);
    QVERIFY((tree.toInorder() == std::vector<int>{2, 5, 7, 10, 15, 20}));

    // Replacing an occupied slot returns the old subtree with its parent
    // cleared, so it cannot be re-attached while still claiming a parent.
    std::unique_ptr<BinaryPlusNode<int>> previous =
        tree.setChild(tree.root(), false, nullptr);
    QVERIFY(previous != nullptr);
    QCOMPARE(previous->parent, nullptr);
    QCOMPARE(tree.size(), std::size_t(3));
    // The remaining right subtree is a two-level chain, so the root is at 3.
    QCOMPARE(tree.height(), 3);
}

void TestAugmented::repairsAfterRawEdit()
{
    BinaryPlusTree<int> tree;
    auto& root = tree.insertRoot(1);

    // Attach a node the tree does not know about yet.
    auto graft = std::make_unique<BinaryPlusNode<int>>(2);
    graft->parent = &root;
    root.left = std::move(graft);

    QCOMPARE(tree.size(), std::size_t(1)); // stale on purpose
    tree.updateToRoot(&root);
    QCOMPARE(tree.size(), std::size_t(2));
    QCOMPARE(tree.height(), 2);

    auto second = std::make_unique<BinaryPlusNode<int>>(3);
    second->parent = root.left.get();
    root.left->right = std::move(second);
    tree.updateToRoot(root.left.get());

    QCOMPARE(tree.size(), std::size_t(3));
    QCOMPARE(tree.height(), 3);
    QCOMPARE(tree.root()->size, std::size_t(3));
}

void TestAugmented::refreshRebuildsEverything()
{
    auto root = std::make_unique<BinaryPlusNode<int>>(1);
    root->left = std::make_unique<BinaryPlusNode<int>>(2);
    root->right = std::make_unique<BinaryPlusNode<int>>(3);
    root->height = 99; // deliberately wrong
    root->size = 99;

    BinaryPlusTree<int> tree;
    tree.setRoot(std::move(root)); // setRoot refreshes

    QCOMPARE(tree.height(), 2);
    QCOMPARE(tree.size(), std::size_t(3));
    QCOMPARE(tree.root()->left->parent, tree.root());
    QCOMPARE(tree.root()->right->parent, tree.root());
}

void TestAugmented::copiesKeepParents()
{
    BinaryPlusTree<int> original = sample();
    BinaryPlusTree<int> copy = original;

    QVERIFY(copy.equals(original));
    QCOMPARE(copy.size(), original.size());
    QVERIFY(copy.root() != original.root());
    QCOMPARE(copy.root()->parent, nullptr);
    QCOMPARE(copy.root()->left->parent, copy.root());
    QCOMPARE(copy.root()->right->parent, copy.root());
    QCOMPARE(copy.inorderIndex(copy.root()), std::size_t(3));
}

void TestAugmented::buildsFromLevelOrder()
{
    BinaryPlusTree<int> tree = BinaryPlusTree<int>::fromLevelOrder({1, 2, 3, 4, 5, 6, 7});

    QCOMPARE(tree.size(), std::size_t(7));
    QCOMPARE(tree.height(), 3);
    QVERIFY(tree.isFull());
    QVERIFY(tree.isBalanced());
    QVERIFY((tree.toLevelOrder() == std::vector<int>{1, 2, 3, 4, 5, 6, 7}));
}

QTEST_APPLESS_MAIN(TestAugmented)
#include "tst_augmented.moc"
