// Periodicity — compound structure tests: multiway tree, segment tree, skip
// list.
//
// These three share the property that their shape is a contract rather than an
// accident: arity is fixed for the multiway tree, the layout is implicit for
// the segment tree, and the level structure is probabilistic for the skip list.
// Each suite checks the contract.

#include <md/structure/skip/skip_list.h>
#include <md/structure/tree/multi_tree.h>
#include <md/structure/tree/segment_tree.h>

#include <QtTest/QtTest>

#include <string>
#include <vector>

using md::structure::MultiTree;
using md::structure::SegmentTree;
using md::structure::SkipList;

class TestCompound : public QObject
{
    Q_OBJECT

private slots:
    void multiTreeKeepsArity();
    void multiTreeEditsChildren();
    void multiTreeRejectsFullParent();
    void multiTreeSurvivesDeepChain();
    void multiTreeCopies();
    void segmentTreeAnswersRanges();
    void segmentTreeUpdatesAndRebuilds();
    void segmentTreeKeepsOrder();
    void skipListOrdersAndFinds();
    void skipListErases();
    void skipListStaysShallow();
    void skipListCopies();
};

void TestCompound::multiTreeKeepsArity()
{
    static_assert(MultiTree<int, 3>::arity == 3);

    MultiTree<int, 3> tree;
    QVERIFY(tree.empty());

    auto& root = tree.insertRoot(1);
    QVERIFY(root.isLeaf());
    QVERIFY(!root.isFull());

    auto* childA = tree.appendChild(&root, 2);
    QVERIFY(childA != nullptr);
    tree.appendChild(&root, 3);
    tree.appendChild(&root, 4);
    tree.appendChild(childA, 5);
    tree.appendChild(childA, 6);

    QVERIFY(root.isFull());
    QCOMPARE(root.childCount, std::size_t(3));
    QCOMPARE(root.child(0)->value, 2);
    QCOMPARE(root.child(2)->value, 4);
    QCOMPARE(root.child(3), nullptr);

    QCOMPARE(tree.size(), std::size_t(6));
    QCOMPARE(tree.height(), 3);
    QCOMPARE(tree.countLeaves(), std::size_t(4));
    QVERIFY(!tree.isSaturated());

    QVERIFY((tree.toPreorder() == std::vector<int>{1, 2, 5, 6, 3, 4}));
}

void TestCompound::multiTreeEditsChildren()
{
    MultiTree<int, 3> tree;
    auto& root = tree.insertRoot(1);
    tree.appendChild(&root, 2);
    tree.appendChild(&root, 3);
    tree.appendChild(&root, 4);

    // Removing shifts later children down, so the array stays packed.
    auto removed = tree.removeChild(&root, 1);
    QVERIFY(removed != nullptr);
    QCOMPARE(removed->value, 3);
    QCOMPARE(root.childCount, std::size_t(2));
    QCOMPARE(root.child(0)->value, 2);
    QCOMPARE(root.child(1)->value, 4);
    QCOMPARE(root.child(2), nullptr);

    QCOMPARE(tree.size(), std::size_t(3));
    QVERIFY(tree.removeChild(&root, 5) == nullptr);

    // A saturated tree is one where every internal node uses its full arity.
    MultiTree<int, 2> binary;
    auto& binaryRoot = binary.insertRoot(1);
    binary.appendChild(&binaryRoot, 2);
    binary.appendChild(&binaryRoot, 3);
    QVERIFY(binary.isSaturated());
}

void TestCompound::multiTreeRejectsFullParent()
{
    MultiTree<int, 2> tree;
    auto& root = tree.insertRoot(1);

    auto* left = tree.appendChild(&root, 2);
    auto* right = tree.appendChild(&root, 3);
    QVERIFY(left != nullptr);
    QVERIFY(right != nullptr);
    QVERIFY(root.isFull());

    // A full parent is a recoverable failure: the rejected value is destroyed,
    // the tree is unchanged and no reference into freed storage is returned.
    QVERIFY(tree.appendChild(&root, 4) == nullptr);
    QCOMPARE(root.childCount, std::size_t(2));
    QCOMPARE(tree.size(), std::size_t(3));
    QVERIFY((tree.toPreorder() == std::vector<int>{1, 2, 3}));

    // Capacity is per node, so a child with a free slot still accepts.
    auto* grandchild = tree.appendChild(left, 4);
    QVERIFY(grandchild != nullptr);
    QCOMPARE(grandchild->value, 4);
    QCOMPARE(tree.size(), std::size_t(4));
}

void TestCompound::multiTreeSurvivesDeepChain()
{
    // A chain of single children is a legal shape at any depth: the
    // measurements, traversals, copy and the teardown at scope exit must all be
    // independent of that depth.
    constexpr int chainLength = 100000;

    MultiTree<int, 2> chain;
    auto& root = chain.insertRoot(0);
    MultiTree<int, 2>::node_type* parent = &root;
    for (int value = 1; value < chainLength; ++value)
        parent = chain.appendChild(parent, value);

    QCOMPARE(chain.size(), std::size_t(chainLength));
    QCOMPARE(chain.height(), chainLength);
    QCOMPARE(chain.countLeaves(), std::size_t(1));
    QVERIFY(!chain.isSaturated());

    const std::vector<int> preorder = chain.toPreorder();
    QCOMPARE(preorder.size(), std::size_t(chainLength));
    QCOMPARE(preorder.front(), 0);
    QCOMPARE(preorder.back(), chainLength - 1);

    std::vector<int> levels;
    chain.forEachLevelOrder([&levels](int value) { levels.push_back(value); });
    QCOMPARE(levels.size(), std::size_t(chainLength));
    QCOMPARE(levels.front(), 0);

    // The copy walks the shape iteratively and is released iteratively too.
    const MultiTree<int, 2> copy = chain;
    QCOMPARE(copy.size(), std::size_t(chainLength));
    QVERIFY(copy.equals(chain));
}

void TestCompound::multiTreeCopies()
{
    MultiTree<int, 3> tree;
    auto& root = tree.insertRoot(1);
    tree.appendChild(&root, 2);

    MultiTree<int, 3> copy = tree;
    QVERIFY(copy.equals(tree));
    QVERIFY(copy.root() != tree.root());

    copy.appendChild(copy.root(), 3);
    QCOMPARE(copy.size(), std::size_t(3));
    QCOMPARE(tree.size(), std::size_t(2));
    QVERIFY(!copy.equals(tree));
}

void TestCompound::segmentTreeAnswersRanges()
{
    const SegmentTree<int> tree({5, 2, 8, 1, 9, 3, 7, 4}, 0);

    QCOMPARE(tree.size(), std::size_t(8));
    QCOMPARE(tree.capacity(), std::size_t(8));
    QCOMPARE(tree.queryAll(), 39);
    QCOMPARE(tree.query(0, 8), 39);
    QCOMPARE(tree.query(2, 5), 18);
    QCOMPARE(tree.query(0, 1), 5);
    QCOMPARE(tree.query(3, 3), 0); // empty range yields the identity
    QCOMPARE(tree.at(4), 9);
}

void TestCompound::segmentTreeUpdatesAndRebuilds()
{
    SegmentTree<int> tree({1, 2, 3, 4}, 0);
    QCOMPARE(tree.queryAll(), 10);

    tree.set(0, 10);
    QCOMPARE(tree.queryAll(), 19);
    QCOMPARE(tree.query(0, 2), 12);
    QCOMPARE(tree.at(0), 10);

    tree.assign({5, 5, 5});
    QCOMPARE(tree.size(), std::size_t(3));
    QCOMPARE(tree.queryAll(), 15);
    QCOMPARE(tree.query(1, 3), 10);

    // A non-power-of-two array pads with the identity, not with zeros that
    // could leak into a query.
    SegmentTree<int> padded({2, 4, 6}, 0);
    QCOMPARE(padded.capacity(), std::size_t(4));
    QCOMPARE(padded.query(0, 3), 12);
    QCOMPARE(padded.query(2, 3), 6);

    padded.clear();
    QVERIFY(padded.empty());
    QCOMPARE(padded.queryAll(), 0);
}

void TestCompound::segmentTreeKeepsOrder()
{
    // Concatenation is associative but not commutative, so a segment tree that
    // joined its partial results in the wrong order would be caught here.
    SegmentTree<std::string> words({"a", "b", "c", "d", "e"}, std::string{});
    QCOMPARE(words.query(0, 5), "abcde");
    QCOMPARE(words.query(1, 4), "bcd");
    QCOMPARE(words.query(3, 5), "de");
    QCOMPARE(words.query(2, 2), "");

    words.set(2, "X");
    QCOMPARE(words.query(0, 5), "abXde");
}

void TestCompound::skipListOrdersAndFinds()
{
    SkipList<int> list;
    QVERIFY(list.empty());
    QCOMPARE(list.height(), std::size_t(0));

    for (int value : {5, 3, 9, 1, 7, 2, 8, 6, 4, 0})
        QVERIFY(list.insert(value));

    QVERIFY(!list.insert(5)); // duplicate
    QCOMPARE(list.size(), std::size_t(10));
    QVERIFY(list.isValidList());
    QVERIFY((list.toVector() == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));

    QVERIFY(list.contains(7));
    QVERIFY(!list.contains(11));
    QCOMPARE(list.find(7)->value, 7);
    QCOMPARE(list.find(11), nullptr);
    QCOMPARE(list.first()->value, 0);

    QCOMPARE(list.lowerBound(6)->value, 6);
    QCOMPARE(list.lowerBound(10), nullptr);
}

void TestCompound::skipListErases()
{
    SkipList<int> list;
    for (int value = 0; value < 12; ++value)
        list.insert(value);
    QVERIFY(list.isValidList());

    QVERIFY(list.erase(0));
    QVERIFY(list.erase(11));
    QVERIFY(!list.erase(11));
    QCOMPARE(list.size(), std::size_t(10));
    QVERIFY(list.isValidList());
    QVERIFY((list.toVector() == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));

    // Erasing everything collapses the level structure back to empty.
    for (int value = 1; value <= 10; ++value)
        QVERIFY(list.erase(value));
    QVERIFY(list.empty());
    QCOMPARE(list.height(), std::size_t(0));
    QCOMPARE(list.first(), nullptr);
    QVERIFY(list.isValidList());
}

void TestCompound::skipListStaysShallow()
{
    SkipList<int> list;
    for (int value = 0; value < 2000; ++value)
        list.insert(value);

    QCOMPARE(list.size(), std::size_t(2000));
    QVERIFY(list.isValidList());

    // Caps at maximumLevel, and in practice sits near log2(2000) ~ 11.
    QVERIFY(list.height() <= SkipList<int>::maximumLevel);
    QVERIFY(list.height() >= 6);

    // A second list with the same seed has the same shape.
    SkipList<int> twin;
    for (int value = 0; value < 2000; ++value)
        twin.insert(value);
    QCOMPARE(twin.height(), list.height());
}

void TestCompound::skipListCopies()
{
    SkipList<int> original;
    for (int value : {3, 1, 4, 1, 5})
        original.insert(value);

    SkipList<int> copy = original;
    QCOMPARE(copy.size(), original.size());
    QVERIFY((copy.toVector() == original.toVector()));

    copy.insert(9);
    QCOMPARE(copy.size(), std::size_t(5));
    QCOMPARE(original.size(), std::size_t(4));

    SkipList<int> assigned;
    assigned = original;
    QVERIFY((assigned.toVector() == original.toVector()));

    original.clear();
    QVERIFY(original.empty());
    QCOMPARE(assigned.size(), std::size_t(4));
}

QTEST_APPLESS_MAIN(TestCompound)
#include "tst_compound.moc"
