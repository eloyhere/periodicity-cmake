// Unit tests for the first batch of Material 3 components (framework.md §14).
//
// Every widget is exercised offscreen: the application theme is pinned to the
// baseline light scheme, the widgets are measured from sizeHint(), rendered
// into a transparent QImage (so unpainted pixels are deterministic rather than
// uninitialised) and sampled in logical coordinates. Pointer and key input is
// delivered straight to the widget with QApplication::sendEvent, exactly as
// tests/view does, so no window activation or virtual cursor is involved.

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QFont>
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

#include <md/components/button.h>
#include <md/components/checkbox.h>
#include <md/components/icon.h>
#include <md/components/progress.h>
#include <md/components/radio.h>
#include <md/components/rate.h>
#include <md/components/ring.h>
#include <md/components/seekbar.h>
#include <md/components/text.h>
#include <md/theme/theme.h>
#include <md/tokens/color_scheme.h>
#include <md/tokens/elevation.h>
#include <md/tokens/type_scale.h>

#include <algorithm>

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

int paintedPixels(const QImage& image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) != 0)
                ++count;
        }
    }
    return count;
}

int darkPixelsIn(const QImage& image, const QRectF& area)
{
    int count = 0;
    for (int y = qRound(area.top()); y < qRound(area.bottom()); ++y) {
        for (int x = qRound(area.left()); x < qRound(area.right()); ++x) {
            const QColor colour = pixelAt(image, QPointF(x, y));
            if (colour.alpha() > 0 && colour.red() + colour.green() + colour.blue() < 384)
                ++count;
        }
    }
    return count;
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

class TestBasicWidgets : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void iconPaintsTintedSource();
    void textPaintsAndWraps();

    void buttonVariantsPaint();
    void buttonDisabledDiffers();
    void buttonClicksOnlyInside();
    void buttonKeyboardActivates();

    void checkBoxClickAndKeyboard();
    void checkBoxIndeterminate();

    void radioGroupIsExclusive();

    void rateClickSetsValue();
    void rateKeyboardAdjusts();

    void progressValueChangesFraction();
    void progressIndeterminateTracksVisibility();

    void ringIndeterminateAnimates();

    void seekBarClickAndDrag();
    void seekBarKeyboard();
};

void TestBasicWidgets::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestBasicWidgets::iconPaintsTintedSource()
{
    QPixmap source(24, 24);
    source.fill(Qt::transparent);
    {
        QPainter painter(&source);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(Qt::white));
        painter.drawRect(QRectF(2.0, 2.0, 20.0, 20.0));
    }

    md::Icon icon;
    icon.setPixmap(source);
    icon.setSize(24.0);
    icon.setColour(lightColours().primary);
    icon.resize(icon.sizeHint());

    QCOMPARE(icon.sizeHint(), QSize(24, 24));

    const QImage image = renderWidget(icon);
    QVERIFY(paintedPixels(image) > 0);
    QCOMPARE(pixelAt(image, QPointF(12.0, 12.0)).rgb(), lightColours().primary.rgb());
}

void TestBasicWidgets::textPaintsAndWraps()
{
    md::Text text(QStringLiteral("Material"));
    text.setRole(md::Text::TextRole::Title);
    text.setAlignment(Qt::AlignCenter);
    text.resize(240, 48);

    const QImage image = renderWidget(text);
    QVERIFY(paintedPixels(image) > 0);
    QVERIFY(darkPixelsIn(image, QRectF(40.0, 8.0, 160.0, 32.0)) > 20);

    md::Text wrapped(QStringLiteral("The quick brown fox jumps over the lazy dog"));
    wrapped.setRole(md::Text::TextRole::Body);
    wrapped.setWordWrap(true);
    wrapped.resize(300, 100);

    const int narrow = wrapped.heightForWidth(80);
    const int wide = wrapped.heightForWidth(320);
    QVERIFY(narrow > wide);
    QVERIFY(wide > 0);
}

void TestBasicWidgets::buttonVariantsPaint()
{
    const md::ColorScheme& colours = lightColours();

    md::Button filled(QStringLiteral("OK"));
    filled.resize(filled.sizeHint());
    QCOMPARE(filled.sizeHint().height(), 48);
    QCOMPARE(pixelAt(renderWidget(filled), QPointF(10.0, 24.0)).rgb(), colours.primary.rgb());

    md::Button tonal(QStringLiteral("OK"));
    tonal.setVariant(md::Button::ButtonVariant::FilledTonal);
    tonal.resize(tonal.sizeHint());
    QCOMPARE(pixelAt(renderWidget(tonal), QPointF(10.0, 24.0)).rgb(),
             colours.secondaryContainer.rgb());

    md::Button elevated(QStringLiteral("OK"));
    elevated.setVariant(md::Button::ButtonVariant::Elevated);
    elevated.resize(elevated.sizeHint());
    QCOMPARE(pixelAt(renderWidget(elevated), QPointF(10.0, 24.0)).rgb(),
             colours.surfaceAtElevation(md::Elevation::Level1).rgb());

    md::Button outlined(QStringLiteral("OK"));
    outlined.setVariant(md::Button::ButtonVariant::Outlined);
    outlined.resize(outlined.sizeHint());
    QCOMPARE(pixelAt(renderWidget(outlined), QPointF(10.0, 24.0)).alpha(), 0);

    md::Button plain(QStringLiteral("OK"));
    plain.setVariant(md::Button::ButtonVariant::Text);
    plain.resize(plain.sizeHint());
    QCOMPARE(pixelAt(renderWidget(plain), QPointF(10.0, 24.0)).alpha(), 0);
}

void TestBasicWidgets::buttonDisabledDiffers()
{
    md::Button enabled(QStringLiteral("OK"));
    enabled.resize(enabled.sizeHint());
    const QColor enabledPixel = pixelAt(renderWidget(enabled), QPointF(10.0, 24.0));

    md::Button disabled(QStringLiteral("OK"));
    disabled.setEnabled(false);
    disabled.resize(disabled.sizeHint());
    const QColor disabledPixel = pixelAt(renderWidget(disabled), QPointF(10.0, 24.0));

    QVERIFY(enabledPixel != disabledPixel);
    QVERIFY(disabledPixel.alpha() > 0);
    QVERIFY(disabledPixel.alpha() < enabledPixel.alpha());
}

void TestBasicWidgets::buttonClicksOnlyInside()
{
    md::Button button(QStringLiteral("OK"));
    button.resize(button.sizeHint());
    QSignalSpy spy(&button, &md::Button::clicked);

    const QPointF inside(10.0, 24.0);
    clickAt(button, inside);
    QCOMPARE(spy.count(), 1);

    // A click that starts outside never reaches the widget as a press.
    clickAt(button, QPointF(button.width() + 20.0, 24.0));
    QCOMPARE(spy.count(), 1);

    // A press inside followed by a release outside is a drag-off, not a click.
    sendMouse(button, QEvent::MouseButtonPress, inside, Qt::LeftButton, Qt::LeftButton);
    sendMouse(button, QEvent::MouseButtonRelease, QPointF(button.width() + 20.0, 24.0),
              Qt::LeftButton, Qt::NoButton);
    QCOMPARE(spy.count(), 1);
}

void TestBasicWidgets::buttonKeyboardActivates()
{
    md::Button button(QStringLiteral("OK"));
    button.resize(button.sizeHint());
    QSignalSpy spy(&button, &md::Button::clicked);

    sendKey(button, Qt::Key_Space);
    QCOMPARE(spy.count(), 1);

    sendKey(button, Qt::Key_Return);
    QCOMPARE(spy.count(), 2);
}

void TestBasicWidgets::checkBoxClickAndKeyboard()
{
    md::CheckBox box;
    QCOMPARE(box.sizeHint(), QSize(48, 48));
    box.resize(box.sizeHint());
    QSignalSpy spy(&box, &md::CheckBox::toggled);

    clickAt(box, QPointF(24.0, 24.0));
    QVERIFY(box.checkState() == Qt::Checked);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    const QImage checked = renderWidget(box);
    QCOMPARE(pixelAt(checked, QPointF(20.0, 19.0)).rgb(), lightColours().primary.rgb());

    clickAt(box, QPointF(24.0, 24.0));
    QVERIFY(box.checkState() == Qt::Unchecked);
    QCOMPARE(spy.count(), 2);

    sendKey(box, Qt::Key_Space);
    QVERIFY(box.checkState() == Qt::Checked);
    QCOMPARE(spy.count(), 3);
}

void TestBasicWidgets::checkBoxIndeterminate()
{
    md::CheckBox box;
    box.setTristate(true);
    box.resize(box.sizeHint());
    box.setCheckState(Qt::PartiallyChecked);
    QVERIFY(box.checkState() == Qt::PartiallyChecked);

    const QImage partial = renderWidget(box);
    QCOMPARE(pixelAt(partial, QPointF(20.0, 19.0)).rgb(), lightColours().primary.rgb());

    QSignalSpy spy(&box, &md::CheckBox::toggled);
    clickAt(box, QPointF(24.0, 24.0));
    QVERIFY(box.checkState() == Qt::Checked);
    QCOMPARE(spy.count(), 1);
}

void TestBasicWidgets::radioGroupIsExclusive()
{
    QWidget parent;
    md::Radio first(&parent);
    md::Radio second(&parent);
    first.setGroupId(4);
    second.setGroupId(4);
    first.resize(first.sizeHint());
    second.resize(second.sizeHint());
    first.move(0, 0);
    second.move(60, 0);

    QCOMPARE(first.sizeHint(), QSize(48, 48));

    clickAt(first, QPointF(24.0, 24.0));
    QVERIFY(first.isChecked());
    QVERIFY(!second.isChecked());

    clickAt(second, QPointF(24.0, 24.0));
    QVERIFY(second.isChecked());
    QVERIFY(!first.isChecked());

    const QImage checked = renderWidget(second);
    QCOMPARE(pixelAt(checked, QPointF(24.0, 24.0)).rgb(), lightColours().primary.rgb());

    // Clicking an already checked radio keeps it checked.
    clickAt(second, QPointF(24.0, 24.0));
    QVERIFY(second.isChecked());

    // A radio in another group is independent.
    md::Radio third(&parent);
    third.setGroupId(9);
    third.resize(third.sizeHint());
    clickAt(third, QPointF(24.0, 24.0));
    QVERIFY(third.isChecked());
    QVERIFY(second.isChecked());
}

void TestBasicWidgets::rateClickSetsValue()
{
    md::Rate rate;
    rate.setMax(5);
    rate.setStep(1.0);
    rate.resize(rate.sizeHint());

    QCOMPARE(rate.sizeHint().height(), 48);
    QSignalSpy spy(&rate, &md::Rate::valueChanged);

    // Star 3 is centred at 2 * 28 + 12 = 68 logical pixels.
    clickAt(rate, QPointF(68.0, 24.0));
    QCOMPARE(rate.value(), 3.0);
    QCOMPARE(spy.count(), 1);

    const QImage image = renderWidget(rate);
    QVERIFY(paintedPixels(image) > 0);
    QCOMPARE(pixelAt(image, QPointF(12.0, 24.0)).rgb(), lightColours().primary.rgb());
}

void TestBasicWidgets::rateKeyboardAdjusts()
{
    md::Rate rate;
    rate.setMax(5);
    rate.setStep(0.5);
    rate.setValue(2.0);
    rate.resize(rate.sizeHint());

    sendKey(rate, Qt::Key_Right);
    QCOMPARE(rate.value(), 2.5);

    sendKey(rate, Qt::Key_Left);
    QCOMPARE(rate.value(), 2.0);

    sendKey(rate, Qt::Key_End);
    QCOMPARE(rate.value(), 5.0);

    sendKey(rate, Qt::Key_Home);
    QCOMPARE(rate.value(), 0.0);
}

void TestBasicWidgets::progressValueChangesFraction()
{
    md::Progress progress;
    progress.resize(progress.sizeHint());
    QCOMPARE(progress.sizeHint(), QSize(200, 4));

    progress.setValue(0.25);
    const QImage quarter = renderWidget(progress);
    QCOMPARE(pixelAt(quarter, QPointF(20.0, 2.0)).rgb(), lightColours().primary.rgb());
    QCOMPARE(pixelAt(quarter, QPointF(100.0, 2.0)).rgb(),
             lightColours().surfaceContainerHighest.rgb());

    progress.setValue(0.75);
    const QImage threeQuarters = renderWidget(progress);
    QCOMPARE(pixelAt(threeQuarters, QPointF(100.0, 2.0)).rgb(),
             lightColours().primary.rgb());
    QCOMPARE(pixelAt(threeQuarters, QPointF(180.0, 2.0)).rgb(),
             lightColours().surfaceContainerHighest.rgb());
}

void TestBasicWidgets::progressIndeterminateTracksVisibility()
{
    md::Progress progress;
    progress.resize(progress.sizeHint());
    progress.setIndeterminate(true);

    // A hidden indicator must not burn a timer.
    QVERIFY(!progress.isAnimating());

    progress.show();
    QTest::qWait(20);
    QVERIFY(progress.isAnimating());

    progress.hide();
    QVERIFY(!progress.isAnimating());
}

void TestBasicWidgets::ringIndeterminateAnimates()
{
    md::Ring ring;
    ring.resize(ring.sizeHint());
    QCOMPARE(ring.sizeHint(), QSize(48, 48));

    ring.setIndeterminate(true);
    ring.show();
    QTest::qWait(30);
    QVERIFY(ring.isAnimating());

    const QImage first = renderWidget(ring);
    QVERIFY(paintedPixels(first) > 0);

    // The rotating, trimmed arc must have moved by the second grab.
    QTest::qWait(150);
    const QImage second = renderWidget(ring);
    QVERIFY(first != second);

    ring.hide();
    QVERIFY(!ring.isAnimating());
}

void TestBasicWidgets::seekBarClickAndDrag()
{
    md::SeekBar seek;
    seek.setRange(0.0, 100.0);
    seek.setStep(1.0);
    seek.resize(seek.sizeHint());
    QCOMPARE(seek.sizeHint(), QSize(200, 48));

    QSignalSpy spy(&seek, &md::SeekBar::valueChanged);

    // The usable track is [10, 190]; x = 145 is 75 % of it.
    clickAt(seek, QPointF(145.0, 32.0));
    QCOMPARE(seek.value(), 75.0);
    QCOMPARE(spy.count(), 1);

    sendMouse(seek, QEvent::MouseButtonPress, QPointF(145.0, 32.0), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(seek, QEvent::MouseMove, QPointF(55.0, 32.0), Qt::NoButton, Qt::LeftButton);
    sendMouse(seek, QEvent::MouseButtonRelease, QPointF(55.0, 32.0), Qt::LeftButton,
              Qt::NoButton);
    QCOMPARE(seek.value(), 25.0);
}

void TestBasicWidgets::seekBarKeyboard()
{
    md::SeekBar seek;
    seek.setRange(0.0, 100.0);
    seek.setStep(5.0);
    seek.setValue(50.0);
    seek.resize(seek.sizeHint());

    sendKey(seek, Qt::Key_Right);
    QCOMPARE(seek.value(), 55.0);

    sendKey(seek, Qt::Key_Left);
    QCOMPARE(seek.value(), 50.0);

    sendKey(seek, Qt::Key_Home);
    QCOMPARE(seek.value(), 0.0);

    sendKey(seek, Qt::Key_End);
    QCOMPARE(seek.value(), 100.0);

}

QTEST_MAIN(TestBasicWidgets)
#include "tst_basic_widgets.moc"
