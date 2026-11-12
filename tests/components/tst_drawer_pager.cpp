// Unit tests for the Drawer, Pager and Page components (framework.md §14).
//
// The suite is offscreen and deterministic, exactly like the other component
// suites: the application theme is pinned to the baseline light scheme, the
// hosts are sized explicitly, and pointer and key input is delivered with
// QApplication::sendEvent rather than through a window system. The one thing
// that would otherwise be timing-dependent — a slide or a snap — is ended with
// finishAnimation(), so every geometry assertion runs against a settled layout.
//
// The assertions target the contract, not the pixels: a drawer opens onto the
// window and dismisses from the scrim or Escape, a pager turns a drag into a
// page change only past its threshold, and a page keeps the body between the
// header and the footer and clamps its scroll to the content it actually has.

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QRectF>
#include <QSignalSpy>
#include <QSizeF>
#include <QString>
#include <QTest>
#include <QWheelEvent>
#include <QWidget>

#include <md/components/drawer.h>
#include <md/components/page.h>
#include <md/components/pager.h>
#include <md/theme/theme.h>
#include <md/view/view.h>

namespace {

// A View with no children and a fixed preferred size: enough to stand in for a
// page, a body or a header action without pulling another component in.
class SolidView : public md::View
{
public:
    explicit SolidView(const QColor& colour = QColor(0x7F, 0xA6, 0xD9),
                       const QSizeF& preferred = QSizeF(120.0, 80.0),
                       QWidget* parent = nullptr)
        : md::View(parent)
        , colour_(colour)
        , preferred_(preferred)
    {
        setBackgroundColour(QColor(Qt::transparent));
    }

    void setPreferredSize(const QSizeF& size)
    {
        preferred_ = size;
        updateGeometry();
    }

protected:
    void measureOverride(md::MeasureSpec width, md::MeasureSpec height) override
    {
        setMeasuredSize(QSizeF(width.resolve(preferred_.width()),
                               height.resolve(preferred_.height())));
    }

    void paintContent(QPainter& painter, const QRectF& content) override
    {
        painter.fillRect(content, colour_);
    }

private:
    QColor colour_;
    QSizeF preferred_;
};

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

void sendWheel(QWidget& widget, const QPointF& position, int angleY)
{
    const QPoint global = widget.mapToGlobal(position.toPoint());
    QWheelEvent event(position, QPointF(global), QPoint(), QPoint(0, angleY),
                      Qt::MouseButtons(Qt::NoButton), Qt::KeyboardModifiers(Qt::NoModifier),
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(&widget, &event);
}

} // namespace

class TestDrawerPager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void drawerOpensAndSettles();
    void drawerScrimDismiss();
    void drawerEscapeCloses();
    void drawerItemClickActivation();
    void drawerKeyboardSelection();
    void drawerEdgeAndWidth();
    void drawerStandardMode();
    void drawerItemRectHit();

    void pagerNavigation();
    void pagerLoopAndBounds();
    void pagerDragBeyondThreshold();
    void pagerDragSnapsBack();
    void pagerIndicatorClick();
    void pagerReducedMotion();

    void pageGeometry();
    void pageTitleElide();
    void pageBackButton();
    void pageHeaderActions();
    void pageScrollClamp();
    void pageWheelAndKeys();
    void pageContentReplacement();

    void componentsRender();
};

void TestDrawerPager::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestDrawerPager::init()
{
    // A reduced-motion test installs a different application theme; every test
    // starts from the baseline one so an ordering change cannot leak it.
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

// ---------------------------------------------------------------------------
// Drawer
// ---------------------------------------------------------------------------

void TestDrawerPager::drawerOpensAndSettles()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::Drawer drawer(&host);
    drawer.setWidthDp(320.0);
    drawer.setHeader(QStringLiteral("Mail"), QStringLiteral("you@example.com"));
    drawer.addSection(QStringLiteral("Inbox"));
    drawer.addItem(QStringLiteral("Primary"));
    drawer.addItem(QStringLiteral("Social"), QIcon(), QStringLiteral("3"));

    QCOMPARE(drawer.itemCount(), 2);
    QCOMPARE(drawer.mode(), md::Drawer::Mode::Modal);
    QCOMPARE(drawer.edge(), md::Drawer::Edge::Start);
    QVERIFY(!drawer.isOpen());
    QVERIFY(!drawer.isAnimating());

    QSignalSpy openedSpy(&drawer, &md::Drawer::opened);
    QSignalSpy closedSpy(&drawer, &md::Drawer::closed);

    drawer.open();

    QVERIFY(drawer.isOpen());
    QVERIFY(drawer.isVisible());
    QCOMPARE(openedSpy.count(), 1);
    QVERIFY(drawer.isAnimating());

    // The panel starts off the leading edge and slides in.
    QVERIFY(drawer.panelRect().left() < 0.0);

    drawer.finishAnimation();

    QVERIFY(!drawer.isAnimating());
    QCOMPARE(drawer.animationProgress(), 1.0);
    QCOMPARE(drawer.panelRect(), QRectF(0.0, 0.0, 320.0, 600.0));
    QCOMPARE(drawer.scrimRect(), QRectF(0.0, 0.0, 800.0, 600.0));
    QVERIFY(!drawer.headerRect().isEmpty());
    QCOMPARE(drawer.headerRect().top(), 0.0);
    QCOMPARE(drawer.headerRect().width(), 320.0);

    drawer.close();

    // close() records the intent immediately; the widget stays until the slide
    // out has finished.
    QVERIFY(!drawer.isOpen());
    QCOMPARE(closedSpy.count(), 0);

    drawer.finishAnimation();

    QCOMPARE(closedSpy.count(), 1);
    QVERIFY(!drawer.isVisible());
    QCOMPARE(drawer.animationProgress(), 0.0);
}

void TestDrawerPager::drawerScrimDismiss()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::Drawer drawer(&host);
    drawer.setWidthDp(300.0);
    drawer.addItem(QStringLiteral("One"));
    drawer.setScrimDismissible(true);
    QVERIFY(drawer.isScrimDismissible());

    drawer.open();
    drawer.finishAnimation();

    QSignalSpy closedSpy(&drawer, &md::Drawer::closed);

    // Inside the panel is not the scrim.
    clickAt(drawer, drawer.panelRect().center());
    QVERIFY(drawer.isOpen());
    QCOMPARE(closedSpy.count(), 0);

    // Outside it is.
    clickAt(drawer, QPointF(700.0, 300.0));
    QVERIFY(!drawer.isOpen());
    drawer.finishAnimation();
    QCOMPARE(closedSpy.count(), 1);

    // A non-dismissible scrim swallows the same press.
    drawer.open();
    drawer.finishAnimation();
    drawer.setScrimDismissible(false);
    QVERIFY(!drawer.isScrimDismissible());

    clickAt(drawer, QPointF(700.0, 300.0));

    QVERIFY(drawer.isOpen());
    QCOMPARE(closedSpy.count(), 1);

    // The panel is still interactive while the scrim is locked.
    clickAt(drawer, drawer.itemRect(0).center());
    QVERIFY(!drawer.isOpen());
}

void TestDrawerPager::drawerEscapeCloses()
{
    QWidget host;
    host.resize(640, 480);
    host.show();

    md::Drawer drawer(&host);
    drawer.addItem(QStringLiteral("One"));
    drawer.open();
    drawer.finishAnimation();

    QSignalSpy closedSpy(&drawer, &md::Drawer::closed);

    QTest::keyClick(&drawer, Qt::Key_Escape);

    QVERIFY(!drawer.isOpen());
    drawer.finishAnimation();
    QCOMPARE(closedSpy.count(), 1);
    QVERIFY(!drawer.isVisible());
}

void TestDrawerPager::drawerItemClickActivation()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::Drawer drawer(&host);
    drawer.setWidthDp(300.0);
    drawer.setHeader(QStringLiteral("Mail"));
    drawer.addSection(QStringLiteral("All"));
    drawer.addItem(QStringLiteral("Inbox"));
    drawer.addItem(QStringLiteral("Sent"));
    drawer.addItem(QStringLiteral("Drafts"));

    drawer.open();
    drawer.finishAnimation();

    QSignalSpy selectionSpy(&drawer, &md::Drawer::selectionChanged);
    QSignalSpy activatedSpy(&drawer, &md::Drawer::itemActivated);

    const QRectF row = drawer.itemRect(1);
    QVERIFY(!row.isEmpty());
    QVERIFY(row.top() >= drawer.headerRect().bottom());

    clickAt(drawer, row.center());

    QCOMPARE(selectionSpy.count(), 1);
    QCOMPARE(selectionSpy.at(0).at(0).toInt(), 1);
    QCOMPARE(activatedSpy.count(), 1);
    QCOMPARE(activatedSpy.at(0).at(0).toInt(), 1);
    QCOMPARE(activatedSpy.at(0).at(1).toString(), QStringLiteral("Sent"));
    QCOMPARE(drawer.selectedIndex(), 1);

    // A modal drawer closes itself once the item has been reported.
    QVERIFY(!drawer.isOpen());
}

void TestDrawerPager::drawerKeyboardSelection()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::Drawer drawer(&host);
    drawer.setWidthDp(300.0);
    drawer.addItem(QStringLiteral("Inbox"));
    drawer.addItem(QStringLiteral("Sent"));
    drawer.addItem(QStringLiteral("Drafts"));
    drawer.open();
    drawer.finishAnimation();

    QSignalSpy activatedSpy(&drawer, &md::Drawer::itemActivated);
    drawer.setSelectedIndex(0);
    QCOMPARE(drawer.selectedIndex(), 0);

    QTest::keyClick(&drawer, Qt::Key_Down);
    QCOMPARE(drawer.selectedIndex(), 1);

    QTest::keyClick(&drawer, Qt::Key_Down);
    QCOMPARE(drawer.selectedIndex(), 2);

    // The last row is the end of the list.
    QTest::keyClick(&drawer, Qt::Key_Down);
    QCOMPARE(drawer.selectedIndex(), 2);

    QTest::keyClick(&drawer, Qt::Key_Up);
    QCOMPARE(drawer.selectedIndex(), 1);

    QTest::keyClick(&drawer, Qt::Key_Home);
    QCOMPARE(drawer.selectedIndex(), 0);

    QTest::keyClick(&drawer, Qt::Key_End);
    QCOMPARE(drawer.selectedIndex(), 2);

    QTest::keyClick(&drawer, Qt::Key_Return);
    QCOMPARE(activatedSpy.count(), 1);
    QCOMPARE(activatedSpy.at(0).at(0).toInt(), 2);
    QCOMPARE(activatedSpy.at(0).at(1).toString(), QStringLiteral("Drafts"));

    // Space activates too; reopen because the activation closed the drawer.
    drawer.open();
    drawer.finishAnimation();
    drawer.setSelectedIndex(0);
    QTest::keyClick(&drawer, Qt::Key_Space);

    QCOMPARE(activatedSpy.count(), 2);
    QCOMPARE(activatedSpy.at(1).at(0).toInt(), 0);
}

void TestDrawerPager::drawerEdgeAndWidth()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::Drawer drawer(&host);
    drawer.setWidthDp(280.0);
    drawer.addItem(QStringLiteral("One"));

    drawer.setEdge(md::Drawer::Edge::End);
    QCOMPARE(drawer.edge(), md::Drawer::Edge::End);

    drawer.open();
    drawer.finishAnimation();

    QCOMPARE(drawer.panelRect().left(), 520.0);
    QCOMPARE(drawer.panelRect().right(), 800.0);

    drawer.close();
    drawer.finishAnimation();

    drawer.setEdge(md::Drawer::Edge::Start);
    drawer.open();
    drawer.finishAnimation();

    QCOMPARE(drawer.panelRect().left(), 0.0);
    QCOMPARE(drawer.panelRect().width(), 280.0);

    // The preferred width is clamped to the window.
    QWidget narrow;
    narrow.resize(240, 400);
    narrow.show();

    md::Drawer clamped(&narrow);
    clamped.setWidthDp(360.0);
    clamped.addItem(QStringLiteral("One"));
    clamped.open();
    clamped.finishAnimation();

    QCOMPARE(clamped.panelRect().width(), 240.0);
    QCOMPARE(clamped.panelRect().left(), 0.0);
}

void TestDrawerPager::drawerStandardMode()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::Drawer drawer(&host);
    drawer.setMode(md::Drawer::Mode::Standard);
    drawer.setWidthDp(280.0);
    drawer.addItem(QStringLiteral("One"));
    drawer.arrange(QRect(0, 0, 280, 600));
    drawer.show();

    QVERIFY(!drawer.isOpen());

    drawer.open();

    QVERIFY(drawer.isOpen());
    QVERIFY(drawer.isVisible());
    QVERIFY(!drawer.isAnimating());
    QVERIFY(drawer.scrimRect().isEmpty());
    QCOMPARE(drawer.panelRect(), QRectF(0.0, 0.0, 280.0, 600.0));
    QCOMPARE(drawer.animationProgress(), 1.0);

    drawer.close();

    QVERIFY(!drawer.isOpen());
    QVERIFY(!drawer.isVisible());
    QCOMPARE(drawer.animationProgress(), 0.0);
}

void TestDrawerPager::drawerItemRectHit()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::Drawer drawer(&host);
    drawer.setWidthDp(320.0);
    drawer.setHeader(QStringLiteral("Mail"));
    drawer.addSection(QStringLiteral("All"));
    drawer.addItem(QStringLiteral("Inbox"));
    drawer.addItem(QStringLiteral("Sent"), QIcon(), QStringLiteral("12"));
    drawer.open();
    drawer.finishAnimation();

    QCOMPARE(drawer.itemCount(), 2);

    for (int index = 0; index < drawer.itemCount(); ++index) {
        const QRectF row = drawer.itemRect(index);
        QVERIFY(!row.isEmpty());
        QVERIFY(drawer.panelRect().contains(row));
        QVERIFY(row.contains(row.center()));
    }

    // The section sits above both rows, and the rows do not overlap.
    QVERIFY(drawer.itemRect(0).bottom() <= drawer.itemRect(1).top());
    QVERIFY(drawer.itemRect(0).top() >= drawer.headerRect().bottom());

    QSignalSpy activatedSpy(&drawer, &md::Drawer::itemActivated);
    clickAt(drawer, drawer.itemRect(0).center());

    QCOMPARE(activatedSpy.count(), 1);
    QCOMPARE(activatedSpy.at(0).at(0).toInt(), 0);

    // An index outside the list has no geometry and no effect.
    QVERIFY(drawer.itemRect(-1).isEmpty());
    QVERIFY(drawer.itemRect(99).isEmpty());
    drawer.setSelectedIndex(99);
    QCOMPARE(drawer.selectedIndex(), -1);
}

// ---------------------------------------------------------------------------
// Pager
// ---------------------------------------------------------------------------

void TestDrawerPager::pagerNavigation()
{
    md::Pager pager;
    SolidView first;
    SolidView second;
    SolidView third;

    pager.arrange(QRect(0, 0, 600, 400));
    pager.show();
    pager.setPages({&first, &second, &third});

    QCOMPARE(pager.pageCount(), 3);
    QCOMPARE(pager.pages().size(), 3);
    QCOMPARE(pager.currentIndex(), 0);
    QCOMPARE(pager.dragOffset(), 0.0);

    // Only the current page is visible at rest.
    QVERIFY(!first.isHidden());
    QVERIFY(second.isHidden());
    QVERIFY(third.isHidden());
    QCOMPARE(pager.pageRect(0), pager.pageArea());
    QVERIFY(pager.pageRect(0).contains(pager.pageRect(0).center()));

    QSignalSpy changedSpy(&pager, &md::Pager::pageChanged);

    pager.next();

    QCOMPARE(pager.currentIndex(), 1);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy.at(0).at(0).toInt(), 1);
    QVERIFY(pager.isAnimating());
    QVERIFY(!second.isHidden());

    pager.finishAnimation();

    QVERIFY(!pager.isAnimating());
    QCOMPARE(pager.dragOffset(), 0.0);
    QCOMPARE(pager.pageRect(1), pager.pageArea());
    QVERIFY(first.isHidden());
    QVERIFY(!second.isHidden());

    pager.next();

    QCOMPARE(pager.currentIndex(), 2);
    pager.finishAnimation();
    QCOMPARE(pager.pageRect(2), pager.pageArea());

    pager.previous();

    QCOMPARE(pager.currentIndex(), 1);
    pager.finishAnimation();
    QCOMPARE(pager.pageRect(1), pager.pageArea());

    // A jump of more than one page snaps instead of sliding.
    pager.setCurrentIndex(0, false);
    QCOMPARE(pager.currentIndex(), 0);
    pager.setCurrentIndex(2, true);
    QCOMPARE(pager.currentIndex(), 2);
    QVERIFY(!pager.isAnimating());
    QCOMPARE(pager.pageRect(2), pager.pageArea());
}

void TestDrawerPager::pagerLoopAndBounds()
{
    md::Pager pager;
    SolidView first;
    SolidView second;
    SolidView third;

    pager.arrange(QRect(0, 0, 480, 320));
    pager.show();
    pager.setPages({&first, &second, &third});

    QVERIFY(!pager.loop());

    pager.previous();
    QCOMPARE(pager.currentIndex(), 0);

    pager.setCurrentIndex(2, false);
    QCOMPARE(pager.currentIndex(), 2);

    pager.next();
    QCOMPARE(pager.currentIndex(), 2);
    QVERIFY(!pager.isAnimating());

    pager.setLoop(true);
    QVERIFY(pager.loop());

    QSignalSpy changedSpy(&pager, &md::Pager::pageChanged);

    pager.next();
    QCOMPARE(pager.currentIndex(), 0);
    QCOMPARE(changedSpy.count(), 1);

    pager.finishAnimation();
    QCOMPARE(pager.pageRect(0), pager.pageArea());

    pager.previous();
    QCOMPARE(pager.currentIndex(), 2);
    pager.finishAnimation();
    QCOMPARE(pager.pageRect(2), pager.pageArea());

    // The keyboard drives the same transitions.
    QTest::keyClick(&pager, Qt::Key_Home);
    QCOMPARE(pager.currentIndex(), 0);
    pager.finishAnimation();

    QTest::keyClick(&pager, Qt::Key_Right);
    QCOMPARE(pager.currentIndex(), 1);
    pager.finishAnimation();

    QTest::keyClick(&pager, Qt::Key_Left);
    QCOMPARE(pager.currentIndex(), 0);
    pager.finishAnimation();

    QTest::keyClick(&pager, Qt::Key_End);
    QCOMPARE(pager.currentIndex(), 2);
    QCOMPARE(pager.pageRect(2), pager.pageArea());
}

void TestDrawerPager::pagerDragBeyondThreshold()
{
    md::Pager pager;
    SolidView first;
    SolidView second;

    pager.arrange(QRect(0, 0, 600, 400));
    pager.show();
    pager.setPages({&first, &second});

    QSignalSpy dragSpy(&pager, &md::Pager::dragStateChanged);
    QSignalSpy changedSpy(&pager, &md::Pager::pageChanged);

    const qreal y = pager.pageArea().center().y();
    const QPointF from(480.0, y);
    const QPointF to(200.0, y);

    sendMouse(pager, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    sendMouse(pager, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);

    QVERIFY(pager.isDragging());
    QCOMPARE(dragSpy.count(), 1);
    QCOMPARE(dragSpy.at(0).at(0).toBool(), true);

    // The pages follow the pointer by the same number of pixels.
    QVERIFY(pager.dragOffset() < -100.0);
    QVERIFY(pager.pageRect(0).left() < pager.pageArea().left());
    QVERIFY(!second.isHidden());

    sendMouse(pager, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);

    QVERIFY(!pager.isDragging());
    QCOMPARE(dragSpy.count(), 2);
    QCOMPARE(dragSpy.at(1).at(0).toBool(), false);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy.at(0).at(0).toInt(), 1);
    QCOMPARE(pager.currentIndex(), 1);

    pager.finishAnimation();

    QCOMPARE(pager.dragOffset(), 0.0);
    QCOMPARE(pager.pageRect(1), pager.pageArea());
    QVERIFY(first.isHidden());
    QVERIFY(!second.isHidden());
}

void TestDrawerPager::pagerDragSnapsBack()
{
    md::Pager pager;
    SolidView first;
    SolidView second;

    pager.arrange(QRect(0, 0, 600, 400));
    pager.show();
    pager.setPages({&first, &second});

    QSignalSpy changedSpy(&pager, &md::Pager::pageChanged);

    const qreal y = pager.pageArea().center().y();

    sendMouse(pager, QEvent::MouseButtonPress, QPointF(480.0, y), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(pager, QEvent::MouseMove, QPointF(430.0, y), Qt::NoButton, Qt::LeftButton);

    QVERIFY(pager.isDragging());
    QVERIFY(pager.dragOffset() < 0.0);
    QVERIFY(pager.dragOffset() > -pager.pageArea().width() / 3.0);

    sendMouse(pager, QEvent::MouseButtonRelease, QPointF(430.0, y), Qt::LeftButton,
              Qt::NoButton);

    // Below the threshold the release snaps back to the page it came from.
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(pager.currentIndex(), 0);

    pager.finishAnimation();

    QCOMPARE(pager.dragOffset(), 0.0);
    QCOMPARE(pager.pageRect(0), pager.pageArea());
    QVERIFY(second.isHidden());

    // The first page has nothing before it, so a backwards drag is damped and
    // never changes the page.
    sendMouse(pager, QEvent::MouseButtonPress, QPointF(100.0, y), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(pager, QEvent::MouseMove, QPointF(520.0, y), Qt::NoButton, Qt::LeftButton);

    QVERIFY(pager.isDragging());
    QVERIFY(pager.dragOffset() > 0.0);
    QVERIFY(pager.dragOffset() < pager.pageArea().width() / 2.0);

    sendMouse(pager, QEvent::MouseButtonRelease, QPointF(520.0, y), Qt::LeftButton,
              Qt::NoButton);

    QCOMPARE(pager.currentIndex(), 0);
    pager.finishAnimation();
    QCOMPARE(pager.dragOffset(), 0.0);
}

void TestDrawerPager::pagerIndicatorClick()
{
    md::Pager pager;
    SolidView first;
    SolidView second;
    SolidView third;

    pager.arrange(QRect(0, 0, 600, 400));
    pager.show();
    pager.setPages({&first, &second, &third});

    QVERIFY(pager.isIndicatorVisible());
    QVERIFY(!pager.indicatorBarRect().isEmpty());

    for (int index = 0; index < pager.pageCount(); ++index) {
        const QRectF dot = pager.indicatorRect(index);
        QVERIFY(!dot.isEmpty());
        QVERIFY(pager.indicatorBarRect().contains(dot));
    }

    // The active dot is the wide one.
    QVERIFY(pager.indicatorRect(0).width() > pager.indicatorRect(1).width());

    QSignalSpy changedSpy(&pager, &md::Pager::pageChanged);
    clickAt(pager, pager.indicatorRect(2).center());

    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(pager.currentIndex(), 2);
    QCOMPARE(pager.pageRect(2), pager.pageArea());

    clickAt(pager, pager.indicatorRect(1).center());
    QCOMPARE(pager.currentIndex(), 1);
    pager.finishAnimation();
    QCOMPARE(pager.pageRect(1), pager.pageArea());

    pager.setIndicatorVisible(false);
    QVERIFY(!pager.isIndicatorVisible());
    QVERIFY(pager.indicatorBarRect().isEmpty());
    QVERIFY(pager.indicatorRect(1).isEmpty());
    QCOMPARE(pager.pageRect(1), pager.pageArea());
}

void TestDrawerPager::pagerReducedMotion()
{
    md::Theme theme = md::Theme::baselineLight();
    md::MotionScheme motion = theme.motion();
    motion.reducedMotion = true;
    md::Theme::setApplicationTheme(theme.withMotion(motion));

    md::Pager pager;
    SolidView first;
    SolidView second;
    SolidView third;

    pager.arrange(QRect(0, 0, 600, 400));
    pager.show();
    pager.setPages({&first, &second, &third});

    pager.setCurrentIndex(1);

    QCOMPARE(pager.currentIndex(), 1);
    QVERIFY(!pager.isAnimating());
    QCOMPARE(pager.dragOffset(), 0.0);
    QCOMPARE(pager.pageRect(1), pager.pageArea());

    pager.next();

    QCOMPARE(pager.currentIndex(), 2);
    QVERIFY(!pager.isAnimating());
    QCOMPARE(pager.pageRect(2), pager.pageArea());

    // A reduced-motion drawer snaps too, without a running transition.
    QWidget host;
    host.resize(640, 480);
    host.show();

    md::Drawer drawer(&host);
    drawer.addItem(QStringLiteral("One"));
    drawer.open();

    QVERIFY(!drawer.isAnimating());
    QCOMPARE(drawer.animationProgress(), 1.0);
    QCOMPARE(drawer.panelRect(), QRectF(0.0, 0.0, 360.0, 480.0));
}

// ---------------------------------------------------------------------------
// Page
// ---------------------------------------------------------------------------

void TestDrawerPager::pageGeometry()
{
    md::Page page;
    page.arrange(QRect(0, 0, 480, 600));
    page.show();

    QVERIFY(page.isHeaderVisible());
    QCOMPARE(page.headerRect(), QRectF(0.0, 0.0, 480.0, 64.0));
    QCOMPARE(page.bodyRect().top(), 64.0);
    QCOMPARE(page.bodyRect().bottom(), 600.0);
    QVERIFY(page.footerRect().isEmpty());

    page.setTitle(QStringLiteral("Inbox"));
    page.setSubtitle(QStringLiteral("3 unread"));
    QCOMPARE(page.title(), QStringLiteral("Inbox"));
    QCOMPARE(page.subtitle(), QStringLiteral("3 unread"));

    SolidView footer(QColor(0x90, 0x90, 0x90), QSizeF(100.0, 56.0));
    page.setFooter(&footer);

    QCOMPARE(page.footer(), &footer);
    QCOMPARE(page.footerRect().height(), 56.0);
    QCOMPARE(page.footerRect().bottom(), 600.0);
    QCOMPARE(page.footerRect().width(), 480.0);
    QCOMPARE(page.bodyRect().bottom(), page.footerRect().top());
    QCOMPARE(page.bodyRect().top(), 64.0);

    // The footer is a real child, arranged inside the page.
    QCOMPARE(footer.geometry(), QRect(0, 544, 480, 56));

    page.setHeaderVisible(false);
    QVERIFY(!page.isHeaderVisible());
    QVERIFY(page.headerRect().isEmpty());
    QCOMPARE(page.bodyRect().top(), 0.0);
    QCOMPARE(page.bodyRect().bottom(), page.footerRect().top());

    page.setHeaderVisible(true);
    page.setFooter(nullptr);
    QVERIFY(page.footer() == nullptr);
    QVERIFY(page.footerRect().isEmpty());
}

void TestDrawerPager::pageTitleElide()
{
    md::Page page;
    page.arrange(QRect(0, 0, 480, 600));
    page.show();

    page.setTitle(QString(160, QLatin1Char('W')));
    const QRectF wide = page.titleRect();

    QVERIFY(!wide.isEmpty());
    QVERIFY(wide.width() > 0.0);
    QVERIFY(wide.right() <= page.headerRect().right());
    QVERIFY(wide.left() >= page.headerRect().left());

    // A back button moves the title in.
    page.setBackVisible(true);
    const QRectF shifted = page.titleRect();
    QVERIFY(shifted.left() > wide.left());
    QVERIFY(shifted.left() >= page.backButtonRect().right());

    // A header action takes its width out of the title.
    SolidView action(QColor(0x40, 0x80, 0xC0), QSizeF(40.0, 40.0));
    page.addHeaderAction(&action);
    const QRectF withAction = page.titleRect();

    QVERIFY(withAction.width() < shifted.width());
    QVERIFY(withAction.right() <= page.headerRect().right() - 40.0 + 0.5);
}

void TestDrawerPager::pageBackButton()
{
    md::Page page;
    page.arrange(QRect(0, 0, 480, 600));
    page.show();

    QVERIFY(!page.isBackVisible());
    QVERIFY(page.backButtonRect().isEmpty());

    page.setBackVisible(true);
    QVERIFY(page.isBackVisible());

    const QRectF button = page.backButtonRect();
    QVERIFY(!button.isEmpty());
    QVERIFY(page.headerRect().contains(button));
    QCOMPARE(button.width(), 48.0);

    QSignalSpy backSpy(&page, &md::Page::backRequested);

    // A press outside the affordance is not a back request.
    clickAt(page, QPointF(300.0, 30.0));
    QCOMPARE(backSpy.count(), 0);

    clickAt(page, button.center());
    QCOMPARE(backSpy.count(), 1);

    page.setBackVisible(false);
    QVERIFY(page.backButtonRect().isEmpty());
}

void TestDrawerPager::pageHeaderActions()
{
    md::Page page;
    page.arrange(QRect(0, 0, 480, 600));
    page.show();
    page.setTitle(QStringLiteral("Inbox"));

    SolidView first(QColor(0x20, 0x60, 0xA0), QSizeF(48.0, 40.0));
    SolidView second(QColor(0x20, 0xA0, 0x60), QSizeF(36.0, 40.0));

    page.addHeaderAction(&first);
    page.addHeaderAction(&second);

    QCOMPARE(page.headerActions().size(), 2);
    QCOMPARE(page.headerActions().at(0), &first);

    QVERIFY(page.headerRect().contains(first.geometry()));
    QVERIFY(page.headerRect().contains(second.geometry()));
    QCOMPARE(first.geometry().height(), 40);
    QCOMPARE(second.geometry().height(), 40);

    // The first action is the right-most one; the second sits to its left.
    QVERIFY(first.geometry().left() > second.geometry().left());
    QVERIFY(second.geometry().right() < first.geometry().left());

    page.clearHeaderActions();
    QVERIFY(page.headerActions().isEmpty());
}

void TestDrawerPager::pageScrollClamp()
{
    md::Page page;
    page.arrange(QRect(0, 0, 400, 600));
    page.show();

    SolidView content(QColor(0x80, 0x80, 0x80), QSizeF(200.0, 1200.0));
    page.setContent(&content);

    QCOMPARE(page.content(), &content);
    QCOMPARE(page.contentHeight(), 1200.0);
    QCOMPARE(page.scrollOffset(), 0.0);

    const qreal body = page.bodyRect().height();
    QVERIFY(body > 0.0);

    page.setScrollOffset(5000.0);
    QCOMPARE(page.scrollOffset(), 1200.0 - body);

    page.setScrollOffset(-42.0);
    QCOMPARE(page.scrollOffset(), 0.0);

    // The body View really is displaced inside its viewport.
    page.setScrollOffset(120.0);
    QCOMPARE(page.scrollOffset(), 120.0);
    QCOMPARE(content.geometry().top(), -120);
    QCOMPARE(content.geometry().height(), 1200);

    // A non-scrollable page pins the offset and ignores further requests.
    page.setScrollable(false);
    QVERIFY(!page.isScrollable());
    QCOMPARE(page.scrollOffset(), 0.0);

    page.setScrollOffset(600.0);
    QCOMPARE(page.scrollOffset(), 0.0);

    page.setScrollable(true);
    QVERIFY(page.isScrollable());

    // Content shorter than the body never scrolls.
    SolidView small(QColor(0x80, 0x80, 0x80), QSizeF(200.0, 100.0));
    page.setContent(&small);
    QCOMPARE(page.contentHeight(), 100.0);
    page.setScrollOffset(300.0);
    QCOMPARE(page.scrollOffset(), 0.0);

    page.setContent(nullptr);
    QVERIFY(page.content() == nullptr);
    QCOMPARE(page.contentHeight(), 0.0);
}

void TestDrawerPager::pageWheelAndKeys()
{
    md::Page page;
    page.arrange(QRect(0, 0, 400, 600));
    page.show();

    SolidView content(QColor(0x80, 0x80, 0x80), QSizeF(200.0, 1600.0));
    page.setContent(&content);

    const qreal maximum = page.contentHeight() - page.bodyRect().height();
    QVERIFY(maximum > 0.0);

    sendWheel(page, QPointF(200.0, 300.0), -120);
    QVERIFY(page.scrollOffset() > 0.0);
    const qreal afterWheel = page.scrollOffset();

    sendWheel(page, QPointF(200.0, 300.0), 120);
    QCOMPARE(page.scrollOffset(), 0.0);
    QVERIFY(afterWheel > 0.0);

    QTest::keyClick(&page, Qt::Key_PageDown);
    const qreal afterPageDown = page.scrollOffset();
    QVERIFY(afterPageDown > 0.0);
    QVERIFY(afterPageDown < maximum);

    QTest::keyClick(&page, Qt::Key_End);
    QCOMPARE(page.scrollOffset(), maximum);

    QTest::keyClick(&page, Qt::Key_PageUp);
    QVERIFY(page.scrollOffset() < maximum);

    QTest::keyClick(&page, Qt::Key_Home);
    QCOMPARE(page.scrollOffset(), 0.0);
}

void TestDrawerPager::pageContentReplacement()
{
    md::Page page;
    page.arrange(QRect(0, 0, 400, 600));
    page.show();

    SolidView first(QColor(0xD0, 0x40, 0x40), QSizeF(200.0, 900.0));
    SolidView second(QColor(0x40, 0xD0, 0x40), QSizeF(200.0, 400.0));

    page.setContent(&first);
    QCOMPARE(page.content(), &first);
    QVERIFY(page.contentHeight() > 0.0);

    page.setScrollOffset(100.0);
    QVERIFY(page.scrollOffset() > 0.0);

    page.setContent(&second);

    QCOMPARE(page.content(), &second);
    QVERIFY(first.parentWidget() == nullptr);
    QCOMPARE(page.contentHeight(), 400.0);
    QCOMPARE(page.scrollOffset(), 0.0);
    QCOMPARE(second.geometry().height(), 400);
}

// ---------------------------------------------------------------------------
// Paint smoke
// ---------------------------------------------------------------------------

void TestDrawerPager::componentsRender()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::Drawer drawer(&host);
    drawer.setWidthDp(320.0);
    drawer.setHeader(QStringLiteral("Mail"), QStringLiteral("you@example.com"));
    drawer.addSection(QStringLiteral("Inbox"));
    drawer.addItem(QStringLiteral("Primary"));
    drawer.addItem(QStringLiteral("Social"), QIcon(), QStringLiteral("3"));
    drawer.open();
    drawer.finishAnimation();

    QVERIFY(!drawer.grab().isNull());

    drawer.close();
    drawer.finishAnimation();

    md::Pager pager;
    SolidView first(QColor(0xD0, 0x60, 0x60));
    SolidView second(QColor(0x60, 0xD0, 0x60));

    pager.arrange(QRect(0, 0, 480, 320));
    pager.show();
    pager.setPages({&first, &second});

    QVERIFY(!pager.grab().isNull());

    pager.next();
    pager.finishAnimation();

    QVERIFY(!pager.grab().isNull());

    md::Page page;
    page.arrange(QRect(0, 0, 480, 600));
    page.show();
    page.setTitle(QStringLiteral("Inbox"));
    page.setSubtitle(QStringLiteral("3 unread"));
    page.setBackVisible(true);

    SolidView content(QColor(0xF0, 0xF0, 0xF0), QSizeF(200.0, 900.0));
    SolidView footer(QColor(0xE0, 0xE0, 0xE0), QSizeF(200.0, 56.0));

    page.setContent(&content);
    page.setFooter(&footer);
    page.setScrollOffset(120.0);

    const QImage image = page.grab().toImage();
    QVERIFY(!image.isNull());
    QVERIFY(image.width() > 0);
    QVERIFY(image.height() > 0);
}

QTEST_MAIN(TestDrawerPager)
#include "tst_drawer_pager.moc"
