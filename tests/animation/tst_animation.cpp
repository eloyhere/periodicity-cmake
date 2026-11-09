// Unit tests for the animation module (L3c): the animator's progress stream,
// its interruptible restart rule and its zero-duration snap path.
//
// Durations are deliberately short (40–200 ms) so the suite stays fast, but
// every wait uses QTRY_* rather than a fixed sleep, so a slow machine reports
// a timeout instead of a flake.

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <md/animation/animator.h>
#include <md/tokens/motion.h>

#include <chrono>

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
    void destructorStopsWithoutFinishing();
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

QTEST_GUILESS_MAIN(TestAnimation)

#include "tst_animation.moc"
