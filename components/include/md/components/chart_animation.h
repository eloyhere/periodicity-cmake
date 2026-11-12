// Periodicity — the chart animation state (framework.md §5.9, §7).
//
// Every chart owns one ChartAnimation. It is a value: a motion configuration
// plus the progress of the run it describes. The chart itself owns the
// md::animation::Animator that plays that value, which is why this type stays a
// plain, copyable description instead of becoming a second QObject player in
// the component tree.
//
// Two modes exist:
//
//   Reveal   the first appearance of the chart. The data marks grow in over the
//            configured duration, after the configured delay.
//   Update   a data change after that first appearance. It is the same reveal
//            shape at a shorter duration (never above Level2, 500 ms), so a
//            live chart acknowledges the new data quickly instead of replaying
//            its whole entrance choreography. The previous data is not kept:
//            the morph is a re-reveal, not an interpolation between two data
//            sets.
//
// PlayMode::None is the absence of a run: it is the state of a fresh value and
// of one that has been stopped.
//
// Progress is eased. The Animator applies the curve from the motion tokens, so
// progress() is already shaped for painting and always lies in 0..1. It starts
// at 1.0, because a chart that has never animated must paint its settled frame
// (an offscreen grab, a chart embedded in a print pass) rather than a blank one.
//
// Reduced motion belongs to the state as well: setReducedMotion(true), or a
// theme whose MotionScheme reports reduced motion, makes play() settle on
// progress 1 without starting an animator. The chart's driver performs the snap
// so it can also repaint; this value only supplies the decision.
//
// Attachment. A ChartAnimation on its own cannot run: nothing would deliver the
// frames. ChartBase implements Driver and attaches itself when the value is
// constructed into a chart or adopted by ChartBase::setAnimation(). Copies drop
// the attachment, so a copied value can never steer the chart it was copied
// from.

#pragma once

#include <md/tokens/motion.h>

#include <QtGlobal>

namespace md {

class ChartBase;

class ChartAnimation
{
public:
    enum class PlayMode {
        None,   // no run: fresh value or stopped
        Reveal, // the chart appears for the first time
        Update, // the data changed after the first appearance
    };

    // The chart side of a run. ChartBase implements it; a test or a headless
    // tool may implement it too, which is what makes ChartAnimation testable
    // without a widget.
    class Driver
    {
    public:
        virtual ~Driver() = default;

        // Starts `mode` for `animation`. The driver reads the resolved motion
        // from animation.specFor(mode) and either starts an Animator or, for
        // reduced motion / an animated(false) chart / a zero-duration spec,
        // settles the value with finishRun() without creating one.
        virtual void startChartAnimation(ChartAnimation& animation, PlayMode mode) = 0;

        // Stops the run in flight, if any, and schedules the repaint. The value
        // settles itself afterwards (stop() and setReducedMotion(true) both do).
        virtual void stopChartAnimation(ChartAnimation& animation) = 0;
    };

    ChartAnimation() = default;
    ~ChartAnimation() = default;

    // A copy carries the configuration and the progress snapshot but never the
    // attachment: two values never drive the same chart. A copied value reports
    // isRunning() == false until a chart adopts it.
    ChartAnimation(const ChartAnimation& other);
    ChartAnimation& operator=(const ChartAnimation& other);

    // The run length, from the motion-level ladder. Level0 is 0 ms (a snap);
    // the rest are 250 ms apart up to Level6 = 1500 ms. Default Level3
    // (750 ms): long enough to read as a reveal, short enough not to delay the
    // first useful frame. Changing it does not retime a run in flight; it
    // applies to the next play().
    void setDuration(MotionLevel level) noexcept;
    [[nodiscard]] MotionLevel duration() const noexcept;

    // How long the run waits before its first frame, from the same ladder.
    // Default Level0 (no delay). Changing it applies to the next play().
    void setDelay(MotionLevel level) noexcept;
    [[nodiscard]] MotionLevel delay() const noexcept;

    // The easing curve of the run. Default Easing::Standard, the baseline's
    // standard curve. The Animator applies it; progress() is the eased value.
    void setEasing(Easing easing) noexcept;
    [[nodiscard]] Easing easing() const noexcept;

    // An explicit reduced-motion override on top of the theme's
    // MotionScheme::reducedMotion. Either one makes play() snap to the finished
    // frame. Setting it true while a run is in flight stops that run and
    // settles it; setting it false cannot re-enable motion that the theme
    // already reduced.
    void setReducedMotion(bool reduced) noexcept;
    [[nodiscard]] bool isReducedMotion() const noexcept;

    // Starts a run. progress() drops to 0 and isRunning() becomes true; the
    // driver then either animates the value or, for a reduced / zero-duration
    // run, settles it on 1 immediately. play(PlayMode::None) is equivalent to
    // stop(). A value that is not attached to a chart settles immediately: it
    // has nothing that could deliver frames.
    void play(PlayMode mode);

    // Ends the run in flight. The chart stays at progress 1.0 — the finished
    // frame — so a stopped chart is always repaint-safe and never freezes
    // half-revealed. progress() reports 1.0 and isRunning() false afterwards.
    void stop();

    // The eased progress of the current (or last) run, in 0..1. A fresh value
    // reports 1.0, the settled frame.
    [[nodiscard]] qreal progress() const noexcept;

    // True while a driver is animating this value (including the delay before
    // the first frame). False for a fresh value, a finished run, a stopped run
    // and a value that is not attached.
    [[nodiscard]] bool isRunning() const noexcept;

    // The mode of the current run, or of the last one for a finished run.
    // PlayMode::None before the first play() and after stop().
    [[nodiscard]] PlayMode mode() const noexcept;

    // The resolved motion of `mode`: duration and delay from this value's
    // levels, the curve from easing(), and both times zeroed when reduced
    // motion applies. Update caps its duration at Level2 (500 ms). The chart's
    // driver consumes this; a caller may read it to know how long a run will
    // take without starting one.
    [[nodiscard]] MotionSpec specFor(PlayMode mode) const noexcept;

private:
    friend class ChartBase;

    // Called by the driver installed in a ChartBase (and by nothing else).
    void attach(Driver* driver) noexcept;
    void beginRun(PlayMode mode) noexcept;
    void setRunProgress(qreal progress) noexcept;
    void finishRun() noexcept;

    MotionLevel duration_ = MotionLevel::Level3;
    MotionLevel delay_ = MotionLevel::Level0;
    Easing easing_ = Easing::Standard;
    bool reducedMotion_ = false;

    qreal progress_ = 1.0;
    bool running_ = false;
    PlayMode mode_ = PlayMode::None;
    Driver* driver_ = nullptr;
};

} // namespace md
