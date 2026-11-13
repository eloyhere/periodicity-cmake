// Continuity and termination tests for the animation audit (docs/audit/animation.md).
//
// Every timeline assertion runs on the Animator's manual clock: the suite never
// sleeps and never waits for a wall-clock timer. The component assertions are
// synchronous — show()/hide()/setIndeterminate() deliver their state changes in
// the caller's turn, so "did it stop" is observable without pumping the event
// loop.
//
// The cases pin the defects the audit fixed:
//
//   * an interruption retargets from the value the caller already saw — it does
//     not jump back to 0 (a restart) and does not replay the whole duration;
//   * a settled run requests no further frames;
//   * a reduced-motion spec snaps on the first turn and never animates;
//   * an animated View tracks the View lifecycle (onShown/onHidden), so hiding
//     mid-animation settles and stops instead of leaving the animator running;
//   * a chart, and a pie in particular, has exactly one animation owner, and a
//     hidden chart settles its run.

#include <QApplication>
#include <QPixmap>
#include <QTest>

#include <md/animation/animator.h>
#include <md/components/pie_chart.h>
#include <md/components/progress.h>
#include <md/components/ring.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/motion.h>
#include <md/view/lifecycle.h>
#include <md/view/view.h>

#include <chrono>

namespace {

using Ms = std::chrono::milliseconds;

md::Theme reducedMotionTheme()
{
    md::Theme theme = md::Theme::baselineLight();
    md::MotionScheme motion = theme.motion();
    motion.reducedMotion = true;
    return theme.withMotion(motion);
}

// The spec every component resolves through the ladder: no literals on either
// side of the assertion.
md::MotionSpec levelSpec(md::MotionLevel level)
{
    return md::MotionScheme::baseline().level(level);
}

} // namespace

class TestMotionContinuity : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void interruptionRetargetsFromTheCurrentValue();
    void interruptionKeepsOnlyTheRemainingDuration();
    void settledRunRequestsNoFurtherFrames();
    void reducedMotionSpecSnapsOnTheFirstTurn();

    void hiddenProgressSettlesAndStops();
    void progressTracksTheViewLifecycle();
    void ringTracksTheViewLifecycle();

    void pieChartSweepHasASingleOwner();
    void hiddenChartStopsItsReveal();
    void pieChartReducedMotionSnaps();
};

void TestMotionContinuity::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
    QCOMPARE(md::Theme::applicationTheme(), md::Theme::baselineLight());
}

void TestMotionContinuity::interruptionRetargetsFromTheCurrentValue()
{
    md::animation::Animator animator;
    animator.setManualClock(true);

    const md::MotionSpec spec = levelSpec(md::MotionLevel::Level2);
    QVERIFY(spec.duration > Ms(0));

    qreal last = -1.0;
    int frames = 0;
    animator.start(spec, [&](qreal value) {
        last = value;
        ++frames;
    });

    // Advance to roughly the middle of the run. The easing makes the value
    // slightly ahead of the linear fraction, so the exact number is not pinned —
    // only that it is strictly between the endpoints.
    animator.advanceTimeline(Ms(200));
    QVERIFY(frames > 0);
    QVERIFY2(last > 0.05 && last < 0.95,
             qPrintable(QStringLiteral("mid-run value %1").arg(last)));

    const qreal before = last;
    const qreal progressBefore = animator.progress();

    // Interrupt with a new run. The animator carries the value it already
    // emitted into the new run: that is the retarget contract.
    qreal firstAfter = -1.0;
    animator.start(spec, [&](qreal value) {
        if (firstAfter < 0.0)
            firstAfter = value;
        last = value;
        ++frames;
    });

    // The stored progress is the continuity proof: a restart would have dropped
    // it to 0 here, before a single frame of the new run was emitted.
    QVERIFY2(qFuzzyCompare(animator.progress() + 1.0, progressBefore + 1.0),
             qPrintable(QStringLiteral("progress %1 after retarget, was %2")
                            .arg(animator.progress())
                            .arg(progressBefore)));

    animator.advanceTimeline(Ms(1));
    QVERIFY2(firstAfter >= 0.0, "the retargeted run emitted no value");
    QVERIFY2(qAbs(firstAfter - before) <= 0.05,
             qPrintable(QStringLiteral("jump from %1 to %2 across the interruption")
                            .arg(before)
                            .arg(firstAfter)));
}

void TestMotionContinuity::interruptionKeepsOnlyTheRemainingDuration()
{
    md::animation::Animator animator;
    animator.setManualClock(true);

    const md::MotionSpec spec = levelSpec(md::MotionLevel::Level2);

    animator.start(spec, [](qreal) {});
    animator.advanceTimeline(Ms(200));

    const qreal progressBefore = animator.progress();
    QVERIFY(progressBefore > 0.05 && progressBefore < 0.95);

    // Retarget. The new run may only spend the time that is left; restarting the
    // full token would make every interruption slower than the token says.
    animator.start(spec, [](qreal) {});
    const auto remaining = Ms(static_cast<qint64>((1.0 - progressBefore)
                                                 * static_cast<qreal>(spec.duration.count())));
    QVERIFY(remaining > Ms(0));

    animator.advanceTimeline(remaining - Ms(15));
    QVERIFY2(animator.isRunning(), "the run ended before the remaining time was consumed");
    QVERIFY(animator.progress() < 1.0);

    animator.advanceTimeline(Ms(20));
    QVERIFY(!animator.isRunning());
    QVERIFY2(qFuzzyCompare(animator.progress() + 1.0, 2.0),
             qPrintable(QStringLiteral("final progress %1").arg(animator.progress())));
}

void TestMotionContinuity::settledRunRequestsNoFurtherFrames()
{
    md::animation::Animator animator;
    animator.setManualClock(true);

    const md::MotionSpec spec = levelSpec(md::MotionLevel::Level1);

    qreal last = -1.0;
    int frames = 0;
    animator.start(spec, [&](qreal value) {
        last = value;
        ++frames;
    });

    // One advance past the end: the run completes.
    animator.advanceTimeline(spec.duration + Ms(50));
    QVERIFY(!animator.isRunning());
    QCOMPARE(last, 1.0);

    // A settled animation must not request another frame, however much timeline
    // is consumed afterwards.
    const int settledFrames = frames;
    animator.advanceTimeline(Ms(1000));
    QCOMPARE(frames, settledFrames);
    QCOMPARE(animator.progress(), 1.0);
    QVERIFY(!animator.isRunning());
}

void TestMotionContinuity::reducedMotionSpecSnapsOnTheFirstTurn()
{
    md::MotionScheme reduced;
    reduced.reducedMotion = true;

    const md::MotionSpec spec = reduced.level(md::MotionLevel::Level4);
    QVERIFY2(spec.duration.count() == 0, "a reduced-motion level must resolve to zero duration");

    md::animation::Animator animator;
    animator.setManualClock(true);

    qreal last = -1.0;
    int frames = 0;
    animator.start(spec, [&](qreal value) {
        last = value;
        ++frames;
    });

    // Nothing is delivered synchronously inside start(): the snap is a turn of
    // its own, which is what keeps a caller's re-entrancy safe.
    QCOMPARE(frames, 0);
    QVERIFY(animator.isRunning());

    animator.advanceTimeline(Ms(0));
    QCOMPARE(frames, 1);
    QCOMPARE(last, 1.0);
    QVERIFY(!animator.isRunning());

    // The snap is one frame, not a loop.
    animator.advanceTimeline(Ms(100));
    QCOMPARE(frames, 1);
}

void TestMotionContinuity::hiddenProgressSettlesAndStops()
{
    md::Progress progress;
    progress.resize(progress.sizeHint());
    progress.show();

    progress.setIndeterminate(true);
    QVERIFY2(progress.isAnimating(), "a visible indeterminate progress must animate");

    // Hiding mid-run settles and stops in the same turn: no timer may keep
    // repainting an invisible widget.
    progress.hide();
    QVERIFY(!progress.isAnimating());
    QVERIFY(progress.lifecycleState() == md::LifecycleState::Hidden);
}

void TestMotionContinuity::progressTracksTheViewLifecycle()
{
    md::Progress progress;
    progress.resize(progress.sizeHint());
    progress.setIndeterminate(true);

    // The component reacts through View's hooks, not by replacing showEvent()
    // and hideEvent(): the base class owns the lifecycle and the ripple, so the
    // state has to move with the hooks.
    QVERIFY(progress.lifecycleState() == md::LifecycleState::Created);

    progress.show();
    QVERIFY(progress.lifecycleState() == md::LifecycleState::Shown);
    QVERIFY(progress.isAnimating());

    progress.hide();
    QVERIFY(progress.lifecycleState() == md::LifecycleState::Hidden);
    QVERIFY(!progress.isAnimating());

    // Showing again restarts the loop from the hook, which is what proves
    // onShown() — not a stale showEvent() override — is the live path.
    progress.show();
    QVERIFY(progress.lifecycleState() == md::LifecycleState::Shown);
    QVERIFY(progress.isAnimating());
}

void TestMotionContinuity::ringTracksTheViewLifecycle()
{
    md::Ring ring;
    ring.resize(ring.sizeHint());
    ring.setIndeterminate(true);

    QVERIFY(ring.lifecycleState() == md::LifecycleState::Created);

    ring.show();
    QVERIFY(ring.lifecycleState() == md::LifecycleState::Shown);
    QVERIFY(ring.isAnimating());

    ring.hide();
    QVERIFY(ring.lifecycleState() == md::LifecycleState::Hidden);
    QVERIFY(!ring.isAnimating());
}

void TestMotionContinuity::pieChartSweepHasASingleOwner()
{
    md::PieChart pie;
    pie.resize(240, 240);
    pie.show();

    // Force the first paint, which is where the base chart starts its reveal.
    const QPixmap first = pie.grab();
    QVERIFY(!first.isNull());

    // The pie has no animator of its own: what it paints and what it reports is
    // the base chart's single run. Two drivers would show up as a mismatch here.
    QCOMPARE(pie.revealProgress(), pie.animation().progress());
    QVERIFY(pie.isRevealing());
    QVERIFY(pie.animation().isRunning());
}

void TestMotionContinuity::hiddenChartStopsItsReveal()
{
    md::PieChart pie;
    pie.resize(240, 240);
    pie.show();
    (void)pie.grab();

    QVERIFY(pie.isRevealing());

    // A hidden chart has no frames to show: the run settles on the finished
    // frame instead of keeping the animator's timer alive off-screen.
    pie.hide();
    QVERIFY(!pie.isRevealing());
    QCOMPARE(pie.revealProgress(), 1.0);
    QVERIFY(!pie.animation().isRunning());
}

void TestMotionContinuity::pieChartReducedMotionSnaps()
{
    md::PieChart pie;
    pie.resize(240, 240);
    md::ThemeScope* scope = md::ThemeScope::install(&pie, reducedMotionTheme());
    QVERIFY(scope != nullptr);

    pie.show();
    (void)pie.grab();

    // Reduced motion reaches the finished chart in the first paint: no partial
    // sweep and no frame timer.
    QCOMPARE(pie.revealProgress(), 1.0);
    QVERIFY(!pie.isRevealing());
    QVERIFY(!pie.animation().isRunning());
}

QTEST_MAIN(TestMotionContinuity)
#include "tst_motion_continuity.moc"
