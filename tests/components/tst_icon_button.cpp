// Unit tests for the IconButton component (framework.md §14).
//
// Every widget is exercised offscreen: the application theme is pinned to the
// baseline light scheme, the widgets are measured from sizeHint(), rendered
// into a transparent QImage (so unpainted pixels are deterministic rather than
// uninitialised) and sampled in logical coordinates. Pointer and key input is
// delivered straight to the widget with QApplication::sendEvent, exactly as
// tests/components/tst_basic_widgets.cpp does, so no window activation or
// virtual cursor is involved.

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QRegion>
#include <QSignalSpy>
#include <QSize>
#include <QTest>
#include <QWidget>

#include <md/components/icon_button.h>
#include <md/theme/theme.h>
#include <md/tokens/color_scheme.h>

namespace {

const md::ColorScheme& colours()
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

// A white 20 x 20 square inside a 24 x 24 box. IconDrawable tints it, so the
// painted centre is the content role and not the source colour.
QIcon solidIcon()
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(Qt::white));
        painter.drawRect(QRectF(2.0, 2.0, 20.0, 20.0));
    }
    return QIcon(pixmap);
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

class TestIconButton : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void defaultsMatchSpec();
    void containerFitsTouchTarget();
    void clickEmitsClickedOnce();
    void checkableClickToggles();
    void checkedStateIsProgrammatic();
    void keyboardActivates();
    void disabledEmitsNothing();
    void variantsPaintRoles();
    void checkedUsesSelectedColours();
    void disabledDiffersFromEnabled();
};

void TestIconButton::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestIconButton::defaultsMatchSpec()
{
    md::IconButton button;
    QVERIFY(button.variant() == md::IconButton::Variant::Standard);
    QCOMPARE(button.iconSize(), 24.0);
    QVERIFY(!button.isCheckable());
    QVERIFY(!button.isChecked());
    QVERIFY(button.icon().isNull());
    QVERIFY(button.focusPolicy() == Qt::StrongFocus);
    QVERIFY(button.isEnabled());

    // The API round-trips.
    button.setIcon(solidIcon());
    QVERIFY(!button.icon().isNull());

    button.setIconSize(20.0);
    QCOMPARE(button.iconSize(), 20.0);

    button.setVariant(md::IconButton::Variant::Outlined);
    QVERIFY(button.variant() == md::IconButton::Variant::Outlined);

    button.setCheckable(true);
    QVERIFY(button.isCheckable());

    button.setChecked(true);
    QVERIFY(button.isChecked());

    button.setChecked(false);
    QVERIFY(!button.isChecked());

    // The icon constructor seeds the drawable.
    const QIcon source = solidIcon();
    md::IconButton withIcon(source);
    QVERIFY(!withIcon.icon().isNull());
    QCOMPARE(withIcon.icon().cacheKey(), source.cacheKey());
}

void TestIconButton::containerFitsTouchTarget()
{
    md::IconButton button;
    button.setVariant(md::IconButton::Variant::Filled);

    // The layout size is the 48 dp touch target, not the 40 dp visual.
    const QSize hint = button.sizeHint();
    QVERIFY(hint.width() >= 48);
    QVERIFY(hint.height() >= 48);
    QCOMPARE(hint, QSize(48, 48));

    button.resize(hint);
    const QImage image = renderWidget(button);

    // The 40 dp container is centred, leaving 4 dp of transparent padding on
    // every side of the 48 dp target.
    QCOMPARE(pixelAt(image, QPointF(24.0, 24.0)).rgb(), colours().primary.rgb());
    QVERIFY(pixelAt(image, QPointF(24.0, 5.0)).alpha() > 0);
    QCOMPARE(pixelAt(image, QPointF(24.0, 2.0)).alpha(), 0);
    QCOMPARE(pixelAt(image, QPointF(2.0, 24.0)).alpha(), 0);
    QCOMPARE(pixelAt(image, QPointF(46.0, 24.0)).alpha(), 0);

    // The full shape makes the container circular, so the bounding-box corner
    // stays transparent.
    QCOMPARE(pixelAt(image, QPointF(4.0, 4.0)).alpha(), 0);
}

void TestIconButton::clickEmitsClickedOnce()
{
    md::IconButton button;
    button.resize(button.sizeHint());
    QSignalSpy clicked(&button, &md::IconButton::clicked);
    QSignalSpy toggled(&button, &md::IconButton::toggled);

    clickAt(button, QPointF(24.0, 24.0));
    QCOMPARE(clicked.count(), 1);
    QCOMPARE(toggled.count(), 0);

    // The transparent padding is still part of the touch target.
    clickAt(button, QPointF(2.0, 2.0));
    QCOMPARE(clicked.count(), 2);

    // A press inside followed by a release outside is a drag-off, not a click.
    sendMouse(button, QEvent::MouseButtonPress, QPointF(24.0, 24.0), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(button, QEvent::MouseButtonRelease, QPointF(button.width() + 20.0, 24.0),
              Qt::LeftButton, Qt::NoButton);
    QCOMPARE(clicked.count(), 2);

    // Non-left buttons never activate the button.
    sendMouse(button, QEvent::MouseButtonPress, QPointF(24.0, 24.0), Qt::RightButton,
              Qt::RightButton);
    sendMouse(button, QEvent::MouseButtonRelease, QPointF(24.0, 24.0), Qt::RightButton,
              Qt::NoButton);
    QCOMPARE(clicked.count(), 2);
}

void TestIconButton::checkableClickToggles()
{
    md::IconButton button;
    button.setCheckable(true);
    button.resize(button.sizeHint());
    QSignalSpy clicked(&button, &md::IconButton::clicked);
    QSignalSpy toggled(&button, &md::IconButton::toggled);

    clickAt(button, QPointF(24.0, 24.0));
    QVERIFY(button.isChecked());
    QCOMPARE(clicked.count(), 1);
    QCOMPARE(toggled.count(), 1);
    QCOMPARE(toggled.at(0).at(0).toBool(), true);

    clickAt(button, QPointF(24.0, 24.0));
    QVERIFY(!button.isChecked());
    QCOMPARE(clicked.count(), 2);
    QCOMPARE(toggled.count(), 2);
    QCOMPARE(toggled.at(1).at(0).toBool(), false);

    // A non-checkable button reports the click but never toggles.
    md::IconButton plain;
    plain.resize(plain.sizeHint());
    QSignalSpy plainToggled(&plain, &md::IconButton::toggled);
    clickAt(plain, QPointF(24.0, 24.0));
    QVERIFY(!plain.isChecked());
    QCOMPARE(plainToggled.count(), 0);
}

void TestIconButton::checkedStateIsProgrammatic()
{
    md::IconButton button;
    button.setCheckable(true);
    button.resize(button.sizeHint());
    QSignalSpy toggled(&button, &md::IconButton::toggled);

    button.setChecked(true);
    QVERIFY(button.isChecked());
    QCOMPARE(toggled.count(), 1);
    QCOMPARE(toggled.at(0).at(0).toBool(), true);

    // Setting the current state again is not a change and emits nothing.
    button.setChecked(true);
    QCOMPARE(toggled.count(), 1);

    // Clearing checkable clears the state, because a non-checkable button
    // cannot stay checked.
    button.setCheckable(false);
    QVERIFY(!button.isCheckable());
    QVERIFY(!button.isChecked());
    QCOMPARE(toggled.count(), 2);
    QCOMPARE(toggled.at(1).at(0).toBool(), false);

    // setChecked() is ignored while the button is not checkable.
    button.setChecked(true);
    QVERIFY(!button.isChecked());
    QCOMPARE(toggled.count(), 2);
}

void TestIconButton::keyboardActivates()
{
    md::IconButton button;
    button.resize(button.sizeHint());
    QSignalSpy clicked(&button, &md::IconButton::clicked);

    sendKey(button, Qt::Key_Space);
    QCOMPARE(clicked.count(), 1);

    sendKey(button, Qt::Key_Return);
    QCOMPARE(clicked.count(), 2);

    sendKey(button, Qt::Key_Enter);
    QCOMPARE(clicked.count(), 3);

    // Any other key stays inert.
    sendKey(button, Qt::Key_A);
    QCOMPARE(clicked.count(), 3);

    // A checkable button toggles from the keyboard too.
    md::IconButton toggle;
    toggle.setCheckable(true);
    toggle.resize(toggle.sizeHint());
    QSignalSpy toggled(&toggle, &md::IconButton::toggled);

    sendKey(toggle, Qt::Key_Space);
    QVERIFY(toggle.isChecked());
    QCOMPARE(toggled.count(), 1);

    sendKey(toggle, Qt::Key_Space);
    QVERIFY(!toggle.isChecked());
    QCOMPARE(toggled.count(), 2);
}

void TestIconButton::disabledEmitsNothing()
{
    md::IconButton button;
    button.setCheckable(true);
    button.resize(button.sizeHint());
    QSignalSpy clicked(&button, &md::IconButton::clicked);
    QSignalSpy toggled(&button, &md::IconButton::toggled);

    button.setEnabled(false);
    QVERIFY(!button.isEnabled());

    clickAt(button, QPointF(24.0, 24.0));
    sendKey(button, Qt::Key_Space);
    sendKey(button, Qt::Key_Return);

    QCOMPARE(clicked.count(), 0);
    QCOMPARE(toggled.count(), 0);
    QVERIFY(!button.isChecked());
}

void TestIconButton::variantsPaintRoles()
{
    md::IconButton standard(solidIcon());
    standard.resize(standard.sizeHint());
    const QImage standardImage = renderWidget(standard);
    QCOMPARE(pixelAt(standardImage, QPointF(10.0, 24.0)).alpha(), 0);
    QCOMPARE(pixelAt(standardImage, QPointF(24.0, 24.0)).rgb(),
             colours().onSurfaceVariant.rgb());

    md::IconButton filled(solidIcon());
    filled.setVariant(md::IconButton::Variant::Filled);
    filled.resize(filled.sizeHint());
    const QImage filledImage = renderWidget(filled);
    QCOMPARE(pixelAt(filledImage, QPointF(10.0, 24.0)).rgb(), colours().primary.rgb());
    QCOMPARE(pixelAt(filledImage, QPointF(24.0, 24.0)).rgb(), colours().onPrimary.rgb());

    md::IconButton tonal(solidIcon());
    tonal.setVariant(md::IconButton::Variant::Tonal);
    tonal.resize(tonal.sizeHint());
    const QImage tonalImage = renderWidget(tonal);
    QCOMPARE(pixelAt(tonalImage, QPointF(10.0, 24.0)).rgb(),
             colours().secondaryContainer.rgb());
    QCOMPARE(pixelAt(tonalImage, QPointF(24.0, 24.0)).rgb(),
             colours().onSecondaryContainer.rgb());

    md::IconButton outlined(solidIcon());
    outlined.setVariant(md::IconButton::Variant::Outlined);
    outlined.resize(outlined.sizeHint());
    const QImage outlinedImage = renderWidget(outlined);
    QCOMPARE(pixelAt(outlinedImage, QPointF(10.0, 24.0)).alpha(), 0);
    QVERIFY(pixelAt(outlinedImage, QPointF(5.0, 24.0)).alpha() > 0);
    QCOMPARE(pixelAt(outlinedImage, QPointF(24.0, 24.0)).rgb(),
             colours().onSurfaceVariant.rgb());
}

void TestIconButton::checkedUsesSelectedColours()
{
    md::IconButton standard(solidIcon());
    standard.setCheckable(true);
    standard.setChecked(true);
    standard.resize(standard.sizeHint());
    const QImage standardImage = renderWidget(standard);
    QCOMPARE(pixelAt(standardImage, QPointF(10.0, 24.0)).rgb(),
             colours().primaryContainer.rgb());
    QCOMPARE(pixelAt(standardImage, QPointF(24.0, 24.0)).rgb(),
             colours().onPrimaryContainer.rgb());

    md::IconButton filled(solidIcon());
    filled.setVariant(md::IconButton::Variant::Filled);
    filled.setCheckable(true);
    filled.setChecked(true);
    filled.resize(filled.sizeHint());
    const QImage filledImage = renderWidget(filled);
    QCOMPARE(pixelAt(filledImage, QPointF(10.0, 24.0)).rgb(), colours().primary.rgb());
    QCOMPARE(pixelAt(filledImage, QPointF(24.0, 24.0)).rgb(), colours().onPrimary.rgb());

    md::IconButton tonal(solidIcon());
    tonal.setVariant(md::IconButton::Variant::Tonal);
    tonal.setCheckable(true);
    tonal.setChecked(true);
    tonal.resize(tonal.sizeHint());
    const QImage tonalImage = renderWidget(tonal);
    QCOMPARE(pixelAt(tonalImage, QPointF(10.0, 24.0)).rgb(), colours().primary.rgb());
    QCOMPARE(pixelAt(tonalImage, QPointF(24.0, 24.0)).rgb(), colours().onPrimary.rgb());

    md::IconButton outlined(solidIcon());
    outlined.setVariant(md::IconButton::Variant::Outlined);
    outlined.setCheckable(true);
    outlined.setChecked(true);
    outlined.resize(outlined.sizeHint());
    const QImage outlinedImage = renderWidget(outlined);
    QCOMPARE(pixelAt(outlinedImage, QPointF(10.0, 24.0)).rgb(), colours().primary.rgb());
    QCOMPARE(pixelAt(outlinedImage, QPointF(24.0, 24.0)).rgb(), colours().onPrimary.rgb());
}

void TestIconButton::disabledDiffersFromEnabled()
{
    md::IconButton enabled;
    enabled.setVariant(md::IconButton::Variant::Filled);
    enabled.resize(enabled.sizeHint());
    const QColor enabledPixel = pixelAt(renderWidget(enabled), QPointF(24.0, 24.0));

    md::IconButton disabled;
    disabled.setVariant(md::IconButton::Variant::Filled);
    disabled.setEnabled(false);
    disabled.resize(disabled.sizeHint());
    const QColor disabledPixel = pixelAt(renderWidget(disabled), QPointF(24.0, 24.0));

    QVERIFY(enabledPixel != disabledPixel);
    QVERIFY(disabledPixel.alpha() > 0);
    QVERIFY(disabledPixel.alpha() < enabledPixel.alpha());
}

QTEST_MAIN(TestIconButton)
#include "tst_icon_button.moc"
