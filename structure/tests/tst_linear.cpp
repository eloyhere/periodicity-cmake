// Periodicity — linear tree tests.
//
// LinearTree is the sequence container of the catalogue, so the suite is
// ordered the way the structure is used: build, inspect, mutate in the middle,
// then the bulk operations that distinguish a linked chain from a vector.

#include <md/structure/tree/linear_tree.h>

#include <QtTest/QtTest>

#include <type_traits>
#include <utility>
#include <vector>

using md::structure::LinearLink;
using md::structure::LinearNode;
using md::structure::LinearTree;

class TestLinear : public QObject
{
    Q_OBJECT

private slots:
    void aliasNamesTheSameType();
    void pushesAndPops();
    void insertAndEraseAfterNode();
    void reverseKeepsTail();
    void spliceAndAppend();
    void removesValuesAndPredicates();
    void iteratesInOrder();
    void movesWithoutCopying();
};

void TestLinear::aliasNamesTheSameType()
{
    static_assert(std::is_same_v<LinearLink<int>, LinearTree<int>>);
    static_assert(std::is_same_v<LinearNode<int>::value_type, int>);
    static_assert(std::is_move_constructible_v<LinearTree<int>>);
    static_assert(!std::is_copy_constructible_v<LinearTree<int>>);
}

void TestLinear::pushesAndPops()
{
    LinearTree<int> list;
    QVERIFY(list.empty());
    QCOMPARE(list.size(), std::size_t(0));

    QCOMPARE(list.pushBack(1), 1);
    QCOMPARE(list.pushFront(0), 0);
    QCOMPARE(list.pushBack(2), 2);

    QCOMPARE(list.size(), std::size_t(3));
    QCOMPARE(list.front(), 0);
    QCOMPARE(list.back(), 2);
    QCOMPARE(list.head()->value, 0);
    QCOMPARE(list.tail()->value, 2);

    list.popFront();
    QCOMPARE(list.front(), 1);
    QCOMPARE(list.size(), std::size_t(2));

    list.popBack();
    QCOMPARE(list.back(), 1);
    QCOMPARE(list.size(), std::size_t(1));

    list.popBack();
    QVERIFY(list.empty());
    QCOMPARE(list.head(), nullptr);
    QCOMPARE(list.tail(), nullptr);
}

void TestLinear::insertAndEraseAfterNode()
{
    LinearTree<int> list;
    list.pushBack(1);
    list.pushBack(3);

    QCOMPARE(list.insertAfter(list.head(), 2), 2);
    QCOMPARE(list.size(), std::size_t(3));
    QCOMPARE(list.back(), 3);

    // Inserting after the tail moves the cached tail.
    QCOMPARE(list.insertAfter(list.tail(), 4), 4);
    QCOMPARE(list.back(), 4);

    // Nothing follows the tail, so erasing after it reports false.
    QVERIFY(!list.eraseAfter(list.tail()));

    // Erasing after the head drops the element that follows it.
    QVERIFY(list.eraseAfter(list.head()));
    QCOMPARE(list.size(), std::size_t(3));
    QCOMPARE(list.head()->next->value, 3);
}

void TestLinear::reverseKeepsTail()
{
    LinearTree<int> list;
    for (int value = 0; value < 5; ++value)
        list.pushBack(value);

    list.reverse();
    QCOMPARE(list.front(), 4);
    QCOMPARE(list.back(), 0);
    QCOMPARE(list.size(), std::size_t(5));

    std::vector<int> seen;
    list.forEach([&seen](int value) { seen.push_back(value); });
    QVERIFY((seen == std::vector<int>{4, 3, 2, 1, 0}));

    // Pushing after a reverse must keep the invariant intact.
    list.pushBack(9);
    QCOMPARE(list.back(), 9);
    QCOMPARE(list.size(), std::size_t(6));

    // Reversing a single-element chain is a no-op.
    LinearTree<int> single;
    single.pushBack(1);
    single.reverse();
    QCOMPARE(single.front(), 1);
    QCOMPARE(single.back(), 1);
}

void TestLinear::spliceAndAppend()
{
    LinearTree<int> first;
    first.pushBack(1);
    first.pushBack(4);

    LinearTree<int> second;
    second.pushBack(2);
    second.pushBack(3);

    first.spliceAfter(first.head(), std::move(second));
    QCOMPARE(first.size(), std::size_t(4));
    QVERIFY(second.empty());

    std::vector<int> seen;
    first.forEach([&seen](int value) { seen.push_back(value); });
    QVERIFY((seen == std::vector<int>{1, 2, 3, 4}));
    QCOMPARE(first.back(), 4);

    LinearTree<int> tail;
    tail.pushBack(5);
    first.append(std::move(tail));
    QCOMPARE(first.back(), 5);
    QCOMPARE(first.size(), std::size_t(5));

    // Splicing an empty list changes nothing.
    LinearTree<int> empty;
    first.append(std::move(empty));
    QCOMPARE(first.size(), std::size_t(5));
}

void TestLinear::removesValuesAndPredicates()
{
    LinearTree<int> list;
    for (int value = 0; value < 6; ++value)
        list.pushBack(value);

    QVERIFY(list.remove(0));       // head
    QVERIFY(list.remove(5));       // tail
    QVERIFY(!list.remove(42));     // absent
    QCOMPARE(list.size(), std::size_t(4));

    std::vector<int> seen;
    list.forEach([&seen](int value) { seen.push_back(value); });
    QVERIFY((seen == std::vector<int>{1, 2, 3, 4}));

    const auto removed = list.removeIf([](int value) { return value % 2 == 0; });
    QCOMPARE(removed, std::size_t(2));
    QCOMPARE(list.size(), std::size_t(2));
    QCOMPARE(list.front(), 1);
    QCOMPARE(list.back(), 3);

    QCOMPARE(list.find(3)->value, 3);
    QCOMPARE(list.find(4), nullptr);
    QVERIFY(list.contains(1));

    list.clear();
    QVERIFY(list.empty());
    QCOMPARE(list.size(), std::size_t(0));
}

void TestLinear::iteratesInOrder()
{
    LinearTree<int> list;
    QCOMPARE(list.begin(), list.end());

    for (int value : {10, 20, 30})
        list.pushBack(value);

    std::vector<int> seen;
    for (int value : list)
        seen.push_back(value);
    QVERIFY((seen == std::vector<int>{10, 20, 30}));

    const LinearTree<int>& constant = list;
    int total = 0;
    for (int value : constant)
        total += value;
    QCOMPARE(total, 60);

    auto found = list.findIf([](int value) { return value > 15; });
    QVERIFY(found != nullptr);
    QCOMPARE(found->value, 20);
}

void TestLinear::movesWithoutCopying()
{
    LinearTree<int> source;
    source.pushBack(1);
    source.pushBack(2);

    LinearTree<int> moved = std::move(source);
    QCOMPARE(moved.size(), std::size_t(2));
    QCOMPARE(moved.back(), 2);

    // A moved-from list is empty and usable again.
    QVERIFY(source.empty());
    source.pushBack(3);
    QCOMPARE(source.size(), std::size_t(1));

    LinearTree<int> assigned;
    assigned = std::move(moved);
    QCOMPARE(assigned.size(), std::size_t(2));
    QVERIFY(moved.empty());
}

QTEST_APPLESS_MAIN(TestLinear)
#include "tst_linear.moc"
