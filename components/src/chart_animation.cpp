// Periodicity — the chart animation state implementation (framework.md §5.9).
//
// The value never touches a clock. It resolves a MotionSpec from the token
// ladder and asks its Driver to play it; the chart owns the Animator, so this
// file stays a pure state machine that a headless test can drive with a fake
// driver.

#include <md/components/chart_animation.h>

#include <algorithm>
#include <chrono>

namespace md {

namespace {

// The duration of one rung of the motion ladder. The scheme owns the mapping
// (Level0 = 0 ms, then 250 ms apart), so the chart does not restate the table.
[[nodiscard]] std::chrono::milliseconds levelDuration(MotionLevel level) noexcept
{
    return MotionScheme::baseline().level(level).duration;
}

// Update never exceeds Level2 (500 ms). A long reveal is an entrance; a data
// change is an acknowledgement, and replaying a 1500 ms entrance on every live
// update would make the chart feel unresponsive.
[[nodiscard]] MotionLevel updateLevel(MotionLevel level) noexcept
{
    const auto raw = static_cast<quint8>(level);
    const auto cap = static_cast<quint8>(MotionLevel::Level2);
    return raw > cap ? MotionLevel::Level2 : level;
}

} // namespace

ChartAnimation::ChartAnimation(const ChartAnimation& other)
    : duration_(other.duration_)
    , delay_(other.delay_)
    , easing_(other.easing_)
    , reducedMotion_(other.reducedMotion_)
    , progress_(other.progress_)
    , running_(false)
    , mode_(other.mode_)
    , driver_(nullptr)
{
    // A copy is a snapshot of the configuration and the last painted progress.
    // It is deliberately not running and not attached: adopting it is what
    // gives it a player again, and two values must never drive one chart.
}

ChartAnimation& ChartAnimation::operator=(const ChartAnimation& other)
{
    if (this == &other)
        return *this;

    duration_ = other.duration_;
    delay_ = other.delay_;
    easing_ = other.easing_;
    reducedMotion_ = other.reducedMotion_;
    progress_ = other.progress_;
    running_ = false;
    mode_ = other.mode_;
    driver_ = nullptr;
    return *this;
}

void ChartAnimation::setDuration(MotionLevel level) noexcept
{
    const auto raw = static_cast<quint8>(level);
    const auto highest = static_cast<quint8>(MotionLevel::Level6);
    duration_ = raw > highest ? MotionLevel::Level6 : level;
}

MotionLevel ChartAnimation::duration() const noexcept
{
    return duration_;
}

void ChartAnimation::setDelay(MotionLevel level) noexcept
{
    const auto raw = static_cast<quint8>(level);
    const auto highest = static_cast<quint8>(MotionLevel::Level6);
    delay_ = raw > highest ? MotionLevel::Level6 : level;
}

MotionLevel ChartAnimation::delay() const noexcept
{
    return delay_;
}

void ChartAnimation::setEasing(Easing easing) noexcept
{
    const auto raw = static_cast<quint8>(easing);
    const auto highest = static_cast<quint8>(Easing::LegacyAccelerate);
    easing_ = raw > highest ? Easing::Standard : easing;
}

Easing ChartAnimation::easing() const noexcept
{
    return easing_;
}

void ChartAnimation::setReducedMotion(bool reduced) noexcept
{
    if (reducedMotion_ == reduced)
        return;

    reducedMotion_ = reduced;

    if (!reduced || !running_)
        return;

    // A run that is already in flight must not keep animating once the caller
    // asked for reduced motion: stop it and settle on the finished frame.
    if (driver_ != nullptr)
        driver_->stopChartAnimation(*this);

    finishRun();
}

bool ChartAnimation::isReducedMotion() const noexcept
{
    return reducedMotion_;
}

void ChartAnimation::play(PlayMode mode)
{
    if (mode == PlayMode::None) {
        stop();
        return;
    }

    mode_ = mode;
    progress_ = 0.0;
    running_ = true;

    if (driver_ == nullptr) {
        // Nothing can deliver frames, so claiming a run would leave progress
        // at 0 forever. Settle instead: an unattached value paints finished.
        finishRun();
        return;
    }

    driver_->startChartAnimation(*this, mode);
}

void ChartAnimation::stop()
{
    if (driver_ != nullptr)
        driver_->stopChartAnimation(*this);

    progress_ = 1.0;
    running_ = false;
    mode_ = PlayMode::None;
}

qreal ChartAnimation::progress() const noexcept
{
    return progress_;
}

bool ChartAnimation::isRunning() const noexcept
{
    return running_;
}

ChartAnimation::PlayMode ChartAnimation::mode() const noexcept
{
    return mode_;
}

MotionSpec ChartAnimation::specFor(PlayMode mode) const noexcept
{
    MotionSpec spec;
    spec.easing = easingCurve(easing_);

    if (mode == PlayMode::None)
        return spec;

    const MotionLevel runLevel = mode == PlayMode::Update ? updateLevel(duration_) : duration_;
    spec.duration = levelDuration(runLevel);
    spec.delay = levelDuration(delay_);

    if (reducedMotion_) {
        spec.duration = std::chrono::milliseconds{0};
        spec.delay = std::chrono::milliseconds{0};
    }

    return spec;
}

void ChartAnimation::attach(Driver* driver) noexcept
{
    driver_ = driver;
}

void ChartAnimation::beginRun(PlayMode mode) noexcept
{
    mode_ = mode;
    progress_ = 0.0;
    running_ = true;
}

void ChartAnimation::setRunProgress(qreal progress) noexcept
{
    progress_ = std::clamp(progress, 0.0, 1.0);
    if (progress_ >= 1.0)
        running_ = false;
}

void ChartAnimation::finishRun() noexcept
{
    progress_ = 1.0;
    running_ = false;
}

} // namespace md
