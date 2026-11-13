// Unit tests for md::ToggleButton.
//
// The suite runs offscreen like the other component tests: the application
// theme is pinned to the baseline light scheme, widgets are measured from
// sizeHint(), rendered into a transparent QImage and sampled in logical
// coordinates, and pointer and key input is delivered straight to the widget
// with QApplication::sendEvent. The checked transition is driven through the
// component's manual Animator clock, never by sleeping.

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QRectF>
#include <QRegion>
#include <QSignalSpy>
#include <QSize>
#include <QStringList>
#include <QTest>
#include <QVariant>
#include <QWidget>

#include <md/components/toggle_button.h>
#include <md/core/view_state.h>
#include <md/theme/theme.h>
#include <md/tokens/color_scheme.h>
#include <md/tokens/motion.h>

#include <chrono>

namespace {

const md::ColorScheme& lightColours()
{
    return md::Theme::applicationTheme().colours();
}

// A fully filled 24 x 24 icon. IconDrawable tints every pixel, so the painted
// centre is the content role and not the source colour.
QIcon solidIcon()
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);

    {
        QPainter painter(&pixmap);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(Qt::white));
        painter.drawRect(QRectF(0.0, 0.0, 24.0, 24.0));
    }

    return QIcon(pixmap);
}

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

bool nearColour(const QColor& lhs, const QColor& rhs, int tolerance = 70)
{
    return qAbs(lhs.red() - rhs.red()) <= tolerance
        && qAbs(lhs.green() - rhs.green()) <= tolerance
        && qAbs(lhs.blue() - rhs.blue()) <= tolerance;
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

void sendLeave(QWidget& widget)
{
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&widget, &leave);
}

void sendEnter(QWidget& widget)
{
    const QPointF at(8.0, 24.0);
    QEnterEvent enter(at, at, at);
    QApplication::sendEvent(&widget, &enter);
}

} // namespace

class TestToggleButton : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void defaultsUncheckedAndCheckable();
    void clickTogglesAndEmitsInOrder();
    void setCheckedWithoutAChangeEmitsNothing();
    void checkableFlagGatesTheState();
    void releaseOutsideKeepsState();
    void spaceAndEnterToggle();
    void disabledEmitsNothing();

    void checkedVariantsPaintTokenRoles();
    void iconOnlyIsSquareAndSized();

    void transitionReachesCheckedColour();
    void deletingToggledHandlerDoesNotCrash();
    void hoverLayerClearsOnPressLeaveAndDisable();
    void focusRingAppearsOnSetFocus();
};

void TestToggleButton::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestToggleButton::defaultsUncheckedAndCheckable()
{
    md::ToggleButton button;

    QVERIFY(!button.isChecked());
    QVERIFY(button.isCheckable());
    QVERIFY(button.isEnabled());
    QVERIFY(button.variant() == md::ToggleButton::ToggleButtonVariant::Filled);
    QVERIFY(button.buttonSize() == md::ToggleButton::ButtonSize::Regular);
    QVERIFY(button.text().isEmpty());
    QVERIFY(button.icon().isNull());
    QVERIFY(button.focusPolicy() == Qt::StrongFocus);

    // Regular is Button's scale: the 48 dp touch target, not the 40 dp visual.
    QCOMPARE(button.sizeHint(), QSize(48, 48));
    QVERIFY(button.property("md-checked").isValid());
    QCOMPARE(button.property("md-checked").toBool(), false);
}

void TestToggleButton::clickTogglesAndEmitsInOrder()
{
    md::ToggleButton button(QStringLiteral("On"));
    button.resize(button.sizeHint());

    QStringList order;
    QObject::connect(&button, &md::ToggleButton::toggled, [&](bool checked) {
        order.append(checked ? QStringLiteral("toggled:true")
                             : QStringLiteral("toggled:false"));
    });
    QObject::connect(&button, &md::ToggleButton::clicked,
                     [&] { order.append(QStringLiteral("clicked")); });

    QSignalSpy toggled(&button, &md::ToggleButton::toggled);
    QSignalSpy clicked(&button, &md::ToggleButton::clicked);

    const QPointF centre(button.width() / 2.0, button.height() / 2.0);

    clickAt(button, centre);
    QVERIFY(button.isChecked());
    QCOMPARE(toggled.count(), 1);
    QCOMPARE(clicked.count(), 1);
    QCOMPARE(order, QStringList({QStringLiteral("toggled:true"),
                                 QStringLiteral("clicked")}));

    order.clear();
    clickAt(button, centre);
    QVERIFY(!button.isChecked());
    QCOMPARE(toggled.count(), 2);
    QCOMPARE(clicked.count(), 2);
    QCOMPARE(toggled.at(1).at(0).toBool(), false);
    QCOMPARE(order, QStringList({QStringLiteral("toggled:false"),
                                 QStringLiteral("clicked")}));
}

void TestToggleButton::setCheckedWithoutAChangeEmitsNothing()
{
    md::ToggleButton button;
    QSignalSpy toggled(&button, &md::ToggleButton::toggled);

    button.setChecked(true);
    QVERIFY(button.isChecked());
    QCOMPARE(toggled.count(), 1);

    // Pushing the same value again is a no-op, not a second signal.
    button.setChecked(true);
    QCOMPARE(toggled.count(), 1);

    button.setChecked(false);
    QCOMPARE(toggled.count(), 2);

    button.setChecked(false);
    QCOMPARE(toggled.count(), 2);
}

void TestToggleButton::checkableFlagGatesTheState()
{
    md::ToggleButton button;
    button.resize(button.sizeHint());
    QSignalSpy toggled(&button, &md::ToggleButton::toggled);
    QSignalSpy clicked(&button, &md::ToggleButton::clicked);

    // A non-checkable button reports the click but never changes state.
    button.setCheckable(false);
    clickAt(button, QRectF(button.rect()).center());
    QVERIFY(!button.isChecked());
    QCOMPARE(clicked.count(), 1);
    QCOMPARE(toggled.count(), 0);

    // setChecked() is ignored while the button is not checkable.
    button.setChecked(true);
    QVERIFY(!button.isChecked());
    QCOMPARE(toggled.count(), 0);

    // Turning checkable back on allows the state again.
    button.setCheckable(true);
    button.setChecked(true);
    QVERIFY(button.isChecked());
    QCOMPARE(toggled.count(), 1);

    // Clearing checkable clears a checked state and reports the change.
    button.setCheckable(false);
    QVERIFY(!button.isChecked());
    QCOMPARE(toggled.count(), 2);
    QCOMPARE(toggled.at(1).at(0).toBool(), false);
}

void TestToggleButton::releaseOutsideKeepsState()
{
    md::ToggleButton button(QStringLiteral("On"));
    button.resize(button.sizeHint());
    QSignalSpy toggled(&button, &md::ToggleButton::toggled);
    QSignalSpy clicked(&button, &md::ToggleButton::clicked);

    const QPointF inside(button.width() / 2.0, button.height() / 2.0);
    sendMouse(button, QEvent::MouseButtonPress, inside, Qt::LeftButton, Qt::LeftButton);
    sendMouse(button, QEvent::MouseButtonRelease,
              QPointF(button.width() + 20.0, inside.y()), Qt::LeftButton, Qt::NoButton);

    QVERIFY(!button.isChecked());
    QCOMPARE(toggled.count(), 0);
    QCOMPARE(clicked.count(), 0);

    // The reverse case: a press that starts outside and is released inside is
    // not an activation either.
    sendMouse(button, QEvent::MouseButtonPress,
              QPointF(button.width() + 20.0, inside.y()), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(button, QEvent::MouseButtonRelease, inside, Qt::LeftButton, Qt::NoButton);

    QVERIFY(!button.isChecked());
    QCOMPARE(toggled.count(), 0);
    QCOMPARE(clicked.count(), 0);
}

void TestToggleButton::spaceAndEnterToggle()
{
    md::ToggleButton button(QStringLiteral("On"));
    button.resize(button.sizeHint());

    QStringList order;
    QObject::connect(&button, &md::ToggleButton::toggled, [&](bool checked) {
        order.append(checked ? QStringLiteral("toggled:true")
                             : QStringLiteral("toggled:false"));
    });
    QObject::connect(&button, &md::ToggleButton::clicked,
                     [&] { order.append(QStringLiteral("clicked")); });

    sendKey(button, Qt::Key_Space);
    QVERIFY(button.isChecked());
    QCOMPARE(order, QStringList({QStringLiteral("toggled:true"),
                                 QStringLiteral("clicked")}));

    order.clear();
    sendKey(button, Qt::Key_Enter);
    QVERIFY(!button.isChecked());
    QCOMPARE(order, QStringList({QStringLiteral("toggled:false"),
                                 QStringLiteral("clicked")}));
}

void TestToggleButton::disabledEmitsNothing()
{
    md::ToggleButton button;
    button.resize(button.sizeHint());
    QSignalSpy toggled(&button, &md::ToggleButton::toggled);
    QSignalSpy clicked(&button, &md::ToggleButton::clicked);

    button.setEnabled(false);
    QVERIFY(!button.isEnabled());

    button.setFocus(Qt::TabFocusReason);
    QVERIFY(!button.hasFocus());

    clickAt(button, QRectF(button.rect()).center());
    sendKey(button, Qt::Key_Space);
    sendKey(button, Qt::Key_Return);
    sendKey(button, Qt::Key_Enter);

    QCOMPARE(clicked.count(), 0);
    QCOMPARE(toggled.count(), 0);
    QVERIFY(!button.isChecked());
}

void TestToggleButton::checkedVariantsPaintTokenRoles()
{
    const QIcon icon = solidIcon();

    struct Case
    {
        md::ToggleButton::ToggleButtonVariant variant;
        QColor container;
        QColor content;
        bool hasContainer;
    };

    const Case cases[] = {
        {md::ToggleButton::ToggleButtonVariant::Filled, lightColours().primary,
         lightColours().onPrimary, true},
        {md::ToggleButton::ToggleButtonVariant::Tonal,
         lightColours().secondaryContainer, lightColours().onSecondaryContainer, true},
        {md::ToggleButton::ToggleButtonVariant::Outlined,
         lightColours().inverseSurface, lightColours().inverseOnSurface, true},
        {md::ToggleButton::ToggleButtonVariant::Text, QColor(Qt::transparent),
         lightColours().primary, false},
    };

    for (const Case& testCase : cases) {
        md::ToggleButton button;
        button.setIcon(icon);
        button.setVariant(testCase.variant);
        button.setChecked(true);
        button.resize(button.sizeHint());

        const QImage image = renderWidget(button);

        // (6, 24) is inside the 40 dp visual and clear of the centred 20 dp
        // icon box, so it samples the raw container role.
        const QColor container = pixelAt(image, QPointF(6.0, 24.0));
        if (testCase.hasContainer)
            QCOMPARE(container.rgb(), testCase.container.rgb());
        else
            QCOMPARE(container.alpha(), 0);

        QCOMPARE(pixelAt(image, QPointF(24.0, 24.0)).rgb(), testCase.content.rgb());
    }
}

void TestToggleButton::iconOnlyIsSquareAndSized()
{
    md::ToggleButton button;

    // No text: a square target, like IconButton.
    QCOMPARE(button.sizeHint(), QSize(48, 48));
    button.setIcon(solidIcon());
    QCOMPARE(button.sizeHint(), QSize(48, 48));

    QCOMPARE(button.iconSize(), 20.0);
    button.setButtonSize(md::ToggleButton::ButtonSize::Small);
    QCOMPARE(button.sizeHint(), QSize(40, 40));
    QCOMPARE(button.iconSize(), 18.0);

    button.setButtonSize(md::ToggleButton::ButtonSize::Large);
    QCOMPARE(button.sizeHint(), QSize(56, 56));
    QCOMPARE(button.iconSize(), 24.0);

    // An explicit icon size survives a button-size change.
    button.setIconSize(12.0);
    button.setButtonSize(md::ToggleButton::ButtonSize::Regular);
    QCOMPARE(button.iconSize(), 12.0);

    // Text mode becomes a wide button again.
    button.setText(QStringLiteral("Save"));
    QVERIFY(button.sizeHint().width() > button.sizeHint().height());
    QCOMPARE(button.sizeHint().height(), 48);
}

void TestToggleButton::transitionReachesCheckedColour()
{
    QWidget host;
    host.resize(200, 100);

    auto* button = new md::ToggleButton(QStringLiteral("On"), &host);
    button->resize(button->sizeHint());
    button->move(10, 10);

    // The pixel assertions are about the checked transition alone. Turning the
    // shared interaction layers off keeps a hover ripple scheduled by the show
    // from tinting the sampled container pixel; hover feedback has its own test.
    button->setInteractionFeedbackEnabled(false);

    host.show();
    host.activateWindow();
    (void)QTest::qWaitForWindowExposed(&host);
    QCoreApplication::processEvents();
    button->clearFocus();
    sendLeave(*button);

    button->setManualAnimationClock(true);
    QVERIFY(!button->isAnimating());

    const QImage unchecked = renderWidget(*button);
    QCOMPARE(pixelAt(unchecked, QPointF(8.0, 24.0)).rgb(),
             lightColours().surfaceContainer.rgb());

    button->setChecked(true);
    QVERIFY(button->isChecked());
    QVERIFY(button->isAnimating());

    const auto duration = md::Theme::forWidget(button)
                              .motion()
                              .level(md::ToggleButton::transitionMotionLevel())
                              .duration;
    QVERIFY(duration.count() > 0);

    // One millisecond before the end the run is still in flight; the final
    // millisecond delivers the end state exactly.
    button->advanceAnimation(duration - std::chrono::milliseconds(1));
    QVERIFY(button->isAnimating());
    QVERIFY(button->animationProgress() < 1.0);

    button->advanceAnimation(std::chrono::milliseconds(1));
    QVERIFY(!button->isAnimating());
    QCOMPARE(button->animationProgress(), 1.0);

    const QImage checked = renderWidget(*button);
    QCOMPARE(pixelAt(checked, QPointF(8.0, 24.0)).rgb(), lightColours().primary.rgb());
    QVERIFY(pixelAt(checked, QPointF(8.0, 24.0)) != pixelAt(unchecked, QPointF(8.0, 24.0)));

    // The centre differs too: the label colour moves with the container.
    const QPointF centre(button->width() / 2.0, button->height() / 2.0);
    QVERIFY(pixelAt(checked, centre) != pixelAt(unchecked, centre));
}

void TestToggleButton::deletingToggledHandlerDoesNotCrash()
{
    auto* button = new md::ToggleButton(QStringLiteral("Delete me"));
    button->resize(button->sizeHint());

    QPointer<md::ToggleButton> guard(button);
    QSignalSpy clicked(button, &md::ToggleButton::clicked);

    // The handler deletes the button from inside toggled(). activate() must
    // not emit clicked() (or touch any member) after that.
    QObject::connect(button, &md::ToggleButton::toggled, button,
                     [button](bool) { delete button; });

    clickAt(*button, QRectF(button->rect()).center());

    QVERIFY(guard.isNull());
    QCOMPARE(clicked.count(), 0);
}

void TestToggleButton::hoverLayerClearsOnPressLeaveAndDisable()
{
    md::ToggleButton button(QStringLiteral("Hover"));
    button.resize(button.sizeHint());

    sendEnter(button);
    QVERIFY(button.state().testFlag(md::ViewState::Hovered));

    const QImage hovered = renderWidget(button);
    QVERIFY(pixelAt(hovered, QPointF(8.0, 24.0)).alpha() > 0);

    // A press clears the hover bit; the stronger pressed layer takes over.
    sendMouse(button, QEvent::MouseButtonPress, QPointF(8.0, 24.0), Qt::LeftButton,
              Qt::LeftButton);
    QVERIFY(!button.state().testFlag(md::ViewState::Hovered));
    QVERIFY(button.state().testFlag(md::ViewState::Pressed));

    sendMouse(button, QEvent::MouseButtonRelease, QPointF(8.0, 24.0), Qt::LeftButton,
              Qt::NoButton);
    QVERIFY(button.state().testFlag(md::ViewState::Hovered));
    QVERIFY(!button.state().testFlag(md::ViewState::Pressed));

    // Leaving clears it; so does disabling while hovered.
    sendLeave(button);
    QVERIFY(!button.state().testFlag(md::ViewState::Hovered));

    sendEnter(button);
    QVERIFY(button.state().testFlag(md::ViewState::Hovered));
    button.setEnabled(false);
    QVERIFY(!button.state().testFlag(md::ViewState::Hovered));
    QVERIFY(!button.state().testFlag(md::ViewState::Pressed));
}

void TestToggleButton::focusRingAppearsOnSetFocus()
{
    QWidget host;
    host.resize(200, 120);

    auto* button = new md::ToggleButton(QStringLiteral("Focus"), &host);
    button->resize(button->sizeHint());
    button->move(20, 20);

    host.show();
    host.activateWindow();
    (void)QTest::qWaitForWindowExposed(&host);
    QCoreApplication::processEvents();

    // Window activation may hand the first focusable child the focus; the
    // unfocused frame must really have no focus ring.
    button->clearFocus();
    QCoreApplication::processEvents();

    const QImage unfocused = renderWidget(*button);

    button->setFocus(Qt::TabFocusReason);
    QVERIFY(button->hasFocus());
    QCoreApplication::processEvents();

    const QImage focused = renderWidget(*button);
    QVERIFY(focused != unfocused);

    // The 2 dp inside ring is the theme's secondary role. The pill's left edge
    // at mid-height is the clearest sample: without focus it is the container.
    const QPointF edge(1.0, 24.0);
    QVERIFY(nearColour(pixelAt(focused, edge), lightColours().secondary, 45));
    QVERIFY(!nearColour(pixelAt(unfocused, edge), lightColours().secondary, 45));
}

QTEST_MAIN(TestToggleButton)
#include "tst_toggle_button.moc"
