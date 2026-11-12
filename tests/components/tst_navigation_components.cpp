// Tests for the navigation menu components: Link, Scrollbar, MenuItem,
// SubMenu and MenuGroup.
//
// The suite runs offscreen against the baseline light theme. Every geometry
// assertion is made after an explicit resize/arrange, because the components
// derive their rectangles from the arrangement rather than from a stale size
// hint.

#include <md/components/link.h>
#include <md/components/menu_group.h>
#include <md/components/menu_item.h>
#include <md/components/scrollbar.h>
#include <md/components/sub_menu.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

namespace {

void clickAt(QWidget& widget, const QPointF& position)
{
    QMouseEvent press(QEvent::MouseButtonPress, position, position,
                      widget.mapToGlobal(position.toPoint()), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);

    QMouseEvent release(QEvent::MouseButtonRelease, position, position,
                        widget.mapToGlobal(position.toPoint()), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(&widget, &release);
}

void dragTo(QWidget& widget, const QPointF& from, const QPointF& to)
{
    QMouseEvent press(QEvent::MouseButtonPress, from, from, widget.mapToGlobal(from.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);

    QMouseEvent move(QEvent::MouseMove, to, to, widget.mapToGlobal(to.toPoint()), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &move);

    QMouseEvent release(QEvent::MouseButtonRelease, to, to, widget.mapToGlobal(to.toPoint()),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &release);
}

void sendWheel(QWidget& widget, const QPointF& position, int angleDelta)
{
    QWheelEvent event(position, widget.mapToGlobal(position.toPoint()), QPoint(),
                      QPoint(0, angleDelta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                      false);
    QApplication::sendEvent(&widget, &event);
}

QIcon sampleIcon()
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setBrush(QColor(0x67, 0x50, 0xA4));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QRectF(2.0, 2.0, 12.0, 12.0));
    return QIcon(pixmap);
}

QString longText()
{
    return QStringLiteral(
        "The quick brown fox jumps over the lazy dog and keeps running through the "
        "navigation drawer until the row has to truncate the label");
}

} // namespace

class TestNavigationComponents : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // Link -----------------------------------------------------------------
    void linkClickedCarriesHref();
    void linkKeyboardActivates();
    void linkVisitedAndUnderlineChange();
    void linkElidesLongText();
    void linkMirrorsInRightToLeft();
    void linkRenders();

    // Scrollbar ------------------------------------------------------------
    void scrollbarDragMovesValue();
    void scrollbarTrackClickPages();
    void scrollbarRangeClampsValue();
    void scrollbarGeometryFollowsOrientation();
    void scrollbarEmitsOnlyOnChange();
    void scrollbarWheelAndKeyboard();
    void scrollbarRenders();

    // MenuItem -------------------------------------------------------------
    void menuItemMouseAndKeyboardActivate();
    void menuItemGeometryFollowsIndentAndDirection();
    void menuItemElidesLongLabel();

    // MenuGroup ------------------------------------------------------------
    void groupSelectionIsExclusive();
    void groupClickActivatesRow();
    void groupKeyboardSkipsHiddenChildren();
    void groupCollapsesItsRows();
    void groupRowsDoNotOverlap();

    // SubMenu --------------------------------------------------------------
    void subMenuExpandsAndCollapses();
    void subMenuChildActivation();
    void subMenuIndentsChildren();
    void subMenuHonoursReducedMotion();

    // Paint smoke ----------------------------------------------------------
    void componentsRenderWithoutCrashing();
};

void TestNavigationComponents::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestNavigationComponents::cleanupTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

// ---------------------------------------------------------------------------
// Link
// ---------------------------------------------------------------------------

void TestNavigationComponents::linkClickedCarriesHref()
{
    md::Link link(QStringLiteral("Periodicity"), QStringLiteral("https://example.com/docs"));
    link.resize(240, 32);

    QSignalSpy clicked(&link, &md::Link::clicked);
    QVERIFY(!link.linkRect().isEmpty());

    clickAt(link, link.linkRect().center());
    QCOMPARE(clicked.count(), 1);
    QCOMPARE(clicked.at(0).at(0).toString(), QStringLiteral("https://example.com/docs"));

    // A press released outside the text is not an activation.
    const QPointF outside(link.linkRect().center().x(), link.linkRect().bottom() + 40.0);
    QMouseEvent press(QEvent::MouseButtonPress, outside, outside,
                      link.mapToGlobal(outside.toPoint()), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&link, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, outside, outside,
                        link.mapToGlobal(outside.toPoint()), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(&link, &release);
    QCOMPARE(clicked.count(), 1);

    // The component advertises the link affordance.
    QCOMPARE(link.cursor().shape(), Qt::PointingHandCursor);
}

void TestNavigationComponents::linkKeyboardActivates()
{
    md::Link link(QStringLiteral("Docs"), QStringLiteral("docs.md"));
    link.resize(160, 32);
    link.show();
    link.setFocus();
    QTest::qWait(10);

    QSignalSpy clicked(&link, &md::Link::clicked);

    QTest::keyClick(&link, Qt::Key_Return);
    QCOMPARE(clicked.count(), 1);
    QTest::keyClick(&link, Qt::Key_Space);
    QCOMPARE(clicked.count(), 2);
    QCOMPARE(clicked.at(1).at(0).toString(), QStringLiteral("docs.md"));
}

void TestNavigationComponents::linkVisitedAndUnderlineChange()
{
    md::Link link(QStringLiteral("Changelog"), QStringLiteral("changelog.md"));
    link.resize(200, 32);

    const md::ColorScheme& colours = md::Theme::forWidget(&link).colours();
    QCOMPARE(link.colour(), colours.primary);
    QVERIFY(!link.isUnderline());

    link.setVisited(true);
    QVERIFY(link.isVisited());
    QCOMPARE(link.colour(), colours.tertiary);

    link.setUnderline(true);
    QVERIFY(link.isUnderline());
    QCOMPARE(link.colour(), colours.tertiary);

    link.setVisited(false);
    QCOMPARE(link.colour(), colours.primary);

    // Hover deepens the colour without changing the role.
    link.resize(200, 32);
    link.show();
    QTest::qWait(10);
    QMouseEvent move(QEvent::MouseMove, link.linkRect().center(), link.linkRect().center(),
                     link.mapToGlobal(link.linkRect().center().toPoint()), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&link, &move);
    QVERIFY(link.colour() != colours.primary);
}

void TestNavigationComponents::linkElidesLongText()
{
    md::Link link(longText(), QStringLiteral("long.md"));
    link.resize(140, 32);

    const QString elided = link.elidedText();
    QVERIFY(elided.size() < link.text().size());
    QVERIFY(elided.endsWith(QChar(0x2026)));
    QCOMPARE(link.linkRect().width(), 140.0);

    // A wide link shows the whole label.
    link.resize(2000, 32);
    QCOMPARE(link.elidedText(), link.text());
}

void TestNavigationComponents::linkMirrorsInRightToLeft()
{
    md::Link link(longText(), QStringLiteral("rtl.md"));
    link.setLayoutDirection(Qt::RightToLeft);
    link.resize(140, 32);

    const QString elided = link.elidedText();
    QVERIFY(elided.size() < link.text().size());
    QVERIFY(elided.startsWith(QChar(0x2026)));
}

void TestNavigationComponents::linkRenders()
{
    md::Link link(QStringLiteral("Rendered link"), QStringLiteral("render.md"));
    link.setVisited(true);
    link.setUnderline(true);
    link.resize(180, 32);
    link.show();
    QTest::qWait(10);

    const QPixmap rendered = link.grab();
    QVERIFY(!rendered.isNull());
    QCOMPARE(rendered.size(), link.size());
}

// ---------------------------------------------------------------------------
// Scrollbar
// ---------------------------------------------------------------------------

void TestNavigationComponents::scrollbarDragMovesValue()
{
    md::Scrollbar bar(md::Scrollbar::Orientation::Vertical);
    bar.resize(12, 200);
    bar.setRange(0, 1000);
    bar.setPageStep(100);
    bar.setViewportExtent(200);

    QCOMPARE(bar.value(), 0);
    QVERIFY(bar.thumbRect().height() < bar.trackRect().height());

    QSignalSpy changed(&bar, &md::Scrollbar::valueChanged);
    QSignalSpy pressed(&bar, &md::Scrollbar::sliderPressed);
    QSignalSpy released(&bar, &md::Scrollbar::sliderReleased);

    const QPointF start(6.0, bar.thumbRect().center().y());
    const QPointF middle(6.0, bar.trackRect().center().y());
    dragTo(bar, start, middle);

    QVERIFY2(qAbs(bar.value() - 500) <= 20, qPrintable(QString::number(bar.value())));
    QVERIFY(changed.count() >= 1);
    QCOMPARE(pressed.count(), 1);
    QCOMPARE(released.count(), 1);
    QVERIFY(bar.isDragging() == false);

    // Dragging to the bottom clamps at the maximum.
    dragTo(bar, QPointF(6.0, bar.thumbRect().center().y()), QPointF(6.0, 199.0));
    QCOMPARE(bar.value(), 1000);
}

void TestNavigationComponents::scrollbarTrackClickPages()
{
    md::Scrollbar bar(md::Scrollbar::Orientation::Vertical);
    bar.resize(12, 200);
    bar.setRange(0, 1000);
    bar.setPageStep(100);
    bar.setViewportExtent(200);

    // Below the thumb: one page forward.
    clickAt(bar, QPointF(6.0, bar.trackRect().bottom() - 4.0));
    QCOMPARE(bar.value(), 100);

    // Above the thumb: one page back.
    clickAt(bar, QPointF(6.0, 1.0));
    QCOMPARE(bar.value(), 0);

    // A press on the thumb does not page.
    clickAt(bar, bar.thumbRect().center());
    QCOMPARE(bar.value(), 0);
}

void TestNavigationComponents::scrollbarRangeClampsValue()
{
    md::Scrollbar bar;
    bar.setRange(0, 100);

    QSignalSpy changed(&bar, &md::Scrollbar::valueChanged);
    bar.setValue(500);
    QCOMPARE(bar.value(), 100);
    bar.setValue(-40);
    QCOMPARE(bar.value(), 0);

    bar.setValue(80);
    bar.setRange(200, 100); // an inverted range is normalised
    QCOMPARE(bar.minimum(), 100);
    QCOMPARE(bar.maximum(), 200);
    QCOMPARE(bar.value(), 100);

    bar.setValue(150);
    QCOMPARE(bar.value(), 150);

    // The range change clamps the value and reports the new one.
    const int before = changed.count();
    bar.setRange(0, 50);
    QCOMPARE(bar.value(), 50);
    QCOMPARE(changed.count(), before + 1);
}

void TestNavigationComponents::scrollbarGeometryFollowsOrientation()
{
    md::Scrollbar vertical(md::Scrollbar::Orientation::Vertical);
    vertical.resize(12, 200);
    vertical.setRange(0, 900);
    vertical.setViewportExtent(100);

    const QRectF track = vertical.trackRect();
    QCOMPARE(track.width(), 8.0);
    QCOMPARE(track.height(), 200.0);
    QCOMPARE(track.center().x(), 6.0);
    QVERIFY(vertical.thumbRect().width() >= 8.0);
    QVERIFY(vertical.thumbRect().height() < track.height());

    vertical.setValue(450);
    QVERIFY(vertical.thumbRect().center().y() > track.center().y() - 1.0);
    QVERIFY(vertical.thumbRect().center().y() < track.bottom());

    md::Scrollbar horizontal(md::Scrollbar::Orientation::Horizontal);
    horizontal.resize(200, 12);
    horizontal.setRange(0, 900);
    horizontal.setViewportExtent(100);

    const QRectF horizontalTrack = horizontal.trackRect();
    QCOMPARE(horizontalTrack.height(), 8.0);
    QCOMPARE(horizontalTrack.width(), 200.0);
    QCOMPARE(horizontalTrack.center().y(), 6.0);
    QVERIFY(horizontal.thumbRect().width() < horizontalTrack.width());
    QVERIFY(horizontal.thumbRect().height() >= 8.0);
}

void TestNavigationComponents::scrollbarEmitsOnlyOnChange()
{
    md::Scrollbar bar;
    bar.setRange(0, 10);

    QSignalSpy changed(&bar, &md::Scrollbar::valueChanged);
    bar.setValue(5);
    QCOMPARE(changed.count(), 1);
    bar.setValue(5);
    QCOMPARE(changed.count(), 1);
    bar.setValue(6);
    QCOMPARE(changed.count(), 2);

    // Hover widens the thumb but never changes the value. The resting width is
    // read before the pointer enters (showing a widget can report an enter on
    // some platforms).
    bar.resize(12, 120);
    const qreal restingWidth = bar.thumbRect().width();
    QEnterEvent enter(QPointF(6.0, 40.0), QPointF(6.0, 40.0), bar.mapToGlobal(QPoint(6, 40)));
    QApplication::sendEvent(&bar, &enter);
    QMouseEvent move(QEvent::MouseMove, QPointF(6.0, 40.0), QPointF(6.0, 40.0),
                     bar.mapToGlobal(QPoint(6, 40)), Qt::NoButton, Qt::NoButton,
                     Qt::NoModifier);
    QApplication::sendEvent(&bar, &move);
    QVERIFY(bar.state().testFlag(md::ViewState::Hovered));
    QVERIFY(bar.thumbRect().width() > restingWidth);
    QCOMPARE(changed.count(), 2);
}

void TestNavigationComponents::scrollbarWheelAndKeyboard()
{
    md::Scrollbar bar;
    bar.resize(12, 200);
    bar.setRange(0, 1000);
    bar.setPageStep(100);

    sendWheel(bar, QPointF(6.0, 100.0), -120);
    QCOMPARE(bar.value(), 25); // pageStep / 4 per notch (the automatic single step)

    sendWheel(bar, QPointF(6.0, 100.0), 120);
    QCOMPARE(bar.value(), 0);

    QTest::keyClick(&bar, Qt::Key_End);
    QCOMPARE(bar.value(), 1000);
    QTest::keyClick(&bar, Qt::Key_PageUp);
    QCOMPARE(bar.value(), 900);
    QTest::keyClick(&bar, Qt::Key_Home);
    QCOMPARE(bar.value(), 0);
    QTest::keyClick(&bar, Qt::Key_Down);
    QCOMPARE(bar.value(), 25);
}

void TestNavigationComponents::scrollbarRenders()
{
    md::Scrollbar bar(md::Scrollbar::Orientation::Vertical);
    bar.resize(12, 160);
    bar.setRange(0, 400);
    bar.setViewportExtent(80);
    bar.setValue(120);
    bar.show();
    QTest::qWait(10);

    const QPixmap rendered = bar.grab();
    QVERIFY(!rendered.isNull());
    QCOMPARE(rendered.size(), bar.size());
}

// ---------------------------------------------------------------------------
// MenuItem
// ---------------------------------------------------------------------------

void TestNavigationComponents::menuItemMouseAndKeyboardActivate()
{
    md::MenuItem item(QStringLiteral("Inbox"));
    item.setBadge(QStringLiteral("3"));
    item.resize(240, 48);

    QSignalSpy clicked(&item, &md::MenuItem::clicked);
    QSignalSpy activated(&item, &md::MenuItem::activated);

    clickAt(item, item.itemRect().center());
    QCOMPARE(clicked.count(), 1);
    QCOMPARE(activated.count(), 1);

    QTest::keyClick(&item, Qt::Key_Return);
    QCOMPARE(clicked.count(), 2);
    QTest::keyClick(&item, Qt::Key_Space);
    QCOMPARE(clicked.count(), 3);

    item.setEnabled(false);
    clickAt(item, item.itemRect().center());
    QKeyEvent disabledKey(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&item, &disabledKey);
    QCOMPARE(clicked.count(), 3);
}

void TestNavigationComponents::menuItemGeometryFollowsIndentAndDirection()
{
    md::MenuItem item(QStringLiteral("Archive"));
    item.setIcon(sampleIcon());
    item.setBadge(QStringLiteral("12"));
    item.setShortcut(QStringLiteral("Ctrl+A"));
    item.setRowHeight(48.0);
    item.resize(320, 48);

    QCOMPARE(item.itemRect().height(), 48.0);
    QVERIFY(item.iconRect().width() > 0.0);
    QVERIFY(item.iconRect().right() < item.textRect().left());
    QVERIFY(item.badgeRect().width() > 0.0);
    QVERIFY(item.textRect().right() <= item.badgeRect().left() + 1.0);
    QVERIFY(item.arrowRect().isEmpty());

    item.setExpandable(true);
    QVERIFY(!item.arrowRect().isEmpty());
    QVERIFY(item.arrowRect().left() >= item.badgeRect().right() - 1.0);
    QVERIFY(item.arrowRect().right() <= 320.0);

    const qreal plainLeft = item.textRect().left();
    item.setIndentLevel(2);
    QCOMPARE(item.indentLevel(), 2);
    QCOMPARE(item.textRect().left(), plainLeft + 32.0);

    // Right-to-left mirrors the whole row: icon on the trailing side, chevron
    // on the leading side, label right-aligned.
    item.setLayoutDirection(Qt::RightToLeft);
    QVERIFY(item.iconRect().center().x() > item.textRect().center().x());
    QVERIFY(item.arrowRect().right() <= item.textRect().left() + 1.0);
    QVERIFY(item.textRect().width() > 0.0);

    // The chevron rotation is a number, so a submenu can drive it.
    item.setExpanded(true);
    QCOMPARE(item.arrowRotation(), 90.0);
    item.setArrowRotation(45.0);
    QCOMPARE(item.arrowRotation(), 45.0);
    item.setArrowRotation(400.0);
    QCOMPARE(item.arrowRotation(), 90.0);
}

void TestNavigationComponents::menuItemElidesLongLabel()
{
    md::MenuItem item(longText());
    item.setExpandable(true);
    item.resize(180, 48);

    const QString elided = item.elidedLabel();
    QVERIFY(elided.size() < item.label().size());
    QVERIFY(elided.endsWith(QChar(0x2026)));
    QVERIFY(item.textRect().width() > 0.0);
}

// ---------------------------------------------------------------------------
// MenuGroup
// ---------------------------------------------------------------------------

namespace {

struct GroupFixture
{
    md::MenuGroup group;
    md::MenuItem* first = nullptr;
    md::SubMenu* more = nullptr;
    md::MenuItem* childA = nullptr;
    md::MenuItem* childB = nullptr;
    md::MenuItem* last = nullptr;

    GroupFixture()
    {
        first = new md::MenuItem(QStringLiteral("First"));
        group.addItem(first);
        more = new md::SubMenu(QStringLiteral("More"));
        group.addItem(more);
        childA = new md::MenuItem(QStringLiteral("Child A"));
        more->addItem(childA);
        childB = new md::MenuItem(QStringLiteral("Child B"));
        more->addItem(childB);
        last = new md::MenuItem(QStringLiteral("Last"));
        group.addItem(last);
        group.resize(240, 400);
    }
};

} // namespace

void TestNavigationComponents::groupSelectionIsExclusive()
{
    GroupFixture fixture;

    QCOMPARE(fixture.group.count(), 3);
    QCOMPARE(fixture.group.selectedIndex(), -1);
    QVERIFY(!fixture.first->isSelected());

    QSignalSpy selection(&fixture.group, &md::MenuGroup::selectionChanged);

    fixture.group.setSelectedIndex(0);
    QCOMPARE(fixture.group.selectedIndex(), 0);
    QVERIFY(fixture.first->isSelected());
    QCOMPARE(selection.count(), 1);
    QCOMPARE(selection.at(0).at(0).toInt(), 0);

    fixture.group.setSelectedIndex(2);
    QCOMPARE(fixture.group.selectedIndex(), 2);
    QVERIFY(fixture.last->isSelected());
    QVERIFY(!fixture.first->isSelected());

    // Exactly one row is selected at a time.
    int selectedRows = 0;
    for (md::MenuItem* item : fixture.group.items())
        selectedRows += item->isSelected() ? 1 : 0;
    QCOMPARE(selectedRows, 1);

    fixture.group.clearSelection();
    QCOMPARE(fixture.group.selectedIndex(), -1);
    QVERIFY(!fixture.last->isSelected());

    // An out-of-range index is ignored rather than clearing the selection.
    fixture.group.setSelectedIndex(1);
    fixture.group.setSelectedIndex(99);
    QCOMPARE(fixture.group.selectedIndex(), 1);
}

void TestNavigationComponents::groupClickActivatesRow()
{
    GroupFixture fixture;

    QSignalSpy activated(&fixture.group, &md::MenuGroup::itemActivated);
    QVERIFY(!fixture.group.itemRect(2).isEmpty());
    QCOMPARE(fixture.group.itemRect(2).height(), 48.0);
    QCOMPARE(fixture.group.itemRect(2).top(), 48.0 * 2.0 + 2.0 * 2.0);

    clickAt(*fixture.last, fixture.last->itemRect().center());
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.at(0).at(0).toInt(), 2);
    QCOMPARE(activated.at(0).at(1).toString(), QStringLiteral("Last"));
    QCOMPARE(fixture.group.selectedIndex(), 2);

    // Activating an expandable row opens it and does not select it.
    QCOMPARE(fixture.more->isExpanded(), false);
    clickAt(*fixture.more, fixture.more->itemRect().center());
    QCOMPARE(fixture.more->isExpanded(), true);
    QVERIFY(!fixture.more->isSelected());

    // The header toggle emits its own expansion signal.
    QSignalSpy expanded(fixture.more, &md::SubMenu::expandedChanged);
    clickAt(*fixture.more, fixture.more->itemRect().center());
    QCOMPARE(expanded.count(), 1);
    QCOMPARE(expanded.at(0).at(0).toBool(), false);
}

void TestNavigationComponents::groupKeyboardSkipsHiddenChildren()
{
    GroupFixture fixture;
    fixture.group.show();
    fixture.group.setFocus();
    QTest::qWait(10);

    // A collapsed submenu contributes one row; its children are hidden.
    QCOMPARE(fixture.group.navigationRows().size(), 3);
    QVERIFY(!fixture.group.navigationRows().contains(fixture.childA));

    QTest::keyClick(&fixture.group, Qt::Key_Down);
    QCOMPARE(fixture.group.currentRow(), 0);
    QCOMPARE(fixture.group.currentItem(), fixture.first);

    QTest::keyClick(&fixture.group, Qt::Key_Down);
    QCOMPARE(fixture.group.currentItem(), fixture.more);

    fixture.more->setExpanded(true);
    fixture.more->finishAnimation();
    QCOMPARE(fixture.group.navigationRows().size(), 5);

    QTest::keyClick(&fixture.group, Qt::Key_Down);
    QCOMPARE(fixture.group.currentItem(), fixture.childA);

    QTest::keyClick(&fixture.group, Qt::Key_Down);
    QCOMPARE(fixture.group.currentItem(), fixture.childB);

    QTest::keyClick(&fixture.group, Qt::Key_Down);
    QCOMPARE(fixture.group.currentItem(), fixture.last);

    QTest::keyClick(&fixture.group, Qt::Key_Home);
    QCOMPARE(fixture.group.currentItem(), fixture.first);

    // Collapsing removes the children from the keyboard order again.
    fixture.more->setExpanded(false);
    fixture.more->finishAnimation();
    QCOMPARE(fixture.group.navigationRows().size(), 3);
    QTest::keyClick(&fixture.group, Qt::Key_Down);
    QCOMPARE(fixture.group.currentItem(), fixture.more);
    QVERIFY(!fixture.group.navigationRows().contains(fixture.childA));
}

void TestNavigationComponents::groupCollapsesItsRows()
{
    md::MenuGroup group;
    group.setTitle(QStringLiteral("Foundations"));
    group.setCollapsible(true);
    group.resize(240, 400);

    auto* row = new md::MenuItem(QStringLiteral("Button"));
    group.addItem(row);
    auto* second = new md::MenuItem(QStringLiteral("CheckBox"));
    group.addItem(second);

    QCOMPARE(group.titleRect().height(), 40.0);
    QCOMPARE(group.titleRect().width(), 240.0);
    QVERIFY(group.isExpanded());
    QVERIFY(!row->isHidden());

    QSignalSpy expanded(&group, &md::MenuGroup::expandedChanged);
    group.setExpanded(false);
    QCOMPARE(expanded.count(), 1);
    QVERIFY(row->isHidden());
    QVERIFY(!group.isExpanded());
    QVERIFY(group.navigationRows().isEmpty());
    QCOMPARE(group.itemRect(0), QRectF());

    // A collapsed group is only its title row.
    group.show();
    QTest::qWait(10);
    QCOMPARE(group.sizeHint().height(), 40);

    group.setExpanded(true);
    QVERIFY(!row->isHidden());
    QCOMPARE(group.navigationRows().size(), 2);
    QVERIFY(group.sizeHint().height() > 40.0);
}

// ---------------------------------------------------------------------------
// SubMenu
// ---------------------------------------------------------------------------

void TestNavigationComponents::groupRowsDoNotOverlap()
{
    md::MenuGroup group;
    group.setTitle(QStringLiteral("Section"));
    group.resize(240, 520);

    auto* one = new md::MenuItem(QStringLiteral("One"));
    group.addItem(one);
    auto* family = new md::SubMenu(QStringLiteral("Family"));
    auto* firstChild = new md::MenuItem(QStringLiteral("First child"));
    auto* secondChild = new md::MenuItem(QStringLiteral("Second child"));
    family->addItem(firstChild);
    family->addItem(secondChild);
    group.addItem(family);
    auto* last = new md::MenuItem(QStringLiteral("Last"));
    group.addItem(last);

    // One row per added item, no duplicates, and every row has its own band:
    // a row that appeared twice would share its rectangle with the copy.
    QCOMPARE(group.count(), 3);
    QCOMPARE(group.items().size(), 3);
    QCOMPARE(group.navigationRows().size(), 3);

    int previousBottom = -1;
    for (int index = 0; index < group.count(); ++index) {
        const QRectF rect = group.itemRect(index);
        QVERIFY(rect.height() > 0.0);
        QVERIFY(rect.top() >= previousBottom);
        previousBottom = static_cast<int>(rect.bottom());
        QCOMPARE(group.items().at(index)->geometry(), rect.toAlignedRect());
    }

    // Expanded: the header keeps the first band and the two children follow it,
    // each on its own row, without leaving the submenu's rectangle.
    family->setExpanded(true);
    family->finishAnimation();
    QCOMPARE(family->childCount(), 2);
    QVERIFY(!firstChild->isHidden());
    QVERIFY(!secondChild->isHidden());
    QCOMPARE(group.navigationRows().size(), 5);

    const int headerRowHeight = static_cast<int>(qRound(family->rowHeight()));
    QCOMPARE(firstChild->geometry().top(), headerRowHeight);
    QVERIFY2(firstChild->geometry().bottom() < family->height(),
             qPrintable(QStringLiteral("family height %1, first child bottom %2")
                            .arg(family->height())
                            .arg(firstChild->geometry().bottom())));
    QCOMPARE(secondChild->geometry().top(), firstChild->geometry().bottom() + 1 + 2);
    QVERIFY(secondChild->geometry().bottom() < family->height());
    QCOMPARE(family->geometry().height(),
             static_cast<int>(qRound(family->preferredHeight())));

    // The rows below the family keep their own bands.
    const QRectF familyRect = group.itemRect(1);
    QVERIFY(group.itemRect(2).top() >= static_cast<int>(familyRect.bottom()));
}

// ---------------------------------------------------------------------------
// SubMenu (expanded painting)
// ---------------------------------------------------------------------------

void TestNavigationComponents::subMenuExpandsAndCollapses()
{
    md::SubMenu submenu(QStringLiteral("Inputs"));
    auto* one = new md::MenuItem(QStringLiteral("TextInput"));
    auto* two = new md::MenuItem(QStringLiteral("CheckBox"));
    submenu.addItem(one);
    submenu.addItem(two);
    submenu.resize(240, 48);

    QCOMPARE(submenu.childCount(), 2);
    QCOMPARE(submenu.expansion(), 0.0);
    QCOMPARE(submenu.preferredHeight(), 48.0);
    QVERIFY(submenu.isExpandable());

    QSignalSpy expanded(&submenu, &md::SubMenu::expandedChanged);
    QSignalSpy finished(&submenu, &md::SubMenu::animationFinished);

    submenu.setExpanded(true);
    QCOMPARE(expanded.count(), 1);
    QVERIFY(submenu.isExpanded());
    submenu.finishAnimation();
    QCOMPARE(finished.count(), 1);
    QCOMPARE(submenu.expansion(), 1.0);
    QCOMPARE(submenu.preferredHeight(), 48.0 + 48.0 * 2.0 + 2.0);
    QVERIFY(!submenu.isAnimating());

    // The children are revealed under the row.
    QCOMPARE(one->isHidden(), false);
    QCOMPARE(one->indentLevel(), 1);
    submenu.resize(240, static_cast<int>(submenu.preferredHeight()));
    QCOMPARE(one->geometry().top(), 48);
    QCOMPARE(two->geometry().top(), 98);

    QSignalSpy collapsed(&submenu, &md::SubMenu::expandedChanged);
    submenu.setExpanded(false);
    submenu.finishAnimation();
    QCOMPARE(collapsed.count(), 1);
    QCOMPARE(submenu.expansion(), 0.0);
    QCOMPARE(submenu.preferredHeight(), 48.0);
    QVERIFY(one->isHidden());

    // activate() is the one activation path: it toggles.
    submenu.activate();
    QVERIFY(submenu.isExpanded());
    submenu.finishAnimation();
}

void TestNavigationComponents::subMenuChildActivation()
{
    md::SubMenu submenu(QStringLiteral("Display"));
    auto* leaf = new md::MenuItem(QStringLiteral("Card"));
    submenu.addItem(leaf);
    submenu.setExpanded(true);
    submenu.finishAnimation();
    submenu.resize(240, static_cast<int>(submenu.preferredHeight()));

    QSignalSpy activated(&submenu, &md::SubMenu::childActivated);
    clickAt(*leaf, leaf->itemRect().center());
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.at(0).at(0).toInt(), 0);

    // The same signal fires for a keyboard activation of the focused child.
    QTest::keyClick(leaf, Qt::Key_Return);
    QCOMPARE(activated.count(), 2);
}

void TestNavigationComponents::subMenuIndentsChildren()
{
    md::SubMenu submenu(QStringLiteral("Media"));
    auto* leaf = new md::MenuItem(QStringLiteral("Avatar"));
    submenu.addItem(leaf);

    QCOMPARE(submenu.indentLevel(), 0);
    QCOMPARE(leaf->indentLevel(), 1);

    submenu.setIndentLevel(2);
    QCOMPARE(submenu.indentLevel(), 2);
    QCOMPARE(leaf->indentLevel(), 3);

    submenu.resize(300, 48);
    QVERIFY(leaf->indentLevel() > submenu.indentLevel());
}

void TestNavigationComponents::subMenuHonoursReducedMotion()
{
    md::MotionScheme reduced;
    reduced.reducedMotion = true;
    const md::Theme baseline = md::Theme::baselineLight();
    md::Theme::setApplicationTheme(baseline.withMotion(reduced));

    md::SubMenu submenu(QStringLiteral("Reduced"));
    submenu.addItem(new md::MenuItem(QStringLiteral("Row")));

    submenu.setExpanded(true);
    QVERIFY(!submenu.isAnimating());
    QCOMPARE(submenu.expansion(), 1.0);

    submenu.setExpanded(false);
    QVERIFY(!submenu.isAnimating());
    QCOMPARE(submenu.expansion(), 0.0);

    md::Theme::setApplicationTheme(baseline);
}

// ---------------------------------------------------------------------------
// Paint smoke
// ---------------------------------------------------------------------------

void TestNavigationComponents::componentsRenderWithoutCrashing()
{
    auto* host = new QWidget;
    host->resize(320, 420);

    auto* link = new md::Link(QStringLiteral("Rendered"), QStringLiteral("rendered.md"), host);
    link->setGeometry(8, 8, 200, 28);
    link->setVisited(true);

    auto* group = new md::MenuGroup(host);
    group->setTitle(QStringLiteral("Overlays"));
    group->setCollapsible(true);
    group->setGeometry(8, 48, 240, 260);

    auto* item = new md::MenuItem(QStringLiteral("Menu"));
    item->setIcon(sampleIcon());
    item->setBadge(QStringLiteral("2"));
    item->setShortcut(QStringLiteral("Ctrl+M"));
    group->addItem(item);

    auto* submenu = new md::SubMenu(QStringLiteral("Menu rows"));
    group->addItem(submenu);
    auto* child = new md::MenuItem(QStringLiteral("MenuItem"));
    submenu->addItem(child);
    submenu->setExpanded(true);
    submenu->finishAnimation();

    auto* bar = new md::Scrollbar(md::Scrollbar::Orientation::Vertical, host);
    bar->setGeometry(300, 8, 12, 380);
    bar->setRange(0, 500);
    bar->setViewportExtent(120);
    bar->setValue(200);

    host->show();
    QTest::qWait(20);

    QCOMPARE(group->navigationRows().size(), 3);
    const QPixmap rendered = host->grab();
    QVERIFY(!rendered.isNull());
    QCOMPARE(rendered.size(), host->size());

    // Each component paints on its own as well.
    QVERIFY(!link->grab().isNull());
    QVERIFY(!item->grab().isNull());
    QVERIFY(!submenu->grab().isNull());
    QVERIFY(!bar->grab().isNull());

    delete host;
}

QTEST_MAIN(TestNavigationComponents)
#include "tst_navigation_components.moc"
