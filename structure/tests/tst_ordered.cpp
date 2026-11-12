// Periodicity — ordered tree tests: binary search tree and AVL.
//
// The search tree suite checks the ordering contract; the AVL suite checks the
// balance contract, including under the adversarial insertion order (sorted
// input) and under a long random mix of insertions and removals.

#include <md/structure/tree/avl_tree.h>
#include <md/structure/tree/binary_search_tree.h>

#include <QtTest/QtTest>

#include <algorithm>
#include <functional>
#include <memory>
#include <numeric>
#include <random>
#include <vector>

using md::structure::AvlTree;
using md::structure::BinaryPlusNode;
using md::structure::BinarySearchTree;

namespace {

// Verifies parent links, cached height and size, and the AVL balance rule.
bool checkNode(const BinaryPlusNode<int>* node, const BinaryPlusNode<int>* parent,
               int& height, std::size_t& size)
{
    if (node == nullptr) {
        height = 0;
        size = 0;
        return true;
    }
    if (node->parent != parent)
        return false;

    int leftHeight = 0;
    int rightHeight = 0;
    std::size_t leftSize = 0;
    std::size_t rightSize = 0;
    if (!checkNode(node->left.get(), node, leftHeight, leftSize))
        return false;
    if (!checkNode(node->right.get(), node, rightHeight, rightSize))
        return false;

    height = 1 + std::max(leftHeight, rightHeight);
    size = 1 + leftSize + rightSize;
    if (node->height != height || node->size != size)
        return false;
    if (leftHeight - rightHeight > 1 || rightHeight - leftHeight > 1)
        return false;
    return true;
}

bool isValidAvl(const AvlTree<int>& tree)
{
    int height = 0;
    std::size_t size = 0;
    if (!checkNode(tree.root(), nullptr, height, size))
        return false;
    return height == tree.height() && size == tree.size() && tree.isValidOrder();
}

std::vector<int> ascending(int count)
{
    std::vector<int> values(static_cast<std::size_t>(count));
    std::iota(values.begin(), values.end(), 0);
    return values;
}

} // namespace

class TestOrdered : public QObject
{
    Q_OBJECT

private slots:
    void insertsAndFinds();
    void keepsOrderStatistics();
    void boundsAndNeighbours();
    void erasesEveryShape();
    void rejectsDuplicates();
    void avlBalancesSortedInput();
    void avlSurvivesChurn();
    void honoursCustomCompare();
    void plainTreeSurvivesDeepChain();
};

void TestOrdered::insertsAndFinds()
{
    BinarySearchTree<int> tree;
    for (int value : {50, 30, 70, 20, 40, 60, 80})
        tree.insert(value);

    QCOMPARE(tree.size(), std::size_t(7));
    QVERIFY(tree.isValidOrder());
    QVERIFY((tree.toInorder() == std::vector<int>{20, 30, 40, 50, 60, 70, 80}));

    QVERIFY(tree.contains(40));
    QVERIFY(!tree.contains(41));
    QCOMPARE(tree.find(60)->value, 60);
    QCOMPARE(tree.find(61), nullptr);
    QCOMPARE(tree.height(), 3);
}

void TestOrdered::keepsOrderStatistics()
{
    BinarySearchTree<int> tree;
    for (int value : {50, 30, 70, 20, 40, 60, 80})
        tree.insert(value);

    // The i-th smallest element is selectInorder(i) once the ordering holds.
    for (int index = 0; index < 7; ++index) {
        const int expected = 20 + index * 10;
        QCOMPARE(tree.selectInorder(static_cast<std::size_t>(index))->value, expected);
    }

    QCOMPARE(tree.inorderIndex(tree.find(20)), std::size_t(0));
    QCOMPARE(tree.inorderIndex(tree.find(50)), std::size_t(3));
    QCOMPARE(tree.inorderIndex(tree.find(80)), std::size_t(6));
}

void TestOrdered::boundsAndNeighbours()
{
    BinarySearchTree<int> tree;
    for (int value : {50, 30, 70, 20, 40, 60, 80})
        tree.insert(value);

    QCOMPARE(tree.min(), 20);
    QCOMPARE(tree.max(), 80);
    QCOMPARE(tree.minNode()->value, 20);
    QCOMPARE(tree.maxNode()->value, 80);

    QCOMPARE(tree.lowerBound(35)->value, 40);
    QCOMPARE(tree.upperBound(40)->value, 50);
    QCOMPARE(tree.lowerBound(20)->value, 20);
    QCOMPARE(tree.lowerBound(81), nullptr);
    QCOMPARE(tree.upperBound(80), nullptr);

    QCOMPARE(tree.successor(40)->value, 50);
    QCOMPARE(tree.predecessor(40)->value, 30);
    QCOMPARE(tree.predecessor(20), nullptr);
    QCOMPARE(tree.successor(80), nullptr);
}

void TestOrdered::erasesEveryShape()
{
    BinarySearchTree<int> tree;
    for (int value : {50, 30, 70, 20, 40, 60, 80, 65})
        tree.insert(value);

    // Leaf.
    QVERIFY(tree.erase(20));
    QVERIFY(tree.isValidOrder());
    QCOMPARE(tree.size(), std::size_t(7));

    // One child: 60 keeps its right child 65 after removal.
    QVERIFY(tree.erase(60));
    QVERIFY(tree.isValidOrder());
    QVERIFY((tree.toInorder() == std::vector<int>{30, 40, 50, 65, 70, 80}));

    // Two children.
    QVERIFY(tree.erase(50));
    QVERIFY(tree.isValidOrder());
    QVERIFY((tree.toInorder() == std::vector<int>{30, 40, 65, 70, 80}));
    QCOMPARE(tree.size(), std::size_t(5));

    // Absent key.
    QVERIFY(!tree.erase(50));

    // Drain.
    for (int value : {30, 40, 65, 70, 80})
        QVERIFY(tree.erase(value));
    QVERIFY(tree.empty());
    QCOMPARE(tree.root(), nullptr);
}

void TestOrdered::rejectsDuplicates()
{
    BinarySearchTree<int> tree;
    QCOMPARE(tree.insert(5)->value, 5);
    QCOMPARE(tree.insert(5)->value, 5);
    QCOMPARE(tree.size(), std::size_t(1));
}

void TestOrdered::avlBalancesSortedInput()
{
    AvlTree<int> tree;
    for (int value : ascending(1000))
        tree.insert(value);

    QCOMPARE(tree.size(), std::size_t(1000));
    QVERIFY(tree.isValidOrder());
    QVERIFY(tree.isAvlBalanced());
    QVERIFY(isValidAvl(tree));
    QVERIFY(tree.height() <= AvlTree<int>::maximumHeightFor(1000));

    // A plain search tree on the same input degenerates; the AVL one must not.
    BinarySearchTree<int> plain;
    for (int value : ascending(100))
        plain.insert(value);
    QCOMPARE(plain.height(), 100);
    QVERIFY(tree.height() < 20);
}

void TestOrdered::avlSurvivesChurn()
{
    AvlTree<int> tree;
    std::vector<int> present;
    std::mt19937 rng(2026);
    std::uniform_int_distribution<int> key(0, 299);

    for (int step = 0; step < 4000; ++step) {
        const int value = key(rng);
        if (step % 3 != 2) {
            tree.insert(value);
            present.push_back(value);
            std::sort(present.begin(), present.end());
            present.erase(std::unique(present.begin(), present.end()), present.end());
        } else {
            tree.erase(value);
            present.erase(std::remove(present.begin(), present.end(), value), present.end());
        }

        if (step % 250 == 0) {
            QVERIFY(tree.isAvlBalanced());
            QVERIFY(isValidAvl(tree));
            QVERIFY((tree.toInorder() == present));
        }
    }

    QVERIFY(tree.isAvlBalanced());
    QVERIFY(isValidAvl(tree));
    QVERIFY((tree.toInorder() == present));

    // Drain and confirm the tree unwinds to empty.
    for (int value : present)
        QVERIFY(tree.erase(value));
    QVERIFY(tree.empty());
    QCOMPARE(tree.height(), 0);
}

void TestOrdered::honoursCustomCompare()
{
    BinarySearchTree<int, std::greater<int>> tree;
    for (int value : {3, 1, 4, 1, 5, 9, 2, 6})
        tree.insert(value);

    QVERIFY(tree.isValidOrder());
    QVERIFY((tree.toInorder() == std::vector<int>{9, 6, 5, 4, 3, 2, 1}));
    QCOMPARE(tree.min(), 9);
    QCOMPARE(tree.max(), 1);

    AvlTree<int, std::greater<int>> balanced;
    for (int value : ascending(500))
        balanced.insert(value);
    QVERIFY(balanced.isAvlBalanced());

    std::vector<int> descending = ascending(500);
    std::reverse(descending.begin(), descending.end());
    QVERIFY((balanced.toInorder() == descending));
}

void TestOrdered::plainTreeSurvivesDeepChain()
{
    // Sorted inserts into a plain search tree produce a chain by design. Doing
    // that through insert() costs O(n^2) ancestor bookkeeping, so the same
    // 100k-node shape is assembled directly and adopted through setRoot():
    // refresh(), size(), height(), the traversals and teardown must all be
    // independent of the chain's depth.
    constexpr int chainLength = 100000;
    using node_type = BinarySearchTree<int>::node_type;

    auto root = std::make_unique<node_type>(0);
    node_type* tail = root.get();
    for (int value = 1; value < chainLength; ++value) {
        tail->right = std::make_unique<node_type>(value);
        tail->right->parent = tail;
        tail = tail->right.get();
    }

    BinarySearchTree<int> tree;
    tree.setRoot(std::move(root));
    QCOMPARE(tree.size(), std::size_t(chainLength));
    QCOMPARE(tree.height(), chainLength);
    QVERIFY(tree.isValidOrder());
    QCOMPARE(tree.selectInorder(std::size_t(chainLength - 1))->value, chainLength - 1);
    QCOMPARE(tree.min(), 0);
    QCOMPARE(tree.max(), chainLength - 1);

    const std::vector<int> inorder = tree.toInorder();
    QCOMPARE(inorder.size(), std::size_t(chainLength));
    QCOMPARE(inorder.front(), 0);
    QCOMPARE(inorder.back(), chainLength - 1);

    // The deep copy is built and released iteratively as well.
    const BinarySearchTree<int> copy = tree;
    QCOMPARE(copy.size(), std::size_t(chainLength));
    QCOMPARE(copy.height(), chainLength);
    QVERIFY(copy.equals(tree));

    tree.clear();
    QVERIFY(tree.empty());
    QCOMPARE(tree.size(), std::size_t(0));
}

QTEST_APPLESS_MAIN(TestOrdered)
#include "tst_ordered.moc"
