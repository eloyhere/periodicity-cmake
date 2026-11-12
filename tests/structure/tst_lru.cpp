// Periodicity — tests for the LRU containers and their building blocks.
//
// The suite covers three layers of the same feature:
//
//   allocator/  BlockAllocator chunks and the ObjectPool lifetime on top
//   lru/        LinkedLRUList and ArrayLRUList as recency order containers
//   lru/        the four public LRU faces (unordered/ordered map/set), their
//               promotion, eviction, capacity, handler and order-policy rules
//
// Everything is deterministic; the randomized test uses a fixed seed and
// checks the size/capacity invariant after every operation.

#include <md/structure/allocator/block_allocator.h>
#include <md/structure/allocator/object_pool.h>
#include <md/structure/lru/array_lru_list.h>
#include <md/structure/lru/linked_lru_list.h>
#include <md/structure/lru/ordered_lru_map.h>
#include <md/structure/lru/ordered_lru_set.h>
#include <md/structure/lru/unordered_lru_map.h>
#include <md/structure/lru/unordered_lru_set.h>

#include <QtTest/QtTest>

#include <QString>
#include <QVariant>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <random>
#include <stdexcept>
#include <vector>

using md::structure::ArrayLRUList;
using md::structure::BlockAllocator;
using md::structure::LinkedLRUList;
using md::structure::ObjectPool;
using md::structure::OrderedLRUMap;
using md::structure::OrderedLRUSet;
using md::structure::UnorderedLRUMap;
using md::structure::UnorderedLRUSet;

namespace {

template <class List>
std::vector<int> valuesOf(const List& list)
{
    std::vector<int> values;
    for (const int& value : list)
        values.push_back(value);
    return values;
}

template <class Map>
std::vector<int> mapKeys(const Map& map)
{
    std::vector<int> keys;
    for (const auto& entry : map)
        keys.push_back(entry.first);
    return keys;
}

template <class Set>
std::vector<int> setValues(const Set& set)
{
    std::vector<int> values;
    for (const int& value : set)
        values.push_back(value);
    return values;
}

// Counts constructions/destructions so ObjectPool can be checked exactly.
struct Tracked
{
    static int constructions;
    static int destructions;

    int value = 0;

    explicit Tracked(int initial) : value(initial) { ++constructions; }
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    ~Tracked() { ++destructions; }
};

int Tracked::constructions = 0;
int Tracked::destructions = 0;

struct Throwing
{
    explicit Throwing(bool doThrow)
    {
        if (doThrow)
            throw std::runtime_error("Throwing");
    }
};

// A value type that can be stored but not copied: the map must move it.
struct MoveOnly
{
    int value = 0;

    explicit MoveOnly(int initial) : value(initial) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;

    MoveOnly(MoveOnly&& other) noexcept : value(other.value) { other.value = -1; }

    MoveOnly& operator=(MoveOnly&& other) noexcept
    {
        value = other.value;
        other.value = -1;
        return *this;
    }
};

} // namespace

class TestLru : public QObject
{
    Q_OBJECT

private slots:
    // Allocator
    void blockAllocatorChunksAndReuse();
    void blockAllocatorReserveAndClear();
    void objectPoolConstructsAndDestroys();
    void objectPoolReusesFreedSlots();
    void objectPoolThrowingConstructorReturnsSlot();
    void objectPoolOwnership();

    // Order containers
    void linkedRecencyAndTouch();
    void linkedPopOldestAndErase();
    void linkedIteratorStability();
    void linkedPoolNoGrowthAfterReserve();
    void linkedClearKeepsPoolWarm();
    void arrayRecencyAndTouch();
    void arraySlotRecycling();
    void arrayPopOldestAndErase();
    void arrayGrowthKeepsHandles();
    void arrayNoGrowthWithRecycling();
    void arrayClearKeepsCapacity();

    // Unordered map
    void unorderedMapPromotesOnAccess();
    void unorderedMapEvictsInOrder();
    void unorderedMapPeekDoesNotPromote();
    void unorderedMapTryGetAndGet();
    void unorderedMapSetCapacity();
    void unorderedMapEraseTakeClear();
    void unorderedMapEvictionHandler();
    void unorderedMapMoveOnlyValues();
    void unorderedMapSupportsQtTypes();
    void unorderedMapCopiesIndependently();

    // Sets
    void unorderedSetMirrorsMap();
    void unorderedSetEvictionHandler();
    void orderedSetIteratesSorted();

    // Ordered map
    void orderedMapIteratesSortedEvictsByRecency();
    void orderedMapCustomCompare();

    // Order policy and stress
    void orderPolicyAcceptsBothLists();
    void randomizedOpsStayWithinCapacity();
};

// ---------------------------------------------------------------------------
// BlockAllocator
// ---------------------------------------------------------------------------

void TestLru::blockAllocatorChunksAndReuse()
{
    BlockAllocator allocator(32, 4);

    QCOMPARE(allocator.blockSize(), std::size_t(32));
    QCOMPARE(allocator.blocksPerChunk(), std::size_t(4));
    QCOMPARE(allocator.chunkCount(), std::size_t(0));
    QCOMPARE(allocator.capacity(), std::size_t(0));
    QCOMPARE(allocator.inUse(), std::size_t(0));

    std::vector<void*> blocks;
    for (int i = 0; i < 10; ++i)
        blocks.push_back(allocator.allocate());

    // Crossing blocksPerChunk boundaries grows the chunk count exactly.
    QCOMPARE(allocator.chunkCount(), std::size_t(3));
    QCOMPARE(allocator.capacity(), std::size_t(12));
    QCOMPARE(allocator.inUse(), std::size_t(10));
    QCOMPARE(allocator.pooled(), std::size_t(2));

    for (void* block : blocks) {
        QVERIFY(allocator.owns(block));
        QVERIFY(reinterpret_cast<std::uintptr_t>(block) % alignof(std::max_align_t) == 0);
    }

    for (void* block : blocks)
        allocator.deallocate(block);

    QCOMPARE(allocator.inUse(), std::size_t(0));
    QCOMPARE(allocator.pooled(), std::size_t(12));
    QCOMPARE(allocator.chunkCount(), std::size_t(3));

    // A freed block is reused before the bump cursor or a new chunk.
    void* reused = allocator.allocate();
    QVERIFY(allocator.owns(reused));
    QCOMPARE(allocator.chunkCount(), std::size_t(3));
    allocator.deallocate(reused);

    // With no live block, shrinkToFit releases every chunk.
    allocator.shrinkToFit();
    QCOMPARE(allocator.chunkCount(), std::size_t(0));
    QCOMPARE(allocator.capacity(), std::size_t(0));
    QCOMPARE(allocator.inUse(), std::size_t(0));
}

void TestLru::blockAllocatorReserveAndClear()
{
    BlockAllocator allocator(16, 8);

    allocator.reserve(20); // 20 / 8 -> three chunks (24 blocks)
    QCOMPARE(allocator.chunkCount(), std::size_t(3));
    QCOMPARE(allocator.capacity(), std::size_t(24));
    QCOMPARE(allocator.inUse(), std::size_t(0));

    std::vector<void*> blocks;
    for (int i = 0; i < 20; ++i)
        blocks.push_back(allocator.allocate());

    QCOMPARE(allocator.chunkCount(), std::size_t(3));
    QCOMPARE(allocator.inUse(), std::size_t(20));

    allocator.reserve(10); // already large enough
    QCOMPARE(allocator.chunkCount(), std::size_t(3));

    for (void* block : blocks)
        allocator.deallocate(block);

    allocator.clear();
    QCOMPARE(allocator.chunkCount(), std::size_t(0));
    QCOMPARE(allocator.capacity(), std::size_t(0));
    QCOMPARE(allocator.inUse(), std::size_t(0));
}

// ---------------------------------------------------------------------------
// ObjectPool
// ---------------------------------------------------------------------------

void TestLru::objectPoolConstructsAndDestroys()
{
    Tracked::constructions = 0;
    Tracked::destructions = 0;

    {
        ObjectPool<Tracked> pool(4);
        Tracked* first = pool.create(7);
        Tracked* second = pool.create(9);

        QVERIFY(first != nullptr);
        QVERIFY(second != nullptr);
        QCOMPARE(first->value, 7);
        QCOMPARE(second->value, 9);
        QCOMPARE(pool.inUse(), std::size_t(2));
        QCOMPARE(Tracked::constructions, 2);

        pool.destroy(first);
        QCOMPARE(Tracked::destructions, 1);
        QCOMPARE(pool.inUse(), std::size_t(1));

        pool.destroy(nullptr);
        QCOMPARE(pool.inUse(), std::size_t(1));

        pool.destroy(second);
        QCOMPARE(Tracked::destructions, 2);
        QCOMPARE(pool.inUse(), std::size_t(0));
    }
}

void TestLru::objectPoolReusesFreedSlots()
{
    ObjectPool<int> pool(4);
    pool.reserve(8);

    const std::size_t chunks = pool.chunkCount();
    QCOMPARE(pool.capacity(), std::size_t(8));

    std::vector<int*> objects;
    for (int i = 0; i < 8; ++i)
        objects.push_back(pool.create(i));

    QCOMPARE(pool.chunkCount(), chunks);
    QCOMPARE(pool.inUse(), std::size_t(8));
    QCOMPARE(pool.pooled(), std::size_t(0));

    for (int* object : objects)
        pool.destroy(object);

    QCOMPARE(pool.inUse(), std::size_t(0));
    QCOMPARE(pool.pooled(), std::size_t(8));

    for (int i = 0; i < 8; ++i)
        objects[static_cast<std::size_t>(i)] = pool.create(i);

    QCOMPARE(pool.chunkCount(), chunks);
    QCOMPARE(pool.inUse(), std::size_t(8));

    for (int* object : objects)
        pool.destroy(object);
}

void TestLru::objectPoolThrowingConstructorReturnsSlot()
{
    ObjectPool<Throwing> pool(4);

    bool caught = false;
    try {
        (void)pool.create(true);
    } catch (const std::runtime_error&) {
        caught = true;
    }

    QVERIFY(caught);
    QCOMPARE(pool.inUse(), std::size_t(0));

    Throwing* object = pool.create(false);
    QVERIFY(object != nullptr);
    QCOMPARE(pool.inUse(), std::size_t(1));
    pool.destroy(object);
    QCOMPARE(pool.inUse(), std::size_t(0));
}

void TestLru::objectPoolOwnership()
{
    ObjectPool<int> pool(4);
    int stackValue = 5;

    QVERIFY(!pool.owns(nullptr));
    QVERIFY(!pool.owns(&stackValue));

    pool.destroy(nullptr);

    int* object = pool.create(5);
    QVERIFY(pool.owns(object));
    QCOMPARE(*object, 5);
    pool.destroy(object);
}

// ---------------------------------------------------------------------------
// LinkedLRUList
// ---------------------------------------------------------------------------

void TestLru::linkedRecencyAndTouch()
{
    LinkedLRUList<int> list(4);
    auto first = list.insert(1);
    auto second = list.insert(2);
    auto third = list.insert(3);

    QCOMPARE(list.size(), std::size_t(3));
    QVERIFY((valuesOf(list) == std::vector<int>{1, 2, 3}));
    QCOMPARE(*first, 1);
    QCOMPARE(*second, 2);
    QCOMPARE(*third, 3);

    list.touch(first);
    QVERIFY((valuesOf(list) == std::vector<int>{2, 3, 1}));

    // Handles keep pointing at their own value after the splice.
    QCOMPARE(*first, 1);
    QCOMPARE(*second, 2);
    QCOMPARE(*third, 3);

    list.touch(list.end()); // no-op
    QVERIFY((valuesOf(list) == std::vector<int>{2, 3, 1}));
}

void TestLru::linkedPopOldestAndErase()
{
    LinkedLRUList<int> list(4);
    auto first = list.insert(10);
    auto second = list.insert(20);
    auto third = list.insert(30);

    auto popped = list.popOldest();
    QVERIFY(popped.has_value());
    QCOMPARE(*popped, 10);
    QCOMPARE(list.size(), std::size_t(2));
    QVERIFY((valuesOf(list) == std::vector<int>{20, 30}));

    list.erase(second);
    QCOMPARE(list.size(), std::size_t(1));
    QVERIFY((valuesOf(list) == std::vector<int>{30}));

    list.erase(list.end()); // no-op
    QCOMPARE(list.size(), std::size_t(1));

    list.erase(third);
    QVERIFY(list.empty());
    QVERIFY(!list.popOldest().has_value());
    (void)first; // erased by popOldest
}

void TestLru::linkedIteratorStability()
{
    LinkedLRUList<int> list(2); // small chunks force pool growth
    auto node10 = list.insert(10);
    auto node20 = list.insert(20);
    auto node30 = list.insert(30);
    list.insert(40);
    list.insert(50);

    auto saved = ++list.begin(); // oldest is 10, so this is 20
    QCOMPARE(*saved, 20);

    list.touch(node10); // 20, 30, 40, 50, 10
    QCOMPARE(*saved, 20);
    QCOMPARE(list.size(), std::size_t(5));

    // Erasing a neighbour keeps the iterator valid and re-links around it.
    list.erase(node30);
    QCOMPARE(*saved, 20);
    ++saved;
    QCOMPARE(*saved, 40);

    QCOMPARE(*node20, 20);
    list.erase(node20);
    QCOMPARE(list.size(), std::size_t(3));
}

void TestLru::linkedPoolNoGrowthAfterReserve()
{
    LinkedLRUList<int> list(4);
    list.reserve(64);

    const std::size_t chunks = list.chunkCount();
    QCOMPARE(list.capacity(), std::size_t(64));

    for (int i = 0; i < 64; ++i)
        (void)list.insert(i);

    QCOMPARE(list.size(), std::size_t(64));
    QCOMPARE(list.chunkCount(), chunks);
    QCOMPARE(list.pooledNodes(), std::size_t(0));

    for (int i = 0; i < 32; ++i)
        list.erase(list.begin());

    QCOMPARE(list.size(), std::size_t(32));
    QCOMPARE(list.pooledNodes(), std::size_t(32));
    QCOMPARE(list.chunkCount(), chunks);

    for (int i = 0; i < 32; ++i)
        (void)list.insert(100 + i);

    QCOMPARE(list.size(), std::size_t(64));
    QCOMPARE(list.chunkCount(), chunks);
    QCOMPARE(list.pooledNodes(), std::size_t(0));
}

void TestLru::linkedClearKeepsPoolWarm()
{
    LinkedLRUList<int> list(4);
    for (int i = 0; i < 10; ++i)
        (void)list.insert(i);

    const std::size_t chunks = list.chunkCount();
    const std::size_t capacity = list.capacity();
    QVERIFY(capacity >= 10);

    list.clear();
    QVERIFY(list.empty());
    QCOMPARE(list.size(), std::size_t(0));
    QCOMPARE(list.chunkCount(), chunks);
    QCOMPARE(list.capacity(), capacity);
    QCOMPARE(list.pooledNodes(), capacity);
    QVERIFY((valuesOf(list) == std::vector<int>{}));

    list.shrinkToFit();
    QCOMPARE(list.chunkCount(), std::size_t(0));
    QCOMPARE(list.capacity(), std::size_t(0));
}

// ---------------------------------------------------------------------------
// ArrayLRUList
// ---------------------------------------------------------------------------

void TestLru::arrayRecencyAndTouch()
{
    ArrayLRUList<int> list;
    list.reserve(4);

    const auto first = list.insert(1);
    const auto second = list.insert(2);
    const auto third = list.insert(3);

    QCOMPARE(list.size(), std::size_t(3));
    QVERIFY((valuesOf(list) == std::vector<int>{1, 2, 3}));

    list.touch(first);
    QVERIFY((valuesOf(list) == std::vector<int>{2, 3, 1}));

    list.touch(third); // 2, 3, 1 -> 2, 1, 3
    QVERIFY((valuesOf(list) == std::vector<int>{2, 1, 3}));

    QCOMPARE(list.at(first), 1);
    QCOMPARE(list.at(second), 2);
    QCOMPARE(list.at(third), 3);
}

void TestLru::arraySlotRecycling()
{
    ArrayLRUList<int> list;
    list.reserve(4);

    const auto h0 = list.insert(10);
    const auto h1 = list.insert(20);
    const auto h2 = list.insert(30);
    const auto h3 = list.insert(40);

    QCOMPARE(list.capacity(), std::size_t(4));
    list.erase(h1);
    list.erase(h3);
    QCOMPARE(list.size(), std::size_t(2));
    QVERIFY(!list.isLive(h1));
    QVERIFY(!list.isLive(h3));

    const auto r1 = list.insert(50);
    const auto r2 = list.insert(60);

    // The free list recycles one of the erased slots for each insert.
    QVERIFY(r1 == h1 || r1 == h3);
    QVERIFY(r2 == h1 || r2 == h3);
    QVERIFY(r1 != r2);
    QCOMPARE(list.capacity(), std::size_t(4));

    QVERIFY((valuesOf(list) == std::vector<int>{10, 30, 50, 60}));
    QCOMPARE(list.at(h0), 10);
    QCOMPARE(list.at(h2), 30);
    QCOMPARE(list.at(r1), 50);
    QCOMPARE(list.at(r2), 60);
}

void TestLru::arrayPopOldestAndErase()
{
    ArrayLRUList<int> list;
    list.reserve(3);

    const auto first = list.insert(1);
    const auto second = list.insert(2);
    const auto third = list.insert(3);

    // Recency is 1, 2, 3; popOldest removes 1.
    auto popped = list.popOldest();
    QVERIFY(popped.has_value());
    QCOMPARE(*popped, 1);
    QCOMPARE(list.size(), std::size_t(2));
    QVERIFY((valuesOf(list) == std::vector<int>{2, 3}));

    list.erase(second);
    QCOMPARE(list.size(), std::size_t(1));
    QVERIFY((valuesOf(list) == std::vector<int>{3}));
    QCOMPARE(list.at(third), 3);

    list.erase(third);
    QVERIFY(list.empty());
    QVERIFY(!list.popOldest().has_value());
    (void)first; // erased by popOldest
}

void TestLru::arrayGrowthKeepsHandles()
{
    ArrayLRUList<int> list;
    list.reserve(2);

    const auto h0 = list.insert(1);
    const auto h1 = list.insert(2);
    const auto h2 = list.insert(3); // grows the slot array by one

    QCOMPARE(list.capacity(), std::size_t(3));
    QCOMPARE(h2, std::size_t(2));

    // Handles are indices, so growth does not invalidate them.
    QCOMPARE(list.at(h0), 1);
    QCOMPARE(list.at(h1), 2);
    QCOMPARE(list.at(h2), 3);

    list.touch(h0);
    QVERIFY((valuesOf(list) == std::vector<int>{2, 3, 1}));
}

void TestLru::arrayNoGrowthWithRecycling()
{
    ArrayLRUList<int> list;
    list.reserve(8);
    const std::size_t capacity = list.capacity();

    std::deque<ArrayLRUList<int>::handle> live;
    for (int i = 0; i < 8; ++i)
        live.push_back(list.insert(i));

    for (int round = 0; round < 50; ++round) {
        const auto victim = live.front();
        live.pop_front();
        list.erase(victim);
        QCOMPARE(list.capacity(), capacity);

        live.push_back(list.insert(1000 + round));
        QCOMPARE(list.capacity(), capacity);
    }

    QCOMPARE(list.size(), std::size_t(8));
}

void TestLru::arrayClearKeepsCapacity()
{
    ArrayLRUList<int> list;
    list.reserve(4);
    for (int i = 0; i < 3; ++i)
        (void)list.insert(i);

    const std::size_t capacity = list.capacity();
    list.clear();

    QVERIFY(list.empty());
    QCOMPARE(list.size(), std::size_t(0));
    QCOMPARE(list.capacity(), capacity);
    QVERIFY((valuesOf(list) == std::vector<int>{}));

    const auto handle = list.insert(9);
    QVERIFY(handle < capacity);
    QCOMPARE(list.at(handle), 9);

    list.erase(handle);
    list.shrinkToFit();
    QCOMPARE(list.capacity(), std::size_t(0));
}

// ---------------------------------------------------------------------------
// UnorderedLRUMap
// ---------------------------------------------------------------------------

void TestLru::unorderedMapPromotesOnAccess()
{
    UnorderedLRUMap<int, int> map(2);
    map.put(1, 10);
    map.put(2, 20);

    int value = 0;
    QVERIFY(map.get(1, value));
    QCOMPARE(value, 10);
    QVERIFY(map.tryGet(1, &value));

    map.insert_or_assign(1, 11);
    QCOMPARE(*map.peek(1), 11);

    map.put(3, 30); // evicts 2: the least recently used after 1 was promoted
    QVERIFY(map.contains(1));
    QVERIFY(map.contains(3));
    QVERIFY(!map.contains(2));
    QVERIFY((mapKeys(map) == std::vector<int>{1, 3}));

    // touch() promotes just like get().
    UnorderedLRUMap<int, int> touched(2);
    touched.put(1, 10);
    touched.put(2, 20);
    touched.touch(1);
    touched.put(3, 30);
    QVERIFY(touched.contains(1));
    QVERIFY(!touched.contains(2));
}

void TestLru::unorderedMapEvictsInOrder()
{
    UnorderedLRUMap<int, int> map(3);
    map.put(1, 10);
    map.put(2, 20);
    map.put(3, 30);
    QVERIFY((mapKeys(map) == std::vector<int>{1, 2, 3}));

    map.put(4, 40); // evicts 1
    QVERIFY((mapKeys(map) == std::vector<int>{2, 3, 4}));

    (void)map.find(2); // promote 2 -> recency 3, 4, 2
    map.put(5, 50); // evicts 3
    QVERIFY(!map.contains(3));
    QVERIFY((mapKeys(map) == std::vector<int>{4, 2, 5}));
    QCOMPARE(*map.peek(2), 20);
    QCOMPARE(*map.peek(4), 40);
    QCOMPARE(*map.peek(5), 50);
}

void TestLru::unorderedMapPeekDoesNotPromote()
{
    UnorderedLRUMap<int, int> map(2);
    map.put(1, 10);
    map.put(2, 20);

    const auto* peeked = map.peek(1);
    QVERIFY(peeked != nullptr);
    QCOMPARE(*peeked, 10);

    QVERIFY(map.contains(1)); // contains also does not promote

    map.put(3, 30); // oldest is still 1
    QVERIFY(!map.contains(1));
    QVERIFY(map.contains(2));
    QVERIFY(map.contains(3));
}

void TestLru::unorderedMapTryGetAndGet()
{
    UnorderedLRUMap<int, int> map(3);
    map.put(7, 70);

    int value = 0;
    QVERIFY(map.tryGet(7, &value));
    QCOMPARE(value, 70);

    value = -1;
    QVERIFY(!map.tryGet(8, &value));
    QCOMPARE(value, -1);

    QVERIFY(!map.tryGet(7, nullptr));
    QVERIFY(map.get(7, value));

    value = -1;
    QVERIFY(!map.get(8, value));
    QCOMPARE(value, -1);
}

void TestLru::unorderedMapSetCapacity()
{
    UnorderedLRUMap<int, int> map(5);
    for (int i = 1; i <= 5; ++i)
        map.put(i, i * 10);

    map.setCapacity(2); // evicts oldest first: 1, 2, 3
    QCOMPARE(map.size(), std::size_t(2));
    QVERIFY((mapKeys(map) == std::vector<int>{4, 5}));

    map.setCapacity(4);
    map.put(6, 60);
    map.put(7, 70);
    QCOMPARE(map.size(), std::size_t(4));
    QVERIFY((mapKeys(map) == std::vector<int>{4, 5, 6, 7}));

    map.setCapacity(0); // unbounded
    QCOMPARE(map.capacity(), std::size_t(0));
    for (int i = 0; i < 100; ++i)
        map.put(100 + i, i);
    QCOMPARE(map.size(), std::size_t(104));

    map.setCapacity(3); // evicts everything except the newest three
    QCOMPARE(map.size(), std::size_t(3));
    QVERIFY(map.contains(197));
    QVERIFY(map.contains(198));
    QVERIFY(map.contains(199));
    QVERIFY(!map.contains(196));
}

void TestLru::unorderedMapEraseTakeClear()
{
    UnorderedLRUMap<int, int> map(3);
    map.put(1, 10);
    map.put(2, 20);
    map.put(3, 30);

    QVERIFY(map.erase(2));
    QVERIFY(!map.erase(2));
    QVERIFY((mapKeys(map) == std::vector<int>{1, 3}));

    auto taken = map.take(1);
    QVERIFY(taken.has_value());
    QCOMPARE(*taken, 10);
    QVERIFY(!map.contains(1));
    QVERIFY(!map.take(42).has_value());
    QVERIFY((mapKeys(map) == std::vector<int>{3}));

    map.clear();
    QVERIFY(map.empty());
    QCOMPARE(map.size(), std::size_t(0));

    map.put(4, 40);
    QVERIFY(map.contains(4));
    QCOMPARE(map.size(), std::size_t(1));
}

void TestLru::unorderedMapEvictionHandler()
{
    UnorderedLRUMap<int, int> map(2);
    std::vector<int> evicted;
    bool removedBeforeCallback = true;
    int sizeInsideCallback = -1;

    map.setEvictionHandler([&](const int& key, const int& value) {
        evicted.push_back(key);
        removedBeforeCallback = removedBeforeCallback && !map.contains(key);
        sizeInsideCallback = static_cast<int>(map.size());
        (void)value;
    });

    map.put(1, 10);
    map.put(2, 20);
    QVERIFY(evicted.empty());

    map.put(3, 30);
    QVERIFY((evicted == std::vector<int>{1}));
    QVERIFY(removedBeforeCallback);
    QCOMPARE(sizeInsideCallback, 1); // the victim was already removed

    map.setCapacity(1); // evicts 2
    QVERIFY((evicted == std::vector<int>{1, 2}));

    // Explicit erase/take/clear never call the handler.
    map.clear();
    QVERIFY((evicted == std::vector<int>{1, 2}));
    map.put(4, 40);
    QVERIFY(map.erase(4));
    QVERIFY((evicted == std::vector<int>{1, 2}));

    // Destruction never calls the handler either.
    int calls = 0;
    {
        UnorderedLRUMap<int, int> scoped(1);
        scoped.setEvictionHandler([&](const int&, const int&) { ++calls; });
        scoped.put(1, 1);
        scoped.put(2, 2);
        QCOMPARE(calls, 1);
    }
    QCOMPARE(calls, 1);
}

void TestLru::unorderedMapMoveOnlyValues()
{
    UnorderedLRUMap<int, MoveOnly> map(2);
    map.put(1, MoveOnly(10));
    map.put(2, MoveOnly(20));

    MoveOnly* found = map.find(1);
    QVERIFY(found != nullptr);
    QCOMPARE(found->value, 10);

    map.put(3, MoveOnly(30)); // evicts 2
    QVERIFY(!map.contains(2));
    QVERIFY(map.contains(1));

    auto taken = map.take(3);
    QVERIFY(taken.has_value());
    QCOMPARE(taken->value, 30);

    QVERIFY(map.take(1).has_value());

    // Eviction still hands the value to the handler.
    UnorderedLRUMap<int, MoveOnly> observed(1);
    int evictedValue = 0;
    observed.setEvictionHandler([&](const int&, const MoveOnly& value) {
        evictedValue = value.value;
    });
    observed.put(1, MoveOnly(11));
    observed.put(2, MoveOnly(22));
    QCOMPARE(evictedValue, 11);
    QVERIFY(observed.contains(2));
}

void TestLru::unorderedMapSupportsQtTypes()
{
    UnorderedLRUMap<QString, QVariant> cache(2);
    cache.put(QStringLiteral("one"), QVariant(1));
    cache.put(QStringLiteral("two"), QVariant(QStringLiteral("second")));

    QVariant value;
    QVERIFY(cache.get(QStringLiteral("one"), value));
    QCOMPARE(value.toInt(), 1);

    const QVariant* peeked = cache.peek(QStringLiteral("two"));
    QVERIFY(peeked != nullptr);
    QCOMPARE(peeked->toString(), QStringLiteral("second"));

    cache.put(QStringLiteral("three"), QVariant(3)); // evicts "two"
    QVERIFY(!cache.contains(QStringLiteral("two")));
    QVERIFY(cache.contains(QStringLiteral("one")));
    QVERIFY(cache.contains(QStringLiteral("three")));
    QCOMPARE(cache.keys().size(), std::size_t(2));
}

void TestLru::unorderedMapCopiesIndependently()
{
    UnorderedLRUMap<int, int> original(3);
    original.put(1, 10);
    original.put(2, 20);
    original.put(3, 30);
    (void)original.find(1); // recency 2, 3, 1

    UnorderedLRUMap<int, int> copy(original);
    QVERIFY((mapKeys(copy) == std::vector<int>{2, 3, 1}));
    QCOMPARE(copy.size(), original.size());

    copy.put(4, 40); // evicts 2 in the copy only
    QVERIFY(!copy.contains(2));
    QVERIFY(original.contains(2));
    QVERIFY((mapKeys(copy) == std::vector<int>{3, 1, 4}));
}

// ---------------------------------------------------------------------------
// Sets
// ---------------------------------------------------------------------------

void TestLru::unorderedSetMirrorsMap()
{
    UnorderedLRUSet<int> set(2);
    QVERIFY(set.insert(1));
    QVERIFY(!set.insert(1)); // promotes
    QVERIFY(set.insert(2));
    QCOMPARE(set.size(), std::size_t(2));

    set.touch(1); // recency 2, 1
    QVERIFY(set.insert(3)); // evicts 2
    QVERIFY(set.contains(1));
    QVERIFY(!set.contains(2));
    QVERIFY((setValues(set) == std::vector<int>{1, 3}));
    QVERIFY((set.keys() == std::vector<int>{1, 3}));

    QVERIFY(set.erase(1));
    QVERIFY(!set.erase(1));
    QVERIFY((setValues(set) == std::vector<int>{3}));

    set.clear();
    QVERIFY(set.empty());
    QCOMPARE(set.size(), std::size_t(0));

    // capacity 0 is unbounded.
    UnorderedLRUSet<int> unbounded(0);
    for (int i = 0; i < 500; ++i)
        (void)unbounded.insert(i);
    QCOMPARE(unbounded.size(), std::size_t(500));
    unbounded.setCapacity(2);
    QCOMPARE(unbounded.size(), std::size_t(2));
    QVERIFY(unbounded.contains(498));
    QVERIFY(unbounded.contains(499));
}

void TestLru::unorderedSetEvictionHandler()
{
    UnorderedLRUSet<int> set(2);
    std::vector<int> evicted;
    set.setEvictionHandler([&](const int& key) { evicted.push_back(key); });

    set.insert(1);
    set.insert(2);
    set.insert(3);
    QVERIFY((evicted == std::vector<int>{1}));

    set.setCapacity(1); // evicts 2
    QVERIFY((evicted == std::vector<int>{1, 2}));

    set.clear();
    QVERIFY((evicted == std::vector<int>{1, 2}));

    set.setEvictionHandler(nullptr);
    set.insert(9);
    QVERIFY((evicted == std::vector<int>{1, 2}));
}

void TestLru::orderedSetIteratesSorted()
{
    OrderedLRUSet<int> set(3);
    QVERIFY(set.insert(3));
    QVERIFY(set.insert(1));
    QVERIFY(set.insert(2));

    QVERIFY((setValues(set) == std::vector<int>{1, 2, 3}));

    set.touch(3);
    QVERIFY(set.insert(4)); // evicts the oldest key, 1
    QVERIFY(!set.contains(1));
    QVERIFY((setValues(set) == std::vector<int>{2, 3, 4}));
    QVERIFY((set.keys() == std::vector<int>{2, 3, 4}));

    QVERIFY(set.erase(3));
    QVERIFY((setValues(set) == std::vector<int>{2, 4}));
    QCOMPARE(*set.begin(), 2);
}

// ---------------------------------------------------------------------------
// OrderedLRUMap
// ---------------------------------------------------------------------------

void TestLru::orderedMapIteratesSortedEvictsByRecency()
{
    OrderedLRUMap<int, int> map(3);
    map.put(3, 30);
    map.put(1, 10);
    map.put(2, 20);

    QVERIFY((mapKeys(map) == std::vector<int>{1, 2, 3}));
    QCOMPARE(map.begin()->first, 1);
    QCOMPARE(map.begin()->second, 10);

    auto last = map.end();
    --last;
    QCOMPARE(last->first, 3);
    QCOMPARE(last->second, 30);

    std::vector<int> visitedKeys;
    std::vector<int> visitedValues;
    map.forEach([&](const int& key, const int& value) {
        visitedKeys.push_back(key);
        visitedValues.push_back(value);
    });
    QVERIFY((visitedKeys == std::vector<int>{1, 2, 3}));
    QVERIFY((visitedValues == std::vector<int>{10, 20, 30}));

    int value = 0;
    QVERIFY(map.get(3, value)); // touch 3
    map.put(4, 40);             // evicts 1, the least recently used
    QVERIFY(!map.contains(1));
    QVERIFY((mapKeys(map) == std::vector<int>{2, 3, 4}));
    QVERIFY((map.keys() == std::vector<int>{2, 3, 4}));

    (void)map.find(2); // recency becomes 3, 4, 2
    QVERIFY((map.keys() == std::vector<int>{3, 4, 2}));
    QVERIFY((mapKeys(map) == std::vector<int>{2, 3, 4})); // still key order
}

void TestLru::orderedMapCustomCompare()
{
    OrderedLRUMap<int, int, std::greater<int>> map(2, std::greater<int>{});
    map.put(1, 10);
    map.put(2, 20);
    map.put(3, 30); // evicts 1

    QVERIFY(!map.contains(1));
    QVERIFY((mapKeys(map) == std::vector<int>{3, 2}));
    QCOMPARE(map.begin()->first, 3);
    QCOMPARE(map.begin()->second, 30);
}

// ---------------------------------------------------------------------------
// Order policy and stress
// ---------------------------------------------------------------------------

void TestLru::orderPolicyAcceptsBothLists()
{
    using ArrayUnorderedMap =
        UnorderedLRUMap<int, int, std::hash<int>, std::equal_to<int>, ArrayLRUList<int>>;
    ArrayUnorderedMap unordered(2);
    unordered.put(1, 10);
    unordered.put(2, 20);
    int value = 0;
    QVERIFY(unordered.get(1, value));
    unordered.put(3, 30); // evicts 2
    QVERIFY(!unordered.contains(2));
    QVERIFY((mapKeys(unordered) == std::vector<int>{1, 3}));

    using ArrayOrderedMap =
        OrderedLRUMap<int, int, std::less<int>, ArrayLRUList<int>>;
    ArrayOrderedMap ordered(2);
    ordered.put(2, 20);
    ordered.put(1, 10);
    QVERIFY((mapKeys(ordered) == std::vector<int>{1, 2}));
    ordered.put(3, 30); // evicts 2, the oldest
    QVERIFY(!ordered.contains(2));
    QVERIFY((mapKeys(ordered) == std::vector<int>{1, 3}));

    using LinkedOrderedMap =
        OrderedLRUMap<int, int, std::less<int>, LinkedLRUList<int>>;
    LinkedOrderedMap linked(2);
    linked.put(2, 20);
    linked.put(1, 10);
    linked.put(3, 30); // evicts 2
    QVERIFY(!linked.contains(2));
    QVERIFY((mapKeys(linked) == std::vector<int>{1, 3}));

    using ArrayUnorderedSet =
        UnorderedLRUSet<int, std::hash<int>, std::equal_to<int>, ArrayLRUList<int>>;
    ArrayUnorderedSet set(2);
    QVERIFY(set.insert(1));
    QVERIFY(set.insert(2));
    QVERIFY(set.insert(3)); // evicts 1
    QVERIFY(!set.contains(1));
    QVERIFY((setValues(set) == std::vector<int>{2, 3}));
}

void TestLru::randomizedOpsStayWithinCapacity()
{
    UnorderedLRUMap<int, int> map(32);
    std::mt19937 rng(20261112u);

    for (int step = 0; step < 10000; ++step) {
        const int key = static_cast<int>(rng() % 64u);
        switch (rng() % 6u) {
        case 0:
            map.put(key, step);
            break;
        case 1: {
            int value = 0;
            (void)map.get(key, value);
            break;
        }
        case 2:
            (void)map.peek(key);
            break;
        case 3:
            (void)map.erase(key);
            break;
        case 4:
            (void)map.take(key);
            break;
        default:
            map.touch(key);
            break;
        }

        QVERIFY(map.size() <= map.capacity());

        const std::vector<int> keys = map.keys();
        QCOMPARE(keys.size(), map.size());
        for (int listed : keys)
            QVERIFY(map.contains(listed));
    }
}

QTEST_MAIN(TestLru)

#include "tst_lru.moc"
