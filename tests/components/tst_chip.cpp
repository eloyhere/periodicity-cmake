// Unit tests for the Chip component (framework.md §5.9, §14).
//
// The widget is exercised offscreen, exactly like tst_media_basics: the
// application theme is pinned to baseline light, geometry is measured from
// sizeHint(), and pointer/key input is delivered with QApplication::sendEvent
// (no window activation and no virtual cursor). The tests cover the measure
// contract, the filter toggle, the stateless assist/suggestion variants, the
// separate close target, keyboard activation and the disabled contract.

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QSignalSpy>
#include <QSize>
#include <QTest>
#include <QWidget>
#include <Qt>

#include <md/components/chip.h>
#include <md/theme/theme.h>

namespace {

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

// A key press and its release: the component activates on the release, like
// Button and Tag.
void sendKey(QWidget& widget, int key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(&widget, &release);
}

// Renders into an explicitly transparent image, with DrawChildren only: the
// window background is not part of the component.
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

QIcon squareIcon()
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(Qt::black));
        painter.drawRect(QRectF(3.0, 3.0, 12.0, 12.0));
    }
    return QIcon(pixmap);
}

} // namespace

class TestChip : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void defaultsAndTextMeasurement();
    void filterClickTogglesAndEmits();
    void statelessVariants();
    void closeTargetEmitsClosed();
    void keyboardActivation();
    void disabledChipEmitsNothing();
    void iconsAndCheckSlot();
    void paintSmoke();
};

void TestChip::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestChip::defaultsAndTextMeasurement()
{
    md::Chip chip;
    QCOMPARE(chip.text(), QString());
    QVERIFY(chip.variant() == md::Chip::Variant::Assist);
    QVERIFY(!chip.isSelected());
    QVERIFY(!chip.isClosable());
    QVERIFY(chip.leadingIcon().isNull());

    md::Chip shortChip(QStringLiteral("A"));
    md::Chip longChip(QStringLiteral("A much longer chip label"));

    QCOMPARE(shortChip.text(), QStringLiteral("A"));
    QCOMPARE(shortChip.sizeHint().height(), 32);
    QCOMPARE(longChip.sizeHint().height(), 32);
    QVERIFY(longChip.sizeHint().width() > shortChip.sizeHint().width());

    // A later setText() re-measures: the width follows the label.
    const int shortWidth = shortChip.sizeHint().width();
    shortChip.setText(QStringLiteral("A much longer chip label"));
    QCOMPARE(shortChip.text(), QStringLiteral("A much longer chip label"));
    QVERIFY(shortChip.sizeHint().width() > shortWidth);
    QCOMPARE(shortChip.sizeHint().width(), longChip.sizeHint().width());
}

void TestChip::filterClickTogglesAndEmits()
{
    md::Chip chip(QStringLiteral("Filter"));
    chip.setVariant(md::Chip::Variant::Filter);
    chip.resize(chip.sizeHint());

    QVERIFY(chip.variant() == md::Chip::Variant::Filter);
    QCOMPARE(chip.sizeHint().height(), 32);

    QSignalSpy clickedSpy(&chip, &md::Chip::clicked);
    QSignalSpy selectedSpy(&chip, &md::Chip::selectedChanged);

    const QPointF body(chip.width() / 2.0, 16.0);

    QVERIFY(!chip.isSelected());
    clickAt(chip, body);
    QVERIFY(chip.isSelected());
    QCOMPARE(clickedSpy.count(), 1);
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(selectedSpy.at(0).at(0).toBool(), true);

    clickAt(chip, body);
    QVERIFY(!chip.isSelected());
    QCOMPARE(clickedSpy.count(), 2);
    QCOMPARE(selectedSpy.count(), 2);
    QCOMPARE(selectedSpy.at(1).at(0).toBool(), false);

    // A press that starts on the chip but is released outside is not a click
    // and does not change the selection.
    const int clicksBefore = clickedSpy.count();
    sendMouse(chip, QEvent::MouseButtonPress, body, Qt::LeftButton, Qt::LeftButton);
    sendMouse(chip, QEvent::MouseButtonRelease, QPointF(chip.width() + 30.0, 16.0),
              Qt::LeftButton, Qt::NoButton);
    QCOMPARE(clickedSpy.count(), clicksBefore);
    QVERIFY(!chip.isSelected());
}

void TestChip::statelessVariants()
{
    md::Chip assist(QStringLiteral("Assist"));
    assist.resize(assist.sizeHint());

    QSignalSpy assistClicked(&assist, &md::Chip::clicked);
    QSignalSpy assistSelected(&assist, &md::Chip::selectedChanged);

    clickAt(assist, QPointF(assist.width() / 2.0, 16.0));
    QCOMPARE(assistClicked.count(), 1);
    QCOMPARE(assistSelected.count(), 0);
    QVERIFY(!assist.isSelected());

    // setSelected() is a no-op on a stateless variant, not a silent state that
    // is later painted.
    assist.setSelected(true);
    QVERIFY(!assist.isSelected());
    QCOMPARE(assistSelected.count(), 0);

    md::Chip suggestion(QStringLiteral("Suggestion"));
    suggestion.setVariant(md::Chip::Variant::Suggestion);
    suggestion.resize(suggestion.sizeHint());

    QSignalSpy suggestionClicked(&suggestion, &md::Chip::clicked);
    QSignalSpy suggestionSelected(&suggestion, &md::Chip::selectedChanged);

    clickAt(suggestion, QPointF(suggestion.width() / 2.0, 16.0));
    QCOMPARE(suggestionClicked.count(), 1);
    QCOMPARE(suggestionSelected.count(), 0);
    QVERIFY(!suggestion.isSelected());

    // A variant switch away from a selectable chip clears the selection and
    // reports that the state no longer applies.
    md::Chip filter(QStringLiteral("Filter"));
    filter.setVariant(md::Chip::Variant::Filter);
    QSignalSpy filterSelected(&filter, &md::Chip::selectedChanged);
    filter.setSelected(true);
    QCOMPARE(filterSelected.count(), 1);

    filter.setVariant(md::Chip::Variant::Assist);
    QVERIFY(!filter.isSelected());
    QCOMPARE(filterSelected.count(), 2);
    QCOMPARE(filterSelected.at(1).at(0).toBool(), false);
}

void TestChip::closeTargetEmitsClosed()
{
    md::Chip chip(QStringLiteral("Input"));
    chip.setVariant(md::Chip::Variant::Input);
    chip.setClosable(true);
    chip.resize(chip.sizeHint());

    QVERIFY(chip.isClosable());
    QSignalSpy clickedSpy(&chip, &md::Chip::clicked);
    QSignalSpy selectedSpy(&chip, &md::Chip::selectedChanged);
    QSignalSpy closedSpy(&chip, &md::Chip::closed);

    // The close target is 24 dp wide and ends 8 dp from the trailing edge, so
    // it spans [width - 32, width - 8]. A point 31 dp from the edge is inside
    // the target but outside the 18 dp glyph, which proves the enlarged target.
    clickAt(chip, QPointF(chip.width() - 31.0, 16.0));
    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(clickedSpy.count(), 0);
    QCOMPARE(selectedSpy.count(), 0);
    QVERIFY(!chip.isSelected());

    // A body click away from the close target activates the chip.
    clickAt(chip, QPointF(20.0, 16.0));
    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(clickedSpy.count(), 1);
    QCOMPARE(selectedSpy.count(), 1);
    QVERIFY(chip.isSelected());

    // Delete closes without activating, and closed() is the only signal.
    sendKey(chip, Qt::Key_Delete);
    QCOMPARE(closedSpy.count(), 2);
    QCOMPARE(clickedSpy.count(), 1);
    QCOMPARE(selectedSpy.count(), 1);

    // A closable chip grows by the 24 dp target (the end padding drops from
    // 16 dp to 8 dp and the 8 dp gap is added).
    md::Chip open(QStringLiteral("Input"));
    open.setVariant(md::Chip::Variant::Input);
    const int openWidth = open.sizeHint().width();
    open.setClosable(true);
    QCOMPARE(open.sizeHint().width(), openWidth + 24);
}

void TestChip::keyboardActivation()
{
    md::Chip filter(QStringLiteral("Filter"));
    filter.setVariant(md::Chip::Variant::Filter);
    filter.resize(filter.sizeHint());

    QSignalSpy clickedSpy(&filter, &md::Chip::clicked);
    QSignalSpy selectedSpy(&filter, &md::Chip::selectedChanged);

    sendKey(filter, Qt::Key_Space);
    QVERIFY(filter.isSelected());
    QCOMPARE(clickedSpy.count(), 1);
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(selectedSpy.at(0).at(0).toBool(), true);

    sendKey(filter, Qt::Key_Return);
    QVERIFY(!filter.isSelected());
    QCOMPARE(clickedSpy.count(), 2);
    QCOMPARE(selectedSpy.count(), 2);
    QCOMPARE(selectedSpy.at(1).at(0).toBool(), false);

    // A stateless assist chip activates on the keyboard without ever entering
    // the selected state.
    md::Chip assist(QStringLiteral("Assist"));
    assist.resize(assist.sizeHint());
    QSignalSpy assistClicked(&assist, &md::Chip::clicked);
    QSignalSpy assistSelected(&assist, &md::Chip::selectedChanged);

    sendKey(assist, Qt::Key_Space);
    QCOMPARE(assistClicked.count(), 1);
    QCOMPARE(assistSelected.count(), 0);
    QVERIFY(!assist.isSelected());

    // Delete only closes a chip that has a close affordance.
    md::Chip assistDelete(QStringLiteral("Assist"));
    assistDelete.resize(assistDelete.sizeHint());
    QSignalSpy assistClosed(&assistDelete, &md::Chip::closed);
    sendKey(assistDelete, Qt::Key_Delete);
    QCOMPARE(assistClosed.count(), 0);
}

void TestChip::disabledChipEmitsNothing()
{
    md::Chip chip(QStringLiteral("Input"));
    chip.setVariant(md::Chip::Variant::Input);
    chip.setClosable(true);
    chip.resize(chip.sizeHint());

    QSignalSpy clickedSpy(&chip, &md::Chip::clicked);
    QSignalSpy selectedSpy(&chip, &md::Chip::selectedChanged);
    QSignalSpy closedSpy(&chip, &md::Chip::closed);

    chip.setEnabled(false);
    QVERIFY(!chip.isEnabled());

    clickAt(chip, QPointF(20.0, 16.0));
    clickAt(chip, QPointF(chip.width() - 12.0, 16.0));
    sendKey(chip, Qt::Key_Space);
    sendKey(chip, Qt::Key_Return);
    sendKey(chip, Qt::Key_Delete);

    QCOMPARE(clickedSpy.count(), 0);
    QCOMPARE(selectedSpy.count(), 0);
    QCOMPARE(closedSpy.count(), 0);
    QVERIFY(!chip.isSelected());
}

void TestChip::iconsAndCheckSlot()
{
    // A leading icon replaces the 16 dp text-side start padding with the 8 dp
    // icon-side padding and adds an 18 dp box plus an 8 dp gap: +18 dp net.
    md::Chip plain(QStringLiteral("Filter"));
    plain.setVariant(md::Chip::Variant::Filter);
    const int plainWidth = plain.sizeHint().width();

    plain.setLeadingIcon(squareIcon());
    QVERIFY(!plain.leadingIcon().isNull());
    QCOMPARE(plain.sizeHint().width(), plainWidth + 18);

    // Selecting replaces the icon with the check mark in the same slot, so the
    // width does not change again.
    const int iconWidth = plain.sizeHint().width();
    plain.setSelected(true);
    QVERIFY(plain.isSelected());
    QCOMPARE(plain.sizeHint().width(), iconWidth);

    // A filter chip without an icon still reserves the check slot when it is
    // selected, and drops it when it is not.
    md::Chip bare(QStringLiteral("Filter"));
    bare.setVariant(md::Chip::Variant::Filter);
    const int bareWidth = bare.sizeHint().width();
    bare.setSelected(true);
    QCOMPARE(bare.sizeHint().width(), bareWidth + 18);
    bare.setSelected(false);
    QCOMPARE(bare.sizeHint().width(), bareWidth);

    // The leading icon setter round-trips through leadingIcon().
    md::Chip assist(QStringLiteral("Assist"));
    QVERIFY(assist.leadingIcon().isNull());
    assist.setLeadingIcon(squareIcon());
    QVERIFY(!assist.leadingIcon().isNull());
    assist.setLeadingIcon(QIcon());
    QVERIFY(assist.leadingIcon().isNull());
}

void TestChip::paintSmoke()
{
    // The selected filter paints the secondary container and the check glyph;
    // sampling the padded trailing area avoids the label and the leading slot.
    md::Chip filter(QStringLiteral("Filter"));
    filter.setVariant(md::Chip::Variant::Filter);
    filter.setSelected(true);
    filter.resize(filter.sizeHint());

    const QImage filterImage = renderWidget(filter);
    QVERIFY(paintedPixels(filterImage) > 0);
    QCOMPARE(pixelAt(filterImage, QPointF(filter.width() - 4.0, 16.0)).rgb(),
             md::Theme::applicationTheme().colours().secondaryContainer.rgb());

    // A closable input chip paints its leading icon, label, outline and close
    // glyph without a crash.
    md::Chip input(QStringLiteral("Input"));
    input.setVariant(md::Chip::Variant::Input);
    input.setClosable(true);
    input.setLeadingIcon(squareIcon());
    input.resize(input.sizeHint());
    QVERIFY(paintedPixels(renderWidget(input)) > 0);
}

QTEST_MAIN(TestChip)
#include "tst_chip.moc"
