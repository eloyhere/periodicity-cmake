// Periodicity — ipc_echo: the child half of the process-communication demo.
//
// The parent (the gallery page or the IPC test) starts this executable and
// talks to it over two pipes. The protocol is deliberately tiny:
//
//   PING <n>       -> PONG <n> <2n>
//   SHUTDOWN       -> exits 0
//
// With --ring <name> it also opens the parent's shared-memory ring and pushes a
// TICK frame every millisecond. That is the throughput channel: a pipe copies
// every byte through the kernel, the ring only moves indices.
//
// stdin and stdout are switched to binary mode first. Frame lengths are binary
// integers, and byte 0x0A inside one must not be translated into 0x0D 0x0A.

#include <md/pool/ipc/frame.h>
#include <md/pool/ipc/shared_ring.h>

#include <QByteArray>
#include <QByteArrayView>
#include <QCoreApplication>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#ifdef Q_OS_WIN
#  include <fcntl.h>
#  include <io.h>
#else
#  include <unistd.h>
#endif

namespace {

constexpr std::size_t kRingCapacity = std::size_t{1} << 20;
constexpr std::size_t kReadBufferSize = 4096;

bool writeAll(const QByteArray& bytes)
{
    std::size_t written = 0;
    while (written < static_cast<std::size_t>(bytes.size())) {
        const std::size_t chunk = std::fwrite(bytes.constData() + written, 1,
                                              static_cast<std::size_t>(bytes.size()) - written,
                                              stdout);
        if (chunk == 0)
            return false;
        written += chunk;
    }
    std::fflush(stdout);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    const QStringList arguments = QCoreApplication::arguments();

    QString ringName;
    const int ringIndex = arguments.indexOf(QStringLiteral("--ring"));
    if (ringIndex >= 0 && ringIndex + 1 < arguments.size())
        ringName = arguments.at(ringIndex + 1);

    std::unique_ptr<md::pool::ipc::SharedRingBuffer> ring;
    if (!ringName.isEmpty()) {
        QString error;
        ring = md::pool::ipc::SharedRingBuffer::open(ringName, kRingCapacity, &error);
        if (ring == nullptr) {
            std::fprintf(stderr, "ipc_echo: cannot open ring: %s\n", qPrintable(error));
            return 3;
        }
    }

    std::atomic<bool> stopped{false};
    std::thread ticker;

    if (ring != nullptr) {
        // A steady stream, not a reply: the parent's "messages/s" figure should
        // move even when nobody is sending PINGs. The batch matters: a 1 ms
        // sleep is ~15 ms on Windows' default timer resolution, so one frame per
        // wake would look like a trickle. Sixteen per wake is ~1000/s.
        ticker = std::thread([shared = ring.get(), &stopped] {
            std::uint64_t tick = 0;
            while (!stopped.load(std::memory_order_relaxed)) {
                for (int batch = 0; batch < 16; ++batch) {
                    const QByteArray payload =
                        "TICK " + QByteArray::number(static_cast<qint64>(tick++));
                    shared->push(payload);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    md::pool::ipc::FrameDecoder decoder;
    char buffer[kReadBufferSize];
    int result = 0;
    bool shuttingDown = false;

    while (!shuttingDown) {
        // A raw read, not fread: fread keeps asking the pipe until its buffer
        // is full, which on a blocking pipe means it never returns for a short
        // PING. _read()/read() return whatever arrived.
#ifdef Q_OS_WIN
        const int read = _read(_fileno(stdin), buffer, static_cast<unsigned int>(sizeof(buffer)));
#else
        const ssize_t read = ::read(STDIN_FILENO, buffer, sizeof(buffer));
#endif
        if (read <= 0)
            break;

        decoder.append(QByteArrayView(buffer, static_cast<qsizetype>(read)));

        while (const std::optional<QByteArray> frame = decoder.next()) {
            const QByteArray request = *frame;

            if (request == "SHUTDOWN") {
                shuttingDown = true;
                break;
            }

            if (!request.startsWith("PING "))
                continue;

            const QByteArray counter = request.mid(5);
            bool ok = false;
            const qlonglong value = counter.toLongLong(&ok);
            if (!ok)
                continue;

            QByteArray reply = "PONG ";
            reply += counter;
            reply += ' ';
            reply += QByteArray::number(value * 2);
            if (!writeAll(md::pool::ipc::encodeFrame(reply))) {
                result = 4;
                shuttingDown = true;
                break;
            }
        }

        if (decoder.hasError()) {
            std::fprintf(stderr, "ipc_echo: %s\n", qPrintable(decoder.error()));
            result = 5;
            break;
        }
    }

    stopped.store(true, std::memory_order_relaxed);
    if (ticker.joinable())
        ticker.join();

    return result;
}
