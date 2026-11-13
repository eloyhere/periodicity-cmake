// Periodicity — the animation layer (framework.md §5.6).
//
// Animator is the only stateful part of the motion system: it turns a
// MotionSpec into a 0..1 progress stream. Durations, delays and easing curves
// live in the tokens layer, so a component never writes a millisecond literal
// and the animator never invents a curve.
//
// One Animator owns one QVariantAnimation, reused for every run. There is no
// QTimer for callers to manage and no QWidget dependency: the animator works
// in a headless application.

#pragma once

#include <md/tokens/motion.h>

#include <QObject>
#include <QTimer>
#include <QVariantAnimation>
#include <QtGlobal>

#include <functional>
#include <limits>

namespace md::animation {

class Animator : public QObject
{
    Q_OBJECT

public:
    // The longest delay or duration the animator can honour, in milliseconds.
    //
    // QVariantAnimation and QTimer measure time in `int` milliseconds, so a
    // longer value is clamped to this one (about 24.8 days) instead of being
    // truncated into a different number of milliseconds. A run or a delay above
    // the cap therefore finishes or fires early, never immediately.
    static constexpr qint64 kMaximumTimelineMs = std::numeric_limits<int>::max();

    explicit Animator(QObject* parent = nullptr);
    ~Animator() override;

    Animator(const Animator&) = delete;
    Animator& operator=(const Animator&) = delete;

    // Drives a 0..1 progress value over `spec.duration` after `spec.delay`,
    // shaped by `spec.easing`. `onValue` is called with every new progress
    // value; the final call is always 1.0 for a run that completes.
    //
    // `onValue` may call start(), stop(), pause() or resume(), and it may
    // destroy the animator — deleting the widget that owns it is the usual
    // case. The callback is invoked from a copy and a QPointer guards the state
    // after it returns, so no member is touched and no signal is emitted once
    // the animator is gone. An exception thrown by the callback is swallowed so
    // that it cannot unwind through Qt's animation stack.
    //
    // Starting while a run is in flight retargets: the old run is stopped
    // without emitting finished(), and the new run starts from the last emitted
    // progress with the duration scaled by (1 - progress), so the value never
    // jumps back to 0. Starting from a completed run (progress 1.0) starts a
    // fresh run from 0, so the new callback runs instead of being skipped.
    //
    // A zero-duration spec (which is what reduced motion produces) snaps:
    // onValue(1.0) and finished() are delivered on the next event-loop turn,
    // never synchronously inside start(). The delay is part of the spec, so a
    // zero-duration run with a delay waits out the delay and then snaps.
    //
    // A duration or delay above kMaximumTimelineMs is clamped to it.
    void start(MotionSpec spec, std::function<void(qreal)> onValue);

    // Stops the current run without emitting finished(). progress() keeps the
    // last value that was emitted, ready for a caller that wants to snap to the
    // end state itself. stop() ends the run: a later start() begins a fresh run
    // from 0. Use pause() to suspend a run that should continue.
    void stop();

    // Suspends the current run — including one that is still waiting out its
    // delay — without emitting finished() and without losing progress. A paused
    // animator is not running; resume() continues from the last emitted progress
    // with only the time that was left, so the value never rewinds. Pausing an
    // idle or already paused animator does nothing, and so does resuming one
    // that is not paused.
    void pause();
    void resume();

    // Deterministic clock -------------------------------------------------
    // The default clock is the event loop: a run advances with wall-clock
    // time. A manual clock is for a host that renders on its own cadence and
    // for tests: start() records the run but arms no timer, and
    // advanceTimeline() consumes exactly the time it is given. The progress,
    // easing, final value and finished() signal are identical to a real run,
    // so the caller does not need a second animation path.
    //
    // Enabling or disabling the manual clock stops a run that is already in
    // flight; enable it before start(). pause()/resume() keep their meaning
    // and do not consume timeline time.
    void setManualClock(bool manual);
    [[nodiscard]] bool isManualClock() const noexcept;

    // Consumes `delta` of the current manual run. A run started without the
    // manual clock, a paused run and an idle run ignore it. A zero-duration
    // run settles on the first call. The final value and finished() are
    // delivered when the consumed time reaches the run duration.
    void advanceTimeline(std::chrono::milliseconds delta);

    // True while a run is in flight: waiting out its delay, animating, or
    // waiting for the queued snap of a zero-duration run. A paused run is not
    // running.
    [[nodiscard]] bool isRunning() const noexcept;

    // True between pause() and resume()/stop(): the run is suspended, including
    // while a delay or the queued snap of a zero-duration run is waiting.
    [[nodiscard]] bool isPaused() const noexcept;

    // The last progress value the animator emitted, in 0..1. A freshly
    // constructed animator reports 0.0.
    [[nodiscard]] qreal progress() const noexcept;

signals:
    // Emitted after the run reached 1.0 and onValue(1.0) was delivered. Not
    // emitted by stop(), pause() or the destructor.
    void finished();

private:
    // Where a run is in its life cycle. Waiting covers both a real delay and
    // the queued turn that delivers a zero-duration snap or a completion, so a
    // run is never completed synchronously inside start().
    enum class State { Idle, Waiting, Running, Paused };

    // Arms the delay timer for the current run, or begins the animation when
    // there is no time left to wait.
    void arm();
    // Delivers the queued snap or completion of a Waiting run, or begins the
    // animation once the delay has elapsed.
    void onWaitElapsed();
    // Begins the QVariantAnimation part of a run, once any delay has elapsed.
    void beginRun(const MotionSpec& spec, qreal from, qint64 remainingMs);
    // Delivers the final value, when one is still owed, and finished() for a
    // run that reached its end.
    void finishRun(bool deliverEvenIfComplete);
    // Stops the QVariantAnimation, deferring the call when it is executing.
    void stopAnimation();
    // Invokes onValue_ through a copy, guarding against the callback destroying
    // the animator. Returns false when the animator no longer exists.
    [[nodiscard]] bool invokeValue(qreal value);

    QVariantAnimation animation_;
    // A child rather than a value member, so it follows the animator to another
    // thread and is destroyed with it.
    QTimer* delayTimer_ = nullptr;
    std::function<void(qreal)> onValue_;
    MotionSpec activeSpec_;
    qreal progress_ = 0.0;
    State state_ = State::Idle;
    // The parts of the current run that a deferred or paused restart needs:
    // where it starts, how much animation time is left and how much delay is
    // still ahead. They are captured by start() so no lambda needs to outlive
    // the state it refers to.
    qreal activeFrom_ = 0.0;
    qint64 activeRemainingMs_ = 0;
    qint64 activeDelayMs_ = 0;
    // True when the current run must be armed on the next event-loop turn
    // because start() was called from inside onValue.
    bool restartPending_ = false;
    // True while onValue is on the stack. The animator must not reconfigure
    // QVariantAnimation from there, and it must not destroy the callback that
    // is executing.
    bool inValueCallback_ = false;
    // Manual-clock state: how much of the delay and of the run have been
    // consumed, where the run started, and whether the clock is manual.
    bool manualClock_ = false;
    qint64 manualDelayLeftMs_ = 0;
    qint64 manualElapsedMs_ = 0;
    qint64 manualRemainingMs_ = 0;
    // Where pause() interrupted the run and how much time was left.
    bool pausedWhileWaiting_ = false;
    qint64 pausedRemainingMs_ = 0;
    qint64 pausedDelayMs_ = 0;
    // Incremented whenever a run starts or is cancelled, so a final value
    // delivered from inside onValue can tell that the callback superseded it.
    quint64 generation_ = 0;
};

} // namespace md::animation
