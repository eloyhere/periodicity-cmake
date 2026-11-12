// Unit tests for the BottomSheet component (framework.md §14).
//
// The suite is offscreen and deterministic, exactly like the other overlay
// suites: the application theme is pinned to the baseline light scheme, the
// hosts are sized explicitly, and pointer and key input is delivered with
// QApplication::sendEvent rather than through a window system. The one thing
// that would otherwise be timing-dependent — the slide — is ended with
// finishAnimation(), so every geometry assertion runs against a settled layout.
//
// The assertions target the contract, not the pixels: a sheet starts closed,
// opens once and closes once, a modal sheet dismisses from Escape, a backdrop
// press and a long drag while a non-modal one ignores Escape, the body is
// reparented but stays owned by the caller, and the surface never grows past
// 90% of the host.

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QRectF>
#include <QSignalSpy>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QTest>
#include <QWheelEvent>
#include <QWidget>

#include <md/components/bottom_sheet.h>
#include <md/components/modal.h>
#include <md/theme/theme.h>
#include <md/view/view.h>

namespace {

// A View with no children and a fixed preferred size: enough to stand in for a
// body without pulling another component in.
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

class TestBottomSheet : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void startsClosed();
    void openShowsAndEmitsOpenedOnce();
    void closeHidesAndEmitsClosedOnce();
    void escapeClosesModal();
    void nonModalIgnoresEscape();
    void contentIsReparentedAndDetached();
    void measuredHeightIsCapped();
    void modalRegistersWithLayer();
    void backdropDismissal();
    void dragPastThresholdCloses();
    void contentScrolls();
    void reducedMotionSnaps();
    void renders();
};

void TestBottomSheet::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestBottomSheet::init()
{
    // A reduced-motion test installs a different application theme; every test
    // starts from the baseline one so an ordering change cannot leak it.
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestBottomSheet::startsClosed()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::BottomSheet sheet(&host);
    sheet.setTitle(QStringLiteral("Filters"));

    QVERIFY(!sheet.isOpen());
    QVERIFY(!sheet.isVisible());
    QVERIFY(sheet.isModal());
    QVERIFY(sheet.dismissesOnBackdrop());
    QCOMPARE(sheet.title(), QStringLiteral("Filters"));
    QVERIFY(sheet.content() == nullptr);
    QVERIFY(sheet.surfaceRect().isEmpty());
    QVERIFY(sheet.scrimRect().isEmpty());
    QVERIFY(sheet.handleRect().isEmpty());
    QVERIFY(sheet.viewportRect().isEmpty());
    QVERIFY(!sheet.isAnimating());
    QCOMPARE(sheet.animationProgress(), 0.0);
}

void TestBottomSheet::openShowsAndEmitsOpenedOnce()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::BottomSheet sheet(&host);
    sheet.setTitle(QStringLiteral("Filters"));

    SolidView body(QColor(0xE8, 0xE8, 0xE8), QSizeF(200.0, 300.0));
    sheet.setContent(&body);

    QSignalSpy openedSpy(&sheet, &md::BottomSheet::opened);

    sheet.open();

    QVERIFY(sheet.isOpen());
    QVERIFY(sheet.isVisible());
    QCOMPARE(openedSpy.count(), 1);
    QVERIFY(sheet.isAnimating());

    // The surface starts below the bottom edge and slides in.
    sheet.open();
    QCOMPARE(openedSpy.count(), 1);

    sheet.finishAnimation();

    QVERIFY(!sheet.isAnimating());
    QCOMPARE(sheet.animationProgress(), 1.0);
    QCOMPARE(sheet.surfaceRect().width(), 800.0);
    QCOMPARE(sheet.surfaceRect().bottom(), 600.0);
    QCOMPARE(sheet.surfaceRect().height(), sheet.measuredSize().height());
    QCOMPARE(sheet.scrimRect(), QRectF(0.0, 0.0, 800.0, 600.0));

    // The handle is 32x4 dp, 22 dp below the top edge and centred on the
    // surface; the title and the body sit below it.
    QCOMPARE(sheet.handleRect().width(), 32.0);
    QCOMPARE(sheet.handleRect().height(), 4.0);
    QCOMPARE(sheet.handleRect().top(), sheet.surfaceRect().top() + 22.0);
    QCOMPARE(sheet.handleRect().center().x(), sheet.surfaceRect().center().x());
    QVERIFY(!sheet.titleRect().isEmpty());
    QVERIFY(sheet.titleRect().top() > sheet.handleRect().bottom());
    QVERIFY(sheet.viewportRect().top() >= sheet.titleRect().bottom());
    QVERIFY(sheet.viewportRect().height() > 0.0);
}

void TestBottomSheet::closeHidesAndEmitsClosedOnce()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::BottomSheet sheet(&host);
    sheet.setTitle(QStringLiteral("Filters"));
    sheet.open();
    sheet.finishAnimation();

    QSignalSpy closedSpy(&sheet, &md::BottomSheet::closed);

    sheet.close();

    // close() records the intent immediately; the widget stays until the
    // slide out has finished.
    QVERIFY(!sheet.isOpen());
    QVERIFY(sheet.isVisible());
    QCOMPARE(closedSpy.count(), 0);

    sheet.finishAnimation();

    QCOMPARE(closedSpy.count(), 1);
    QVERIFY(!sheet.isVisible());
    QCOMPARE(sheet.animationProgress(), 0.0);

    // Closing an already closed sheet is a no-op and does not signal again.
    sheet.close();
    QCOMPARE(closedSpy.count(), 1);
}

void TestBottomSheet::escapeClosesModal()
{
    QWidget host;
    host.resize(640, 480);
    host.show();

    md::BottomSheet sheet(&host);
    sheet.setTitle(QStringLiteral("Filters"));
    sheet.open();
    sheet.finishAnimation();

    QSignalSpy closedSpy(&sheet, &md::BottomSheet::closed);

    QTest::keyClick(&sheet, Qt::Key_Escape);

    QVERIFY(!sheet.isOpen());

    sheet.finishAnimation();

    QCOMPARE(closedSpy.count(), 1);
    QVERIFY(!sheet.isVisible());
}

void TestBottomSheet::nonModalIgnoresEscape()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::BottomSheet sheet(&host);
    sheet.setModal(false);
    QVERIFY(!sheet.isModal());

    sheet.setTitle(QStringLiteral("Actions"));
    sheet.open();

    QVERIFY(sheet.isOpen());
    QVERIFY(sheet.isVisible());
    QVERIFY(!sheet.isAnimating());
    QCOMPARE(sheet.animationProgress(), 1.0);

    // A non-modal sheet has no scrim and snaps instead of sliding. It is a
    // docked widget, so the dock is asserted in the host's coordinates and the
    // surface rectangle stays local to the sheet.
    QVERIFY(sheet.scrimRect().isEmpty());
    QCOMPARE(sheet.geometry().bottom(), host.rect().bottom());
    QVERIFY(sheet.surfaceRect().height() > 0.0);

    QSignalSpy closedSpy(&sheet, &md::BottomSheet::closed);

    QTest::keyClick(&sheet, Qt::Key_Escape);

    QVERIFY(sheet.isOpen());
    QCOMPARE(closedSpy.count(), 0);

    sheet.close();

    QVERIFY(!sheet.isOpen());
    QVERIFY(!sheet.isVisible());
    QCOMPARE(closedSpy.count(), 1);
}

void TestBottomSheet::contentIsReparentedAndDetached()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    QWidget* body = new QWidget;
    body->setMinimumSize(120, 80);

    {
        md::BottomSheet sheet(&host);
        sheet.setContent(body);

        QCOMPARE(sheet.content(), body);
        QVERIFY(sheet.isAncestorOf(body));
        QVERIFY(!body->isVisible());

        sheet.open();
        sheet.finishAnimation();

        // The body lives in the sheet's viewport, which is a real child.
        QVERIFY(sheet.isAncestorOf(body));
        QVERIFY(body->parentWidget() != nullptr);
        QVERIFY(sheet.isAncestorOf(body->parentWidget()));
        QVERIFY(body->isVisible());
        QCOMPARE(body->width(), 800);

        sheet.close();
        sheet.finishAnimation();
        QVERIFY(!body->isVisible());

        // A null content detaches the widget without deleting it.
        sheet.setContent(nullptr);
        QVERIFY(sheet.content() == nullptr);
        QVERIFY(body->parentWidget() == nullptr);
    }

    // The sheet went away first: the body is still alive and parentless.
    QVERIFY(body->parentWidget() == nullptr);
    QVERIFY(body->isHidden());
    delete body;
}

void TestBottomSheet::measuredHeightIsCapped()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::BottomSheet sheet(&host);
    sheet.setTitle(QStringLiteral("Tall"));

    SolidView tall(QColor(0xD0, 0xD0, 0xD0), QSizeF(200.0, 5000.0));
    sheet.setContent(&tall);

    sheet.open();
    sheet.finishAnimation();

    // 90% of a 600 px host.
    QCOMPARE(sheet.measuredSize().height(), 540.0);
    QCOMPARE(sheet.surfaceRect().height(), 540.0);
    QCOMPARE(sheet.surfaceRect().top(), 60.0);
    QVERIFY(sheet.viewportRect().height() > 0.0);
    QVERIFY(sheet.viewportRect().height() < tall.measuredSize().height());

    // A body shorter than the cap is not stretched to it.
    SolidView shortBody(QColor(0xE0, 0xE0, 0xE0), QSizeF(200.0, 120.0));
    sheet.setContent(&shortBody);

    QVERIFY(sheet.measuredSize().height() < 540.0);
    QCOMPARE(sheet.surfaceRect().bottom(), 600.0);
    QVERIFY(sheet.surfaceRect().top() > 60.0);
}

void TestBottomSheet::modalRegistersWithLayer()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    const int contentLevel = static_cast<int>(md::Modal::Level::Content);
    const int drawerLevel = static_cast<int>(md::Modal::Level::Drawer);
    const int dialogLevel = static_cast<int>(md::Modal::Level::Dialog);

    QVERIFY(drawerLevel > contentLevel);
    QVERIFY(drawerLevel < dialogLevel);

    md::BottomSheet sheet(&host);
    QCOMPARE(md::Modal::layerOf(&sheet), contentLevel);

    sheet.open();
    QCOMPARE(md::Modal::layerOf(&sheet), drawerLevel);

    sheet.close();
    sheet.finishAnimation();
    QCOMPARE(md::Modal::layerOf(&sheet), contentLevel);
}

void TestBottomSheet::backdropDismissal()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::BottomSheet sheet(&host);
    sheet.setTitle(QStringLiteral("Filters"));
    sheet.open();
    sheet.finishAnimation();

    QSignalSpy closedSpy(&sheet, &md::BottomSheet::closed);

    // A press inside the surface is not a backdrop press.
    clickAt(sheet, sheet.surfaceRect().center());
    QVERIFY(sheet.isOpen());
    QCOMPARE(closedSpy.count(), 0);

    // Above the surface it is.
    clickAt(sheet, QPointF(400.0, 20.0));
    QVERIFY(!sheet.isOpen());

    sheet.finishAnimation();
    QCOMPARE(closedSpy.count(), 1);

    // A locked backdrop swallows the same press.
    sheet.open();
    sheet.finishAnimation();
    sheet.setDismissOnBackdrop(false);
    QVERIFY(!sheet.dismissesOnBackdrop());

    clickAt(sheet, QPointF(400.0, 20.0));
    QVERIFY(sheet.isOpen());
    QCOMPARE(closedSpy.count(), 1);
}

void TestBottomSheet::dragPastThresholdCloses()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::BottomSheet sheet(&host);
    sheet.setTitle(QStringLiteral("Drag"));
    sheet.open();
    sheet.finishAnimation();

    QSignalSpy closedSpy(&sheet, &md::BottomSheet::closed);

    const QRectF handle = sheet.handleRect();
    QVERIFY(!handle.isEmpty());

    const qreal settledTop = sheet.surfaceRect().top();
    const qreal travel = sheet.surfaceRect().height() * 0.4;
    const QPointF from = handle.center();
    const QPointF to = from + QPointF(0.0, travel);

    sendMouse(sheet, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    sendMouse(sheet, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);

    // The surface follows the pointer down while the drag is live.
    QVERIFY(sheet.surfaceRect().top() > settledTop + 1.0);

    sendMouse(sheet, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);

    QVERIFY(!sheet.isOpen());
    sheet.finishAnimation();
    QCOMPARE(closedSpy.count(), 1);

    // A drag below a quarter of the height snaps back and keeps the sheet open.
    sheet.open();
    sheet.finishAnimation();

    const QPointF start = sheet.handleRect().center();
    const QPointF shortDrag = start + QPointF(0.0, sheet.surfaceRect().height() * 0.1);

    sendMouse(sheet, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    sendMouse(sheet, QEvent::MouseMove, shortDrag, Qt::NoButton, Qt::LeftButton);
    sendMouse(sheet, QEvent::MouseButtonRelease, shortDrag, Qt::LeftButton, Qt::NoButton);

    QVERIFY(sheet.isOpen());
    QCOMPARE(sheet.surfaceRect().bottom(), 600.0);
    QCOMPARE(closedSpy.count(), 1);
}

void TestBottomSheet::contentScrolls()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::BottomSheet sheet(&host);
    sheet.setTitle(QStringLiteral("Long"));

    SolidView tall(QColor(0xC0, 0xC0, 0xC0), QSizeF(200.0, 3000.0));
    sheet.setContent(&tall);

    sheet.open();
    sheet.finishAnimation();

    QVERIFY(tall.geometry().height() > sheet.viewportRect().height());
    QCOMPARE(tall.geometry().top(), 0);

    sendWheel(sheet, sheet.viewportRect().center(), -120);

    // The body moved up inside the clipped viewport.
    QVERIFY(tall.geometry().top() < 0);

    sendWheel(sheet, sheet.viewportRect().center(), 120);
    QCOMPARE(tall.geometry().top(), 0);
}

void TestBottomSheet::reducedMotionSnaps()
{
    md::Theme theme = md::Theme::baselineLight();
    md::MotionScheme motion = theme.motion();
    motion.reducedMotion = true;
    md::Theme::setApplicationTheme(theme.withMotion(motion));

    QWidget host;
    host.resize(640, 480);
    host.show();

    md::BottomSheet sheet(&host);
    sheet.setTitle(QStringLiteral("Snap"));
    sheet.open();

    QVERIFY(sheet.isOpen());
    QVERIFY(sheet.isVisible());
    QVERIFY(!sheet.isAnimating());
    QCOMPARE(sheet.animationProgress(), 1.0);
    QCOMPARE(sheet.surfaceRect().bottom(), 480.0);
    QCOMPARE(sheet.scrimRect(), QRectF(0.0, 0.0, 640.0, 480.0));

    QSignalSpy closedSpy(&sheet, &md::BottomSheet::closed);

    sheet.close();

    QVERIFY(!sheet.isOpen());
    QVERIFY(!sheet.isVisible());
    QCOMPARE(closedSpy.count(), 1);
}

void TestBottomSheet::renders()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    md::BottomSheet sheet(&host);
    sheet.setTitle(QStringLiteral("Share"));
    SolidView body(QColor(0xF0, 0xF0, 0xF0), QSizeF(200.0, 240.0));
    sheet.setContent(&body);
    sheet.open();
    sheet.finishAnimation();

    const QImage image = sheet.grab().toImage();
    QVERIFY(!image.isNull());
    QVERIFY(image.width() > 0);
    QVERIFY(image.height() > 0);
}

QTEST_MAIN(TestBottomSheet)
#include "tst_bottom_sheet.moc"
