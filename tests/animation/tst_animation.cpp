// Unit tests for the animation module (L3c): the animator's progress stream,
// its interruptible restart rule and its zero-duration snap path.
//
// Durations are deliberately short (40–200 ms) so the suite stays fast, but
// every wait uses QTRY_* rather than a fixed sleep, so a slow machine reports
// a timeout instead of a flake.

#include <QCoreApplication>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

#include <md/animation/animator.h>
#include <md/tokens/motion.h>

#include <chrono>
#include <limits>
#include <stdexcept>

using md::CubicBezier;
using md::Easing;
using md::MotionSpec;
using md::animation::Animator;

namespace {

// A linear, delay-free spec: monotonic by construction, so an assertion about
// monotonic progress is an assertion about the animator and not the curve.
MotionSpec linearSpec(int milliseconds)
{
    MotionSpec spec;
    spec.duration = std::chrono::milliseconds(milliseconds);
    spec.easing = md::easingCurve(Easing::Linear);
    spec.delay = std::chrono::milliseconds(0);
    return spec;
}

MotionSpec reducedMotionSpec()
{
    MotionSpec spec;
    spec.duration = std::chrono::milliseconds(0);
    spec.easing = md::easingCurve(Easing::Standard);
    spec.delay = std::chrono::milliseconds(0);
    return spec;
}

} // namespace

class TestAnimation : public QObject
{
    Q_OBJECT

private slots:
    void initialState();
    void zeroDurationSnapsOnNextTurn();
    void normalRunIsMonotonicAndFinishes();
    void progressTracksLastEmittedValue();
    void stopDoesNotEmitFinished();
    void restartContinuesFromCurrentProgress();
    void delayedRunStartsAtItsStartValue();
    void destructorStopsWithoutFinishing();
    void callbackDeletesTheAnimator();
    void callbackRestartsTheAnimator();
    void zeroDurationHonoursTheDelay();
    void pauseResumesFromTheSameProgress();
    void hugeTimelineValuesDoNotWrap();
    void retargetAtTheEndRunsTheNewCallback();
    void throwingCallbackIsContained();
};

void TestAnimation::initialState()
{
    Animator animator;

    QVERIFY(!animator.isRunning());
    QCOMPARE(animator.progress(), 0.0);
}

void TestAnimation::zeroDurationSnapsOnNextTurn()
{
    Animator animator;
    QSignalSpy finishedSpy(&animator, &Animator::finished);

    int calls = 0;
    qreal last = 0.0;
    animator.start(reducedMotionSpec(), [&](qreal value) {
        ++calls;
        last = value;
    });

    // Nothing is delivered synchronously: the snap runs on a queued turn.
    QVERIFY(animator.isRunning());
    QCOMPARE(int(finishedSpy.count()), 0);
    QCOMPARE(calls, 0);

    QTRY_COMPARE_WITH_TIMEOUT(int(finishedSpy.count()), 1, 2000);

    QCOMPARE(calls, 1);
    QCOMPARE(last, 1.0);
    QCOMPARE(animator.progress(), 1.0);
    QVERIFY(!animator.isRunning());

    // Exactly one delivery: no duplicate finished, no duplicate value.
    QTest::qWait(20);
    QCOMPARE(calls, 1);
    QCOMPARE(int(finishedSpy.count()), 1);
}

void TestAnimation::normalRunIsMonotonicAndFinishes()
{
    Animator animator;
    QSignalSpy finishedSpy(&animator, &Animator::finished);

    QList<qreal> values;
    animator.start(linearSpec(40), [&](qreal value) { values.append(value); });

    QVERIFY(animator.isRunning());
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 3000);

    QVERIFY(!animator.isRunning());
    QCOMPARE(int(finishedSpy.count()), 1);

    // A 40 ms run at the platform frame interval produces at least one
    // intermediate value plus the final one.
    QVERIFY2(values.size() >= 2,
             qPrintable(QStringLiteral("emitted %1 values").arg(values.size())));

    for (qsizetype i = 1; i < values.size(); ++i) {
        QVERIFY(values[i] >= values[i - 1]);
        QVERIFY(values[i] >= 0.0);
        QVERIFY(values[i] <= 1.0);
    }

    QVERIFY(values.first() > 0.0);
    QVERIFY(values.first() < 1.0);
    QCOMPARE(values.last(), 1.0);
    QCOMPARE(animator.progress(), 1.0);
}

void TestAnimation::progressTracksLastEmittedValue()
{
    Animator animator;
    QSignalSpy finishedSpy(&animator, &Animator::finished);

    QList<qreal> values;
    animator.start(linearSpec(200), [&](qreal value) { values.append(value); });

    // As soon as a value has been emitted, progress() reports that value.
    QTRY_VERIFY_WITH_TIMEOUT(!values.isEmpty(), 2000);
    QCOMPARE(animator.progress(), values.last());
    QVERIFY(animator.progress() > 0.0);

    animator.stop();
    QVERIFY(!animator.isRunning());
    QCOMPARE(animator.progress(), values.last());
    QCOMPARE(int(finishedSpy.count()), 0);

    // A stopped animator keeps its last value; it is not rewound.
    QTest::qWait(40);
    QCOMPARE(animator.progress(), values.last());
}

void TestAnimation::stopDoesNotEmitFinished()
{
    Animator animator;
    QSignalSpy finishedSpy(&animator, &Animator::finished);

    QList<qreal> values;
    animator.start(linearSpec(200), [&](qreal value) { values.append(value); });

    QTRY_VERIFY_WITH_TIMEOUT(animator.progress() > 0.0, 2000);
    animator.stop();

    QVERIFY(!animator.isRunning());
    const qreal stoppedAt = animator.progress();

    // Well past the point at which the run would have finished.
    QTest::qWait(250);

    QCOMPARE(int(finishedSpy.count()), 0);
    QCOMPARE(animator.progress(), stoppedAt);
}

void TestAnimation::restartContinuesFromCurrentProgress()
{
    Animator animator;
    QSignalSpy finishedSpy(&animator, &Animator::finished);

    QList<qreal> firstRun;
    animator.start(linearSpec(200), [&](qreal value) { firstRun.append(value); });

    QTRY_VERIFY_WITH_TIMEOUT(animator.progress() > 0.0, 2000);
    const qreal before = animator.progress();
    QVERIFY(before > 0.0);
    QVERIFY(before < 1.0);
    QCOMPARE(int(finishedSpy.count()), 0);

    // Restart mid-run: the new run continues from the current progress, with
    // the remaining duration, and never jumps back to zero.
    QList<qreal> secondRun;
    animator.start(linearSpec(200), [&](qreal value) { secondRun.append(value); });

    QCOMPARE(animator.progress(), before);

    QTRY_VERIFY_WITH_TIMEOUT(!secondRun.isEmpty(), 2000);
    QVERIFY(secondRun.first() >= before - 0.001);
    QVERIFY(secondRun.first() < 1.0);

    QTRY_COMPARE_WITH_TIMEOUT(int(finishedSpy.count()), 1, 3000);

    QVERIFY(!animator.isRunning());
    QVERIFY(secondRun.size() >= 2);
    for (qsizetype i = 1; i < secondRun.size(); ++i)
        QVERIFY(secondRun[i] >= secondRun[i - 1]);

    QCOMPARE(secondRun.last(), 1.0);
    QCOMPARE(animator.progress(), 1.0);
}

void TestAnimation::delayedRunStartsAtItsStartValue()
{
    Animator animator;
    QSignalSpy finishedSpy(&animator, &Animator::finished);

    // Stop a first run part-way. The underlying QVariantAnimation keeps the
    // currentTime it was stopped at, which is what a retarget used to leak into
    // the callback: the setters re-interpolate at that stale time.
    QList<qreal> firstRun;
    animator.start(linearSpec(300), [&](qreal value) { firstRun.append(value); });
    QTRY_VERIFY_WITH_TIMEOUT(animator.progress() > 0.2, 2000);
    animator.stop();
    QVERIFY(!animator.isRunning());

    MotionSpec delayed = linearSpec(120);
    delayed.delay = std::chrono::milliseconds(80);

    QList<qreal> values;
    animator.start(delayed, [&](qreal value) { values.append(value); });

    // Nothing is delivered while the delay is still running.
    QTest::qWait(30);
    QVERIFY(values.isEmpty());

    QTRY_VERIFY_WITH_TIMEOUT(!values.isEmpty(), 3000);

    // The first value is the start of the run, not an interpolation at the
    // previous run's currentTime (which would be 0.5 or more here).
    QVERIFY2(values.first() < 0.2,
             qPrintable(QStringLiteral("first delayed value: %1").arg(values.first())));

    for (qsizetype i = 1; i < values.size(); ++i)
        QVERIFY(values[i] >= values[i - 1]);

    QTRY_COMPARE_WITH_TIMEOUT(int(finishedSpy.count()), 1, 3000);
    QVERIFY(!animator.isRunning());
    QCOMPARE(values.last(), 1.0);
    QCOMPARE(animator.progress(), 1.0);
}

void TestAnimation::destructorStopsWithoutFinishing()
{
    bool finished = false;

    {
        Animator animator;
        QObject::connect(&animator, &Animator::finished, [&finished] { finished = true; });
        animator.start(linearSpec(200), [](qreal) {});
        QTest::qWait(10);
        QVERIFY(animator.isRunning());
    }

    // The destroyed animator neither emitted finished() nor left a queued
    // callback behind.
    QTest::qWait(50);
    QVERIFY(!finished);
}

void TestAnimation::callbackDeletesTheAnimator()
{
    auto* animator = new Animator;
    QPointer<Animator> guard(animator);

    bool finished = false;
    QObject::connect(animator, &Animator::finished, animator,
                     [&finished] { finished = true; });

    int calls = 0;
    animator->start(reducedMotionSpec(), [&animator, &calls](qreal value) {
        ++calls;
        if (value >= 1.0) {
            // Deleting the animator from inside its own callback must not make
            // it read its members, or emit finished(), afterwards.
            delete animator;
            animator = nullptr;
        }
    });

    QTRY_VERIFY_WITH_TIMEOUT(guard.isNull(), 2000);
    QCOMPARE(calls, 1);
    QVERIFY(!finished);

    // Let the event loop settle: the queued snap must not touch freed memory.
    QTest::qWait(20);
    QVERIFY(!finished);
}

void TestAnimation::callbackRestartsTheAnimator()
{
    Animator animator;
    QSignalSpy finishedSpy(&animator, &Animator::finished);

    int firstRunCalls = 0;
    qreal restartedAt = -1.0;
    bool restarted = false;
    QList<qreal> secondRun;

    animator.start(linearSpec(200), [&](qreal value) {
        ++firstRunCalls;
        if (restarted)
            return;

        restarted = true;
        restartedAt = value;

        // start() from inside the callback must neither free the function that
        // is executing nor reconfigure QVariantAnimation under its own
        // valueChanged emission.
        animator.start(linearSpec(120), [&secondRun](qreal second) {
            secondRun.append(second);
        });
    });

    QTRY_VERIFY_WITH_TIMEOUT(restarted, 2000);
    QVERIFY(restartedAt > 0.0);
    QVERIFY(restartedAt < 1.0);

    // The old callback is gone for good; the replacement completes the run.
    QTRY_COMPARE_WITH_TIMEOUT(int(finishedSpy.count()), 1, 3000);
    QCOMPARE(firstRunCalls, 1);
    QVERIFY(!animator.isRunning());

    QTRY_VERIFY_WITH_TIMEOUT(secondRun.size() >= 2, 1000);
    QVERIFY(secondRun.first() >= restartedAt - 0.001);
    QCOMPARE(secondRun.last(), 1.0);
    QCOMPARE(animator.progress(), 1.0);
}

void TestAnimation::zeroDurationHonoursTheDelay()
{
    Animator animator;
    QSignalSpy finishedSpy(&animator, &Animator::finished);

    MotionSpec spec = reducedMotionSpec();
    spec.delay = std::chrono::milliseconds(120);

    int calls = 0;
    animator.start(spec, [&calls](qreal) { ++calls; });

    QVERIFY(animator.isRunning());

    // The delay is part of the spec, so the snap waits for it instead of firing
    // on the first event-loop turn.
    QTest::qWait(40);
    QCOMPARE(calls, 0);
    QCOMPARE(int(finishedSpy.count()), 0);

    QTRY_COMPARE_WITH_TIMEOUT(int(finishedSpy.count()), 1, 2000);
    QCOMPARE(calls, 1);
    QCOMPARE(animator.progress(), 1.0);
    QVERIFY(!animator.isRunning());
}

void TestAnimation::pauseResumesFromTheSameProgress()
{
    Animator animator;
    QSignalSpy finishedSpy(&animator, &Animator::finished);

    QList<qreal> values;
    animator.start(linearSpec(200), [&values](qreal value) { values.append(value); });

    QTRY_VERIFY_WITH_TIMEOUT(animator.progress() > 0.1, 2000);

    animator.pause();
    QVERIFY(!animator.isRunning());
    QVERIFY(animator.isPaused());

    const qreal pausedAt = animator.progress();
    const qsizetype pausedCalls = values.size();
    QVERIFY(pausedAt > 0.0);
    QVERIFY(pausedAt < 1.0);

    // A paused run emits nothing and keeps its value.
    QTest::qWait(80);
    QCOMPARE(animator.progress(), pausedAt);
    QCOMPARE(values.size(), pausedCalls);
    QCOMPARE(int(finishedSpy.count()), 0);

    animator.resume();
    QVERIFY(animator.isRunning());
    QVERIFY(!animator.isPaused());
    QCOMPARE(animator.progress(), pausedAt);

    QTRY_COMPARE_WITH_TIMEOUT(int(finishedSpy.count()), 1, 3000);
    QVERIFY(values.size() > pausedCalls);

    // The resumed run continues forward from where it stopped; it never rewinds
    // to 0 the way stop() + start() would.
    QVERIFY(values.last() == 1.0);
    QVERIFY(values[pausedCalls] >= pausedAt - 0.001);
    for (qsizetype i = pausedCalls + 1; i < values.size(); ++i)
        QVERIFY(values[i] >= values[i - 1]);

    // A pause on an idle animator is a no-op.
    animator.pause();
    QVERIFY(!animator.isPaused());
    animator.resume();
    QVERIFY(!animator.isRunning());
}

void TestAnimation::hugeTimelineValuesDoNotWrap()
{
    Animator animator;
    QSignalSpy finishedSpy(&animator, &Animator::finished);

    MotionSpec longRun = linearSpec(0);
    longRun.duration = std::chrono::milliseconds(std::numeric_limits<qint64>::max());

    QList<qreal> values;
    animator.start(longRun, [&values](qreal value) { values.append(value); });

    // Without saturation the remaining time wraps negative and the run
    // completes on the next turn instead of animating for the clamped maximum.
    QTest::qWait(60);
    QCOMPARE(int(finishedSpy.count()), 0);
    QVERIFY(animator.isRunning());
    QVERIFY(animator.progress() < 0.5);
    for (qreal value : values)
        QVERIFY(value < 0.5);

    animator.stop();
    QVERIFY(!animator.isRunning());

    // A delay above the int range is clamped the same way: it cannot fire at an
    // arbitrary truncated time.
    MotionSpec longDelay = linearSpec(40);
    longDelay.delay = std::chrono::milliseconds(std::numeric_limits<qint64>::max());

    int calls = 0;
    animator.start(longDelay, [&calls](qreal) { ++calls; });

    QTest::qWait(60);
    QCOMPARE(calls, 0);
    QCOMPARE(int(finishedSpy.count()), 0);
    QVERIFY(animator.isRunning());

    animator.stop();
    QVERIFY(!animator.isRunning());
}

void TestAnimation::retargetAtTheEndRunsTheNewCallback()
{
    Animator animator;
    QSignalSpy finishedSpy(&animator, &Animator::finished);

    int secondRunCalls = 0;
    qreal lastSecondRun = -1.0;
    bool retargeted = false;

    animator.start(linearSpec(40), [&](qreal value) {
        if (value < 1.0 || retargeted)
            return;

        retargeted = true;

        // The run is complete but finished() has not been emitted yet. The new
        // run must reach its own callback instead of only emitting finished().
        animator.start(linearSpec(40), [&secondRunCalls, &lastSecondRun](qreal second) {
            ++secondRunCalls;
            lastSecondRun = second;
        });
    });

    QTRY_VERIFY_WITH_TIMEOUT(retargeted, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(secondRunCalls >= 2, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(lastSecondRun, 1.0, 3000);

    // The superseded run does not emit finished(); the replacement does.
    QTRY_COMPARE_WITH_TIMEOUT(int(finishedSpy.count()), 1, 3000);
    QVERIFY(!animator.isRunning());
    QCOMPARE(animator.progress(), 1.0);
}

void TestAnimation::throwingCallbackIsContained()
{
    Animator animator;
    QSignalSpy finishedSpy(&animator, &Animator::finished);

    int calls = 0;
    animator.start(reducedMotionSpec(), [&calls](qreal) {
        ++calls;
        throw std::runtime_error("callback failure");
    });

    // The animator catches the exception, so it cannot unwind through Qt's
    // event loop, and the run still completes.
    QTRY_COMPARE_WITH_TIMEOUT(int(finishedSpy.count()), 1, 2000);
    QCOMPARE(calls, 1);
    QCOMPARE(animator.progress(), 1.0);
    QVERIFY(!animator.isRunning());
}

QTEST_GUILESS_MAIN(TestAnimation)

#include "tst_animation.moc"
