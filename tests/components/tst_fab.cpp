// Unit tests for the Fab component (Material 3 floating action button).
//
// The suite runs offscreen like tests/components/tst_basic_widgets.cpp: the
// application theme is pinned to the baseline light scheme, sizes are measured
// from sizeHint(), pointer and key input is delivered straight to the widget
// with QApplication::sendEvent, and the one visual test renders into a
// transparent QImage so unpainted pixels are deterministic.

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

#include <md/components/fab.h>
#include <md/theme/theme.h>
#include <md/tokens/color_scheme.h>
#include <md/tokens/elevation.h>

namespace {

const md::ColorScheme& lightColours()
{
    return md::Theme::applicationTheme().colours();
}

// A fully filled 24 x 24 icon: every pixel of the drawn box is the tint, which
// makes the icon sample in paintsPrimaryContainerAndIcon unambiguous.
QIcon squareIcon()
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

class TestFab : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void regularSize();
    void smallAndLargeSizes();
    void extendedGrowsWithText();
    void iconSizeDefaultsAndOverrides();

    void clickEmitsClickedOnce();
    void keyboardActivates();
    void disabledEmitsNothing();

    void paintsPrimaryContainerAndIcon();
};

void TestFab::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestFab::regularSize()
{
    md::Fab fab;

    QCOMPARE(fab.sizeHint(), QSize(56, 56));
    QVERIFY(fab.size() == md::Fab::Size::Regular);
    QVERIFY(!fab.isExtended());
    QVERIFY(fab.elevation() == md::Elevation::Level3);
    QCOMPARE(fab.shape().radii().topStart, 16.0);
    QCOMPARE(fab.iconSize(), 24.0);
    QVERIFY(fab.text().isEmpty());
    QVERIFY(fab.icon().isNull());
}

void TestFab::smallAndLargeSizes()
{
    md::Fab fab;

    fab.setSize(md::Fab::Size::Small);
    QCOMPARE(fab.sizeHint(), QSize(40, 40));
    QCOMPARE(fab.shape().radii().topStart, 16.0);
    QCOMPARE(fab.iconSize(), 24.0);

    fab.setSize(md::Fab::Size::Large);
    QCOMPARE(fab.sizeHint(), QSize(96, 96));
    QCOMPARE(fab.shape().radii().topStart, 28.0);
    QCOMPARE(fab.iconSize(), 36.0);
}

void TestFab::extendedGrowsWithText()
{
    const QIcon icon = squareIcon();
    md::Fab fab(icon);

    fab.setExtended(true);
    fab.setText(QStringLiteral("Add"));

    QVERIFY(fab.isExtended());
    QVERIFY(fab.shape().isPill());
    QCOMPARE(fab.sizeHint().height(), 56);

    const int compactWidth = fab.sizeHint().width();

    fab.setText(QStringLiteral("Add a new item to the list"));
    QCOMPARE(fab.text(), QStringLiteral("Add a new item to the list"));

    const int wideWidth = fab.sizeHint().width();
    QVERIFY(wideWidth > compactWidth);
    QCOMPARE(fab.sizeHint().height(), 56);

    // Collapsing returns to the regular square container.
    fab.setExtended(false);
    QCOMPARE(fab.sizeHint(), QSize(56, 56));

    // The extended height wins over the container size even without a label.
    md::Fab iconOnly;
    iconOnly.setSize(md::Fab::Size::Large);
    iconOnly.setExtended(true);
    QCOMPARE(iconOnly.sizeHint(), QSize(56, 56));
}

void TestFab::iconSizeDefaultsAndOverrides()
{
    md::Fab fab;
    QCOMPARE(fab.iconSize(), 24.0);

    fab.setSize(md::Fab::Size::Large);
    QCOMPARE(fab.iconSize(), 36.0);

    // An explicit size survives a container-size change.
    fab.setIconSize(20.0);
    QCOMPARE(fab.iconSize(), 20.0);
    fab.setSize(md::Fab::Size::Small);
    QCOMPARE(fab.iconSize(), 20.0);
}

void TestFab::clickEmitsClickedOnce()
{
    md::Fab fab;
    fab.resize(fab.sizeHint());

    QSignalSpy spy(&fab, &md::Fab::clicked);

    clickAt(fab, QPointF(28.0, 28.0));
    QCOMPARE(spy.count(), 1);
}

void TestFab::keyboardActivates()
{
    md::Fab fab;
    fab.resize(fab.sizeHint());

    QSignalSpy spy(&fab, &md::Fab::clicked);

    sendKey(fab, Qt::Key_Space);
    QCOMPARE(spy.count(), 1);

    sendKey(fab, Qt::Key_Return);
    QCOMPARE(spy.count(), 2);

    sendKey(fab, Qt::Key_Enter);
    QCOMPARE(spy.count(), 3);
}

void TestFab::disabledEmitsNothing()
{
    md::Fab fab;
    fab.setEnabled(false);
    fab.resize(fab.sizeHint());

    QSignalSpy spy(&fab, &md::Fab::clicked);

    clickAt(fab, QPointF(28.0, 28.0));
    sendKey(fab, Qt::Key_Space);
    sendKey(fab, Qt::Key_Return);
    sendKey(fab, Qt::Key_Enter);

    QCOMPARE(spy.count(), 0);
}

void TestFab::paintsPrimaryContainerAndIcon()
{
    const QIcon icon = squareIcon();
    md::Fab fab(icon);
    fab.resize(fab.sizeHint());

    const QImage image = renderWidget(fab);

    // (8, 28) is inside the 16 dp rounded container and clear of the centred
    // 24 dp icon box, so it samples the raw primaryContainer fill.
    QCOMPARE(pixelAt(image, QPointF(8.0, 28.0)).rgb(), lightColours().primaryContainer.rgb());

    // The source icon is solid white; IconDrawable tints it with the content
    // colour, so the centre must be onPrimaryContainer.
    QCOMPARE(pixelAt(image, QPointF(28.0, 28.0)).rgb(), lightColours().onPrimaryContainer.rgb());
}

QTEST_MAIN(TestFab)
#include "tst_fab.moc"
