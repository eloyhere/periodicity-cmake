// Visual contract for the container / navigation / overlay family.
//
// These assertions pin the Material Design 3 measurements the components resolve
// from the theme, so a later refactor cannot quietly drift away from the spec:
// a basic dialog is an extra-large surface, a menu is an extra-small one, menu
// rows are 48 dp tall, and a plain tooltip's container never grows past 200 dp.
//
// The checks stay on values the components already expose (shape(), itemRect(),
// measuredSize()); nothing here renders pixels or opens an overlay.

#include <md/components/dialog.h>
#include <md/components/menu.h>
#include <md/components/tooltip.h>
#include <md/theme/theme.h>
#include <md/tokens/elevation.h>

#include <QTest>
#include <QWidget>

class TestVisualContainers : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void dialogUsesTheExtraLargeSurface();
    void menuUsesTheExtraSmallContainerShape();
    void menuRowsAre48Dp();
    void tooltipContainerIsCappedAt200Dp();
};

void TestVisualContainers::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestVisualContainers::dialogUsesTheExtraLargeSurface()
{
    QWidget host;
    md::Dialog dialog(&host);

    // M3 basic dialog: 28 dp corner radius, i.e. the extra-large shape token.
    QCOMPARE(dialog.shape(), md::Theme::baselineLight().shapes().extraLarge);
    QCOMPARE(dialog.elevation(), md::Elevation::Level0);
}

void TestVisualContainers::menuUsesTheExtraSmallContainerShape()
{
    QWidget host;
    md::Menu menu(&host);

    // M3 menu container: 4 dp corner radius, i.e. the extra-small shape token.
    QCOMPARE(menu.shape(), md::Theme::baselineLight().shapes().extraSmall);
}

void TestVisualContainers::menuRowsAre48Dp()
{
    QWidget host;
    md::Menu menu(&host);

    md::Menu::MenuItem item;
    item.text = QStringLiteral("Open");
    menu.addItem(item);
    menu.addItem(item);

    // Panel geometry is mapped through rect(); the menu does not have to be
    // open for the row rectangles to exist.
    menu.resize(240, 240);

    QCOMPARE(menu.itemRect(0).height(), 48.0);
    QCOMPARE(menu.itemRect(1).top(), menu.itemRect(0).bottom());
}

void TestVisualContainers::tooltipContainerIsCappedAt200Dp()
{
    md::Tooltip tooltip;
    tooltip.setText(QString(400, QLatin1Char('W')));

    tooltip.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());

    // M3 plain tooltip: max container width 200 dp, padding included.
    QVERIFY(tooltip.measuredSize().width() > 0.0);
    QVERIFY(tooltip.measuredSize().width() <= 200.0);
}

QTEST_MAIN(TestVisualContainers)
#include "tst_visual_containers.moc"
