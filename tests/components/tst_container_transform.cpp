// Tests for the container transform: card enter/exit motion, reduced motion,
// interruption, and the stop-on-hide rule.
//
// The helper is deliberately testable headlessly: it owns no window and only
// writes a QGraphicsEffect plus a painter mirror, so every value the animation
// produces is observable without grabbing pixels.

#include <md/components/container_transform.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QGraphicsEffect>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

namespace {

qreal opacityOf(md::ContainerTransform& transform)
{
    return transform.painterOpacity();
}

bool near(qreal value, qreal expected, qreal tolerance = 0.02)
{
    return qAbs(value - expected) <= tolerance;
}

} // namespace

class TestContainerTransform : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void enterAndExitAnimateTheSurface();
    void effectIsInstalledOnTheSurface();
    void reducedMotionSnapsSynchronously();
    void stopKeepsProgressAndDoesNotEmitFinished();
    void interruptingDoesNotJump();
    void hidingTheSurfaceStopsTheRun();
    void painterMirrorTracksTheEffect();
};

void TestContainerTransform::init()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestContainerTransform::cleanup()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestContainerTransform::enterAndExitAnimateTheSurface()
{
    QWidget surface;
    surface.resize(240, 160);
    surface.show();

    md::ContainerTransform transform(&surface);
    QSignalSpy finished(&transform, &md::ContainerTransform::finished);

    transform.snapTo(false);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(transform.progress(), 0.0);
    QVERIFY(near(transform.painterOpacity(), 0.0));
    QVERIFY(near(transform.painterScale(), transform.scaleFrom()));
    QVERIFY(transform.painterRise() > 0.0);

    transform.enter(true);
    QVERIFY(transform.isRunning());
    QVERIFY(transform.progress() < 0.5);

    QTRY_VERIFY_WITH_TIMEOUT(!transform.isRunning(), 2000);
    QCOMPARE(finished.count(), 2);
    QCOMPARE(transform.isEntered(), true);
    QCOMPARE(transform.progress(), 1.0);
    QVERIFY(near(opacityOf(transform), 1.0));
    QVERIFY(near(transform.painterScale(), 1.0));
    QVERIFY(near(transform.painterRise(), 0.0));

    transform.exit(true);
    QVERIFY(transform.isRunning());
    QTRY_VERIFY_WITH_TIMEOUT(!transform.isRunning(), 2000);
    QCOMPARE(finished.count(), 3);
    QCOMPARE(transform.isEntered(), false);
    QCOMPARE(transform.progress(), 0.0);
    QVERIFY(near(transform.painterOpacity(), 0.0));
}

void TestContainerTransform::effectIsInstalledOnTheSurface()
{
    QWidget surface;
    md::ContainerTransform transform(&surface);
    QVERIFY(surface.graphicsEffect() != nullptr);
}

void TestContainerTransform::reducedMotionSnapsSynchronously()
{
    md::MotionScheme motion;
    motion.reducedMotion = true;
    md::Theme::setApplicationTheme(md::Theme::baselineLight().withMotion(motion));

    QWidget surface;
    surface.show();

    md::ContainerTransform transform(&surface);
    QSignalSpy finished(&transform, &md::ContainerTransform::finished);

    transform.enter(true);
    QVERIFY(!transform.isRunning());
    QCOMPARE(transform.progress(), 1.0);
    QCOMPARE(finished.count(), 1);
    QVERIFY(finished.last().at(0).toBool());
}

void TestContainerTransform::stopKeepsProgressAndDoesNotEmitFinished()
{
    QWidget surface;
    surface.resize(200, 120);
    surface.show();

    md::ContainerTransform transform(&surface);
    transform.snapTo(false);
    QSignalSpy finished(&transform, &md::ContainerTransform::finished);

    transform.enter(true);
    QTest::qWait(140);
    const qreal progress = transform.progress();
    QVERIFY(progress > 0.0);
    QVERIFY(progress < 1.0);

    transform.stop();
    QVERIFY(!transform.isRunning());
    QVERIFY(near(transform.progress(), progress, 0.001));
    QTest::qWait(400);
    QCOMPARE(finished.count(), 0);
}

void TestContainerTransform::interruptingDoesNotJump()
{
    QWidget surface;
    surface.resize(200, 120);
    surface.show();

    md::ContainerTransform transform(&surface);
    transform.snapTo(false);
    transform.enter(true);
    QTest::qWait(160);
    const qreal progress = transform.progress();
    QVERIFY(progress > 0.0);
    QVERIFY(progress < 1.0);

    transform.exit(true);
    // The new run starts from the last painted value, not from the end.
    QVERIFY(near(transform.progress(), progress, 0.05));
    QTRY_VERIFY_WITH_TIMEOUT(transform.progress() < 0.01, 2000);
    QCOMPARE(transform.progress(), 0.0);
}

void TestContainerTransform::hidingTheSurfaceStopsTheRun()
{
    QWidget surface;
    surface.resize(200, 120);
    surface.show();

    md::ContainerTransform transform(&surface);
    transform.snapTo(false);
    transform.enter(true);
    QTest::qWait(120);
    QVERIFY(transform.isRunning());

    surface.hide();
    QVERIFY(!transform.isRunning());
}

void TestContainerTransform::painterMirrorTracksTheEffect()
{
    QWidget surface;
    surface.resize(200, 120);
    surface.show();

    md::ContainerTransform transform(&surface);
    transform.snapTo(false);
    QVERIFY(near(transform.painterOpacity(), 0.0));
    QVERIFY(near(transform.painterScale(), 0.92));
    QVERIFY(transform.painterRise() >= 0.0);

    transform.snapTo(true);
    QVERIFY(near(transform.painterOpacity(), 1.0));
    QVERIFY(near(transform.painterScale(), 1.0));
    QVERIFY(near(transform.painterRise(), 0.0));
    QVERIFY(transform.painterScale() >= transform.scaleFrom());
}

QTEST_MAIN(TestContainerTransform)
#include "tst_container_transform.moc"
