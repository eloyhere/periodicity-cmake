// Periodicity — object pool tests.
//
// The pool is judged on two things at once: that storage is recycled, and that
// an object's lifetime is still created and destroyed for every use. The suite
// checks both separately — a pool that leaked state between users, or one that
// forgot a destructor, would pass a reuse test but fail here.

#include <md/pool/block_pool.h>
#include <md/pool/object_pool.h>

#include <QtTest/QtTest>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

using md::pool::BlockPool;
using md::pool::ObjectPool;
using md::pool::PooledObject;

namespace {

struct Counted
{
    static int constructed;
    static int destroyed;

    static void resetCounters()
    {
        constructed = 0;
        destroyed = 0;
    }

    explicit Counted(int initial = 0) : value(initial) { ++constructed; }

    Counted(const Counted&) = delete;
    Counted& operator=(const Counted&) = delete;

    ~Counted() { ++destroyed; }

    int value = 0;
};

int Counted::constructed = 0;
int Counted::destroyed = 0;

// No default constructor, and a member with a destructor: proves that create()
// forwards arguments and that destroy() really ends a resource-owning lifetime.
struct NoDefault
{
    NoDefault(int number, std::string label) : value(number), name(std::move(label)) {}

    int value = 0;
    std::string name;
};

struct Throwing
{
    explicit Throwing(bool fail)
    {
        if (fail)
            throw std::runtime_error("constructor failed");
    }
};

#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4324) // the padding is exactly what this type is for
#endif
struct alignas(64) OverAligned
{
    int value = 0;
};
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

} // namespace

class TestPool : public QObject
{
    Q_OBJECT

private slots:
    void blockPoolHandsOutBlocks();
    void blockPoolReusesLifo();
    void blockPoolRespectsBounds();
    void blockPoolPreallocates();
    void blockPoolAlignsBlocks();

    void objectPoolCreatesAndDestroys();
    void objectPoolStartsAFreshLifetime();
    void objectPoolForwardsArguments();
    void objectPoolGrowsInChunks();
    void objectPoolRespectsBounds();
    void objectPoolPreallocates();
    void objectPoolHandlesConstructorFailure();
    void objectPoolOwnsAndClears();

    void handleOwnsLifetime();
    void handleMovesOwnership();
    void handleReleasesObject();

    void typesAreNotCopied();
};

void TestPool::blockPoolHandsOutBlocks()
{
    BlockPool pool({.blockSize = 32, .chunkBlocks = 4});

    QCOMPARE(pool.blockSize(), std::size_t(32));
    QCOMPARE(pool.capacity(), std::size_t(0));
    QCOMPARE(pool.available(), std::size_t(0));
    QCOMPARE(pool.inUse(), std::size_t(0));

    void* first = pool.acquire();
    void* second = pool.acquire();
    QVERIFY(first != nullptr);
    QVERIFY(second != nullptr);
    QVERIFY(first != second);

    QCOMPARE(pool.capacity(), std::size_t(4));
    QCOMPARE(pool.inUse(), std::size_t(2));
    QCOMPARE(pool.available(), std::size_t(2));
    QCOMPARE(pool.chunkCount(), std::size_t(1));
    QCOMPARE(pool.highWaterMark(), std::size_t(2));

    QVERIFY(pool.owns(first));
    QVERIFY(pool.owns(second));

    int stackValue = 0;
    QVERIFY(!pool.owns(&stackValue));
    QVERIFY(!pool.owns(nullptr));

    // The chunk holds four blocks: the next two fill it, and the one after
    // that grows the pool by another chunk.
    QVERIFY(pool.acquire() != nullptr);
    QVERIFY(pool.acquire() != nullptr);
    QCOMPARE(pool.capacity(), std::size_t(4));
    QCOMPARE(pool.available(), std::size_t(0));

    QVERIFY(pool.acquire() != nullptr);
    QCOMPARE(pool.capacity(), std::size_t(8));
    QCOMPARE(pool.chunkCount(), std::size_t(2));
}

void TestPool::blockPoolReusesLifo()
{
    BlockPool pool({.blockSize = 16, .chunkBlocks = 2});

    void* first = pool.acquire();
    void* second = pool.acquire();
    QVERIFY(first != second);

    pool.release(first);
    pool.release(second);

    // The free list is a stack: the last block released is the first handed out.
    QVERIFY(pool.acquire() == second);
    QVERIFY(pool.acquire() == first);
    QCOMPARE(pool.capacity(), std::size_t(2));
    QCOMPARE(pool.inUse(), std::size_t(2));
}

void TestPool::blockPoolRespectsBounds()
{
    BlockPool pool({.blockSize = 16, .chunkBlocks = 4, .maxBlocks = 3});

    void* first = pool.acquire();
    QVERIFY(first != nullptr);
    QVERIFY(pool.acquire() != nullptr);
    QVERIFY(pool.acquire() != nullptr);

    QCOMPARE(pool.capacity(), std::size_t(3));
    QCOMPARE(pool.acquire(), nullptr);

    // Releasing makes room again, without growing past the cap.
    pool.release(first);
    QVERIFY(pool.acquire() == first);
    QCOMPARE(pool.capacity(), std::size_t(3));
    QCOMPARE(pool.highWaterMark(), std::size_t(3));
}

void TestPool::blockPoolPreallocates()
{
    BlockPool pool({.blockSize = 16, .chunkBlocks = 4});
    QCOMPARE(pool.capacity(), std::size_t(0));

    pool.reserve(10);
    QCOMPARE(pool.capacity(), std::size_t(12)); // three chunks of four
    QCOMPARE(pool.chunkCount(), std::size_t(3));
    QCOMPARE(pool.inUse(), std::size_t(0));
}

void TestPool::blockPoolAlignsBlocks()
{
    BlockPool pool({.blockSize = sizeof(OverAligned),
                    .blockAlignment = alignof(OverAligned),
                    .chunkBlocks = 4});

    for (int index = 0; index < 4; ++index) {
        void* block = pool.acquire();
        QVERIFY(block != nullptr);
        const auto address = reinterpret_cast<std::uintptr_t>(block);
        QCOMPARE(address % alignof(OverAligned), std::uintptr_t(0));
    }
}

void TestPool::objectPoolCreatesAndDestroys()
{
    Counted::resetCounters();

    ObjectPool<Counted> pool({.chunkBlocks = 2});
    Counted* object = pool.create(7);

    QVERIFY(object != nullptr);
    QCOMPARE(object->value, 7);
    QCOMPARE(pool.liveCount(), std::size_t(1));
    QCOMPARE(Counted::constructed, 1);
    QCOMPARE(Counted::destroyed, 0);

    pool.destroy(object);
    QCOMPARE(pool.liveCount(), std::size_t(0));
    QCOMPARE(Counted::destroyed, 1);

    // destroy(nullptr) composes with create() failing.
    pool.destroy(nullptr);
    QCOMPARE(Counted::destroyed, 1);
    QCOMPARE(Counted::constructed, Counted::destroyed);
}

void TestPool::objectPoolStartsAFreshLifetime()
{
    Counted::resetCounters();

    ObjectPool<Counted> pool({.chunkBlocks = 2});
    Counted* first = pool.create(1);
    pool.destroy(first);

    Counted* second = pool.create(2);

    // Same block, new object: the constructor ran again, and the previous
    // value did not survive.
    QVERIFY(second == first);
    QCOMPARE(second->value, 2);
    QCOMPARE(Counted::constructed, 2);
    QCOMPARE(Counted::destroyed, 1);

    pool.destroy(second);
    QCOMPARE(Counted::constructed, Counted::destroyed);
}

void TestPool::objectPoolForwardsArguments()
{
    ObjectPool<NoDefault> pool;
    NoDefault* object = pool.create(42, std::string("hello"));

    QVERIFY(object != nullptr);
    QCOMPARE(object->value, 42);
    QVERIFY(object->name == "hello");

    pool.destroy(object);
    QCOMPARE(pool.liveCount(), std::size_t(0));
}

void TestPool::objectPoolGrowsInChunks()
{
    ObjectPool<Counted> pool({.chunkBlocks = 2});

    Counted* first = pool.create(1);
    Counted* second = pool.create(2);
    QCOMPARE(pool.capacity(), std::size_t(2));
    QCOMPARE(pool.chunkCount(), std::size_t(1));

    Counted* third = pool.create(3);
    QCOMPARE(pool.capacity(), std::size_t(4));
    QCOMPARE(pool.chunkCount(), std::size_t(2));
    QCOMPARE(pool.highWaterMark(), std::size_t(3));

    pool.destroy(first);
    pool.destroy(second);
    pool.destroy(third);
    QCOMPARE(pool.liveCount(), std::size_t(0));
    QCOMPARE(pool.available(), std::size_t(4));
    QCOMPARE(pool.highWaterMark(), std::size_t(3)); // historical
}

void TestPool::objectPoolRespectsBounds()
{
    ObjectPool<Counted> pool({.chunkBlocks = 4, .maxBlocks = 2});

    Counted* first = pool.create(1);
    Counted* second = pool.create(2);
    QVERIFY(first != nullptr);
    QVERIFY(second != nullptr);

    QCOMPARE(pool.create(3), nullptr);
    QCOMPARE(pool.liveCount(), std::size_t(2));

    pool.destroy(first);
    Counted* third = pool.create(4);
    QVERIFY(third != nullptr);
    QCOMPARE(third->value, 4);

    pool.destroy(second);
    pool.destroy(third);
}

void TestPool::objectPoolPreallocates()
{
    Counted::resetCounters();
    ObjectPool<Counted> pool({.chunkBlocks = 4, .initialBlocks = 8});

    QCOMPARE(pool.capacity(), std::size_t(8));
    QCOMPARE(pool.chunkCount(), std::size_t(2));
    QCOMPARE(pool.liveCount(), std::size_t(0));
    // Reserving storage must not construct anything.
    QCOMPARE(Counted::constructed, 0);
}

void TestPool::objectPoolHandlesConstructorFailure()
{
    ObjectPool<Throwing> pool({.chunkBlocks = 2});

    const auto failingCreate = [&pool] { (void)pool.create(true); };
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, failingCreate());
    QCOMPARE(pool.liveCount(), std::size_t(0));
    QCOMPARE(pool.available(), std::size_t(2)); // the block went back

    Throwing* object = pool.create(false);
    QVERIFY(object != nullptr);
    pool.destroy(object);
}

void TestPool::objectPoolOwnsAndClears()
{
    ObjectPool<Counted> pool({.chunkBlocks = 2});
    Counted* object = pool.create(1);

    QVERIFY(pool.owns(object));
    Counted local(1);
    QVERIFY(!pool.owns(&local));

    pool.destroy(object);
    QCOMPARE(pool.capacity(), std::size_t(2));

    pool.clear();
    QCOMPARE(pool.capacity(), std::size_t(0));
    QCOMPARE(pool.chunkCount(), std::size_t(0));

    // A cleared pool is usable again.
    Counted* again = pool.create(2);
    QVERIFY(again != nullptr);
    QCOMPARE(pool.capacity(), std::size_t(2));
    pool.destroy(again);
}

void TestPool::handleOwnsLifetime()
{
    Counted::resetCounters();

    ObjectPool<Counted> pool;
    {
        auto handle = pool.createHandle(5);
        QVERIFY(handle);
        QCOMPARE(handle->value, 5);
        QCOMPARE((*handle).value, 5);
        QCOMPARE(handle.get()->value, 5);
        QCOMPARE(pool.liveCount(), std::size_t(1));
        QCOMPARE(Counted::destroyed, 0);
    }

    QCOMPARE(pool.liveCount(), std::size_t(0));
    QCOMPARE(Counted::destroyed, 1);

    // A full bounded pool yields an empty handle rather than throwing.
    ObjectPool<Counted> bounded({.chunkBlocks = 1, .maxBlocks = 1});
    auto taken = bounded.createHandle(1);
    auto spare = bounded.createHandle(2);
    QVERIFY(taken);
    QVERIFY(!spare);
}

void TestPool::handleMovesOwnership()
{
    ObjectPool<Counted> pool;
    Counted::resetCounters();

    auto first = pool.createHandle(1);
    auto second = std::move(first);
    QVERIFY(!first);
    QVERIFY(second);
    QCOMPARE(pool.liveCount(), std::size_t(1));

    second.reset();
    QCOMPARE(pool.liveCount(), std::size_t(0));
    QCOMPARE(Counted::destroyed, 1);

    // Move assignment ends the target's old object before taking the new one.
    auto third = pool.createHandle(1);
    auto fourth = pool.createHandle(2);
    QCOMPARE(pool.liveCount(), std::size_t(2));

    fourth = std::move(third);
    QCOMPARE(pool.liveCount(), std::size_t(1));
    QVERIFY(!third);
    QVERIFY(fourth);
    QCOMPARE(fourth->value, 1);
}

void TestPool::handleReleasesObject()
{
    ObjectPool<Counted> pool;
    auto handle = pool.createHandle(3);

    Counted* object = handle.release();
    QVERIFY(object != nullptr);
    QVERIFY(!handle);
    QCOMPARE(object->value, 3);

    // The object is still live and now the caller's to destroy.
    QCOMPARE(pool.liveCount(), std::size_t(1));
    pool.destroy(object);
    QCOMPARE(pool.liveCount(), std::size_t(0));
}

void TestPool::typesAreNotCopied()
{
    static_assert(std::is_same_v<PooledObject<Counted>, ObjectPool<Counted>::Handle>);

    static_assert(!std::is_copy_constructible_v<ObjectPool<Counted>>);
    static_assert(!std::is_copy_assignable_v<ObjectPool<Counted>>);
    static_assert(!std::is_move_constructible_v<ObjectPool<Counted>>);
    static_assert(!std::is_move_assignable_v<ObjectPool<Counted>>);

    static_assert(!std::is_copy_constructible_v<PooledObject<Counted>>);
    static_assert(std::is_move_constructible_v<PooledObject<Counted>>);
    static_assert(std::is_move_assignable_v<PooledObject<Counted>>);

    static_assert(!std::is_copy_constructible_v<BlockPool>);
    static_assert(std::is_move_constructible_v<BlockPool>);
}

QTEST_APPLESS_MAIN(TestPool)
#include "tst_pool.moc"
