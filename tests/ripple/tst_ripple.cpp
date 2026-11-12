// Periodicity — ripple tests (framework.md §5.11).
//
// The contract under test changed with the interaction design: a ripple is one
// shot. A hover plays a single expansion that reaches the component's corners,
// then fades and disappears. Nothing tracks the pointer and nothing persists.

#include <md/ripple/ripple.h>
#include <md/theme/theme.h>

#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>

#include <chrono>

using md::Density;
using md::Theme;
using md::ViewState;
using md::ripple::RippleController;
using md::ripple::RippleSpec;

namespace {

// A spec that is long enough to sample mid-animation but short enough for a
// test to wait for completion.
RippleSpec slowSpec(const Theme& theme, int expansionMs, int fadeMs)
{
    RippleSpec spec = RippleSpec::fromTheme(theme);
    spec.expansion.duration = std::chrono::milliseconds(expansionMs);
    spec.fade.duration = std::chrono::milliseconds(fadeMs);
    return spec;
}

QImage render(const RippleController& controller, int size = 200)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    controller.paint(painter);
    painter.end();
    return image;
}

int alphaAt(const QImage& image, int x, int y)
{
    return image.pixelColor(x, y).alpha();
}

} // namespace

class TestRipple : public QObject
{
    Q_OBJECT

private slots:
    void specMatchesTokens();
    void hoverPlaysOnceThenClears();
    void hoverFillsTheWholeSurface();
    void pressIsStrongerThanHover();
    void disabledPaintsNothing();
    void paintClipsToBounds();
    void themeChangeUpdatesContentColour();
    void invalidContentColourUsesThemeDefault();
    void pointerMovedDoesNotAnimate();
    void repaintRequestedTracksFrames();
    void invisibleSurfaceDoesNotAnimate();
    void emptyBoundsDropRunningRipples();
    void suspendStopsInFlightRipple();
    void pressBurstReusesAnimators();
    void repaintRequestedIsNotReentrant();
};

void TestRipple::specMatchesTokens()
{
    const Theme theme = Theme::baselineLight();
    const RippleSpec spec = RippleSpec::fromTheme(theme);

    QCOMPARE(spec.hoverOpacity, md::stateLayerOpacity(ViewState::Hovered));
    QCOMPARE(spec.pressedOpacity, md::stateLayerOpacity(ViewState::Pressed));
    QCOMPARE(spec.maxRadiusScale, 1.0);
    QVERIFY(spec.expansion.duration.count() > 0);
    QVERIFY(spec.fade.duration.count() > 0);
}

void TestRipple::hoverPlaysOnceThenClears()
{
    const Theme theme = Theme::baselineLight();
    RippleController controller;
    controller.setTheme(theme);
    controller.setBounds(QRectF(0, 0, 200, 200));
    controller.setSpec(slowSpec(theme, 120, 80));

    controller.pointerEntered(QPointF(100, 100));
    QVERIFY(controller.isAnimating());

    // Expansion and fade both complete, and the ripple is removed rather than
    // left behind as a static marker.
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isAnimating(), 3000);

    const QImage image = render(controller);
    QCOMPARE(alphaAt(image, 100, 100), 0);
}

void TestRipple::hoverFillsTheWholeSurface()
{
    const Theme theme = Theme::baselineLight();
    RippleController controller;
    controller.setTheme(theme);
    controller.setCornerRadius(24.0);
    controller.setBounds(QRectF(0, 0, 200, 200));
    controller.setSpec(slowSpec(theme, 1200, 1200));

    // Enter near a corner: a small disc would leave the far corner uncovered,
    // so a non-zero alpha there proves the radius reaches the farthest corner.
    controller.pointerEntered(QPointF(16, 16));
    QTest::qWait(1050);

    const QImage image = render(controller);
    QVERIFY2(alphaAt(image, 184, 184) > 0, "the ripple must cover the farthest corner");
    QVERIFY2(alphaAt(image, 16, 16) > 0, "the ripple starts at the pointer");
}

void TestRipple::pressIsStrongerThanHover()
{
    const Theme theme = Theme::baselineLight();
    const RippleSpec spec = slowSpec(theme, 1000, 1000);

    RippleController hover;
    hover.setTheme(theme);
    hover.setBounds(QRectF(0, 0, 200, 200));
    hover.setSpec(spec);
    hover.pointerEntered(QPointF(100, 100));
    QTest::qWait(300);

    RippleController press;
    press.setTheme(theme);
    press.setBounds(QRectF(0, 0, 200, 200));
    press.setSpec(spec);
    press.pointerPressed(QPointF(100, 100));
    QTest::qWait(300);

    const int hoverAlpha = alphaAt(render(hover), 100, 100);
    const int pressAlpha = alphaAt(render(press), 100, 100);

    QVERIFY(hoverAlpha > 0);
    QVERIFY2(pressAlpha > hoverAlpha, "the press ripple uses the pressed opacity");
}

void TestRipple::disabledPaintsNothing()
{
    RippleController controller;
    controller.setTheme(Theme::baselineLight());
    controller.setBounds(QRectF(0, 0, 200, 200));
    controller.setEnabled(false);

    controller.pointerPressed(QPointF(100, 100));
    QTest::qWait(50);

    QVERIFY(!controller.isAnimating());
    const QImage image = render(controller);
    QCOMPARE(alphaAt(image, 100, 100), 0);
}

void TestRipple::paintClipsToBounds()
{
    RippleController controller;
    controller.setTheme(Theme::baselineLight());
    controller.setBounds(QRectF(0, 0, 100, 100));
    controller.setSpec(slowSpec(Theme::baselineLight(), 800, 800));

    controller.pointerPressed(QPointF(50, 50));
    QTest::qWait(700);

    const QImage image = render(controller);
    QVERIFY(alphaAt(image, 50, 50) > 0);
    QCOMPARE(alphaAt(image, 150, 50), 0);
}

void TestRipple::themeChangeUpdatesContentColour()
{
    RippleController controller;
    const Theme light = Theme::baselineLight();
    controller.setTheme(light);
    controller.setBounds(QRectF(0, 0, 200, 200));
    controller.setSpec(slowSpec(light, 600, 600));
    controller.pointerPressed(QPointF(100, 100));
    QTest::qWait(400);

    const QColor lightPixel = render(controller).pixelColor(100, 100);

    RippleController darkController;
    darkController.setTheme(Theme::baselineDark());
    darkController.setBounds(QRectF(0, 0, 200, 200));
    darkController.setSpec(slowSpec(Theme::baselineDark(), 600, 600));
    darkController.pointerPressed(QPointF(100, 100));
    QTest::qWait(400);

    const QColor darkPixel = render(darkController).pixelColor(100, 100);

    QVERIFY(qAbs(lightPixel.red() - darkPixel.red()) > 20
            || qAbs(lightPixel.blue() - darkPixel.blue()) > 20);
}

void TestRipple::invalidContentColourUsesThemeDefault()
{
    const Theme theme = Theme::baselineLight();
    RippleController controller;
    controller.setTheme(theme);
    controller.setBounds(QRectF(0, 0, 200, 200));
    controller.setSpec(slowSpec(theme, 1000, 1000));
    controller.setContentColour(Qt::red);
    controller.setContentColour(QColor());
    controller.pointerPressed(QPointF(100, 100));
    QTest::qWait(500);

    const QColor pixel = render(controller).pixelColor(100, 100);
    const QColor expected = theme.colours().onSurface;
    QVERIFY(pixel.alpha() > 0);

    // The ripple is blended at the state-layer alpha into a premultiplied image,
    // so unpremultiplying rounds the pixel to a nearby colour instead of the
    // exact token. The tolerance keeps the assertion about the *fallback* — the
    // explicit red that was set before must be gone — without depending on that
    // rounding. A wrong fallback (red, or an invalid colour left in place) is
    // far outside the tolerance.
    constexpr int kRoundingTolerance = 8;
    QVERIFY2(qAbs(pixel.red() - expected.red()) <= kRoundingTolerance,
             qPrintable(QStringLiteral("red %1 vs %2").arg(pixel.red()).arg(expected.red())));
    QVERIFY2(qAbs(pixel.green() - expected.green()) <= kRoundingTolerance,
             qPrintable(QStringLiteral("green %1 vs %2")
                            .arg(pixel.green())
                            .arg(expected.green())));
    QVERIFY2(qAbs(pixel.blue() - expected.blue()) <= kRoundingTolerance,
             qPrintable(QStringLiteral("blue %1 vs %2").arg(pixel.blue()).arg(expected.blue())));
}

void TestRipple::pointerMovedDoesNotAnimate()
{
    RippleController controller;
    controller.setTheme(Theme::baselineLight());
    controller.setBounds(QRectF(0, 0, 200, 200));

    QSignalSpy repaints(&controller, &RippleController::repaintRequested);

    controller.pointerEntered(QPointF(40, 40));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isAnimating(), 3000);

    repaints.clear();
    controller.pointerMoved(QPointF(160, 160));
    QTest::qWait(50);

    QCOMPARE(repaints.count(), 0);
}

void TestRipple::repaintRequestedTracksFrames()
{
    RippleController controller;
    controller.setTheme(Theme::baselineLight());
    controller.setBounds(QRectF(0, 0, 200, 200));
    controller.setSpec(slowSpec(Theme::baselineLight(), 300, 200));

    QSignalSpy repaints(&controller, &RippleController::repaintRequested);
    controller.pointerPressed(QPointF(100, 100));

    QTRY_VERIFY_WITH_TIMEOUT(!controller.isAnimating(), 3000);
    QVERIFY(repaints.count() > 2);
}

void TestRipple::invisibleSurfaceDoesNotAnimate()
{
    const Theme theme = Theme::baselineLight();

    // No bounds at all: there is nothing to clip a circle to.
    {
        RippleController controller;
        controller.setTheme(theme);
        controller.setSpec(slowSpec(theme, 1000, 1000));

        QSignalSpy repaints(&controller, &RippleController::repaintRequested);
        QTest::qWait(10);
        repaints.clear();

        controller.pointerEntered(QPointF(40, 40));
        controller.pointerPressed(QPointF(40, 40));
        QVERIFY(!controller.isAnimating());

        QTest::qWait(50);
        QCOMPARE(repaints.count(), 0);
    }

    // Zero opacity: every circle would blend to nothing.
    {
        RippleController controller;
        controller.setTheme(theme);
        controller.setBounds(QRectF(0, 0, 200, 200));
        RippleSpec spec = slowSpec(theme, 1000, 1000);
        spec.pressedOpacity = 0.0;
        controller.setSpec(spec);

        QSignalSpy repaints(&controller, &RippleController::repaintRequested);
        QTest::qWait(10);
        repaints.clear();

        controller.pointerPressed(QPointF(100, 100));
        QVERIFY(!controller.isAnimating());

        QTest::qWait(50);
        QCOMPARE(repaints.count(), 0);
        QCOMPARE(alphaAt(render(controller), 100, 100), 0);
    }

    // A non-positive radius scale: the circle never grows.
    {
        RippleController controller;
        controller.setTheme(theme);
        controller.setBounds(QRectF(0, 0, 200, 200));
        RippleSpec spec = slowSpec(theme, 1000, 1000);
        spec.maxRadiusScale = 0.0;
        controller.setSpec(spec);

        QSignalSpy repaints(&controller, &RippleController::repaintRequested);
        QTest::qWait(10);
        repaints.clear();

        controller.pointerEntered(QPointF(100, 100));
        QVERIFY(!controller.isAnimating());

        QTest::qWait(50);
        QCOMPARE(repaints.count(), 0);
    }

    // A transparent content colour paints nothing either.
    {
        RippleController controller;
        controller.setTheme(theme);
        controller.setBounds(QRectF(0, 0, 200, 200));
        controller.setSpec(slowSpec(theme, 1000, 1000));
        controller.setContentColour(QColor(255, 0, 0, 0));

        QSignalSpy repaints(&controller, &RippleController::repaintRequested);
        QTest::qWait(10);
        repaints.clear();

        controller.pointerPressed(QPointF(100, 100));
        QVERIFY(!controller.isAnimating());

        QTest::qWait(50);
        QCOMPARE(repaints.count(), 0);
    }
}

void TestRipple::emptyBoundsDropRunningRipples()
{
    const Theme theme = Theme::baselineLight();
    RippleController controller;
    controller.setTheme(theme);
    controller.setBounds(QRectF(0, 0, 200, 200));
    controller.setSpec(slowSpec(theme, 1000, 1000));

    controller.pointerPressed(QPointF(100, 100));
    QVERIFY(controller.isAnimating());

    QSignalSpy repaints(&controller, &RippleController::repaintRequested);
    controller.setBounds(QRectF());

    // The run is dropped, not merely clipped away.
    QVERIFY(!controller.isAnimating());
    QTest::qWait(10);
    repaints.clear();
    QTest::qWait(100);
    QCOMPARE(repaints.count(), 0);

    QCOMPARE(alphaAt(render(controller), 100, 100), 0);
}

void TestRipple::suspendStopsInFlightRipple()
{
    const Theme theme = Theme::baselineLight();
    RippleController controller;
    controller.setTheme(theme);
    controller.setBounds(QRectF(0, 0, 200, 200));
    controller.setSpec(slowSpec(theme, 1000, 1000));

    controller.pointerPressed(QPointF(100, 100));
    QTest::qWait(60);
    QVERIFY(controller.isAnimating());

    controller.suspend();
    QVERIFY(!controller.isSurfaceVisible());
    QVERIFY(controller.isEnabled());
    QVERIFY(!controller.isAnimating());

    QSignalSpy repaints(&controller, &RippleController::repaintRequested);
    QTest::qWait(10);
    repaints.clear();
    QTest::qWait(120);
    QCOMPARE(repaints.count(), 0);

    // A hidden surface shows nothing, ignores input, and a re-show cannot
    // reveal the circle that was in flight.
    QCOMPARE(alphaAt(render(controller), 100, 100), 0);
    controller.pointerPressed(QPointF(100, 100));
    QVERIFY(!controller.isAnimating());

    controller.setSurfaceVisible(true);
    QVERIFY(controller.isSurfaceVisible());
    controller.pointerPressed(QPointF(100, 100));
    QVERIFY(controller.isAnimating());

    controller.setEnabled(false);
    QVERIFY(!controller.isAnimating());
}

void TestRipple::pressBurstReusesAnimators()
{
    const Theme theme = Theme::baselineLight();
    RippleController controller;
    controller.setTheme(theme);
    controller.setBounds(QRectF(0, 0, 200, 200));
    controller.setSpec(slowSpec(theme, 1000, 1000));

    // A synchronous burst far beyond the eight-press cap. Every eviction
    // recycles its animator, so the cap also bounds how many ever exist.
    for (int press = 0; press < 24; ++press)
        controller.pointerPressed(QPointF(100, 100));

    int animators = 0;
    for (QObject* child : controller.findChildren<QObject*>()) {
        if (child->inherits("md::animation::Animator"))
            ++animators;
    }

    QVERIFY2(animators <= 8,
             qPrintable(QStringLiteral("24 presses allocated %1 animators").arg(animators)));
    QVERIFY(controller.isAnimating());

    QTRY_VERIFY_WITH_TIMEOUT(!controller.isAnimating(), 6000);
}

void TestRipple::repaintRequestedIsNotReentrant()
{
    const Theme theme = Theme::baselineLight();
    RippleController controller;
    controller.setTheme(theme);
    controller.setBounds(QRectF(0, 0, 200, 200));
    controller.setSpec(slowSpec(theme, 800, 800));

    int notifications = 0;
    bool disabled = false;
    QObject::connect(&controller, &RippleController::repaintRequested, &controller, [&] {
        ++notifications;
        if (disabled)
            return;

        // A directly connected slot that reacts by disabling the controller used
        // to run inside the animator callback, where removeAll() destroyed the
        // function that was executing. The notification is queued now, so this
        // is safe and the controller ends up idle.
        disabled = true;
        controller.setEnabled(false);
    });

    controller.pointerPressed(QPointF(100, 100));

    QTRY_VERIFY_WITH_TIMEOUT(disabled, 2000);
    QVERIFY(!controller.isEnabled());
    QVERIFY(!controller.isAnimating());

    // No further animation frames arrive once the controller is disabled.
    QTest::qWait(50);
    const int settled = notifications;
    QTest::qWait(100);
    QCOMPARE(notifications, settled);
}

QTEST_MAIN(TestRipple)
#include "tst_ripple.moc"
