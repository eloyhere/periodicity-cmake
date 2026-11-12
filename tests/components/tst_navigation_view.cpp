// Tests for NavigationView: geometry, selection, keyboard, scrolling and paint.

#include <md/components/navigation_view.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

namespace {

void clickAt(QWidget& widget, const QPointF& position)
{
    QMouseEvent press(QEvent::MouseButtonPress, position, widget.mapToGlobal(position.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, position,
                        widget.mapToGlobal(position.toPoint()), Qt::LeftButton, Qt::NoButton,
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

void sendWheel(QWidget& widget, const QPointF& position, int angleDelta)
{
    QWheelEvent event(position, widget.mapToGlobal(position.toPoint()), QPoint(), QPoint(0, angleDelta),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&widget, &event);
}

} // namespace

class TestNavigationView : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void geometryFollowsTokens();
    void clickSelectsAndActivates();
    void headersAndDisabledItemsAreInert();
    void selectedIndexValidatesEntryKind();
    void keyboardSkipsSectionsAndDisabled();
    void scrollingIsClamped();
    void entriesCanBeReplacedAndCleared();
    void rendersWithoutCrashing();
};

void TestNavigationView::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestNavigationView::cleanupTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestNavigationView::geometryFollowsTokens()
{
    md::NavigationView nav;
    nav.setHeaderTitle(QStringLiteral("Ada"));
    nav.setHeaderSubtitle(QStringLiteral("ada@example.com"));
    const int section = nav.addSection(QStringLiteral("Browse"));
    const int inbox = nav.addItem(QStringLiteral("Inbox"));
    const int starred = nav.addItem(QStringLiteral("Starred"));
    nav.addDivider();

    QCOMPARE(section, 0);
    QCOMPARE(inbox, 1);
    QCOMPARE(starred, 2);
    QCOMPARE(nav.entryCount(), 4);
    QCOMPARE(nav.itemCount(), 2);

    nav.resize(320, 400);
    QCOMPARE(nav.headerRect().height(), 96.0);
    QCOMPARE(nav.entryRect(section).height(), 40.0);
    QCOMPARE(nav.entryRect(inbox).height(), 48.0);
    QCOMPARE(nav.entryRect(inbox).top(), 136.0);   // 96 + 40
    QCOMPARE(nav.entryRect(starred).top(), 184.0); // + 48
    QCOMPARE(nav.contentHeight(), 96.0 + 40.0 + 48.0 + 48.0 + 9.0);

    nav.setHeaderVisible(false);
    QCOMPARE(nav.entryRect(section).top(), 0.0);
    QVERIFY(nav.headerRect().isNull());
}

void TestNavigationView::clickSelectsAndActivates()
{
    md::NavigationView nav;
    nav.addSection(QStringLiteral("Browse"));
    nav.addItem(QStringLiteral("Inbox"));
    nav.addItem(QStringLiteral("Starred"));
    nav.resize(320, 400);

    QSignalSpy activated(&nav, &md::NavigationView::itemActivated);
    QSignalSpy selected(&nav, &md::NavigationView::selectionChanged);

    const QRectF row = nav.entryRect(2);
    clickAt(nav, row.center());

    QCOMPARE(nav.selectedIndex(), 2);
    QCOMPARE(nav.selectedLabel(), QStringLiteral("Starred"));
    QCOMPARE(selected.count(), 1);
    QCOMPARE(selected.at(0).at(0).toInt(), 2);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.at(0).at(0).toInt(), 2);
    QCOMPARE(activated.at(0).at(1).toString(), QStringLiteral("Starred"));
}

void TestNavigationView::headersAndDisabledItemsAreInert()
{
    md::NavigationView nav;
    nav.addSection(QStringLiteral("Browse"));
    nav.addItem(QStringLiteral("Inbox"));
    nav.addItem(QStringLiteral("Archive"));
    nav.resize(320, 400);
    nav.setEntryEnabled(2, false);

    QSignalSpy activated(&nav, &md::NavigationView::itemActivated);
    QSignalSpy selected(&nav, &md::NavigationView::selectionChanged);

    clickAt(nav, nav.entryRect(0).center()); // section header
    QCOMPARE(nav.selectedIndex(), -1);
    QCOMPARE(activated.count(), 0);

    clickAt(nav, nav.entryRect(2).center()); // disabled item
    QCOMPARE(nav.selectedIndex(), -1);
    QCOMPARE(selected.count(), 0);
    QCOMPARE(activated.count(), 0);
}

void TestNavigationView::selectedIndexValidatesEntryKind()
{
    md::NavigationView nav;
    nav.addSection(QStringLiteral("Browse"));
    nav.addItem(QStringLiteral("Inbox"));
    nav.addDivider();
    nav.addItem(QStringLiteral("Archive"));
    nav.setEntryEnabled(3, false);

    nav.setSelectedIndex(0); // section
    QCOMPARE(nav.selectedIndex(), -1);
    nav.setSelectedIndex(2); // divider
    QCOMPARE(nav.selectedIndex(), -1);
    nav.setSelectedIndex(3); // disabled item
    QCOMPARE(nav.selectedIndex(), -1);

    nav.setSelectedIndex(1);
    QCOMPARE(nav.selectedIndex(), 1);
    nav.setSelectedIndex(99);
    QCOMPARE(nav.selectedIndex(), 1);

    nav.clearSelection();
    QCOMPARE(nav.selectedIndex(), -1);
}

void TestNavigationView::keyboardSkipsSectionsAndDisabled()
{
    md::NavigationView nav;
    nav.addSection(QStringLiteral("Browse"));
    nav.addItem(QStringLiteral("Inbox"));     // 1
    nav.addItem(QStringLiteral("Archive"));   // 2 (disabled)
    nav.addItem(QStringLiteral("Starred"));   // 3
    nav.setEntryEnabled(2, false);
    nav.resize(320, 400);

    QSignalSpy activated(&nav, &md::NavigationView::itemActivated);

    sendKey(nav, Qt::Key_Home);
    QCOMPARE(nav.selectedIndex(), 1);

    sendKey(nav, Qt::Key_Down);
    QCOMPARE(nav.selectedIndex(), 3);

    sendKey(nav, Qt::Key_Down); // no next enabled item
    QCOMPARE(nav.selectedIndex(), 3);

    sendKey(nav, Qt::Key_Up);
    QCOMPARE(nav.selectedIndex(), 1);

    sendKey(nav, Qt::Key_End);
    QCOMPARE(nav.selectedIndex(), 3);

    sendKey(nav, Qt::Key_Return);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.at(0).at(0).toInt(), 3);
}

void TestNavigationView::scrollingIsClamped()
{
    md::NavigationView nav;
    nav.setHeaderVisible(false);
    for (int i = 0; i < 20; ++i)
        nav.addItem(QStringLiteral("Item %1").arg(i + 1));
    nav.resize(320, 200);

    QCOMPARE(nav.scrollOffset(), 0.0);
    sendWheel(nav, QPointF(100, 100), -120);
    QCOMPARE(nav.scrollOffset(), 64.0);

    nav.setScrollOffset(10'000.0);
    const qreal maximum = nav.contentHeight() - 200.0;
    QCOMPARE(nav.scrollOffset(), maximum);

    nav.setScrollOffset(-50.0);
    QCOMPARE(nav.scrollOffset(), 0.0);

    // The painted rectangles follow the offset.
    nav.setScrollOffset(64.0);
    QCOMPARE(nav.entryRect(1).top(), 48.0 - 64.0);
}

void TestNavigationView::entriesCanBeReplacedAndCleared()
{
    md::NavigationView nav;
    nav.addItem(QStringLiteral("One"));
    nav.addItem(QStringLiteral("Two"));
    nav.setSelectedIndex(1);
    QCOMPARE(nav.selectedIndex(), 1);

    nav.setEntryBadge(1, QStringLiteral("5"));
    QCOMPARE(nav.entry(1).badge, QStringLiteral("5"));

    md::NavigationView::Entry section;
    section.kind = md::NavigationView::Entry::Kind::Header;
    section.label = QStringLiteral("Later");
    nav.setEntries({section});
    QCOMPARE(nav.entryCount(), 1);
    QCOMPARE(nav.selectedIndex(), -1); // the selection no longer exists

    nav.clearEntries();
    QCOMPARE(nav.entryCount(), 0);
    QCOMPARE(nav.itemCount(), 0);
    QCOMPARE(nav.selectedIndex(), -1);
    QCOMPARE(nav.scrollOffset(), 0.0);
}

void TestNavigationView::rendersWithoutCrashing()
{
    md::NavigationView nav;
    nav.setHeaderTitle(QStringLiteral("Periodicity"));
    nav.setHeaderSubtitle(QStringLiteral("Material 3"));
    nav.addSection(QStringLiteral("Browse"));
    nav.addItem(QStringLiteral("Inbox"), QIcon(), QStringLiteral("3"));
    nav.addItem(QStringLiteral("Starred"));
    nav.addDivider();
    nav.addSection(QStringLiteral("Settings"));
    nav.addItem(QStringLiteral("Preferences"));
    nav.setSelectedIndex(1);
    nav.resize(320, 400);
    nav.show();
    QTest::qWait(10);

    const QPixmap rendered = nav.grab();
    QVERIFY(!rendered.isNull());
    QCOMPARE(rendered.size(), nav.size());
}

QTEST_MAIN(TestNavigationView)
#include "tst_navigation_view.moc"
