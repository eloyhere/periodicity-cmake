// Periodicity — motion level tests (framework.md §3.5).
//
// The levels are a fixed duration ladder: 0, 250, 500, 750, 1000 and 1250 ms.
// Every non-zero rung uses the standard curve, so comparing two levels compares
// speed alone; reduced motion collapses every rung except the zero one.

#include <md/tokens/motion.h>

#include <QTest>

using namespace md;

class TestMotionLevels : public QObject
{
    Q_OBJECT

private slots:
    void ladderIsExactlyQuarterSeconds();
    void levelZeroIsAlwaysImmediate();
    void reducedMotionCollapsesEverything();
    void levelZeroIsLinearAndTheRestAreStandard();
};

void TestMotionLevels::ladderIsExactlyQuarterSeconds()
{
    const MotionScheme scheme = MotionScheme::baseline();

    QCOMPARE(scheme.level(MotionLevel::Level0).duration.count(), 0);
    QCOMPARE(scheme.level(MotionLevel::Level1).duration.count(), 250);
    QCOMPARE(scheme.level(MotionLevel::Level2).duration.count(), 500);
    QCOMPARE(scheme.level(MotionLevel::Level3).duration.count(), 750);
    QCOMPARE(scheme.level(MotionLevel::Level4).duration.count(), 1000);
    QCOMPARE(scheme.level(MotionLevel::Level5).duration.count(), 1250);
}

void TestMotionLevels::levelZeroIsAlwaysImmediate()
{
    for (const bool reduced : {false, true}) {
        MotionScheme scheme;
        scheme.reducedMotion = reduced;
        QCOMPARE(scheme.level(MotionLevel::Level0).duration.count(), 0);
    }
}

void TestMotionLevels::reducedMotionCollapsesEverything()
{
    MotionScheme scheme;
    scheme.reducedMotion = true;

    for (const MotionLevel level : {MotionLevel::Level1, MotionLevel::Level2,
                                    MotionLevel::Level3, MotionLevel::Level4,
                                    MotionLevel::Level5}) {
        QCOMPARE(scheme.level(level).duration.count(), 0);
    }
}

void TestMotionLevels::levelZeroIsLinearAndTheRestAreStandard()
{
    const MotionScheme scheme = MotionScheme::baseline();

    QCOMPARE(scheme.level(MotionLevel::Level0).easing, easingCurve(Easing::Linear));
    for (const MotionLevel level : {MotionLevel::Level1, MotionLevel::Level2,
                                    MotionLevel::Level3, MotionLevel::Level4,
                                    MotionLevel::Level5}) {
        QCOMPARE(scheme.level(level).easing, easingCurve(Easing::Standard));
    }
}

QTEST_MAIN(TestMotionLevels)
#include "tst_motion_levels.moc"
