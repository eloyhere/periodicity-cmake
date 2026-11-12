// Periodicity — ring buffer, channel, IO thread and mailbox tests.
//
// The three thread-hand-off primitives are checked for the property that
// matters under load: nothing is lost, nothing is duplicated and the order is
// preserved. The IO thread is checked on its clock (delay, repeat, cancel), and
// the mailbox on its one promise — every posted closure arrives on the owning
// thread, in post order.

#include <md/pool/channel.h>
#include <md/pool/io_thread.h>
#include <md/pool/mailbox.h>
#include <md/pool/ring_buffer.h>

#include <QtTest/QtTest>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

using md::pool::Channel;
using md::pool::IoThread;
using md::pool::Mailbox;
using md::pool::SpscRingBuffer;

namespace {
using namespace std::chrono_literals;
}

class TestRingChannel : public QObject
{
    Q_OBJECT

private slots:
    void ringRoundsCapacity();
    void ringPreservesOrderUnderLoad();
    void ringRejectsWhenFull();
    void channelMultiProducer();
    void channelBoundedDrops();
    void channelCloseWakesReceiver();
    void ioThreadRunsAndDelays();
    void ioThreadRepeatsAndCancels();
    void ioThreadReadsFile();
    void mailboxDeliversInOrder();
};

void TestRingChannel::ringRoundsCapacity()
{
    SpscRingBuffer<int> ring(100);
    QCOMPARE(ring.capacity(), std::size_t(128));
    QVERIFY(ring.empty());
    QCOMPARE(ring.size(), std::size_t(0));

    SpscRingBuffer<int> minimum(1);
    QCOMPARE(minimum.capacity(), std::size_t(2));
}

void TestRingChannel::ringPreservesOrderUnderLoad()
{
    constexpr std::uint64_t kCount = 200000;

    SpscRingBuffer<std::uint64_t> ring(1024);

    std::thread producer([&ring] {
        for (std::uint64_t value = 0; value < kCount;) {
            if (ring.push(value))
                ++value;
        }
    });

    std::uint64_t expected = 0;
    std::uint64_t received = 0;
    bool ordered = true;

    while (received < kCount) {
        std::uint64_t value = 0;
        if (!ring.pop(value))
            continue;
        if (value != expected)
            ordered = false;
        ++expected;
        ++received;
    }

    producer.join();

    QVERIFY(ordered);
    QCOMPARE(received, kCount);
    QCOMPARE(ring.size(), std::size_t(0));
    QVERIFY(ring.highWaterMark() <= ring.capacity());
}

void TestRingChannel::ringRejectsWhenFull()
{
    SpscRingBuffer<int> ring(4);
    QCOMPARE(ring.capacity(), std::size_t(4));

    for (int value = 0; value < 4; ++value)
        QVERIFY(ring.push(value));

    QVERIFY(ring.full());
    QVERIFY(!ring.push(99));
    QCOMPARE(ring.dropped(), std::uint64_t(1));

    int popped = -1;
    QVERIFY(ring.pop(popped));
    QCOMPARE(popped, 0);
    QVERIFY(!ring.full());
}

void TestRingChannel::channelMultiProducer()
{
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 5000;

    Channel<int> channel; // unbounded
    QCOMPARE(channel.capacity(), std::size_t(0));

    std::atomic<long long> expectedSum{0};
    std::vector<std::thread> producers;
    producers.reserve(kProducers);

    for (int producer = 0; producer < kProducers; ++producer) {
        producers.emplace_back([&channel, &expectedSum, producer] {
            for (int index = 0; index < kPerProducer; ++index) {
                const int value = producer * kPerProducer + index;
                expectedSum.fetch_add(value, std::memory_order_relaxed);
                channel.send(value);
            }
        });
    }

    long long receivedSum = 0;
    int received = 0;
    for (int index = 0; index < kProducers * kPerProducer; ++index) {
        const std::optional<int> value = channel.receive();
        if (!value.has_value())
            break;
        receivedSum += *value;
        ++received;
    }

    for (std::thread& producer : producers)
        producer.join();

    QCOMPARE(received, kProducers * kPerProducer);
    QCOMPARE(receivedSum, expectedSum.load());
    QCOMPARE(channel.received(), std::uint64_t(kProducers * kPerProducer));
    QCOMPARE(channel.size(), std::size_t(0));
}

void TestRingChannel::channelBoundedDrops()
{
    Channel<int> channel(2);

    QVERIFY(channel.trySend(1));
    QVERIFY(channel.trySend(2));
    QVERIFY(!channel.trySend(3));
    QCOMPARE(channel.dropped(), std::uint64_t(1));
    QCOMPARE(channel.size(), std::size_t(2));

    QVERIFY(channel.tryReceive().has_value());
    QVERIFY(channel.trySend(3));
    QCOMPARE(channel.size(), std::size_t(2));
}

void TestRingChannel::channelCloseWakesReceiver()
{
    Channel<int> channel;
    std::atomic<bool> finished{false};

    std::thread consumer([&channel, &finished] {
        while (channel.receive().has_value()) {
        }
        finished.store(true);
    });

    QTest::qWait(50);
    QVERIFY(!finished.load());

    channel.close();
    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 2000);
    QVERIFY(channel.isClosed());
    QVERIFY(!channel.send(1));

    consumer.join();
}

void TestRingChannel::ioThreadRunsAndDelays()
{
    IoThread io(QStringLiteral("tst-io"));

    QElapsedTimer timer;
    timer.start();

    std::atomic<bool> immediate{false};
    std::atomic<qint64> delayedAt{-1};

    QVERIFY(io.post([&immediate] { immediate.store(true); }));
    QVERIFY(io.postAfter(120ms, [&delayedAt, &timer] { delayedAt.store(timer.elapsed()); }));

    QTRY_VERIFY_WITH_TIMEOUT(immediate.load(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(delayedAt.load() >= 0, 5000);

    QVERIFY(delayedAt.load() >= 100);
    QTRY_VERIFY_WITH_TIMEOUT(io.stats().executed >= 2, 2000);
    QCOMPARE(io.stats().pending, std::size_t(0));
}

void TestRingChannel::ioThreadRepeatsAndCancels()
{
    IoThread io(QStringLiteral("tst-io-repeat"));

    std::atomic<int> ticks{0};
    const std::uint64_t timerId = io.postEvery(20ms, [&ticks] { ticks.fetch_add(1); });
    QVERIFY(timerId != 0);

    QTRY_VERIFY_WITH_TIMEOUT(ticks.load() >= 3, 5000);

    QVERIFY(io.cancel(timerId));
    QVERIFY(!io.cancel(timerId));

    const int snapshot = ticks.load();
    QTest::qWait(120);
    QCOMPARE(ticks.load(), snapshot);
    QVERIFY(io.stats().cancelled >= 1);

    // Cancelling an unknown id is a no-op, not an error path.
    QVERIFY(!io.cancel(999999));
}

void TestRingChannel::ioThreadReadsFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString path = directory.filePath(QStringLiteral("payload.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("periodicity-io"), qint64(14));
    file.close();

    IoThread io(QStringLiteral("tst-io-file"));

    std::atomic<bool> done{false};
    QByteArray content;
    QString error;

    io.readFile(path, [&done, &content, &error](QByteArray bytes, QString readError) {
        content = std::move(bytes);
        error = std::move(readError);
        done.store(true, std::memory_order_release);
    });

    QTRY_VERIFY_WITH_TIMEOUT(done.load(std::memory_order_acquire), 5000);
    QCOMPARE(content, QByteArray("periodicity-io"));
    QVERIFY(error.isEmpty());
}

void TestRingChannel::mailboxDeliversInOrder()
{
    Mailbox mailbox;
    QVERIFY(mailbox.isCurrentThread());

    std::mutex mutex;
    std::vector<int> order;
    order.reserve(500);

    std::thread producer([&mailbox] {
        for (int index = 0; index < 500; ++index)
            mailbox.post([] {});
    });
    producer.join();

    // The posts above are anonymous; post a second, observable batch from this
    // thread so the order can be checked exactly.
    for (int index = 0; index < 500; ++index) {
        mailbox.post([&mutex, &order, index] {
            std::lock_guard<std::mutex> lock(mutex);
            order.push_back(index);
        });
    }

    QElapsedTimer timer;
    timer.start();
    while (mailbox.delivered() < 1000 && timer.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    QCOMPARE(mailbox.delivered(), std::uint64_t(1000));
    QCOMPARE(mailbox.pending(), std::size_t(0));

    std::lock_guard<std::mutex> lock(mutex);
    QCOMPARE(static_cast<int>(order.size()), 500);
    for (int index = 0; index < 500; ++index)
        QCOMPARE(order[static_cast<std::size_t>(index)], index);
}

QTEST_GUILESS_MAIN(TestRingChannel)

#include "tst_ring_channel.moc"
