// Periodicity — the animator (framework.md §5.6).
//
// The animator owns exactly one QVariantAnimation, constructed with the
// Animator and reused for every run. That keeps the frame timer, the easing
// curve and the interpolation state in one place, and it means restarting an
// animation does not allocate.
//
// Two rules make the callback safe to call: it is invoked from a copy, so
// start()/stop() from inside it can replace the member function without freeing
// the function that is on the stack, and every member access after it returns
// sits behind a QPointer, so a callback that deletes the animator (or the
// widget that owns it) ends the run instead of touching freed memory.

#include <md/animation/animator.h>

#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QSignalBlocker>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace md::animation {

namespace {

// The portion of `duration` that is still ahead when starting at `from`.
// Restarting mid-run must not replay the part that already happened, otherwise
// a retarget would slow the motion down.
qint64 remainingDuration(std::chrono::milliseconds duration, qreal from)
{
    const qint64 total = std::max<qint64>(0, duration.count());
    if (total == 0)
        return 0;

    const qreal fraction = 1.0 - qBound(qreal(0.0), from, qreal(1.0));

    // Saturate before rounding. `total * fraction` is computed in double so a
    // total close to INT64_MAX cannot overflow the multiplication, and the
    // comparison against 2^63 keeps qRound64 inside its domain (2^63 rounds to
    // itself in double, and qRound64 of that value is undefined).
    constexpr double kQint64Limit = 9223372036854775808.0; // 2^63
    const double product = static_cast<double>(total) * static_cast<double>(fraction);
    if (!(product < kQint64Limit))
        return std::numeric_limits<qint64>::max();

    return qRound64(product);
}

// The value the animation drivers can represent. Durations and delays beyond
// int milliseconds are documented as clamped to Animator::kMaximumTimelineMs;
// nothing is silently truncated to a different time.
int toAnimationDuration(qint64 milliseconds)
{
    const qint64 clamped = std::min<qint64>(std::max<qint64>(0, milliseconds),
                                            Animator::kMaximumTimelineMs);
    return static_cast<int>(clamped);
}

} // namespace

Animator::Animator(QObject* parent)
    : QObject(parent)
{
    animation_.setObjectName(QStringLiteral("md.animation.Animator.animation"));
    animation_.setStartValue(0.0);
    animation_.setEndValue(1.0);
    animation_.setEasingCurve(QEasingCurve(QEasingCurve::Linear));

    delayTimer_ = new QTimer(this);
    delayTimer_->setSingleShot(true);
    connect(delayTimer_, &QTimer::timeout, this, [this] { onWaitElapsed(); });

    connect(&animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        if (state_ != State::Running)
            return;

        const qreal next = qBound(qreal(0.0), value.toReal(), qreal(1.0));
        if (next == progress_)
            return;

        progress_ = next;
        // Nothing may touch the animator after this point: the callback is
        // allowed to restart it or to delete it.
        invokeValue(progress_);
    });

    connect(&animation_, &QVariantAnimation::finished, this, [this] {
        if (state_ != State::Running)
            return;

        // Depending on the Qt version a manual stop can surface as finished();
        // only a run that actually reached its end completes.
        if (animation_.currentTime() < animation_.duration())
            return;

        finishRun(false);
    });
}

Animator::~Animator()
{
    // Invalidate a pending final value and detach the callback before the
    // animation is torn down so nothing runs during destruction.
    ++generation_;
    state_ = State::Idle;
    delayTimer_->stop();
    onValue_ = nullptr;
    animation_.stop();
}

void Animator::start(MotionSpec spec, std::function<void(qreal)> onValue)
{
    // Retarget semantics: continue from the value the caller has already seen.
    // A completed run (progress 1.0) has nothing left to animate, so the new
    // run starts over at 0 and its callback sees the whole range.
    const bool continuing = state_ != State::Idle && progress_ < 1.0;
    activeFrom_ = continuing ? progress_ : 0.0;

    // Take the run over before touching QVariantAnimation: every handler checks
    // the state, so the outgoing run cannot be mistaken for this one.
    ++generation_;
    state_ = State::Idle;
    restartPending_ = false;
    delayTimer_->stop();
    stopAnimation();

    // invokeValue() calls a copy of onValue_, so replacing the member here
    // cannot destroy the function that is executing when start() is called from
    // inside the callback.
    onValue_ = std::move(onValue);
    activeSpec_ = spec;
    activeDelayMs_ = std::max<qint64>(0, spec.delay.count());
    activeRemainingMs_ = spec.duration.count() <= 0
                             ? 0
                             : remainingDuration(spec.duration, activeFrom_);

    if (inValueCallback_) {
        // start() came from inside onValue. Reconfiguring QVariantAnimation
        // while it is emitting is not safe, so the new run is armed on the next
        // event-loop turn; Waiting makes the outgoing animation's remaining
        // signals harmless in the meantime.
        restartPending_ = true;
        state_ = State::Waiting;
        delayTimer_->start(0);
        return;
    }

    arm();
}

void Animator::stop()
{
    ++generation_;
    state_ = State::Idle;
    restartPending_ = false;
    delayTimer_->stop();
    // Safe from inside the callback: the function that is executing is the copy
    // invokeValue() holds, not this member.
    onValue_ = nullptr;
    stopAnimation();
}

void Animator::pause()
{
    if (state_ == State::Idle || state_ == State::Paused)
        return;

    pausedWhileWaiting_ = state_ == State::Waiting;
    if (pausedWhileWaiting_) {
        // remainingTime() is -1 for a timer that is not running; a delay that
        // is about to fire reports 0 and resumes immediately.
        pausedDelayMs_ =
            std::max<qint64>(0, static_cast<qint64>(delayTimer_->remainingTime()));
        pausedRemainingMs_ = 0;
    } else {
        pausedDelayMs_ = 0;
        const qint64 elapsed = static_cast<qint64>(animation_.currentTime());
        pausedRemainingMs_ = std::max<qint64>(
            0, static_cast<qint64>(animation_.duration()) - elapsed);
    }

    state_ = State::Paused;
    stopAnimation();
}

void Animator::resume()
{
    if (state_ != State::Paused)
        return;

    if (inValueCallback_) {
        // pause() and resume() were both called from onValue; the animation may
        // only restart after that emission has unwound.
        QMetaObject::invokeMethod(this, [this] { resume(); }, Qt::QueuedConnection);
        return;
    }

    // A pause that ran inside the callback could only defer its stop; make sure
    // the animation is stopped before it is restarted.
    stopAnimation();

    if (pausedWhileWaiting_) {
        state_ = State::Waiting;
        if (restartPending_) {
            delayTimer_->start(0);
        } else if (pausedDelayMs_ > 0) {
            delayTimer_->start(toAnimationDuration(pausedDelayMs_));
        } else if (activeDelayMs_ > 0) {
            // The delay was already over (or about to be) when pause() ran:
            // continue into the run instead of replaying the whole delay.
            delayTimer_->start(0);
        } else {
            arm();
        }
        return;
    }

    if (pausedRemainingMs_ <= 0) {
        // Paused on the last frame: deliver the completion on the next turn.
        state_ = State::Waiting;
        activeRemainingMs_ = 0;
        delayTimer_->start(0);
        return;
    }

    state_ = State::Idle; // beginRun() installs Running
    beginRun(activeSpec_, progress_, pausedRemainingMs_);
}

bool Animator::isRunning() const noexcept
{
    return state_ == State::Waiting || state_ == State::Running;
}

bool Animator::isPaused() const noexcept
{
    return state_ == State::Paused;
}

qreal Animator::progress() const noexcept
{
    return progress_;
}

void Animator::arm()
{
    restartPending_ = false;

    if (activeDelayMs_ > 0) {
        state_ = State::Waiting;
        delayTimer_->start(toAnimationDuration(activeDelayMs_));
        return;
    }

    if (activeSpec_.duration.count() <= 0 || activeRemainingMs_ <= 0) {
        // A zero-duration run — or one with no time left — snaps on the next
        // event-loop turn, so start() never re-enters the caller's callback.
        state_ = State::Waiting;
        delayTimer_->start(0);
        return;
    }

    beginRun(activeSpec_, activeFrom_, activeRemainingMs_);
}

void Animator::onWaitElapsed()
{
    if (state_ != State::Waiting)
        return;

    if (restartPending_) {
        // start() only needed this turn to get out from under the
        // QVariantAnimation emission; take the animation over and arm the run.
        restartPending_ = false;
        stopAnimation();
        arm();
        return;
    }

    if (activeSpec_.duration.count() <= 0 || activeRemainingMs_ <= 0) {
        // A zero-duration run still owes its new callback the documented 1.0; a
        // run that merely had no time left only owes the value it has not
        // delivered yet.
        finishRun(activeSpec_.duration.count() <= 0);
        return;
    }

    beginRun(activeSpec_, activeFrom_, activeRemainingMs_);
}

void Animator::beginRun(const MotionSpec& spec, qreal from, qint64 remainingMs)
{
    // A previous run may still own the animation when a restart was deferred;
    // take it over before reconfiguring.
    stopAnimation();

    // QVariantAnimation re-evaluates its value from the *old* currentTime as
    // soon as a property changes. After stop() that time is still the middle
    // of the previous run, so each setter below would emit a stale value and
    // the caller would see a jump before the run has begun — the delayed start
    // path is the visible case, because there the state is already Waiting.
    // Silence the reconfiguration; start() below emits the first real value.
    {
        const QSignalBlocker blocker(&animation_);
        animation_.setEasingCurve(spec.easing.toEasingCurve());
        animation_.setStartValue(from);
        animation_.setEndValue(1.0);
        animation_.setDuration(toAnimationDuration(remainingMs));
    }

    state_ = State::Running;
    animation_.start();
}

void Animator::finishRun(bool deliverEvenIfComplete)
{
    if (state_ == State::Idle || state_ == State::Paused)
        return;

    state_ = State::Idle;
    const quint64 generation = generation_;

    // The easing curve ends at 1.0, but a frame-based animation can stop just
    // short of the last frame; the final value is always delivered. A run that
    // is completed without animating (a retarget with no time left) still owes
    // its callback the documented 1.0.
    if (deliverEvenIfComplete || progress_ < 1.0) {
        progress_ = 1.0;
        if (!invokeValue(progress_))
            return; // the callback destroyed the animator

        // A callback that restarted or stopped the animator owns the new run;
        // the finished signal for this one has been superseded.
        if (generation != generation_)
            return;
    }

    try {
        emit finished();
    } catch (...) {
        // An observer that throws must not unwind through Qt's animation stack.
        qWarning() << "md::animation::Animator: a finished() observer threw;"
                      " the exception was isolated";
    }
}

void Animator::stopAnimation()
{
    if (!inValueCallback_) {
        animation_.stop();
        return;
    }

    // Called from inside animation_'s own valueChanged emission, where stopping
    // it would re-enter Qt's update loop. The state machine has already moved
    // on, so the run's remaining signals are ignored; stop the timer on the next
    // event-loop turn unless a new run has taken the animation over by then.
    QMetaObject::invokeMethod(this, [this] {
        if (state_ == State::Idle || state_ == State::Paused)
            animation_.stop();
    }, Qt::QueuedConnection);
}

bool Animator::invokeValue(qreal value)
{
    // Copy the callback before calling it: onValue may call start() or stop(),
    // which replaces or clears the member, and the copy keeps the function that
    // is on the stack alive no matter what the callback does.
    const std::function<void(qreal)> callback = onValue_;
    if (!callback)
        return true;

    // The callback may destroy the animator, directly or by deleting the widget
    // that owns it, so every member access after the call is guarded.
    const QPointer<Animator> guard(this);
    inValueCallback_ = true;
    try {
        callback(value);
    } catch (...) {
        // A user callback must not unwind through Qt's animation stack.
    }

    if (guard.isNull())
        return false;

    inValueCallback_ = false;
    return true;
}

} // namespace md::animation
