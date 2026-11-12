// Periodicity — the event thread's input pipeline (framework.md §5.12).
//
// Input does not reach widgets one event at a time. The event thread collects
// it in a lock-protected queue, collapses continuous input per target (the last
// pointer position and the last wheel delta win) and hands one ordered batch to
// the UI thread at the next frame boundary:
//
//   PointerDown / PointerUp / Key   (discrete input, every event preserved)
//   Wheel                            (coalesced per target)
//   PointerMove                      (coalesced per target, last position wins)
//   Gesture                          (after the moves they were built from)
//
// The batch is posted with UiThread::postTask(Priority::UiEvent, ...) from a
// frame callback, so decoding/coalescing and painting interleave by frame
// instead of thrashing the event loop. The event thread never touches a widget:
// its payload carries an opaque target token, never a QWidget pointer. A batch
// handler runs on the UI thread in the Layout phase and is where an application
// turns the payloads into framework events for EventDispatcher.
//
// The queue is a mutex around deques/maps, not one channel per event; postInput
// may be called from any producer thread and takes the lock only long enough to
// insert or replace one entry. A coalescing map keeps the queue depth bounded
// by the number of targets, not by pointer sampling frequency.

#pragma once

#include <md/schedule/frame_scheduler.h>

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QVariant>
#include <Qt>
#include <QtGlobal>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace md::schedule {

enum class InputKind : quint8 {
    PointerDown = 0,
    PointerMove = 1,
    PointerUp = 2,
    Wheel = 3,
    Key = 4,
    Gesture = 5,
};

// The wire format between the event thread and the UI thread. `target` is an
// opaque identity token chosen by the producer (a widget id, a route id); it is
// deliberately not a pointer, because the event thread must not touch widgets.
// `data` carries decoder-specific payload for a Gesture or a specialized key.
struct InputPayload
{
    quint64 target = 0;
    QPointF position;
    QPointF globalPosition;
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons = Qt::NoButton;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    int key = 0;
    int scanCode = 0;
    QPoint angleDelta;
    QPoint pixelDelta;
    QVariant data;
    std::chrono::milliseconds timestamp{0};
};

struct InputEvent
{
    InputKind kind = InputKind::PointerMove;
    InputPayload payload;
    quint64 sequence = 0;
};

class EventThread : public QObject
{
    Q_OBJECT

public:
    using Batch = std::vector<InputEvent>;
    using BatchHandler = std::function<void(const Batch&)>;

    struct Stats
    {
        quint64 posted = 0;     // postInput() calls accepted
        quint64 coalesced = 0;  // moves/wheels replaced in place
        quint64 batches = 0;    // non-empty batches taken
        quint64 handoffs = 0;   // batches posted to the UI thread
        quint64 delivered = 0;  // events passed to the batch handler
        std::size_t depth = 0;  // distinct queued events (after coalescing)
        bool running = false;
    };

    explicit EventThread(QObject* parent = nullptr);
    ~EventThread() override;

    EventThread(const EventThread&) = delete;
    EventThread& operator=(const EventThread&) = delete;

    // Process-wide runtime. install() attaches itself to UiThread::instance()
    // when one exists; the normal application order is UiThread::install()
    // then EventThread::install(). uninstall() detaches and deletes.
    [[nodiscard]] static EventThread* instance() noexcept;
    static EventThread& install();
    static void uninstall();
    [[nodiscard]] bool isInstalled() const noexcept;

    // Binds the boundary and hand-off target. The previous binding is dropped.
    // Both objects must outlive the binding; detach() clears it.
    void attach(UiThread* uiThread);
    void detach();
    [[nodiscard]] UiThread* uiThread() const noexcept;

    // Queues one decoded input. Thread-safe. Coalesces PointerMove/Wheel per
    // target and requests one frame for the burst.
    void postInput(InputKind kind, InputPayload payload);
    void postInput(InputKind kind, quint64 target, QPointF position);

    // The consumer of a handed-off batch. It is invoked on the UI thread in the
    // Layout phase. Setting an empty handler delivers nothing, but the queue is
    // still drained so it cannot grow without bound.
    void setBatchHandler(BatchHandler handler);

    // Removes and orders everything queued. Public for deterministic tests and
    // embedding applications; the normal path calls it at the frame boundary.
    [[nodiscard]] Batch takeBatch();

    // The frame-boundary operation: take a batch and post it to the UI thread
    // as a single Priority::UiEvent task. Normally called from the frame
    // callback; exposed so a host can pump it explicitly.
    void flushToUiThread();

    [[nodiscard]] Stats stats() const;
    [[nodiscard]] std::size_t depth() const;
    [[nodiscard]] static int priorityOf(InputKind kind) noexcept;

private:
    void ensureBoundary();
    void onFrameBoundary();
    void deliver(const Batch& batch);

    mutable std::mutex mutex_;
    std::deque<InputEvent> discrete_; // Down, Up, Key: every event preserved
    std::unordered_map<quint64, InputEvent> moves_;
    std::unordered_map<quint64, InputEvent> wheels_;
    std::deque<InputEvent> gestures_;
    BatchHandler handler_;
    Stats stats_;

    // Read by postInput() without the queue lock, so a producer thread never
    // waits on a GUI-thread callback registration. attach()/detach() are
    // GUI-thread operations; the runtime outlives its attached UiThread in the
    // supported installation order.
    std::atomic<UiThread*> ui_{nullptr};
    std::atomic<bool> boundaryArmed_{false};
    QMetaObject::Connection uiDestroyedConnection_;
    quint64 nextSequence_ = 1;
    bool installed_ = false;
};

} // namespace md::schedule
