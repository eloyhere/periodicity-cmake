// Tests for Dropdown (anchored popup layer) and Modal (scrim and layer order).

#include <md/components/dropdown.h>
#include <md/components/modal.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QGraphicsEffect>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

namespace {

class Leaf final : public md::View
{
public:
    explicit Leaf(QSizeF size)
        : size_(size)
    {
    }

protected:
    void measureOverride(md::MeasureSpec width, md::MeasureSpec height) override
    {
        setMeasuredSize(QSizeF(width.resolve(size_.width()), height.resolve(size_.height())));
    }

    void paintContent(QPainter&, const QRectF&) override {}

private:
    QSizeF size_;
};

QPoint globalOf(const QWidget& widget, const QPoint& local)
{
    return widget.mapToGlobal(local);
}

} // namespace

class TestDropdownModal : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void dropdownOpensBelowTheAnchor();
    void dropdownFlipsAboveNearTheBottom();
    void dropdownClampsToTheScreen();
    void dropdownWithSidePlacements();
    void dropdownClosesOnOutsideClick();
    void dropdownClosesOnEscape();
    void dropdownFollowsTheAnchor();
    void dropdownInstancesAreIndependent();

    void modalCoversTheWindow();
    void modalClickDismisses();
    void modalKeepsItsLevel();
    void modalIsAboveADropdown();

    void modalCardAnimatesInAndOut();
    void modalReducedMotionSnaps();
    void modalTrapsAndRestoresFocus();
    void modalHideStopsAnimation();
};

namespace {

QWidget* host = nullptr;

} // namespace

void TestDropdownModal::init()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());

    host = new QWidget;
    host->resize(800, 600);
    host->show();
    QTest::qWait(10);
}

void TestDropdownModal::cleanup()
{
    delete host;
    host = nullptr;
}

void TestDropdownModal::dropdownOpensBelowTheAnchor()
{
    QWidget anchor(host);
    anchor.setGeometry(100, 100, 200, 40);
    anchor.show();
    md::Dropdown dropdown;

    Leaf content(QSizeF(180.0, 120.0));
    dropdown.setAnchor(&anchor);
    dropdown.setContent(&content);

    QSignalSpy opened(&dropdown, &md::Dropdown::opened);
    dropdown.open();

    QVERIFY(dropdown.isOpen());
    QCOMPARE(opened.count(), 1);
    QCOMPARE(dropdown.effectivePlacement(), md::Dropdown::Placement::Below);

    const QRectF popup = dropdown.popupGlobalRect();
    const QPoint anchorGlobal = globalOf(anchor, QPoint(0, 0));

    QCOMPARE(qRound(popup.left()), anchorGlobal.x());
    QCOMPARE(qRound(popup.width()), anchor.width());
    QCOMPARE(qRound(popup.top()), anchorGlobal.y() + anchor.height() + 4);
    QCOMPARE(content.geometry().size(), QSize(200, 120));
}

void TestDropdownModal::dropdownFlipsAboveNearTheBottom()
{
    QWidget anchor(host);
    anchor.setGeometry(100, host->height() - 40, 200, 40);
    md::Dropdown dropdown;

    Leaf content(QSizeF(180.0, 200.0));
    dropdown.setAnchor(&anchor);
    dropdown.setContent(&content);
    dropdown.open();

    QCOMPARE(dropdown.effectivePlacement(), md::Dropdown::Placement::Above);
    const QRectF popup = dropdown.popupGlobalRect();
    const QPoint anchorGlobal = globalOf(anchor, QPoint(0, 0));
    QVERIFY(popup.bottom() <= anchorGlobal.y() - 4 + 1);
}

void TestDropdownModal::dropdownClampsToTheScreen()
{
    QWidget anchor(host);
    anchor.setGeometry(host->width() - 80, 100, 60, 40);
    md::Dropdown dropdown;

    Leaf content(QSizeF(500.0, 120.0));
    dropdown.setAnchor(&anchor);
    dropdown.setContent(&content);
    dropdown.setMatchAnchorWidth(false);
    dropdown.open();

    const QRect available = anchor.screen()->availableGeometry();
    const QRectF popup = dropdown.popupGlobalRect();
    QVERIFY(popup.right() <= available.right());
    QVERIFY(popup.left() >= available.left());
}

void TestDropdownModal::dropdownWithSidePlacements()
{
    QWidget anchor(host);
    anchor.setGeometry(400, 200, 120, 40);
    md::Dropdown dropdown;

    Leaf content(QSizeF(160.0, 120.0));
    dropdown.setAnchor(&anchor);
    dropdown.setContent(&content);
    dropdown.setPlacement(md::Dropdown::Placement::Right);
    dropdown.open();

    QCOMPARE(dropdown.effectivePlacement(), md::Dropdown::Placement::Right);
    const QPoint anchorGlobal = globalOf(anchor, QPoint(0, 0));
    QCOMPARE(qRound(dropdown.popupGlobalRect().left()),
             anchorGlobal.x() + anchor.width() + 4);
}

void TestDropdownModal::dropdownClosesOnOutsideClick()
{
    QWidget anchor(host);
    anchor.setGeometry(100, 100, 200, 40);
    anchor.show();
    md::Dropdown dropdown;

    Leaf content(QSizeF(180.0, 120.0));
    dropdown.setAnchor(&anchor);
    dropdown.setContent(&content);
    dropdown.open();
    QVERIFY(dropdown.isOpen());

    QSignalSpy closed(&dropdown, &md::Dropdown::closed);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), host->mapToGlobal(QPoint(5, 5)),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(host, &press);

    QVERIFY(!dropdown.isOpen());
    QCOMPARE(closed.count(), 1);
}

void TestDropdownModal::dropdownClosesOnEscape()
{
    QWidget anchor(host);
    anchor.setGeometry(100, 100, 200, 40);
    anchor.show();
    md::Dropdown dropdown;

    Leaf content(QSizeF(180.0, 120.0));
    dropdown.setAnchor(&anchor);
    dropdown.setContent(&content);
    dropdown.open();

    QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(host, &press);
    QVERIFY(!dropdown.isOpen());
}

void TestDropdownModal::dropdownFollowsTheAnchor()
{
    QWidget anchor(host);
    anchor.setGeometry(100, 100, 200, 40);
    anchor.show();
    md::Dropdown dropdown;

    Leaf content(QSizeF(180.0, 120.0));
    dropdown.setAnchor(&anchor);
    dropdown.setContent(&content);
    dropdown.open();

    const QRectF before = dropdown.popupGlobalRect();
    anchor.move(100, 300);
    QCoreApplication::processEvents();

    const QRectF after = dropdown.popupGlobalRect();
    QCOMPARE(qRound(after.top() - before.top()), 200);
}

void TestDropdownModal::dropdownInstancesAreIndependent()
{
    QWidget firstAnchor(host);
    firstAnchor.setGeometry(40, 40, 160, 36);
    QWidget secondAnchor(host);
    secondAnchor.setGeometry(40, 200, 160, 36);

    md::Dropdown first;
    first.setAnchor(&firstAnchor);
    md::Dropdown second;
    second.setAnchor(&secondAnchor);
    Leaf firstContent(QSizeF(140.0, 100.0));
    Leaf secondContent(QSizeF(140.0, 100.0));
    first.setContent(&firstContent);
    second.setContent(&secondContent);

    first.open();
    QVERIFY(first.isOpen());
    QVERIFY(!second.isOpen());

    second.open();
    QVERIFY(second.isOpen());

    // The global rectangles belong to their own anchors.
    QCOMPARE(qRound(second.popupGlobalRect().left()),
             globalOf(secondAnchor, QPoint(0, 0)).x());
    QCOMPARE(qRound(first.popupGlobalRect().left()),
             globalOf(firstAnchor, QPoint(0, 0)).x());
}

void TestDropdownModal::modalCoversTheWindow()
{
    md::Modal modal(host);
    modal.setDim(0.4);
    modal.setLevel(md::Modal::Level::Dialog);

    QSignalSpy opened(&modal, &md::Modal::opened);
    modal.open();

    QCOMPARE(opened.count(), 1);
    QVERIFY(modal.isOpen());
    QCOMPARE(modal.geometry(), host->rect());
    QCOMPARE(modal.scrimRect(), QRectF(host->rect()));
    QCOMPARE(modal.dim(), 0.4);
}

void TestDropdownModal::modalClickDismisses()
{
    md::Modal modal(host);
    modal.open();

    QSignalSpy clicked(&modal, &md::Modal::clicked);
    QSignalSpy closed(&modal, &md::Modal::closed);

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), modal.mapToGlobal(QPoint(10, 10)),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&modal, &press);

    QCOMPARE(clicked.count(), 1);
    QCOMPARE(closed.count(), 1);
    QVERIFY(!modal.isOpen());
}

void TestDropdownModal::modalKeepsItsLevel()
{
    md::Modal modal(host);
    modal.setLevel(md::Modal::Level::Notification);
    modal.open();

    QCOMPARE(md::Modal::layerOf(&modal),
             static_cast<int>(md::Modal::Level::Notification));

    modal.setLevel(md::Modal::Level::Dialog);
    QCOMPARE(md::Modal::layerOf(&modal), static_cast<int>(md::Modal::Level::Dialog));
}

void TestDropdownModal::modalIsAboveADropdown()
{
    md::Modal modal(host);
    modal.setLevel(md::Modal::Level::Dialog);
    modal.open();

    QWidget anchor(host);
    anchor.setGeometry(100, 100, 200, 40);
    anchor.show();
    md::Dropdown dropdown;
    Leaf content(QSizeF(180.0, 120.0));
    dropdown.setAnchor(&anchor);
    dropdown.setContent(&content);
    dropdown.open();

    QCOMPARE(md::Modal::layerOf(&dropdown), static_cast<int>(md::Modal::Level::Dropdown));
    QCOMPARE(md::Modal::layerOf(&modal), static_cast<int>(md::Modal::Level::Dialog));
    QCOMPARE(md::Modal::topLayer(host), static_cast<QWidget*>(&modal));
}

void TestDropdownModal::modalCardAnimatesInAndOut()
{
    md::Modal modal(host);
    auto* card = new QWidget;
    card->setFixedSize(320, 200);
    modal.setCard(card);

    QSignalSpy openFinished(&modal, &md::Modal::openFinished);
    QSignalSpy closeFinished(&modal, &md::Modal::closeFinished);

    modal.open();
    QVERIFY(modal.isOpen());
    QVERIFY(modal.isAnimating());
    QVERIFY(modal.cardRect().isValid());
    QVERIFY(card->graphicsEffect() != nullptr);
    QVERIFY(modal.scrimOpacity() < 1.0);

    QTRY_COMPARE_WITH_TIMEOUT(openFinished.count(), 1, 2000);
    QVERIFY(!modal.isAnimating());
    QCOMPARE(modal.scrimOpacity(), 1.0);

    modal.close();
    QVERIFY(!modal.isOpen());
    QCOMPARE(closeFinished.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(closeFinished.count(), 1, 2000);
    QVERIFY(!modal.isVisible());
    QCOMPARE(modal.scrimOpacity(), 0.0);
}

void TestDropdownModal::modalReducedMotionSnaps()
{
    md::MotionScheme motion;
    motion.reducedMotion = true;
    md::Theme::setApplicationTheme(md::Theme::baselineLight().withMotion(motion));

    md::Modal modal(host);
    auto* card = new QWidget;
    card->setFixedSize(220, 140);
    modal.setCard(card);

    QSignalSpy openFinished(&modal, &md::Modal::openFinished);
    QSignalSpy closeFinished(&modal, &md::Modal::closeFinished);

    modal.open();
    QVERIFY(!modal.isAnimating());
    QCOMPARE(modal.scrimOpacity(), 1.0);
    QCOMPARE(openFinished.count(), 1);

    modal.close();
    QVERIFY(!modal.isOpen());
    QVERIFY(!modal.isVisible());
    QCOMPARE(modal.scrimOpacity(), 0.0);
    QCOMPARE(closeFinished.count(), 1);

    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestDropdownModal::modalTrapsAndRestoresFocus()
{
    auto* chip = new QPushButton(QStringLiteral("chip"), host);
    chip->setGeometry(20, 20, 120, 40);
    chip->show();
    chip->setFocus(Qt::OtherFocusReason);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(chip));

    md::Modal modal(host);
    auto* card = new QWidget;
    card->setFixedSize(300, 200);

    auto* first = new QPushButton(QStringLiteral("A"), card);
    first->setGeometry(20, 20, 120, 40);
    auto* second = new QPushButton(QStringLiteral("B"), card);
    second->setGeometry(20, 90, 120, 40);

    modal.setCard(card);
    modal.setFocusReturn(chip);
    modal.setAnimated(false);
    modal.open();

    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(first));

    QTest::keyClick(first, Qt::Key_Tab);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(second));

    QTest::keyClick(second, Qt::Key_Tab);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(first));

    QTest::keyClick(first, Qt::Key_Backtab);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(second));

    modal.close();
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(chip));
}

void TestDropdownModal::modalHideStopsAnimation()
{
    md::Modal modal(host);
    auto* card = new QWidget;
    card->setFixedSize(220, 140);
    modal.setCard(card);

    modal.open();
    QVERIFY(modal.isAnimating());

    modal.hide();
    QVERIFY(!modal.isOpen());
    QVERIFY(!modal.isAnimating());
    QCOMPARE(modal.scrimOpacity(), 0.0);

    // No callback may still be writing to the hidden card after the abort.
    QTest::qWait(700);
    QVERIFY(!modal.isAnimating());
    QCOMPARE(modal.scrimOpacity(), 0.0);
}

QTEST_MAIN(TestDropdownModal)
#include "tst_dropdown_modal.moc"
