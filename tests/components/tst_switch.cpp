// Unit tests for md::Switch (framework.md §5.9, §14).
//
// The switch is exercised offscreen: the application theme is pinned to the
// baseline light scheme, the widget is measured from sizeHint(), rendered into
// a transparent QImage (so unpainted pixels are deterministic rather than
// uninitialised) and sampled in logical coordinates. Pointer and key input is
// delivered straight to the widget with QApplication::sendEvent, exactly as
// tests/view does, so no window activation or virtual cursor is involved.

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRectF>
#include <QRegion>
#include <QSignalSpy>
#include <QSize>
#include <QTest>
#include <QWidget>

#include <md/components/switch.h>
#include <md/core/density.h>
#include <md/core/measure_spec.h>
#include <md/theme/theme.h>
#include <md/tokens/color_scheme.h>

namespace {

// The theme every test paints from. initTestCase pinning it means no widget
// depends on the platform's default application theme.
const md::ColorScheme& lightColours()
{
    return md::Theme::applicationTheme().colours();
}

// Renders a widget into an explicitly transparent image. QWidget::render()
// would otherwise paint the palette's window background first - and
// QWidget::grab() leaves unpainted pixels uninitialised for widgets that do
// not fill their background - so every pixel assertion in this file renders
// with DrawChildren only, over a cleared image.
QImage renderWidget(QWidget& widget)
{
    const qreal ratio = widget.devicePixelRatioF();
    QImage image(widget.size() * ratio, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    widget.render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();

    return image;
}

QColor pixelAt(const QImage& image, const QPointF& logical)
{
    const qreal ratio = image.devicePixelRatio();
    const int x = qBound(0, qRound(logical.x() * ratio), image.width() - 1);
    const int y = qBound(0, qRound(logical.y() * ratio), image.height() - 1);
    return image.pixelColor(x, y);
}

void sendMouse(QWidget& widget, QEvent::Type type, const QPointF& position,
               Qt::MouseButton button, Qt::MouseButtons buttons)
{
    const QPoint global = widget.mapToGlobal(position.toPoint());
    QMouseEvent event(type, position, QPointF(global), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

void clickAt(QWidget& widget, const QPointF& position)
{
    sendMouse(widget, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
}

void sendKey(QWidget& widget, int key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);

    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(&widget, &release);
}

} // namespace

class TestSwitch : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void defaultsToUnchecked();
    void setCheckedUpdatesState();
    void clickTogglesOnce();
    void spaceToggles();
    void repeatedSetCheckedDoesNotEmit();
    void touchTargetIsAtLeast48Dp();
    void paintsBothStates();
};

void TestSwitch::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestSwitch::defaultsToUnchecked()
{
    md::Switch toggle;
    QVERIFY(!toggle.isChecked());
    QVERIFY(toggle.focusPolicy() == Qt::StrongFocus);
    QCOMPARE(toggle.accessibleName(), QStringLiteral("Switch"));

    const md::Density& density = md::Theme::applicationTheme().density();
    QCOMPARE(toggle.sizeHint(),
             QSize(qRound(density.dp(52.0)), qRound(density.dp(48.0))));
}

void TestSwitch::setCheckedUpdatesState()
{
    md::Switch toggle;

    toggle.setChecked(true);
    QVERIFY(toggle.isChecked());

    toggle.setChecked(false);
    QVERIFY(!toggle.isChecked());
}

void TestSwitch::clickTogglesOnce()
{
    md::Switch toggle;
    toggle.resize(toggle.sizeHint());
    QSignalSpy spy(&toggle, &md::Switch::toggled);

    // The centre of the 48 dp target is inside the visual too.
    clickAt(toggle, QPointF(26.0, 24.0));
    QVERIFY(toggle.isChecked());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    clickAt(toggle, QPointF(26.0, 24.0));
    QVERIFY(!toggle.isChecked());
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);

    // A click outside the widget never reaches it as a press.
    clickAt(toggle, QPointF(toggle.width() + 20.0, 24.0));
    QCOMPARE(spy.count(), 2);
}

void TestSwitch::spaceToggles()
{
    md::Switch toggle;
    toggle.resize(toggle.sizeHint());
    QSignalSpy spy(&toggle, &md::Switch::toggled);

    sendKey(toggle, Qt::Key_Space);
    QVERIFY(toggle.isChecked());
    QCOMPARE(spy.count(), 1);

    sendKey(toggle, Qt::Key_Space);
    QVERIFY(!toggle.isChecked());
    QCOMPARE(spy.count(), 2);
}

void TestSwitch::repeatedSetCheckedDoesNotEmit()
{
    md::Switch toggle;
    QSignalSpy spy(&toggle, &md::Switch::toggled);

    toggle.setChecked(true);
    QCOMPARE(spy.count(), 1);

    // Pushing the same value again is a no-op, not a second signal.
    toggle.setChecked(true);
    QCOMPARE(spy.count(), 1);
    QVERIFY(toggle.isChecked());

    toggle.setChecked(false);
    QCOMPARE(spy.count(), 2);

    toggle.setChecked(false);
    QCOMPARE(spy.count(), 2);
    QVERIFY(!toggle.isChecked());
}

void TestSwitch::touchTargetIsAtLeast48Dp()
{
    md::Switch toggle;
    const md::Density& density = md::Theme::applicationTheme().density();
    const qreal minimum = density.dp(48.0);

    QVERIFY(toggle.sizeHint().height() >= minimum);
    QVERIFY(toggle.sizeHint().width() >= minimum);

    toggle.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());
    QVERIFY(toggle.measuredSize().height() >= minimum);

    // An Exactly height is authoritative, as the measure contract says.
    toggle.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::exactly(64.0));
    QCOMPARE(toggle.measuredSize().height(), qreal(64.0));
}

void TestSwitch::paintsBothStates()
{
    md::Switch toggle;
    toggle.resize(toggle.sizeHint());

    // Both samples are on the track: y = 10 sits below the 32 dp track's top
    // edge (8) and above the 24 dp thumb's top edge (12), so the pixel shows
    // the track role alone.
    const QImage unchecked = renderWidget(toggle);
    QCOMPARE(pixelAt(unchecked, QPointF(26.0, 10.0)).rgb(),
             lightColours().surfaceContainerHighest.rgb());
    QCOMPARE(pixelAt(unchecked, QPointF(16.0, 24.0)).rgb(), lightColours().outline.rgb());

    // The switch is hidden here, so setChecked() snaps instead of animating:
    // the checked roles are exact by the next paint.
    toggle.setChecked(true);
    const QImage checked = renderWidget(toggle);
    QCOMPARE(pixelAt(checked, QPointF(26.0, 10.0)).rgb(), lightColours().primary.rgb());
    QCOMPARE(pixelAt(checked, QPointF(36.0, 24.0)).rgb(), lightColours().onPrimary.rgb());
}

QTEST_MAIN(TestSwitch)
#include "tst_switch.moc"
