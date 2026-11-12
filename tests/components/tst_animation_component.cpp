// Unit tests for md::Animation, the single-child enter/exit container.
//
// The suite runs offscreen. It pins the parts of the contract that are visible
// from the outside: the progress stream over a motion token, replacement of the
// single child, the deferred hide (the widget stays visible while it animates
// out), reversal from the current progress, reduced motion snapping without a
// frame timer, the exact end state of the child, and isolation of a throwing
// callback. The reduced-motion case installs a ThemeScope on the container so
// only that case changes the motion scheme.

#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QPointer>
#include <QRect>
#include <QSignalSpy>
#include <QTest>

#include <md/components/animation.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/view/view.h>

#include <chrono>
#include <stdexcept>

using namespace md;

namespace {

Theme reducedMotionTheme()
{
    Theme theme = Theme::baselineLight();
    MotionScheme motion = theme.motion();
    motion.reducedMotion = true;
    return theme.withMotion(motion);
}

// A plain child with a fixed natural geometry. The opacity effect and the
// geometry assertions need a real QWidget, not a painted-only probe.
QWidget* makeChild(int width = 120, int height = 80)
{
    auto* child = new QWidget;
    child->resize(width, height);
    return child;
}

// A leaf View with a fixed preferred size, used to exercise the View branch of
// the measurement/arrangement delegation.
class FixedView : public View
{
public:
    explicit FixedView(QSizeF size, QWidget* parent = nullptr)
        : View(parent)
        , size_(size)
    {
    }

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override
    {
        setMeasuredSize(QSizeF(width.resolve(size_.width()), height.resolve(size_.height())));
    }

    void paintContent(QPainter&, const QRectF&) override {}

    [[nodiscard]] bool focusable() const override { return false; }

private:
    QSizeF size_;
};

} // namespace

class TestAnimationComponent : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void enterRunsOverTheTokenDuration();
    void secondSetChildReplacesTheFirst();
    void hideAnimatesDownThenHides();
    void interruptedEnterReversesFromCurrentProgress();
    void reducedMotionSnapsInOneTurn();
    void childEndsAtNaturalGeometryWithFullOpacity();
    void delegatesMeasureAndArrangeToAViewChild();
    void unknownPropertyIsRejectedWithAWarning();
    void throwingCallbackDoesNotEscape();
};

void TestAnimationComponent::initTestCase()
{
    Theme::setApplicationTheme(Theme::baselineLight());
    QCOMPARE(Theme::applicationTheme(), Theme::baselineLight());
}

void TestAnimationComponent::enterRunsOverTheTokenDuration()
{
    Animation animation;
    animation.resize(240, 160);

    QWidget* child = makeChild();
    QVERIFY(animation.setChild(child));

    // Level1 is the 250 ms rung of the motion ladder.
    animation.setMotionLevel(MotionLevel::Level1);

    QSignalSpy finishedSpy(&animation, &Animation::finished);
    QSignalSpy progressSpy(&animation, &Animation::progressChanged);

    QCOMPARE(animation.progress(), 0.0);
    QVERIFY(!animation.isShown());
    QVERIFY(!animation.property("mdAnimationRunning").toBool());

    animation.show();

    // The first frame is still the collapsed end; the run advances from there.
    QCOMPARE(animation.progress(), 0.0);
    QVERIFY(animation.isShown());

    QTest::qWait(90);
    const qreal halfWay = animation.progress();
    QVERIFY2(halfWay > 0.0 && halfWay < 1.0,
             qPrintable(QStringLiteral("progress after 90 ms of 250 ms: %1").arg(halfWay)));

    QTRY_COMPARE_WITH_TIMEOUT(animation.progress(), 1.0, 2000);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(0).toBool(), true);
    QVERIFY(progressSpy.count() >= 2);
    QVERIFY(!animation.property("mdAnimationRunning").toBool());
}

void TestAnimationComponent::secondSetChildReplacesTheFirst()
{
    Animation animation;

    QWidget* first = makeChild();
    QVERIFY(animation.setChild(first));
    QCOMPARE(animation.child(), first);
    QCOMPARE(first->parentWidget(), &animation);

    // The same widget is not a replacement.
    QVERIFY(!animation.setChild(first));
    QCOMPARE(animation.child(), first);

    QPointer<QWidget> firstGuard(first);
    QWidget* second = makeChild();
    QVERIFY(animation.setChild(second));

    QCOMPARE(animation.child(), second);
    QCOMPARE(second->parentWidget(), &animation);

    // The old child is deleted with deleteLater(), not synchronously.
    QTRY_VERIFY_WITH_TIMEOUT(firstGuard.isNull(), 2000);
}

void TestAnimationComponent::hideAnimatesDownThenHides()
{
    Animation animation;
    animation.resize(200, 120);

    QWidget* child = makeChild();
    QVERIFY(animation.setChild(child));

    animation.setMotionLevel(MotionLevel::Level1);
    QSignalSpy finishedSpy(&animation, &Animation::finished);

    animation.show();
    QTRY_COMPARE_WITH_TIMEOUT(animation.progress(), 1.0, 2000);
    QCOMPARE(finishedSpy.count(), 1);

    animation.hide();

    // The hide is deferred: the container is still visible while it animates
    // out, and the target state flipped immediately.
    QVERIFY(animation.isVisible());
    QVERIFY(!animation.isShown());
    QCOMPARE(animation.progress(), 1.0);

    QTest::qWait(90);
    const qreal halfway = animation.progress();
    QVERIFY2(halfway > 0.0 && halfway < 1.0,
             qPrintable(QStringLiteral("progress while hiding: %1").arg(halfway)));

    QTRY_VERIFY_WITH_TIMEOUT(!animation.isVisible(), 2000);
    QCOMPARE(animation.progress(), 0.0);
    QCOMPARE(finishedSpy.count(), 2);
    QCOMPARE(finishedSpy.at(1).at(0).toBool(), false);
}

void TestAnimationComponent::interruptedEnterReversesFromCurrentProgress()
{
    Animation animation;
    animation.resize(240, 160);

    QWidget* child = makeChild();
    QVERIFY(animation.setChild(child));

    animation.setMotionLevel(MotionLevel::Level1);

    animation.show();
    QTest::qWait(100);

    const qreal beforeHide = animation.progress();
    QVERIFY2(beforeHide > 0.05 && beforeHide < 0.95,
             qPrintable(QStringLiteral("progress at interruption: %1").arg(beforeHide)));

    animation.hide();

    // The exit starts at the value the enter had reached; it must not jump back
    // to 1 (and does not start from 0 either).
    QCOMPARE(animation.progress(), beforeHide);
    QVERIFY(animation.isVisible());

    QTest::qWait(120);
    QVERIFY2(animation.progress() < beforeHide,
             qPrintable(QStringLiteral("progress did not reverse: %1 then %2")
                            .arg(beforeHide)
                            .arg(animation.progress())));

    QTRY_VERIFY_WITH_TIMEOUT(!animation.isVisible(), 2000);
    QCOMPARE(animation.progress(), 0.0);
}

void TestAnimationComponent::reducedMotionSnapsInOneTurn()
{
    Animation animation;
    animation.resize(200, 120);

    QWidget* child = makeChild();
    QVERIFY(animation.setChild(child));

    QVERIFY(ThemeScope::install(&animation, reducedMotionTheme()) != nullptr);
    animation.setMotionLevel(MotionLevel::Level4);

    QSignalSpy finishedSpy(&animation, &Animation::finished);

    animation.show();
    QCoreApplication::processEvents();

    // No frame timer and no intermediate value: the end state is already here.
    QCOMPARE(animation.progress(), 1.0);
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(!animation.property("mdAnimationRunning").toBool());
    QCOMPARE(child->geometry(), QRect(0, 0, 200, 120));

    animation.hide();
    QCoreApplication::processEvents();

    QVERIFY(!animation.isVisible());
    QCOMPARE(animation.progress(), 0.0);
    QCOMPARE(finishedSpy.count(), 2);
    QCOMPARE(finishedSpy.at(1).at(0).toBool(), false);
}

void TestAnimationComponent::childEndsAtNaturalGeometryWithFullOpacity()
{
    Animation animation;
    animation.resize(200, 120);

    QWidget* child = makeChild();
    QVERIFY(animation.setChild(child));

    // End the steps away from the natural values on purpose: the container's
    // contract is to restore the child after the run regardless of the last
    // step value.
    animation.setEnter({Animation::Step{Animation::Property::Opacity, 0.0, 0.85},
                        Animation::Step{Animation::Property::Scale, 0.5, 0.9},
                        Animation::Step{Animation::Property::Translate, 24.0, 4.0}});
    animation.setDuration(std::chrono::milliseconds(80));

    animation.show();
    QTRY_COMPARE_WITH_TIMEOUT(animation.progress(), 1.0, 2000);

    QCOMPARE(child->geometry(), QRect(0, 0, 200, 120));

    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(child->graphicsEffect());
    QVERIFY(effect != nullptr);
    QCOMPARE(effect->opacity(), 1.0);
}

void TestAnimationComponent::delegatesMeasureAndArrangeToAViewChild()
{
    Animation animation;
    animation.resize(200, 100);

    FixedView child(QSizeF(50.0, 40.0));
    QVERIFY(animation.setChild(&child));

    // The container resolves the child's preferred size through its own specs
    // and then hands its content rectangle to the child's measure()/arrange().
    animation.measure(MeasureSpec::exactly(200.0), MeasureSpec::exactly(100.0));
    QCOMPARE(animation.measuredSize(), QSizeF(200.0, 100.0));

    animation.arrange(QRect(0, 0, 200, 100));
    QCOMPARE(child.geometry(), QRect(0, 0, 200, 100));
    QCOMPARE(child.measuredSize(), QSizeF(200.0, 100.0));
}

void TestAnimationComponent::unknownPropertyIsRejectedWithAWarning()
{
    Animation animation;
    animation.resize(120, 80);

    QWidget* child = makeChild();
    QVERIFY(animation.setChild(child));

    animation.setDuration(std::chrono::milliseconds(40));

    QTest::ignoreMessage(QtWarningMsg,
                         "md::Animation::setEnter: unsupported step property 99 ignored");

    const auto unknown = static_cast<Animation::Property>(99);
    animation.setEnter({Animation::Step{unknown, 0.0, 1.0},
                        Animation::Step{Animation::Property::Scale, 0.5, 1.0}});

    // The rejected step must not disturb the rest of the list: the scale step
    // still runs and the child still ends at its natural rectangle.
    animation.show();
    QTRY_COMPARE_WITH_TIMEOUT(animation.progress(), 1.0, 2000);
    QCOMPARE(child->geometry(), QRect(0, 0, 120, 80));
}

void TestAnimationComponent::throwingCallbackDoesNotEscape()
{
    Animation animation;
    animation.resize(160, 100);

    QWidget* child = makeChild();
    QVERIFY(animation.setChild(child));

    animation.setDuration(std::chrono::milliseconds(80));

    bool threw = false;

    // The observer runs inside the value callback handed to the Animator. If a
    // throw escaped the component it would unwind through Qt's animation stack;
    // the run must instead continue and reach its end state.
    QObject::connect(&animation, &Animation::progressChanged, &animation,
                     [&threw](qreal) {
                         if (threw)
                             return;

                         threw = true;
                         throw std::runtime_error("progress observer");
                     });

    QSignalSpy finishedSpy(&animation, &Animation::finished);

    animation.show();
    QTRY_COMPARE_WITH_TIMEOUT(animation.progress(), 1.0, 2000);

    QVERIFY(threw);
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(!animation.property("mdAnimationRunning").toBool());
    QCOMPARE(child->geometry(), QRect(0, 0, 160, 100));
}

QTEST_MAIN(TestAnimationComponent)
#include "tst_animation_component.moc"
