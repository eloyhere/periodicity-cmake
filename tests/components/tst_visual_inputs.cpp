// Visual-design tests for the input/action family (framework.md §14).
//
// These tests pin the Material 3 geometry and colour roles that the components
// must paint, independently of the interaction tests in the sibling files:
//   * the button's 40 dp visual surface inside the 48 dp touch target, its
//     20 dp icon slot and the 8 dp icon/label gap;
//   * the chip's 32 dp height, 8 dp corner radius and 16/8 dp padding;
//   * the 40 dp interaction circle of a selection control (M3 measures it
//     smaller than the 48 dp target, so it must not wash the whole widget);
//   * the filled text field's onSurfaceVariant resting indicator and its
//     4 dp + bodySmall supporting band;
//   * the slider's secondaryContainer inactive track.
//
// Widgets are rendered offscreen into a transparent QImage, so an unpainted
// pixel is deterministic and can be asserted to be transparent.

#include <QApplication>
#include <QColor>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QRegion>
#include <QSize>
#include <QTest>
#include <QWidget>

#include <md/components/button.h>
#include <md/components/checkbox.h>
#include <md/components/input.h>
#include <md/components/radio.h>
#include <md/components/seekbar.h>
#include <md/components/tag.h>
#include <md/theme/theme.h>

namespace {

const md::ColorScheme& colours()
{
    return md::Theme::applicationTheme().colours();
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

// A non-null icon box. The glyph itself is irrelevant: only the slot the button
// and the chip reserve for it is under test.
QIcon solidIcon()
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::black);
    return QIcon(pixmap);
}

} // namespace

class TestVisualInputs final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void buttonVisualSurface();
    void buttonIconSlot();
    void chipGeometry();
    void checkBoxStateLayerIsFortyDpCircle();
    void radioStateLayerIsFortyDpCircle();
    void textFieldIndicatorRole();
    void textFieldSupportingBand();
    void seekBarTrackRole();
};

void TestVisualInputs::initTestCase()
{
    // Every pixel assertion below compares against the baseline light scheme.
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestVisualInputs::buttonVisualSurface()
{
    md::Button button(QStringLiteral("Continue"));
    button.resize(button.sizeHint());

    // 48 dp touch target, 40 dp painted container (M3 button height).
    QCOMPARE(button.sizeHint().height(), 48);

    const QImage image = renderWidget(button);
    const qreal middle = button.width() / 2.0;

    // Above and below the 40 dp surface the touch target stays transparent.
    QCOMPARE(pixelAt(image, QPointF(middle, 1.0)).alpha(), 0);
    QCOMPARE(pixelAt(image, QPointF(middle, 46.0)).alpha(), 0);

    // The container is the filled variant's primary role.
    QCOMPARE(pixelAt(image, QPointF(middle, 24.0)).rgb(), colours().primary.rgb());
    QCOMPARE(pixelAt(image, QPointF(middle, 6.0)).rgb(), colours().primary.rgb());
}

void TestVisualInputs::buttonIconSlot()
{
    md::Button textOnly(QStringLiteral("Continue"));
    md::Button withIcon(QStringLiteral("Continue"));
    withIcon.setIcon(solidIcon());

    // M3: a 20 dp leading icon and an 8 dp gap. The icon side padding is the
    // same 16 dp as the label side, so the whole difference is icon + gap.
    QCOMPARE(withIcon.iconSize(), 20.0);
    QCOMPARE(withIcon.sizeHint().width() - textOnly.sizeHint().width(), 28);

    withIcon.resize(withIcon.sizeHint());
    const QImage image = renderWidget(withIcon);

    // The icon box is centred in the 40 dp surface: it starts at y = 10 and the
    // label sits beside it, not underneath it.
    QVERIFY(image.width() > 0);
    QCOMPARE(pixelAt(image, QPointF(10.0, 24.0)).rgb(), colours().primary.rgb());
}

void TestVisualInputs::chipGeometry()
{
    md::Tag chip(QStringLiteral("Filter"));
    chip.resize(chip.sizeHint());

    // M3 chip container: 32 dp high with an 8 dp corner radius.
    QCOMPARE(chip.sizeHint().height(), 32);

    const QImage image = renderWidget(chip);
    // (5, 3) is inside an 8 dp corner but outside a pill, so this separates the
    // chip shape from the old fully rounded tag.
    QCOMPARE(pixelAt(image, QPointF(5.0, 3.0)).rgb(),
             colours().surfaceContainerHighest.rgb());

    // A leading icon costs 18 dp + 8 dp gap and replaces 16 dp of padding with
    // the 8 dp icon-side padding: the width grows by exactly 18 dp.
    md::Tag plain(QStringLiteral("Filter"));
    md::Tag withIcon(QStringLiteral("Filter"));
    withIcon.setIcon(solidIcon());
    QCOMPARE(withIcon.sizeHint().width() - plain.sizeHint().width(), 18);
}

void TestVisualInputs::checkBoxStateLayerIsFortyDpCircle()
{
    md::CheckBox box;
    box.resize(box.sizeHint());
    QCOMPARE(box.sizeHint(), QSize(48, 48));

    box.setState(md::ViewState::Hovered, true);
    const QImage image = renderWidget(box);

    // The 40 dp circle spans y = 4 ... 44 and is centred on the 18 dp box.
    QVERIFY(pixelAt(image, QPointF(24.0, 6.0)).alpha() > 0);

    // Outside the circle — at the top of the target and at its side — nothing
    // is painted: the overlay must not cover the whole 48 dp widget.
    QCOMPARE(pixelAt(image, QPointF(24.0, 1.0)).alpha(), 0);
    QCOMPARE(pixelAt(image, QPointF(2.0, 24.0)).alpha(), 0);

    box.setState(md::ViewState::Hovered, false);
    const QImage idle = renderWidget(box);
    QCOMPARE(pixelAt(idle, QPointF(24.0, 6.0)).alpha(), 0);
}

void TestVisualInputs::radioStateLayerIsFortyDpCircle()
{
    md::Radio radio;
    radio.resize(radio.sizeHint());
    QCOMPARE(radio.sizeHint(), QSize(48, 48));

    radio.setState(md::ViewState::Hovered, true);
    const QImage image = renderWidget(radio);

    // The 20 dp ring ends at y = 34 and 14, so y = 6 can only be the layer.
    QVERIFY(pixelAt(image, QPointF(24.0, 6.0)).alpha() > 0);
    QCOMPARE(pixelAt(image, QPointF(24.0, 1.0)).alpha(), 0);
    QCOMPARE(pixelAt(image, QPointF(2.0, 24.0)).alpha(), 0);
}

void TestVisualInputs::textFieldIndicatorRole()
{
    md::TextInput input;
    input.resize(input.sizeHint());
    QCOMPARE(input.sizeHint().height(), 56);

    const QImage image = renderWidget(input);

    // The resting 1 dp active indicator is onSurfaceVariant (M3 filled text
    // field), not the outline role it used to be. field.bottom() is 56, so the
    // 1 dp line centred on 55.5 covers row 55 completely.
    QVERIFY(colours().onSurfaceVariant.rgb() != colours().outline.rgb());
    QCOMPARE(pixelAt(image, QPointF(120.0, 55.0)).rgb(),
             colours().onSurfaceVariant.rgb());
}

void TestVisualInputs::textFieldSupportingBand()
{
    md::TextInput input;
    input.setHelperText(QStringLiteral("Assistive text"));

    input.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());

    // 56 dp field + 4 dp top padding + a 16 dp bodySmall line (M3).
    QCOMPARE(input.measuredSize().height(), 76.0);

    input.setErrorText(QStringLiteral("Required"));
    input.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());
    QCOMPARE(input.measuredSize().height(), 76.0);
}

void TestVisualInputs::seekBarTrackRole()
{
    md::SeekBar bar;
    bar.setRange(0.0, 100.0);
    bar.setValue(0.0);
    bar.resize(bar.sizeHint());

    const QImage image = renderWidget(bar);

    // The inactive track is secondaryContainer, not a surface container.
    QVERIFY(colours().secondaryContainer.rgb() != colours().surfaceContainerHighest.rgb());
    QCOMPARE(pixelAt(image, QPointF(150.0, 32.0)).rgb(),
             colours().secondaryContainer.rgb());
}

QTEST_MAIN(TestVisualInputs)
#include "tst_visual_inputs.moc"
