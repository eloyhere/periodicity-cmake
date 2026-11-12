#include "pool_demo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <chrono>
#include <utility>

using namespace std::chrono_literals;

namespace {

constexpr int kRingCapacity = 8192;
constexpr int kSharedRingCapacity = 1 << 20;

} // namespace

PoolDemo::PoolDemo()
    : pool_(md::pool::ThreadPoolOptions{.workers = 4,
                                           .queueCapacity = 1024,
                                           .name = QStringLiteral("gallery-pool")})
    , io_(QStringLiteral("gallery-io"))
    , lastPoll_(std::chrono::steady_clock::now())
{
    log(QStringLiteral("pool started: %1 worker(s)").arg(pool_.workerCount()));
}

PoolDemo::~PoolDemo()
{
    stopAll();
}

void PoolDemo::stopAll()
{
    stopChild();
    stopRingStress();
    stopChannelDemo();

    pool_.stop(true);
    io_.stop();
    mailbox_.close();
}

void PoolDemo::poll()
{
    const auto now = std::chrono::steady_clock::now();

    if (childRing_) {
        int drained = 0;
        while (drained < 20000) {
            if (!childRing_->pop().has_value())
                break;
            ++drained;
        }
        childShm_.fetch_add(static_cast<std::uint64_t>(drained));
    }

    if (child_ && !childExited_) {
        if (!child_->isRunning()) {
            childExited_ = true;
            childExitCode_ = child_->waitForFinished(0);
            log(QStringLiteral("echo child exited with %1").arg(childExitCode_));
        }
    }

    const double seconds = std::chrono::duration<double>(now - lastPoll_).count();
    if (seconds >= 0.25) {
        const std::uint64_t ringConsumed = ringConsumed_.load();
        const std::uint64_t channelConsumed = channelConsumed_.load();
        const std::uint64_t shm = childShm_.load();

        ringPerSecond_ = static_cast<double>(ringConsumed - lastRingConsumed_) / seconds;
        channelPerSecond_ = static_cast<double>(channelConsumed - lastChannelConsumed_) / seconds;
        childShmPerSecond_ = static_cast<double>(shm - lastChildShm_) / seconds;

        lastRingConsumed_ = ringConsumed;
        lastChannelConsumed_ = channelConsumed;
        lastChildShm_ = shm;
        lastPoll_ = now;
    }
}

PoolDemo::Snapshot PoolDemo::snapshot() const
{
    Snapshot snapshot;
    snapshot.pool = pool_.stats();

    snapshot.demoPoolCapacity = demoPool_.capacity();
    snapshot.demoPoolLive = demoPool_.liveCount();
    snapshot.demoPoolHighWaterMark = demoPool_.highWaterMark();

    snapshot.ringCapacity = ring_.capacity();
    snapshot.ringUsed = ring_.size();
    snapshot.ringProduced = ringProduced_.load();
    snapshot.ringConsumed = ringConsumed_.load();
    snapshot.ringDropped = ring_.dropped();
    snapshot.ringPerSecond = ringPerSecond_;

    if (channel_) {
        snapshot.channelProduced = channelProduced_.load();
        snapshot.channelConsumed = channelConsumed_.load();
    }
    snapshot.channelPerSecond = channelPerSecond_;

    snapshot.io = io_.stats();
    snapshot.ioFileStatus = ioFileStatus_;

    snapshot.mailboxDelivered = mailbox_.delivered();
    snapshot.mailboxPending = mailbox_.pending();

    snapshot.pingRequests = pingRequests_.load();
    snapshot.pingReplies = pingReplies_.load();
    snapshot.lastWorkerRttUs = lastWorkerRttUs_.load();

    snapshot.childTx = childTx_.load();
    snapshot.childRx = childRx_.load();
    snapshot.childShmFrames = childShm_.load();
    snapshot.childShmPerSecond = childShmPerSecond_;
    snapshot.childRttUs = lastChildRttUs_.load();
    snapshot.childRunning = child_ != nullptr && !childExited_;
    snapshot.childPid = child_ != nullptr ? child_->pid() : 0;
    snapshot.childExitCode = childExitCode_;

    snapshot.log = log_;
    return snapshot;
}

void PoolDemo::submitJobs(int count)
{
    for (int index = 0; index < count; ++index) {
        pool_.post([] { std::this_thread::sleep_for(2ms); });
    }
}

void PoolDemo::setPaused(bool paused)
{
    if (paused)
        pool_.pause();
    else
        pool_.resume();
}

bool PoolDemo::drainPool()
{
    return pool_.drain(1500ms);
}

void PoolDemo::restartPool()
{
    pool_.stop(true);
    pool_.start();
}

void PoolDemo::churnObjectPool(int count)
{
    for (int index = 0; index < count; ++index) {
        auto handle = demoPool_.createHandle();
        if (handle)
            handle->sequence = static_cast<std::uint64_t>(index);
    }
}

void PoolDemo::resetObjectPool()
{
    // Every handle is scoped to one churn iteration, so nothing is live here.
    demoPool_.clear();
}

void PoolDemo::startRingStress()
{
    if (ringRunning_.load())
        return;

    ringProduced_.store(0);
    ringConsumed_.store(0);
    ringRunning_.store(true);

    ringProducer_ = std::thread([this] { ringProducerLoop(); });
    ringConsumer_ = std::thread([this] { ringConsumerLoop(); });
}

void PoolDemo::stopRingStress()
{
    if (!ringRunning_.exchange(false))
        return;

    if (ringProducer_.joinable())
        ringProducer_.join();

    // The consumer drains what the producer left behind, then sees the flag.
    if (ringConsumer_.joinable())
        ringConsumer_.join();
}

void PoolDemo::ringProducerLoop()
{
    std::uint64_t value = 0;
    while (ringRunning_.load(std::memory_order_relaxed)) {
        if (ring_.push(value)) {
            ++value;
            ringProduced_.fetch_add(1, std::memory_order_relaxed);
            if ((value & 0x3F) == 0)
                std::this_thread::sleep_for(50us);
        } else {
            std::this_thread::yield();
        }
    }
}

void PoolDemo::ringConsumerLoop()
{
    while (ringRunning_.load(std::memory_order_relaxed) || !ring_.empty()) {
        std::uint64_t value = 0;
        if (ring_.pop(value))
            ringConsumed_.fetch_add(1, std::memory_order_relaxed);
        else
            std::this_thread::sleep_for(50us);
    }
}

void PoolDemo::startChannelDemo()
{
    if (channelRunning_.load())
        return;

    channelProduced_.store(0);
    channelConsumed_.store(0);
    channelRunning_.store(true);
    channel_ = std::make_unique<md::pool::Channel<std::uint64_t>>();

    channelProducer_ = std::thread([this] { channelProducerLoop(); });
    channelConsumer_ = std::thread([this] { channelConsumerLoop(); });
}

void PoolDemo::stopChannelDemo()
{
    if (!channelRunning_.exchange(false))
        return;

    if (channel_)
        channel_->close();

    if (channelProducer_.joinable())
        channelProducer_.join();
    if (channelConsumer_.joinable())
        channelConsumer_.join();

    channel_.reset();
}

void PoolDemo::channelProducerLoop()
{
    std::uint64_t value = 0;
    while (channelRunning_.load(std::memory_order_relaxed)) {
        if (channel_ && channel_->trySend(value)) {
            ++value;
            channelProduced_.fetch_add(1, std::memory_order_relaxed);
        }
        std::this_thread::sleep_for(500us);
    }
}

void PoolDemo::channelConsumerLoop()
{
    if (!channel_)
        return;

    while (const std::optional<std::uint64_t> value = channel_->receive()) {
        Q_UNUSED(value);
        channelConsumed_.fetch_add(1, std::memory_order_relaxed);
    }
}

void PoolDemo::postIoTask()
{
    io_.post([this] {
        const QString status = QStringLiteral("immediate task executed");
        mailbox_.post([this, status] { ioFileStatus_ = status; });
    });
}

void PoolDemo::postIoDelayed()
{
    pendingTimerId_ = io_.postAfter(250ms, [this] {
        const QString status = QStringLiteral("delayed task executed (250 ms)");
        mailbox_.post([this, status] { ioFileStatus_ = status; });
    });
}

void PoolDemo::readProjectFile()
{
    const QString path = QDir::cleanPath(
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../../CMakeLists.txt"));

    io_.readFile(path, [this, path](QByteArray bytes, QString error) {
        mailbox_.post([this, path, bytes = std::move(bytes), error = std::move(error)] {
            if (error.isEmpty()) {
                ioFileStatus_ = QStringLiteral("read %1 — %2 bytes")
                                    .arg(QFileInfo(path).fileName())
                                    .arg(bytes.size());
            } else {
                ioFileStatus_ = QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error);
            }
        });
    });
}

void PoolDemo::cancelIoTimers()
{
    if (pendingTimerId_ != 0 && io_.cancel(pendingTimerId_))
        ioFileStatus_ = QStringLiteral("pending delayed task cancelled");
    pendingTimerId_ = 0;
}

void PoolDemo::pingWorker(int count)
{
    for (int index = 0; index < count; ++index) {
        pingRequests_.fetch_add(1, std::memory_order_relaxed);

        const auto started = std::chrono::steady_clock::now();
        pool_.post([this, started] {
            std::this_thread::sleep_for(300us);
            mailbox_.post([this, started] {
                const double micros = std::chrono::duration<double, std::micro>(
                                          std::chrono::steady_clock::now() - started)
                                          .count();
                lastWorkerRttUs_.store(micros);
                pingReplies_.fetch_add(1, std::memory_order_relaxed);
            });
        });
    }
}

QString PoolDemo::echoExecutable() const
{
    QString path = QCoreApplication::applicationDirPath()
        + QStringLiteral("/periodicity_ipc_echo");
#ifdef Q_OS_WIN
    path += QStringLiteral(".exe");
#endif
    return path;
}

void PoolDemo::startChild()
{
    if (child_)
        return;

    const QString program = echoExecutable();
    if (!QFile::exists(program)) {
        log(QStringLiteral("echo helper missing: %1").arg(QFileInfo(program).fileName()));
        return;
    }

    QString error;
    childRingName_ = md::pool::ipc::SharedRingBuffer::uniqueName(QStringLiteral("gallery"));
    childRing_ = md::pool::ipc::SharedRingBuffer::create(
        childRingName_, std::size_t{1} << 20, &error);
    if (!childRing_) {
        log(QStringLiteral("shared ring: %1").arg(error));
        return;
    }

    {
        std::lock_guard<std::mutex> lock(childMutex_);
        childDecoder_.reset();
    }
    pendingPings_.clear();
    pingSequence_ = 0;
    childTx_.store(0);
    childRx_.store(0);
    childShm_.store(0);
    childExited_ = false;
    childExitCode_ = -1;

    child_ = md::pool::ipc::ChildProcess::spawn(
        md::pool::ipc::ChildProcess::Options{.program = program,
                                                .arguments = {QStringLiteral("--ring"),
                                                              childRingName_}},
        [this](QByteArrayView bytes) { onChildBytes(bytes); },
        &error);

    if (!child_) {
        childRing_.reset();
        log(QStringLiteral("spawn failed: %1").arg(error));
        return;
    }

    log(QStringLiteral("echo child started (pid %1)").arg(child_->pid()));
}

void PoolDemo::onChildBytes(QByteArrayView bytes)
{
    std::lock_guard<std::mutex> lock(childMutex_);
    childDecoder_.append(bytes);

    while (const std::optional<QByteArray> frame = childDecoder_.next()) {
        const QByteArray copy = *frame;
        // The decoder is owned by the reader thread; the UI gets a copy.
        mailbox_.post([this, copy] { handleChildFrame(copy); });
    }
}

void PoolDemo::handleChildFrame(const QByteArray& frame)
{
    if (!frame.startsWith("PONG "))
        return;

    const QList<QByteArray> parts = frame.split(' ');
    if (parts.size() < 2)
        return;

    bool ok = false;
    const std::uint64_t sequence = parts.at(1).toULongLong(&ok);
    if (!ok)
        return;

    childRx_.fetch_add(1, std::memory_order_relaxed);

    const auto pending = pendingPings_.find(sequence);
    if (pending == pendingPings_.end())
        return;

    const double micros = std::chrono::duration<double, std::micro>(
                              std::chrono::steady_clock::now() - pending->second)
                              .count();
    lastChildRttUs_.store(micros);
    pendingPings_.erase(pending);
}

void PoolDemo::sendPing()
{
    if (!child_)
        return;

    const std::uint64_t sequence = ++pingSequence_;
    QByteArray payload = "PING ";
    payload += QByteArray::number(static_cast<qint64>(sequence));

    if (!child_->write(md::pool::ipc::encodeFrame(payload)))
        return;

    pendingPings_[sequence] = std::chrono::steady_clock::now();
    childTx_.fetch_add(1, std::memory_order_relaxed);

    if (pendingPings_.size() > 64)
        pendingPings_.erase(pendingPings_.begin());
}

void PoolDemo::stopChild()
{
    if (!child_)
        return;

    child_->write(md::pool::ipc::encodeFrame(QByteArrayLiteral("SHUTDOWN")));
    if (child_->waitForFinished(1000) < 0)
        child_->terminate(0);

    childExited_ = true;
    childExitCode_ = child_->waitForFinished(0);
    log(QStringLiteral("echo child stopped (%1)").arg(childExitCode_));

    child_.reset();
    childRing_.reset();
    childRingName_.clear();
    pendingPings_.clear();

    {
        std::lock_guard<std::mutex> lock(childMutex_);
        childDecoder_.reset();
    }
}

void PoolDemo::log(const QString& line)
{
    log_.prepend(line);
    while (log_.size() > 5)
        log_.removeLast();
}
