// Tests for NavigationBar: the destination model, selection clamping, click and
// keyboard activation, the empty measurement, painting and RTL mirroring.

#include <md/components/navigation_bar.h>
#include <md/core/measure_spec.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QSignalSpy>
#include <QTest>

namespace {

md::NavigationBar::Destination makeDestination(const QString& label,
                                               const QString& badge = QString())
{
    md::NavigationBar::Destination destination;
    destination.label = label;
    destination.badge = badge;
    return destination;
}

void clickAt(QWidget& widget, const QPointF& position,
             Qt::MouseButton button = Qt::LeftButton)
{
    QMouseEvent press(QEvent::MouseButtonPress, position, widget.mapToGlobal(position.toPoint()),
                      button, button, Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, position,
                        widget.mapToGlobal(position.toPoint()), button, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(&widget, &release);
}

void sendKey(QWidget& widget, int key, const QString& text = QString())
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
    QApplication::sendEvent(&widget, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
    QApplication::sendEvent(&widget, &release);
}

// Samples a widget-space point from a grab, honouring the device pixel ratio.
QColor pixelAt(const QImage& image, const QPointF& widgetPosition)
{
    const qreal ratio = image.devicePixelRatio();
    const int x = qBound(0, qRound(widgetPosition.x() * ratio), image.width() - 1);
    const int y = qBound(0, qRound(widgetPosition.y() * ratio), image.height() - 1);
    return image.pixelColor(x, y);
}

} // namespace

class TestNavigationBar : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void addDestinationReturnsIncreasingIndices();
    void setCurrentIndexClamps();
    void clickActivatesDestination();
    void pressCancelledByReleaseOutside();
    void keyboardMovesAndActivates();
    void emptyBarMeasuresAndPaintsNothing();
    void selectedDestinationPaintsIndicator();
    void rightToLeftMirrorsOrderAndKeys();
    void accessibilityReportsDestinationLabels();
};

void TestNavigationBar::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestNavigationBar::cleanupTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestNavigationBar::addDestinationReturnsIncreasingIndices()
{
    md::NavigationBar bar;
    QCOMPARE(bar.destinationCount(), 0);
    QCOMPARE(bar.currentIndex(), -1);

    const int first = bar.addDestination(makeDestination(QStringLiteral("Home")));
    const int second =
        bar.addDestination(makeDestination(QStringLiteral("Search"), QStringLiteral("3")));
    const int third = bar.addDestination(makeDestination(QStringLiteral("Library")));

    QCOMPARE(first, 0);
    QCOMPARE(second, 1);
    QCOMPARE(third, 2);
    QCOMPARE(bar.destinationCount(), 3);
    QCOMPARE(bar.currentIndex(), 0); // the first destination is current

    QCOMPARE(bar.destinationAt(1).label, QStringLiteral("Search"));
    QCOMPARE(bar.destinationAt(1).badge, QStringLiteral("3"));
    QCOMPARE(bar.destinationAt(2).label, QStringLiteral("Library"));
    QVERIFY(bar.destinationAt(9).label.isEmpty()); // out of range: default value

    bar.clearDestinations();
    QCOMPARE(bar.destinationCount(), 0);
    QCOMPARE(bar.currentIndex(), -1);
    QVERIFY(bar.destinationAt(0).label.isEmpty());
}

void TestNavigationBar::setCurrentIndexClamps()
{
    md::NavigationBar bar;
    bar.addDestination(makeDestination(QStringLiteral("Home")));
    bar.addDestination(makeDestination(QStringLiteral("Search")));
    bar.addDestination(makeDestination(QStringLiteral("Library")));

    QSignalSpy activated(&bar, &md::NavigationBar::destinationActivated);

    bar.setCurrentIndex(99);
    QCOMPARE(bar.currentIndex(), 2);

    bar.setCurrentIndex(-99);
    QCOMPARE(bar.currentIndex(), 0);

    bar.setCurrentIndex(1);
    QCOMPARE(bar.currentIndex(), 1);

    // Selecting is not activating.
    QCOMPARE(activated.count(), 0);

    bar.clearDestinations();
    bar.setCurrentIndex(3);
    QCOMPARE(bar.currentIndex(), -1);
}

void TestNavigationBar::clickActivatesDestination()
{
    md::NavigationBar bar;
    bar.addDestination(makeDestination(QStringLiteral("Home")));
    bar.addDestination(makeDestination(QStringLiteral("Search")));
    bar.addDestination(makeDestination(QStringLiteral("Library")));
    bar.resize(360, 80);

    QSignalSpy activated(&bar, &md::NavigationBar::destinationActivated);

    clickAt(bar, bar.destinationRect(2).center());
    QCOMPARE(bar.currentIndex(), 2);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.at(0).at(0).toInt(), 2);

    // Re-activating the current destination still reports an activation.
    clickAt(bar, bar.destinationRect(2).center());
    QCOMPARE(activated.count(), 2);

    // Only the left button activates.
    clickAt(bar, bar.destinationRect(0).center(), Qt::RightButton);
    QCOMPARE(activated.count(), 2);

    // A disabled bar is inert.
    bar.setEnabled(false);
    clickAt(bar, bar.destinationRect(0).center());
    QCOMPARE(activated.count(), 2);
}

void TestNavigationBar::keyboardMovesAndActivates()
{
    md::NavigationBar bar;
    bar.addDestination(makeDestination(QStringLiteral("Home")));
    bar.addDestination(makeDestination(QStringLiteral("Search")));
    bar.addDestination(makeDestination(QStringLiteral("Library")));
    bar.resize(360, 80);

    QSignalSpy activated(&bar, &md::NavigationBar::destinationActivated);

    QCOMPARE(bar.currentIndex(), 0);

    sendKey(bar, Qt::Key_Right);
    QCOMPARE(bar.currentIndex(), 1);
    sendKey(bar, Qt::Key_Right);
    QCOMPARE(bar.currentIndex(), 2);
    sendKey(bar, Qt::Key_Right); // clamps at the last destination
    QCOMPARE(bar.currentIndex(), 2);

    sendKey(bar, Qt::Key_Left);
    QCOMPARE(bar.currentIndex(), 1);

    sendKey(bar, Qt::Key_Home);
    QCOMPARE(bar.currentIndex(), 0);
    sendKey(bar, Qt::Key_End);
    QCOMPARE(bar.currentIndex(), 2);

    sendKey(bar, Qt::Key_Left);
    QCOMPARE(bar.currentIndex(), 1);

    // Moving the highlight does not activate.
    QCOMPARE(activated.count(), 0);

    sendKey(bar, Qt::Key_Space);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.at(0).at(0).toInt(), 1);
    QCOMPARE(bar.currentIndex(), 1);

    sendKey(bar, Qt::Key_Return);
    QCOMPARE(activated.count(), 2);
    QCOMPARE(activated.at(1).at(0).toInt(), 1);

    // Up/Down are the vertical aliases of the horizontal movement.
    sendKey(bar, Qt::Key_Down);
    QCOMPARE(bar.currentIndex(), 2);
    sendKey(bar, Qt::Key_Up);
    QCOMPARE(bar.currentIndex(), 1);
}

void TestNavigationBar::emptyBarMeasuresAndPaintsNothing()
{
    md::NavigationBar bar;
    QCOMPARE(bar.destinationCount(), 0);

    bar.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());
    QCOMPARE(bar.measuredSize().height(), 80.0);
    QCOMPARE(bar.sizeHint().height(), 80);

    bar.resize(320, 80);
    bar.show();
    QTest::qWait(10);
    bar.clearFocus();

    const QPixmap rendered = bar.grab();
    QVERIFY(!rendered.isNull());
    const QImage image = rendered.toImage();
    QVERIFY(!image.isNull());

    // The empty bar paints its surface and no destination content.
    const QColor background = md::Theme::forWidget(&bar).colours().surfaceContainer;
    QCOMPARE(pixelAt(image, QPointF(2.0, 2.0)).rgb(), background.rgb());
    QCOMPARE(pixelAt(image, QPointF(160.0, 40.0)).rgb(), background.rgb());
    QCOMPARE(pixelAt(image, QPointF(318.0, 78.0)).rgb(), background.rgb());

    QSignalSpy activated(&bar, &md::NavigationBar::destinationActivated);
    clickAt(bar, QPointF(10.0, 10.0));
    QCOMPARE(activated.count(), 0);
}

void TestNavigationBar::selectedDestinationPaintsIndicator()
{
    md::NavigationBar bar;
    bar.addDestination(makeDestination(QStringLiteral("Home")));
    bar.addDestination(makeDestination(QStringLiteral("Search")));
    bar.resize(320, 80); // the first destination is selected by default
    bar.show();
    QTest::qWait(10);
    bar.clearFocus();

    const QImage image = bar.grab().toImage();
    QVERIFY(!image.isNull());

    const QColor indicator = md::Theme::forWidget(&bar).colours().secondaryContainer;
    const QRectF slot = bar.destinationRect(0);
    // 24 dp below the top edge is inside the 32 dp indicator pill, above the
    // 4 dp gap and the label (the exact stack is centred in the 80 dp bar).
    QCOMPARE(pixelAt(image, QPointF(slot.center().x(), 24.0)).rgb(), indicator.rgb());

    // The unselected destination has no pill at that height.
    const QRectF other = bar.destinationRect(1);
    QCOMPARE(pixelAt(image, QPointF(other.center().x(), 24.0)).rgb(),
             md::Theme::forWidget(&bar).colours().surfaceContainer.rgb());
}

void TestNavigationBar::rightToLeftMirrorsOrderAndKeys()
{
    md::NavigationBar bar;
    bar.addDestination(makeDestination(QStringLiteral("Home")));
    bar.addDestination(makeDestination(QStringLiteral("Search")));
    bar.addDestination(makeDestination(QStringLiteral("Library")));
    bar.resize(360, 80);
    bar.setLayoutDirection(Qt::RightToLeft);

    // Destination 0 is painted in the rightmost slot.
    const QRectF first = bar.destinationRect(0);
    const QRectF middle = bar.destinationRect(1);
    const QRectF last = bar.destinationRect(2);
    QVERIFY(first.center().x() > middle.center().x());
    QVERIFY(middle.center().x() > last.center().x());
    QVERIFY(first.left() > last.right());

    // Left/Right are mirrored: Left advances, Right retreats.
    sendKey(bar, Qt::Key_Left);
    QCOMPARE(bar.currentIndex(), 1);
    sendKey(bar, Qt::Key_Right);
    QCOMPARE(bar.currentIndex(), 0);
    sendKey(bar, Qt::Key_Home);
    QCOMPARE(bar.currentIndex(), 0);
    sendKey(bar, Qt::Key_End);
    QCOMPARE(bar.currentIndex(), 2);

    QSignalSpy activated(&bar, &md::NavigationBar::destinationActivated);
    sendKey(bar, Qt::Key_Space);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.at(0).at(0).toInt(), 2);

    // Rendering and hit testing follow the mirrored geometry.
    bar.show();
    QTest::qWait(10);
    const QPixmap rendered = bar.grab();
    QVERIFY(!rendered.isNull());
    QCOMPARE(rendered.size(), bar.size());

    clickAt(bar, bar.destinationRect(0).center());
    QCOMPARE(bar.currentIndex(), 0);
    QCOMPARE(activated.count(), 2);
    QCOMPARE(activated.at(1).at(0).toInt(), 0);
}

void TestNavigationBar::accessibilityReportsDestinationLabels()
{
    md::NavigationBar bar;
    QCOMPARE(bar.accessibleName(), QStringLiteral("Navigation"));

    bar.addDestination(makeDestination(QStringLiteral("Home")));
    bar.addDestination(makeDestination(QStringLiteral("Search")));
    bar.addDestination(makeDestination(QStringLiteral("Library")));

    QVERIFY(bar.accessibleDescription().contains(QStringLiteral("Home")));
    QVERIFY(bar.accessibleDescription().contains(QStringLiteral("Search")));
    QVERIFY(bar.accessibleDescription().contains(QStringLiteral("Library")));

    bar.setCurrentIndex(1);
    QVERIFY(bar.accessibleDescription().contains(QStringLiteral("Search, selected")));
}

QTEST_MAIN(TestNavigationBar)
#include "tst_navigation_bar.moc"

// ---------------------------------------------------------------------------
// Regression test (appended): a destination is activated on release, and only
// when the release lands on the destination that was pressed.
// ---------------------------------------------------------------------------

void TestNavigationBar::pressCancelledByReleaseOutside()
{
    md::NavigationBar bar;
    bar.addDestination(makeDestination(QStringLiteral("Home")));
    bar.addDestination(makeDestination(QStringLiteral("Search")));
    bar.addDestination(makeDestination(QStringLiteral("Library")));
    bar.resize(360, 80);

    QSignalSpy activated(&bar, &md::NavigationBar::destinationActivated);
    QCOMPARE(bar.currentIndex(), 0);

    // A press alone does not activate; the bar waits for the release.
    const QPointF target = bar.destinationRect(1).center();
    QMouseEvent press(QEvent::MouseButtonPress, target, bar.mapToGlobal(target.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&bar, &press);
    QCOMPARE(bar.currentIndex(), 0);
    QCOMPARE(activated.count(), 0);

    // Releasing outside the bar abandons the click.
    const QPointF outside(-40.0, 40.0);
    QMouseEvent outsideRelease(QEvent::MouseButtonRelease, outside,
                               bar.mapToGlobal(outside.toPoint()), Qt::LeftButton,
                               Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&bar, &outsideRelease);
    QCOMPARE(bar.currentIndex(), 0);
    QCOMPARE(activated.count(), 0);

    // Releasing over a different destination abandons it too.
    QApplication::sendEvent(&bar, &press);
    const QPointF other = bar.destinationRect(2).center();
    QMouseEvent otherRelease(QEvent::MouseButtonRelease, other,
                             bar.mapToGlobal(other.toPoint()), Qt::LeftButton,
                             Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&bar, &otherRelease);
    QCOMPARE(bar.currentIndex(), 0);
    QCOMPARE(activated.count(), 0);

    // Press and release on the same destination activate it.
    clickAt(bar, target);
    QCOMPARE(bar.currentIndex(), 1);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.at(0).at(0).toInt(), 1);
}
