// Periodicity — the state behind the gallery's Pool page.
//
// The page is a live read-out, so the objects it reads from must outlive a page
// rebuild: the gallery rebuilds every page when the theme changes, and a
// thread pool that restarted on every theme toggle would be a lie. PoolDemo
// is owned by the window, the page only borrows it.
//
// It owns one of every pool facility:
//
//   ThreadPool        jobs the page submits (and the task-node ObjectPool)
//   ObjectPool        a second, page-driven pool so reuse is visible directly
//   SpscRingBuffer    a producer/consumer pair filling it as fast as it drains
//   Channel           a second producer/consumer pair through the closeable queue
//   IoThread          timers, an immediate queue and a real file read
//   Mailbox           the worker -> UI hop for every result the page shows
//   ChildProcess      an echo process over two pipes
//   SharedRingBuffer  a 1 MiB ring the child streams TICK frames into
//
// The UI thread only ever calls the control methods and snapshot(); everything
// that runs elsewhere reports through atomics or through the mailbox.

#pragma once

#include <md/pool/channel.h>
#include <md/pool/io_thread.h>
#include <md/pool/ipc/child_process.h>
#include <md/pool/ipc/frame.h>
#include <md/pool/ipc/shared_ring.h>
#include <md/pool/mailbox.h>
#include <md/pool/ring_buffer.h>
#include <md/pool/thread_pool.h>
#include <md/pool/object_pool.h>

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

class PoolDemo
{
public:
    struct Snapshot
    {
        md::pool::ThreadPool::Stats pool;

        std::size_t demoPoolCapacity = 0;
        std::size_t demoPoolLive = 0;
        std::size_t demoPoolHighWaterMark = 0;

        std::size_t ringCapacity = 0;
        std::size_t ringUsed = 0;
        std::uint64_t ringProduced = 0;
        std::uint64_t ringConsumed = 0;
        std::uint64_t ringDropped = 0;
        double ringPerSecond = 0.0;

        std::uint64_t channelProduced = 0;
        std::uint64_t channelConsumed = 0;
        double channelPerSecond = 0.0;

        md::pool::IoThread::Stats io;
        QString ioFileStatus;

        std::uint64_t mailboxDelivered = 0;
        std::size_t mailboxPending = 0;

        std::uint64_t pingRequests = 0;
        std::uint64_t pingReplies = 0;
        double lastWorkerRttUs = 0.0;

        std::uint64_t childTx = 0;
        std::uint64_t childRx = 0;
        std::uint64_t childShmFrames = 0;
        double childShmPerSecond = 0.0;
        double childRttUs = 0.0;
        bool childRunning = false;
        qint64 childPid = 0;
        int childExitCode = -1;

        QStringList log;
    };

    PoolDemo();
    ~PoolDemo();

    PoolDemo(const PoolDemo&) = delete;
    PoolDemo& operator=(const PoolDemo&) = delete;

    // Drains the shared ring and recomputes the per-second figures. Called by
    // the page's ticker before it reads snapshot().
    void poll();

    [[nodiscard]] Snapshot snapshot() const;

    // Thread pool.
    void submitJobs(int count);
    void setPaused(bool paused);
    [[nodiscard]] bool drainPool();
    void restartPool();

    // Object pool (page-driven churn on the UI thread).
    void churnObjectPool(int count);
    void resetObjectPool();

    // Ring buffer and channel stress pairs.
    void startRingStress();
    void stopRingStress();
    void startChannelDemo();
    void stopChannelDemo();

    // IO thread.
    void postIoTask();
    void postIoDelayed();
    void readProjectFile();
    void cancelIoTimers();

    // Worker -> UI mailbox round trip.
    void pingWorker(int count);

    // Process communication.
    void startChild();
    void sendPing();
    void stopChild();

private:
    struct Payload
    {
        std::uint64_t sequence = 0;
        std::uint64_t scratch[7] = {};
    };

    void stopAll();
    void ringProducerLoop();
    void ringConsumerLoop();
    void channelProducerLoop();
    void channelConsumerLoop();
    void onChildBytes(QByteArrayView bytes);
    void handleChildFrame(const QByteArray& frame);
    void log(const QString& line);
    [[nodiscard]] QString echoExecutable() const;

    md::pool::ThreadPool pool_;
    md::pool::IoThread io_;
    md::pool::Mailbox mailbox_;

    // Object pool demonstration, separate from the pool's task nodes so that a
    // reset here cannot disturb queued work.
    md::pool::ObjectPool<Payload> demoPool_;

    // Ring buffer pair.
    md::pool::SpscRingBuffer<std::uint64_t> ring_{8192};
    std::atomic<bool> ringRunning_{false};
    std::atomic<std::uint64_t> ringProduced_{0};
    std::atomic<std::uint64_t> ringConsumed_{0};
    std::thread ringProducer_;
    std::thread ringConsumer_;

    // Channel pair.
    std::unique_ptr<md::pool::Channel<std::uint64_t>> channel_;
    std::atomic<bool> channelRunning_{false};
    std::atomic<std::uint64_t> channelProduced_{0};
    std::atomic<std::uint64_t> channelConsumed_{0};
    std::thread channelProducer_;
    std::thread channelConsumer_;

    // Worker mailbox round trip.
    std::atomic<std::uint64_t> pingRequests_{0};
    std::atomic<std::uint64_t> pingReplies_{0};
    std::atomic<double> lastWorkerRttUs_{0.0};
    std::atomic<double> lastChildRttUs_{0.0};

    // IO status, written on the UI thread only (the IO thread posts it over).
    QString ioFileStatus_;

    // Child process and its shared ring.
    std::unique_ptr<md::pool::ipc::ChildProcess> child_;
    std::unique_ptr<md::pool::ipc::SharedRingBuffer> childRing_;
    QString childRingName_;
    mutable std::mutex childMutex_;
    md::pool::ipc::FrameDecoder childDecoder_;
    std::map<std::uint64_t, std::chrono::steady_clock::time_point> pendingPings_;
    std::uint64_t pingSequence_ = 0;
    std::atomic<std::uint64_t> childTx_{0};
    std::atomic<std::uint64_t> childRx_{0};
    std::atomic<std::uint64_t> childShm_{0};
    bool childExited_ = false;
    int childExitCode_ = -1;
    std::uint64_t pendingTimerId_ = 0;
    QStringList log_;

    // Rate bookkeeping.
    std::chrono::steady_clock::time_point lastPoll_{};
    std::uint64_t lastRingConsumed_ = 0;
    std::uint64_t lastChannelConsumed_ = 0;
    std::uint64_t lastChildShm_ = 0;
    double ringPerSecond_ = 0.0;
    double channelPerSecond_ = 0.0;
    double childShmPerSecond_ = 0.0;
};
