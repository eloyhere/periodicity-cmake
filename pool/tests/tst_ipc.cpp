// Periodicity — IPC tests: framing, shared ring and a real child process.
//
// The unit half checks the message boundary rules (split frames, several frames
// in one read, an oversized length) and the shared ring's create/open contract.
// The integration half spawns periodicity_ipc_echo for real: a pipe round trip
// and a shared-memory stream. Nothing is mocked, so the test fails if handle
// inheritance, binary stdio or the mapping name is wrong.

#include <md/pool/ipc/child_process.h>
#include <md/pool/ipc/frame.h>
#include <md/pool/ipc/shared_ring.h>

#include <QtTest/QtTest>

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryDir>

#include <cstdint>
#include <memory>
#include <optional>

namespace ipc = md::pool::ipc;

namespace {

QString echoExecutable()
{
    const QString base =
        QCoreApplication::applicationDirPath() + QStringLiteral("/periodicity_ipc_echo");
#ifdef Q_OS_WIN
    return base + QStringLiteral(".exe");
#else
    return base;
#endif
}

struct Collector
{
    void feed(QByteArrayView bytes)
    {
        QMutexLocker locker(&mutex);
        decoder.append(bytes);
        while (const std::optional<QByteArray> frame = decoder.next()) {
            last = *frame;
            ++replies;
        }
    }

    int count()
    {
        QMutexLocker locker(&mutex);
        return replies;
    }

    QByteArray reply()
    {
        QMutexLocker locker(&mutex);
        return last;
    }

    QMutex mutex;
    ipc::FrameDecoder decoder;
    QByteArray last;
    int replies = 0;
};

} // namespace

class TestIpc : public QObject
{
    Q_OBJECT

private slots:
    void frameSurvivesSplitReads();
    void frameRejectsOversizedLength();
    void sharedRingRoundTripInProcess();
    void childEchoRoundTrip();
    void childStreamsIntoSharedRing();
};

void TestIpc::frameSurvivesSplitReads()
{
    const QByteArray first = ipc::encodeFrame(QByteArrayLiteral("alpha"));
    const QByteArray second = ipc::encodeFrame(QByteArrayLiteral("beta"));

    ipc::FrameDecoder decoder;

    // A header split across two reads, then the payload, then a whole second
    // frame glued onto the tail of the first.
    decoder.append(QByteArrayView(first.constData(), 2));
    QVERIFY(!decoder.next().has_value());

    decoder.append(QByteArrayView(first.constData() + 2, first.size() - 2));
    const std::optional<QByteArray> one = decoder.next();
    QVERIFY(one.has_value());
    QCOMPARE(*one, QByteArrayLiteral("alpha"));
    QVERIFY(!decoder.next().has_value());

    QByteArray glued = first + second;
    decoder.append(glued);
    const std::optional<QByteArray> echoed = decoder.next();
    QVERIFY(echoed.has_value());
    QCOMPARE(*echoed, QByteArrayLiteral("alpha"));

    const std::optional<QByteArray> tail = decoder.next();
    QVERIFY(tail.has_value());
    QCOMPARE(*tail, QByteArrayLiteral("beta"));
    QVERIFY(!decoder.hasError());
}

void TestIpc::frameRejectsOversizedLength()
{
    const std::uint32_t tooLarge =
        static_cast<std::uint32_t>(ipc::kMaxFrameSize + 1);

    QByteArray header;
    header.resize(4);
    header[0] = static_cast<char>((tooLarge >> 0) & 0xFFu);
    header[1] = static_cast<char>((tooLarge >> 8) & 0xFFu);
    header[2] = static_cast<char>((tooLarge >> 16) & 0xFFu);
    header[3] = static_cast<char>((tooLarge >> 24) & 0xFFu);

    ipc::FrameDecoder decoder;
    decoder.append(header);

    QVERIFY(!decoder.next().has_value());
    QVERIFY(decoder.hasError());

    // A failed decoder stays failed until it is reset.
    decoder.append(QByteArray(1024, 'x'));
    QVERIFY(!decoder.next().has_value());
    decoder.reset();
    QVERIFY(!decoder.hasError());
}

void TestIpc::sharedRingRoundTripInProcess()
{
    const QString name = ipc::SharedRingBuffer::uniqueName(QStringLiteral("unit"));
    QString error;

    std::unique_ptr<ipc::SharedRingBuffer> writer =
        ipc::SharedRingBuffer::create(name, 8192, &error);
    QVERIFY2(writer != nullptr, qPrintable(error));
    QCOMPARE(writer->capacity(), std::size_t(8192));

    // The name is taken: a second creator must fail instead of silently
    // handing out a different mapping.
    QVERIFY(ipc::SharedRingBuffer::create(name, 8192, &error) == nullptr);
    QVERIFY(!error.isEmpty());

    QVERIFY(writer->push(QByteArrayLiteral("hello")));
    QCOMPARE(writer->used(), std::size_t(9));

    // A frame larger than the ring is refused outright, not truncated.
    QVERIFY(!writer->push(QByteArray(9000, 'x')));
    QCOMPARE(writer->dropped(), std::uint64_t(0));

    // Opening with the wrong capacity is a header mismatch.
    QVERIFY(ipc::SharedRingBuffer::open(name, 4096, &error) == nullptr);

    std::unique_ptr<ipc::SharedRingBuffer> reader =
        ipc::SharedRingBuffer::open(name, 8192, &error);
    QVERIFY2(reader != nullptr, qPrintable(error));

    const std::optional<QByteArray> popped = reader->pop();
    QVERIFY(popped.has_value());
    QCOMPARE(*popped, QByteArrayLiteral("hello"));
    QVERIFY(!reader->pop().has_value());
    QCOMPARE(reader->used(), std::size_t(0));

    int pushed = 0;
    while (writer->push(QByteArrayLiteral("x"))) {
        ++pushed;
        if (pushed > 10000)
            break;
    }
    QVERIFY(pushed > 0);
    QVERIFY(writer->dropped() >= 1);
    QVERIFY(writer->used() <= writer->capacity());
}

void TestIpc::childEchoRoundTrip()
{
    const QString program = echoExecutable();
    if (!QFile::exists(program))
        QSKIP("periodicity_ipc_echo is not built");

    Collector collector;
    QString spawnError;

    std::unique_ptr<ipc::ChildProcess> child = ipc::ChildProcess::spawn(
        ipc::ChildProcess::Options{.program = program},
        [&collector](QByteArrayView bytes) { collector.feed(bytes); },
        &spawnError);

    QVERIFY2(child != nullptr, qPrintable(spawnError));
    QVERIFY(child->isRunning());
    QVERIFY(child->pid() > 0);

    QVERIFY(child->write(ipc::encodeFrame(QByteArrayLiteral("PING 21"))));

    QTRY_VERIFY_WITH_TIMEOUT(collector.count() >= 1, 5000);
    QCOMPARE(collector.reply(), QByteArrayLiteral("PONG 21 42"));

    QVERIFY(child->write(ipc::encodeFrame(QByteArrayLiteral("SHUTDOWN"))));
    QCOMPARE(child->waitForFinished(5000), 0);
    QVERIFY(!child->isRunning());
}

void TestIpc::childStreamsIntoSharedRing()
{
    const QString program = echoExecutable();
    if (!QFile::exists(program))
        QSKIP("periodicity_ipc_echo is not built");

    const QString ringName = ipc::SharedRingBuffer::uniqueName(QStringLiteral("stream"));
    QString error;

    std::unique_ptr<ipc::SharedRingBuffer> ring =
        ipc::SharedRingBuffer::create(ringName, std::size_t{1} << 20, &error);
    QVERIFY2(ring != nullptr, qPrintable(error));

    QString spawnError;
    std::unique_ptr<ipc::ChildProcess> child = ipc::ChildProcess::spawn(
        ipc::ChildProcess::Options{.program = program,
                                   .arguments = {QStringLiteral("--ring"), ringName}},
        [](QByteArrayView) {},
        &spawnError);

    QVERIFY2(child != nullptr, qPrintable(spawnError));

    int received = 0;
    QElapsedTimer timer;
    timer.start();

    while (received < 20 && timer.elapsed() < 5000) {
        while (const std::optional<QByteArray> frame = ring->pop()) {
            QVERIFY(frame->startsWith(QByteArrayLiteral("TICK ")));
            ++received;
        }
        QTest::qWait(5);
    }

    QVERIFY2(received >= 20, "the child did not stream into the shared ring");

    child->terminate();
    QVERIFY(!child->isRunning());
}

QTEST_GUILESS_MAIN(TestIpc)

#include "tst_ipc.moc"
