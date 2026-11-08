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
#include <QVariantAnimation>
#include <QtGlobal>

#include <functional>

namespace md::animation {

class Animator : public QObject
{
    Q_OBJECT

public:
    explicit Animator(QObject* parent = nullptr);
    ~Animator() override;

    Animator(const Animator&) = delete;
    Animator& operator=(const Animator&) = delete;

    // Drives a 0..1 progress value over `spec.duration` after `spec.delay`,
    // shaped by `spec.easing`. `onValue` is called with every new progress
    // value; the final call is always 1.0 for a run that completes.
    //
    // Starting while a run is in flight retargets: the old run is stopped
    // without emitting finished(), and the new run starts from the last
    // emitted progress with duration scaled by (1 - progress), so the value
    // never jumps back to 0.
    //
    // A zero-duration spec (which is what reduced motion produces) snaps:
    // onValue(1.0) and finished() are delivered on the next event-loop turn,
    // never synchronously inside start().
    void start(MotionSpec spec, std::function<void(qreal)> onValue);

    // Stops the current run without emitting finished(). progress() keeps the
    // last value that was emitted, ready for a caller that wants to snap to
    // the end state itself.
    void stop();

    // True between start() and the delivery of finished()/stop(). For a
    // zero-duration run this stays true until the queued finish is delivered.
    [[nodiscard]] bool isRunning() const noexcept;

    // The last progress value the animator emitted, in 0..1. A freshly
    // constructed animator reports 0.0.
    [[nodiscard]] qreal progress() const noexcept;

signals:
    // Emitted after the run reached 1.0 and onValue(1.0) was delivered. Not
    // emitted by stop() or by the destructor.
    void finished();

private:
    // Begins the QVariantAnimation part of a run, once any delay has elapsed.
    void beginRun(const MotionSpec& spec, qreal from, qint64 remainingMs);
    // Delivers the final value and finished() for a naturally completed run.
    void completeRun();
    // Delivers the queued snap of a zero-duration run.
    void snapRun(quint64 generation);

    QVariantAnimation animation_;
    std::function<void(qreal)> onValue_;
    qreal progress_ = 0.0;
    bool running_ = false;
    // Incremented whenever a run starts or is cancelled, so queued snaps,
    // delayed starts and late animation signals can tell that they are stale.
    quint64 generation_ = 0;
};

} // namespace md::animation
