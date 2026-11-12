// Periodicity — the UI thread and its frame pipeline (framework.md §5.12).
//
// TaskQueue answers "what runs first?". UiThread answers "when does a turn of
// UI work run, and which part of it?" It gives the GUI thread an explicit
// frame with the phases every retained UI toolkit converges on:
//
//   Idle -> PreFrame -> Build -> Layout -> Paint -> PostFrame -> Idle
//
// PreFrame is the frame boundary: one-shot callbacks registered with
// addFrameCallback() run there. The EventThread uses that boundary to hand a
// decoded input batch to the UI thread. Build drains UserInput and UiUpdate
// tasks, Layout drains UiEvent tasks, Paint runs registered paint callbacks and
// is the interval FrameStats reports as paint time, PostFrame drains Background
// and Idle tasks. A postIdleTask() is different: it only runs from runIdle(),
// and only while no frame is pending, so cleanup never delays a frame.
//
// Coalescing and deadlines
// ------------------------
// A burst of requestFrame() calls in one event-loop turn produces one frame.
// The frame work itself is budgeted: once the budget has been used the current
// frame still reaches PostFrame, but it starts no further queued work and
// schedules a follow-up turn instead of looping immediately. An overrun is
// recorded once per frame.
//
// Every queued task carries a deadline. A task that is reached after its
// deadline is dropped and counted rather than executed late. The default TTL is
// one second; postTaskUntil() lets an embedder attach an explicit deadline.
// Expired idle work is dropped by the same rule.
//
// Threading
// ---------
// Frame state belongs to the GUI thread. postTask(), requestFrame() and the
// callback registration methods may be called from another thread (the event
// thread does), because the queue and callback lists are mutex-protected and
// frame scheduling is marshalled to the object's thread with a queued call.
// runFrame() and runIdle() are GUI-thread entry points, primarily for tests
// and embedders that drive their own loop.
//
// Installing a runtime
// --------------------
// UiThread::install() publishes one process-wide instance; when it is absent,
// View::requestUpdate() falls back to QWidget::update() and headless tests and
// embedders see exactly the pre-UiThread behaviour.

#pragma once

#include <md/schedule/task_queue.h>

#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <QtGlobal>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace md::schedule {

// Why a frame was asked for. The reasons are advisory: one frame serves all
// pending requests, and the last reason is kept for diagnostics.
enum class FrameRequest : quint8 {
    Update = 0,
    Resize = 1,
    Input = 2,
    Animation = 3,
};

// The phase the UI thread is in. The values are ordered as the pipeline runs;
// Idle is the resting state between frames.
enum class Phase : quint8 {
    Idle = 0,
    PreFrame = 1,
    Build = 2,
    Layout = 3,
    Paint = 4,
    PostFrame = 5,
};

class UiThread : public QObject
{
    Q_OBJECT

public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::microseconds;

    // Default life of a posted task: long enough for a busy frame, short enough
    // that a stalled UI does not replay stale work after it recovers.
    static constexpr Duration kDefaultTaskTtl = std::chrono::seconds(1);

    struct FrameStats
    {
        quint64 frames = 0;        // completed frames
        quint64 droppedFrames = 0; // frames that overran their budget
        quint64 overruns = 0;      // same event as droppedFrames, explicit name
        quint64 droppedTasks = 0;  // tasks discarded after their deadline
        double meanPaintUs = 0.0;  // mean of the recorded paint intervals
        double maxPaintUs = 0.0;   // longest recorded paint interval
        Phase phase = Phase::Idle;
        quint64 frameNumber = 0;
        std::size_t pendingTasks = 0; // queued frame work, including idle
    };

    explicit UiThread(QObject* parent = nullptr);
    ~UiThread() override;

    UiThread(const UiThread&) = delete;
    UiThread& operator=(const UiThread&) = delete;

    // The process-wide runtime. install() creates and publishes one instance;
    // uninstall() detaches it and deletes it. instance() is null when no
    // runtime was installed, which is the supported headless configuration.
    [[nodiscard]] static UiThread* instance() noexcept;
    static UiThread& install();
    static void uninstall();
    [[nodiscard]] bool isInstalled() const noexcept;

    [[nodiscard]] Phase phase() const noexcept;
    [[nodiscard]] quint64 frameNumber() const noexcept;
    [[nodiscard]] FrameRequest lastFrameRequest() const noexcept;

    // Requests one frame. Repeated calls before the frame runs collapse into
    // that frame; a call made while a frame is running requests the next turn.
    void requestFrame(FrameRequest reason = FrameRequest::Update);

    // One-shot callback, run at the start of PreFrame. A callback registered
    // while PreFrame is already running runs on the next frame. Exceptions are
    // contained. This is the event-thread hand-off boundary.
    void addFrameCallback(std::function<void()> callback);

    // Runs during Paint, in registration order, once per frame. A paint
    // callback is how an embedding renderer participates in the measured paint
    // interval; View reports its actual QWidget paints with recordPaint().
    void addPaintCallback(std::function<void()> callback);

    // Queues frame work. It runs in the phase its priority maps to:
    //   UserInput, UiUpdate -> Build
    //   UiEvent             -> Layout
    //   Background, Idle    -> PostFrame
    // A null callable is ignored. The default deadline is kDefaultTaskTtl.
    void postTask(Priority priority, std::function<void()> task);
    void postTask(Priority priority, std::function<void()> task, Duration ttl);
    void postTaskUntil(Priority priority, std::function<void()> task,
                       Clock::time_point deadline);

    // Queues work that runs only between frames: while a frame is pending the
    // idle queue stays untouched. The default deadline is kDefaultTaskTtl.
    void postIdleTask(std::function<void()> task);
    void postIdleTask(std::function<void()> task, Duration ttl);

    // Frame budget. The default is 8000 µs. Zero means no budget; a negative
    // value is clamped to zero.
    void setFrameBudget(Duration budget);
    [[nodiscard]] Duration frameBudget() const noexcept;

    // Runs one frame synchronously on the GUI thread. Normally the event loop
    // calls this after requestFrame(); tests and embedders can pump it
    // explicitly. Returns false when a frame is already running.
    bool runFrame();

    // Drains idle work if no frame is pending. Returns the number of tasks run.
    // Also normally event-loop driven; exposed for deterministic tests.
    int runIdle(int maxTasks = 64);

    // Last-size coalescing for layout: repeated requests for one target keep
    // only the newest size. takeResizeRequests() hands the batch to the host
    // and clears it. requestResize() requests a frame with FrameRequest::Resize.
    void requestResize(quint64 target, QSizeF size);
    [[nodiscard]] QSizeF pendingResize(quint64 target) const;
    [[nodiscard]] int pendingResizeCount() const;
    [[nodiscard]] std::vector<std::pair<quint64, QSizeF>> takeResizeRequests();

    // Records one real paint interval. View calls this from paintEvent(); an
    // embedding renderer may call it from its own paint callback. The values
    // feed meanPaintUs/maxPaintUs.
    void recordPaint(Duration paintTime);

    [[nodiscard]] FrameStats stats() const;
    [[nodiscard]] quint64 droppedTasks() const noexcept;
    [[nodiscard]] int pending(Priority priority) const noexcept;
    [[nodiscard]] int pending() const noexcept;
    [[nodiscard]] bool framePending() const noexcept;

    // Deterministic time source for tests. The default uses steady_clock.
    void setClockForTesting(std::function<Clock::time_point()> clock);
    void resetClockForTesting();

private:
    struct Task
    {
        quint64 id = 0;
        std::function<void()> task;
        Clock::time_point deadline{};
    };

    void scheduleFrame();
    void scheduleIdle();
    void setPhase(Phase phase) noexcept;
    void runPaint();
    int drainPhase(Priority priority, Clock::time_point frameStarted, bool& overBudget,
                   int maxTasks);
    int drainIdle(int maxTasks);
    [[nodiscard]] bool popTask(std::deque<Task>& queue, Task& out, bool& expired);
    [[nodiscard]] bool expired(const Task& task) const;
    [[nodiscard]] Clock::time_point now() const;
    [[nodiscard]] int pendingLocked() const noexcept;

    mutable std::mutex mutex_;
    std::array<std::deque<Task>, kPriorityCount> queues_;
    std::deque<Task> idle_;
    std::vector<std::function<void()>> frameCallbacks_;
    std::vector<std::function<void()>> paintCallbacks_;
    std::unordered_map<quint64, QSizeF> resizeRequests_;
    std::function<Clock::time_point()> clockForTesting_;

    FrameStats stats_;
    quint64 droppedTasks_ = 0;
    double totalPaintUs_ = 0.0;
    quint64 paintSamples_ = 0;
    quint64 nextTaskId_ = 1;

    Duration frameBudget_{8000};
    FrameRequest lastFrameRequest_ = FrameRequest::Update;

    std::atomic<Phase> phase_{Phase::Idle};
    std::atomic<quint64> frameNumber_{0};
    std::atomic<bool> framePending_{false};
    std::atomic<bool> frameScheduled_{false};
    std::atomic<bool> idleScheduled_{false};
    std::atomic<bool> inFrame_{false};
    bool installed_ = false;
};

} // namespace md::schedule
