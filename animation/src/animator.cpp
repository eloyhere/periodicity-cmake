// Periodicity — the animator (framework.md §5.6).
//
// The animator owns exactly one QVariantAnimation, constructed with the
// Animator and reused for every run. That keeps the frame timer, the easing
// curve and the interpolation state in one place, and it means restarting an
// animation does not allocate.

#include <md/animation/animator.h>

#include <QMetaObject>
#include <QSignalBlocker>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <limits>

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
    return qRound64(static_cast<double>(total) * static_cast<double>(fraction));
}

int toAnimationDuration(qint64 milliseconds)
{
    return static_cast<int>(std::min<qint64>(milliseconds,
                                             std::numeric_limits<int>::max()));
}

} // namespace

Animator::Animator(QObject* parent)
    : QObject(parent)
{
    animation_.setObjectName(QStringLiteral("md.animation.Animator.animation"));
    animation_.setStartValue(0.0);
    animation_.setEndValue(1.0);
    animation_.setEasingCurve(QEasingCurve(QEasingCurve::Linear));

    connect(&animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        if (!running_)
            return;

        const qreal next = qBound(qreal(0.0), value.toReal(), qreal(1.0));
        if (next == progress_)
            return;

        progress_ = next;
        if (onValue_)
            onValue_(progress_);
    });

    connect(&animation_, &QVariantAnimation::finished, this, [this] {
        if (!running_)
            return;

        // Depending on the Qt version a manual stop can surface as finished();
        // only a run that actually reached its end completes.
        if (animation_.currentTime() < animation_.duration())
            return;

        completeRun();
    });
}

Animator::~Animator()
{
    // Invalidate queued snaps and delayed starts, and detach the callback
    // before the animation is torn down so nothing runs during destruction.
    ++generation_;
    running_ = false;
    onValue_ = nullptr;
    animation_.stop();
}

void Animator::start(MotionSpec spec, std::function<void(qreal)> onValue)
{
    // Freeze the old run at its last emitted value; a fresh run starts at 0.
    const qreal from = running_ ? progress_ : 0.0;

    ++generation_;
    running_ = false; // ignore anything the stop below emits
    animation_.stop();

    onValue_ = std::move(onValue);

    if (spec.duration.count() <= 0) {
        // Reduced motion, or a spec that asks for no duration at all: snap.
        // Both the value and finished() arrive on the next event-loop turn so
        // that start() can never re-enter the caller's callback.
        const quint64 generation = generation_;
        running_ = true;
        QMetaObject::invokeMethod(this, [this, generation] { snapRun(generation); },
                                  Qt::QueuedConnection);
        return;
    }

    const qint64 remaining = remainingDuration(spec.duration, from);
    if (remaining <= 0) {
        // Already at the end of the range: there is nothing to interpolate.
        // Report completion on the next turn for the same re-entrancy reason.
        const quint64 generation = generation_;
        running_ = true;
        QMetaObject::invokeMethod(this, [this, generation] {
            if (generation != generation_ || !running_)
                return;
            completeRun();
        }, Qt::QueuedConnection);
        return;
    }

    if (spec.delay.count() > 0) {
        const quint64 generation = generation_;
        const MotionSpec delayed = spec;
        running_ = true;
        QTimer::singleShot(toAnimationDuration(spec.delay.count()), this,
                           [this, generation, delayed, from, remaining] {
                               if (generation != generation_ || !running_)
                                   return;
                               beginRun(delayed, from, remaining);
                           });
        return;
    }

    beginRun(spec, from, remaining);
}

void Animator::beginRun(const MotionSpec& spec, qreal from, qint64 remainingMs)
{
    // QVariantAnimation re-evaluates its value from the *old* currentTime as
    // soon as a property changes. After stop() that time is still the middle
    // of the previous run, so each setter below would emit a stale value and
    // the caller would see a jump before the run has begun — the delayed start
    // path is the visible case, because there running_ is already true.
    // Silence the reconfiguration; start() below emits the first real value.
    {
        const QSignalBlocker blocker(&animation_);
        animation_.setEasingCurve(spec.easing.toEasingCurve());
        animation_.setStartValue(from);
        animation_.setEndValue(1.0);
        animation_.setDuration(toAnimationDuration(remainingMs));
    }

    running_ = true;
    animation_.start();
}

void Animator::completeRun()
{
    if (!running_)
        return;

    const quint64 generation = generation_;
    running_ = false;

    // The easing curve ends at 1.0, but a frame-based animation can stop just
    // short of the last frame; the final value is always delivered.
    if (progress_ < 1.0) {
        progress_ = 1.0;
        if (onValue_)
            onValue_(1.0);
    }

    // A callback that restarted the animator owns the new run; the finished
    // signal for this one has been superseded.
    if (generation == generation_)
        emit finished();
}

void Animator::snapRun(quint64 generation)
{
    if (generation != generation_ || !running_)
        return;

    running_ = false;
    progress_ = 1.0;
    if (onValue_)
        onValue_(1.0);

    if (generation == generation_)
        emit finished();
}

void Animator::stop()
{
    ++generation_;
    running_ = false;
    animation_.stop();
    onValue_ = nullptr;
}

bool Animator::isRunning() const noexcept
{
    return running_;
}

qreal Animator::progress() const noexcept
{
    return progress_;
}

} // namespace md::animation
