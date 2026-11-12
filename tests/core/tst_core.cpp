// Unit tests for the core module (L0): measurement, density, geometry, state
// and logging categories. The suite is deterministic and headless: it creates
// no widgets and needs no window system.

#include <QLoggingCategory>
#include <QPainterPath>
#include <QRectF>
#include <QTest>

#include <md/core/density.h>
#include <md/core/geometry.h>
#include <md/core/logging.h>
#include <md/core/measure_spec.h>
#include <md/core/view_state.h>

using md::CornerRadii;
using md::Density;
using md::Insets;
using md::MeasureSpec;
using md::ViewState;
using md::ViewStates;

namespace {

// QPainterPath arcs are cubic approximations, so bounds are compared with a
// small tolerance rather than bit for bit.
bool rectsAlmostEqual(const QRectF& lhs, const QRectF& rhs, qreal tolerance = 0.01)
{
    return qAbs(lhs.left() - rhs.left()) <= tolerance
        && qAbs(lhs.top() - rhs.top()) <= tolerance
        && qAbs(lhs.width() - rhs.width()) <= tolerance
        && qAbs(lhs.height() - rhs.height()) <= tolerance;
}

} // namespace

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void measureSpecResolve();
    void densityConversion();
    void densityEquality();
    void insetsAppliedTo();
    void cornerRadiiToPath();
    void cornerRadiiToPathClamps();
    void densityInvalidScaleFallsBack();
    void cornerRadiiNonFiniteInputs();
    void viewStatesComposition();
    void loggingCategoryNames();
};

void TestCore::measureSpecResolve()
{
    // Exactly ignores the desired size, whether it is smaller or larger.
    const MeasureSpec exact = MeasureSpec::exactly(120.0);
    QCOMPARE(exact.mode(), MeasureSpec::Mode::Exactly);
    QCOMPARE(exact.size(), 120.0);
    QCOMPARE(exact.resolve(40.0), 120.0);
    QCOMPARE(exact.resolve(120.0), 120.0);
    QCOMPARE(exact.resolve(400.0), 120.0);

    // AtMost clamps the desired size down to the spec size.
    const MeasureSpec atMost = MeasureSpec::atMost(80.0);
    QCOMPARE(atMost.mode(), MeasureSpec::Mode::AtMost);
    QCOMPARE(atMost.size(), 80.0);
    QCOMPARE(atMost.resolve(0.0), 0.0);
    QCOMPARE(atMost.resolve(30.0), 30.0);
    QCOMPARE(atMost.resolve(80.0), 80.0);
    QCOMPARE(atMost.resolve(500.0), 80.0);

    // Unspecified passes the desired size through and carries no size.
    const MeasureSpec unspecified = MeasureSpec::unspecified();
    QCOMPARE(unspecified.mode(), MeasureSpec::Mode::Unspecified);
    QCOMPARE(unspecified.size(), 0.0);
    QCOMPARE(unspecified.resolve(0.0), 0.0);
    QCOMPARE(unspecified.resolve(30.0), 30.0);
    QCOMPARE(unspecified.resolve(1000.0), 1000.0);
}

void TestCore::densityConversion()
{
    // At scale 1.0 one dp is one device-independent pixel; no DPI factor is
    // applied, because Qt has already applied display scaling.
    const Density identity;
    QCOMPARE(identity.scale, 1.0);
    QCOMPARE(identity.fontScale, 1.0);
    QCOMPARE(identity.dp(1.0), 1.0);
    QCOMPARE(identity.dp(24.0), 24.0);
    QCOMPARE(identity.sp(16.0), 16.0);
    QCOMPARE(identity.pxToDp(24.0), 24.0);

    // An application-level zoom of 2 multiplies dp and divides on the way back.
    Density zoomed;
    zoomed.scale = 2.0;
    QCOMPARE(zoomed.dp(12.0), 24.0);
    QCOMPARE(zoomed.pxToDp(24.0), 12.0);
    QCOMPARE(zoomed.sp(8.0), 16.0); // fontScale is still 1.0

    // The accessibility font scale stacks on top of the density scale.
    Density largeText;
    largeText.scale = 1.5;
    largeText.fontScale = 2.0;
    QCOMPARE(largeText.dp(10.0), 15.0);
    QCOMPARE(largeText.sp(10.0), 30.0);
    QCOMPARE(largeText.pxToDp(15.0), 10.0);
}

void TestCore::densityEquality()
{
    const Density baseline;
    const Density same;
    QVERIFY(baseline == same);
    QVERIFY(!(baseline != same));

    Density differentScale = baseline;
    differentScale.scale = 1.25;
    QVERIFY(baseline != differentScale);

    Density differentFontScale = baseline;
    differentFontScale.fontScale = 1.25;
    QVERIFY(baseline != differentFontScale);
    QVERIFY(differentScale != differentFontScale);
}

void TestCore::insetsAppliedTo()
{
    const Insets insets{8.0, 4.0, 8.0, 4.0};
    QCOMPARE(insets.horizontal(), 16.0);
    QCOMPARE(insets.vertical(), 8.0);

    const QRectF outer(0.0, 0.0, 100.0, 50.0);
    const QRectF inner = insets.appliedTo(outer);
    QCOMPARE(inner, QRectF(8.0, 4.0, 84.0, 42.0));

    QVERIFY(Insets{} == Insets{});
    QVERIFY(insets != Insets{});
}

void TestCore::cornerRadiiToPath()
{
    const QRectF bounds(10.0, 20.0, 120.0, 80.0);

    const CornerRadii radii = CornerRadii::uniform(16.0);
    QCOMPARE(radii.topStart, 16.0);
    QCOMPARE(radii.topEnd, 16.0);
    QCOMPARE(radii.bottomEnd, 16.0);
    QCOMPARE(radii.bottomStart, 16.0);
    const CornerRadii uniformExpected{16.0, 16.0, 16.0, 16.0};
    QVERIFY(radii == uniformExpected);

    const QPainterPath path = radii.toPath(bounds);
    QVERIFY(!path.isEmpty());
    QVERIFY2(rectsAlmostEqual(path.boundingRect(), bounds),
             "rounded path must keep the full rectangle as its bounds");
    QVERIFY(path.contains(QPointF(70.0, 60.0)));   // centre
    QVERIFY(!path.contains(QPointF(11.0, 21.0)));  // rounded top-start corner

    // A zero radius degenerates to a plain rectangle.
    const QPainterPath square = CornerRadii{}.toPath(bounds);
    QVERIFY(rectsAlmostEqual(square.boundingRect(), bounds));
    QVERIFY(square.contains(QPointF(11.0, 21.0)));

    // Start and end are the logical corners: in a left-to-right layout the
    // top-start radius rounds the top-left corner only.
    const CornerRadii topStartOnly{16.0, 0.0, 0.0, 0.0};
    const QPainterPath ltr = topStartOnly.toPath(QRectF(0.0, 0.0, 100.0, 40.0));
    QVERIFY(!ltr.contains(QPointF(1.0, 1.0)));  // rounded top-left
    QVERIFY(ltr.contains(QPointF(99.0, 1.0)));  // square top-right
}

void TestCore::cornerRadiiToPathClamps()
{
    const QRectF bounds(0.0, 0.0, 100.0, 40.0);

    // 50 exceeds half the shorter side (40 / 2 = 20), so every radius is
    // clamped and the result must equal an explicitly 20 dp path.
    const QPainterPath clamped = CornerRadii::uniform(50.0).toPath(bounds);
    const QPainterPath reference = CornerRadii::uniform(20.0).toPath(bounds);
    QVERIFY(clamped == reference);

    QVERIFY2(rectsAlmostEqual(clamped.boundingRect(), bounds),
             "clamping must not enlarge or shrink the path bounds");
    QVERIFY(!clamped.contains(QPointF(1.0, 1.0)));
    QVERIFY(clamped.contains(QPointF(50.0, 20.0)));

    // A single oversized radius is clamped independently of the others.
    const CornerRadii mixed{80.0, 4.0, 4.0, 4.0};
    const QPainterPath mixedPath = mixed.toPath(bounds);
    QVERIFY(rectsAlmostEqual(mixedPath.boundingRect(), bounds));
    QVERIFY(!mixedPath.contains(QPointF(0.5, 0.5)));    // clamped 20 top-start
    QVERIFY(!mixedPath.contains(QPointF(99.5, 39.5)));  // unclamped 4 bottom-end
    QVERIFY(mixedPath.contains(QPointF(50.0, 20.0)));

    // An empty rectangle has no outline at all.
    QVERIFY(CornerRadii::uniform(8.0).toPath(QRectF()).isEmpty());
}

void TestCore::densityInvalidScaleFallsBack()
{
    // A zoom that animates through zero must not collapse every measurement.
    // The whole struct uses one effective scale, so dp and pxToDp stay
    // inverses instead of disagreeing.
    Density zero;
    zero.scale = 0.0;
    QCOMPARE(zero.effectiveScale(), 1.0);
    QCOMPARE(zero.dp(24.0), 24.0);
    QCOMPARE(zero.sp(16.0), 16.0);
    QCOMPARE(zero.pxToDp(24.0), 24.0);

    // A negative scale is unusable too: it must not invert dp while pxToDp
    // silently ignores it.
    Density negative;
    negative.scale = -2.0;
    QCOMPARE(negative.effectiveScale(), 1.0);
    QCOMPARE(negative.dp(12.0), 12.0);
    QCOMPARE(negative.sp(8.0), 8.0);
    QCOMPARE(negative.pxToDp(-24.0), -24.0);

    // NaN and infinity are rejected by the same rule.
    Density nan;
    nan.scale = qQNaN();
    QCOMPARE(nan.effectiveScale(), 1.0);
    QCOMPARE(nan.dp(12.0), 12.0);
    QCOMPARE(nan.sp(8.0), 8.0);
    QCOMPARE(nan.pxToDp(12.0), 12.0);

    Density infinite;
    infinite.scale = qInf();
    QCOMPARE(infinite.effectiveScale(), 1.0);
    QCOMPARE(infinite.dp(12.0), 12.0);
    QCOMPARE(infinite.pxToDp(12.0), 12.0);

    // With the identity fallback, dp -> pxToDp is an exact round trip.
    QCOMPARE(zero.pxToDp(zero.dp(37.5)), 37.5);

    // A usable scale is still applied and still inverts exactly.
    Density zoomed;
    zoomed.scale = 2.0;
    QCOMPARE(zoomed.effectiveScale(), 2.0);
    QCOMPARE(zoomed.pxToDp(zoomed.dp(12.0)), 12.0);
}

void TestCore::cornerRadiiNonFiniteInputs()
{
    const QRectF bounds(0.0, 0.0, 100.0, 40.0);

    // NaN compares false with everything, so the old `radius <= 0` test let it
    // fall through and the clamp turned it into the *maximum* radius. It must
    // degenerate to a square corner instead.
    const CornerRadii nanRadii = CornerRadii::uniform(qQNaN());
    const QPainterPath nanPath = nanRadii.toPath(bounds);
    QVERIFY(nanPath == CornerRadii{}.toPath(bounds));
    QVERIFY(rectsAlmostEqual(nanPath.boundingRect(), bounds));
    QVERIFY(nanPath.contains(QPointF(0.5, 0.5)));

    // A single NaN corner degenerates that corner only.
    const CornerRadii mixed{qQNaN(), 8.0, 8.0, 8.0};
    const CornerRadii mixedExpected{0.0, 8.0, 8.0, 8.0};
    QVERIFY(mixed.toPath(bounds) == mixedExpected.toPath(bounds));

    // Infinity is a positive radius and clamps to half the shorter side.
    QVERIFY(CornerRadii::uniform(qInf()).toPath(bounds)
            == CornerRadii::uniform(20.0).toPath(bounds));

    // A non-finite rectangle has no drawable outline; returning a path made of
    // NaN or infinite points would poison every caller downstream.
    const CornerRadii radii = CornerRadii::uniform(8.0);
    QVERIFY(radii.toPath(QRectF(0.0, 0.0, qQNaN(), 40.0)).isEmpty());
    QVERIFY(radii.toPath(QRectF(0.0, 0.0, 100.0, qInf())).isEmpty());
    QVERIFY(radii.toPath(QRectF(qQNaN(), 0.0, 100.0, 40.0)).isEmpty());
    QVERIFY(radii.toPath(QRectF(0.0, qInf(), 100.0, 40.0)).isEmpty());
}

void TestCore::viewStatesComposition()
{
    ViewStates states = ViewState::Enabled;
    QVERIFY(states.testFlag(ViewState::Enabled));
    QVERIFY(!states.testFlag(ViewState::Hovered));

    states |= ViewState::Hovered;
    states |= ViewState::Focused;
    QVERIFY(states.testFlag(ViewState::Hovered));
    QVERIFY(states.testFlag(ViewState::Focused));
    QVERIFY(states.testFlag(ViewState::Enabled));
    QVERIFY(!states.testFlag(ViewState::Pressed));
    QCOMPARE(states.toInt(),
             int(ViewState::Enabled) | int(ViewState::Hovered) | int(ViewState::Focused));

    // setFlag() sets and clears individual bits.
    states.setFlag(ViewState::Pressed, true);
    QVERIFY(states.testFlag(ViewState::Pressed));
    states.setFlag(ViewState::Pressed, false);
    QVERIFY(!states.testFlag(ViewState::Pressed));

    // Clearing a bit leaves the remaining flags untouched.
    states &= ~ViewStates(ViewState::Hovered);
    QVERIFY(!states.testFlag(ViewState::Hovered));
    QVERIFY(states.testFlag(ViewState::Enabled));
    QVERIFY(states.testFlag(ViewState::Focused));

    // An empty flag set tests false for every state.
    states = ViewStates{};
    QVERIFY(states == ViewStates{});
    QVERIFY(!states.testFlag(ViewState::Enabled));
    states.setFlag(ViewState::Selected);
    QVERIFY(states.testFlag(ViewState::Selected));
    QVERIFY(!states.testFlag(ViewState::Checked));
}

void TestCore::loggingCategoryNames()
{
    QCOMPARE(QString::fromLatin1(md::mdCore().categoryName()), QStringLiteral("md.core"));
    QCOMPARE(QString::fromLatin1(md::mdPaint().categoryName()), QStringLiteral("md.paint"));
    QCOMPARE(QString::fromLatin1(md::mdLayout().categoryName()), QStringLiteral("md.layout"));
    QCOMPARE(QString::fromLatin1(md::mdTheme().categoryName()), QStringLiteral("md.theme"));

    // The global aliases used by other layers name the same functions.
    QCOMPARE(QString::fromLatin1(mdTheme().categoryName()), QStringLiteral("md.theme"));
}

QTEST_MAIN(TestCore)

#include "tst_core.moc"
