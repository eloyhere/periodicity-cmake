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
    void pointerMovedDoesNotAnimate();
    void repaintRequestedTracksFrames();
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

QTEST_MAIN(TestRipple)
#include "tst_ripple.moc"
