// Periodicity — heap and trie tests.
//
// Both structures are implicit trees: the heap is an array with a shape
// contract, the trie is a node per symbol. The suites check the contracts
// rather than the layout, except where the layout is the contract (heap order,
// prefix counts).

#include <md/structure/tree/heap.h>
#include <md/structure/tree/trie.h>

#include <QtTest/QtTest>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

using md::structure::BinaryHeap;
using md::structure::Trie;

class TestHeapTrie : public QObject
{
    Q_OBJECT

private slots:
    void heapOrdersItsElements();
    void heapBuildsInLinearTime();
    void heapSupportsMaxOrder();
    void trieInsertsAndFinds();
    void trieCountsPrefixes();
    void trieErasesAndPrunes();
    void trieRejectsForeignSymbols();
    void trieCopies();
};

void TestHeapTrie::heapOrdersItsElements()
{
    BinaryHeap<int> heap;
    QVERIFY(heap.empty());

    for (int value : {5, 3, 9, 1, 7, 2, 8, 6, 4, 0})
        heap.push(value);

    QCOMPARE(heap.size(), std::size_t(10));
    QCOMPARE(heap.top(), 0);
    QVERIFY(heap.isValidHeap());

    std::vector<int> drained;
    while (!heap.empty()) {
        drained.push_back(heap.top());
        heap.pop();
    }
    QVERIFY((drained == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));

    // The array layout is the complete tree in breadth-first order.
    BinaryHeap<int> layout;
    for (int value : {1, 2, 3, 4, 5})
        layout.push(value);
    QCOMPARE(layout.data().size(), std::size_t(5));
    QCOMPARE(layout.data().front(), 1);
}

void TestHeapTrie::heapBuildsInLinearTime()
{
    BinaryHeap<int> heap = BinaryHeap<int>::from({9, 4, 7, 1, 8, 2, 6, 3, 5, 0});
    QCOMPARE(heap.size(), std::size_t(10));
    QCOMPARE(heap.top(), 0);
    QVERIFY(heap.isValidHeap());

    const std::vector<int> sorted = heap.sortedValues();
    QVERIFY((sorted == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
    QVERIFY(heap.empty());
}

void TestHeapTrie::heapSupportsMaxOrder()
{
    BinaryHeap<int, std::greater<int>> heap;
    for (int value : {5, 3, 9, 1, 7})
        heap.push(value);

    QCOMPARE(heap.top(), 9);
    QVERIFY(heap.isValidHeap());

    heap.replaceTop(0);
    QCOMPARE(heap.top(), 7);
    QCOMPARE(heap.size(), std::size_t(5));

    heap.pop();
    QCOMPARE(heap.top(), 5);
    QVERIFY(heap.isValidHeap());
}

void TestHeapTrie::trieInsertsAndFinds()
{
    Trie<int> trie;
    QVERIFY(trie.empty());

    QVERIFY(trie.insert("cat", 1));
    QVERIFY(trie.insert("car", 2));
    QVERIFY(trie.insert("card", 3));
    QVERIFY(trie.insert("dog", 4));

    QCOMPARE(trie.size(), std::size_t(4));
    QVERIFY(trie.contains("cat"));
    QVERIFY(!trie.contains("ca"));
    QVERIFY(trie.contains("car"));
    QCOMPARE(*trie.find("card"), 3);
    QCOMPARE(trie.find("ca"), nullptr);

    // Reinverting an existing key updates it without growing the key count.
    QVERIFY(!trie.insert("cat", 9));
    QCOMPARE(trie.size(), std::size_t(4));
    QCOMPARE(*trie.find("cat"), 9);

    // The empty key is a key like any other.
    QVERIFY(trie.insert("", 0));
    QCOMPARE(trie.size(), std::size_t(5));
    QVERIFY(trie.contains(""));
}

void TestHeapTrie::trieCountsPrefixes()
{
    Trie<int> trie;
    for (const auto& [key, value] : std::vector<std::pair<const char*, int>>{
             {"car", 1}, {"card", 2}, {"care", 3}, {"cart", 4}, {"cat", 5}, {"dog", 6}})
        trie.insert(key, value);

    QCOMPARE(trie.countWithPrefix("car"), std::size_t(4));
    QCOMPARE(trie.countWithPrefix("cart"), std::size_t(1));
    QCOMPARE(trie.countWithPrefix("ca"), std::size_t(5));
    QCOMPARE(trie.countWithPrefix("d"), std::size_t(1));
    QCOMPARE(trie.countWithPrefix("z"), std::size_t(0));

    std::vector<std::string> keys;
    trie.forEachWithPrefix("car", [&keys](std::string_view key, int) {
        keys.emplace_back(key);
    });
    QVERIFY((keys == std::vector<std::string>{"car", "card", "care", "cart"}));

    std::vector<std::string> all;
    trie.forEach([&all](std::string_view key, int) { all.emplace_back(key); });
    QCOMPARE(all.size(), std::size_t(6));
}

void TestHeapTrie::trieErasesAndPrunes()
{
    Trie<int> trie;
    trie.insert("car", 1);
    trie.insert("card", 2);
    trie.insert("cat", 3);

    const std::size_t before = trie.nodeCount();

    // Erasing the only key through a branch prunes the branch.
    QVERIFY(trie.erase("cat"));
    QVERIFY(!trie.erase("cat"));
    QCOMPARE(trie.size(), std::size_t(2));
    QVERIFY(trie.nodeCount() < before);
    QVERIFY(!trie.contains("cat"));
    QVERIFY(trie.contains("car"));

    // Erasing a prefix key keeps the longer key that shares the node.
    QVERIFY(trie.erase("car"));
    QVERIFY(!trie.contains("car"));
    QVERIFY(trie.contains("card"));
    QCOMPARE(trie.size(), std::size_t(1));

    QVERIFY(trie.erase("card"));
    QVERIFY(trie.empty());
    QCOMPARE(trie.nodeCount(), std::size_t(0));
}

void TestHeapTrie::trieRejectsForeignSymbols()
{
    Trie<int> trie;
    QVERIFY(!trie.insert("Cat", 1));   // upper case is outside the alphabet
    QVERIFY(!trie.insert("c4t", 2));   // digits too
    QVERIFY(trie.empty());

    QVERIFY(trie.insert("cat", 3));
    QVERIFY(!trie.contains("Cat"));
    QCOMPARE(trie.countWithPrefix("C"), std::size_t(0));
    QVERIFY(!trie.erase("cAt"));
}

void TestHeapTrie::trieCopies()
{
    Trie<int> original;
    original.insert("alpha", 1);
    original.insert("beta", 2);

    Trie<int> copy = original;
    QCOMPARE(copy.size(), std::size_t(2));
    QCOMPARE(*copy.find("alpha"), 1);

    copy.insert("gamma", 3);
    QCOMPARE(copy.size(), std::size_t(3));
    QCOMPARE(original.size(), std::size_t(2));

    copy.clear();
    QVERIFY(copy.empty());
    QCOMPARE(copy.nodeCount(), std::size_t(0));
    QCOMPARE(original.size(), std::size_t(2));
}

QTEST_APPLESS_MAIN(TestHeapTrie)
#include "tst_heap_trie.moc"
