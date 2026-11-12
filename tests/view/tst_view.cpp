// Unit tests for the view module (L4): the component kernel.
//
// These tests exercise the three contracts through a concrete subclass that
// has a fixed content size and, optionally, paints a solid fill. Nothing here
// depends on the demo application or on a real window system: the tests run
// under the offscreen platform, and every pointer event is either dispatched
// straight into the EventTarget or sent with QApplication::sendEvent, so the
// assertions do not depend on window activation.

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QKeyEvent>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QTest>
#include <QtGlobal>

#include <md/core/measure_spec.h>
#include <md/core/view_state.h>
#include <md/event/event.h>
#include <md/event/pointer_event.h>
#include <md/event/wheel_event.h>
#include <md/schedule/task_queue.h>
#include <md/theme/theme.h>
#include <md/tokens/color_scheme.h>
#include <md/tokens/elevation.h>
#include <md/tokens/shape.h>
#include <md/view/view.h>
#include <md/view/view_group.h>

#include <utility>

namespace {

// The fixed content size every BoxView reports unless told otherwise.
constexpr qreal kContentWidth = 120.0;
constexpr qreal kContentHeight = 40.0;

// Reads a pixel given in logical widget coordinates, honouring the device
// pixel ratio so the assertions hold under Qt's scaling as well.
QColor pixelAt(const QImage& image, const QPointF& logical)
{
    const qreal ratio = image.devicePixelRatio();
    const int x = qBound(0, qRound(logical.x() * ratio), image.width() - 1);
    const int y = qBound(0, qRound(logical.y() * ratio), image.height() - 1);
    return image.pixelColor(x, y);
}

int brightness(const QColor& colour)
{
    return colour.red() + colour.green() + colour.blue();
}

void clearInteraction(md::View& view)
{
    view.setState(md::ViewState::Hovered, false);
    view.setState(md::ViewState::Pressed, false);
    view.setState(md::ViewState::Focused, false);
}

void showWidget(QWidget& widget)
{
    widget.show();
    QTest::qWait(0);

    // The offscreen platform reports the virtual cursor as resting over the
    // widget, so showing it synthesises an Enter and the idle surface would be
    // painted with a hover overlay. Clear the interaction flags deliberately
    // so every pixel assertion sees the idle state.
    const QList<md::View*> views = widget.findChildren<md::View*>();
    for (md::View* view : views)
        clearInteraction(*view);

    if (auto* view = qobject_cast<md::View*>(&widget))
        clearInteraction(*view);
}

// Delivers a mouse event directly: no window system, no activation, no
// dependency on which test widget happens to be on top.
void sendMouse(QWidget& widget, QEvent::Type type, const QPointF& position,
               Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, position, position, position, button, buttons,
                      Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

// A View with a fixed content size and an optional solid fill. The fill goes
// through paintContent(), so it also proves that the content stage runs after
// the background.
class BoxView : public md::View
{
public:
    explicit BoxView(QWidget* parent = nullptr)
        : View(parent)
    {
    }

    void setContentSize(const QSizeF& size) { contentSize_ = size; }
    void setFillColour(const QColor& colour) { fillColour_ = colour; }

protected:
    void measureOverride(md::MeasureSpec width, md::MeasureSpec height) override
    {
        setMeasuredSize(QSizeF(width.resolve(contentSize_.width()),
                               height.resolve(contentSize_.height())));
    }

    void paintContent(QPainter& painter, const QRectF& content) override
    {
        if (fillColour_.isValid())
            painter.fillRect(content, fillColour_);
    }

private:
    QSizeF contentSize_{kContentWidth, kContentHeight};
    QColor fillColour_;
};

// A ViewGroup with a deterministic arrangement: the first child fills the
// top-left corner, the second overlaps it from (20, 10). The overlap is what
// the paint-order test observes.
class BoxGroup : public md::ViewGroup
{
public:
    explicit BoxGroup(QWidget* parent = nullptr)
        : ViewGroup(parent)
    {
    }

protected:
    void measureOverride(md::MeasureSpec width, md::MeasureSpec height) override
    {
        setMeasuredSize(QSizeF(width.resolve(200.0), height.resolve(100.0)));
    }

    void arrangeChildren(const QRectF& content) override
    {
        Q_UNUSED(content);

        const QList<md::View*> children = views();
        if (children.size() > 0)
            children[0]->arrange(QRect(0, 0, 60, 40));
        if (children.size() > 1)
            children[1]->arrange(QRect(20, 10, 60, 40));
    }
};

} // namespace

class TestView : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void defaults();
    void sizeHintAndMeasure();
    void minimumSizeHint();
    void heightForWidth();
    void arrangeSetsGeometry();

    void paintsThemeSurfaceAndCustomBackground();
    void tonalElevationChangesSurface();

    void pointerStateTransitions();
    void keyboardActivation();
    void unhandledFrameworkEventsRemainIgnored();
    void updateRequestsCoalesce();
    void disabledViewIgnoresInput();
    void rippleDarkensSurface();

    void groupArrangesChildren();
    void groupTransferRemovesOldMembership();
    void groupPaintsSecondChildOverFirst();
    void parentTargetAndPointMapping();
};

void TestView::initTestCase()
{
    // Pin the theme: the assertions compare against baseline-light tokens, not
    // against whatever an embedding application may have installed.
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestView::defaults()
{
    BoxView view;

    QVERIFY(view.state().testFlag(md::ViewState::Enabled));
    QVERIFY(!view.state().testFlag(md::ViewState::Hovered));
    QVERIFY(!view.state().testFlag(md::ViewState::Pressed));
    QVERIFY(view.elevation() == md::Elevation::Level0);
    QVERIFY(view.shape() == md::Shape::small());

    // Nothing has been measured yet, and the content rectangle tracks the
    // widget rectangle.
    QCOMPARE(view.measuredSize(), QSizeF(0.0, 0.0));
    QCOMPARE(view.contentRect(), QRectF(view.rect()));
}

void TestView::sizeHintAndMeasure()
{
    BoxView view;

    // sizeHint runs an unspecified measurement pass and rounds up.
    QCOMPARE(view.sizeHint(), QSize(120, 40));
    QCOMPARE(view.measuredSize(), QSizeF(120.0, 40.0));

    // Exactly is authoritative even when it is larger than the desired size.
    view.measure(md::MeasureSpec::exactly(300.0), md::MeasureSpec::exactly(100.0));
    QCOMPARE(view.measuredSize(), QSizeF(300.0, 100.0));

    // AtMost caps the desired size.
    view.measure(md::MeasureSpec::atMost(50.0), md::MeasureSpec::atMost(20.0));
    QCOMPARE(view.measuredSize(), QSizeF(50.0, 20.0));

    // Unspecified leaves the desired size untouched.
    view.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());
    QCOMPARE(view.measuredSize(), QSizeF(120.0, 40.0));
}

void TestView::minimumSizeHint()
{
    BoxView view;

    // The contract measures with atMost(0) on both axes; a subclass whose
    // desired size is positive resolves that to zero.
    QCOMPARE(view.minimumSizeHint(), QSize(0, 0));
    QCOMPARE(view.measuredSize(), QSizeF(0.0, 0.0));
}

void TestView::heightForWidth()
{
    BoxView view;

    QVERIFY(view.hasHeightForWidth());

    // exactly(width) on the horizontal axis, unspecified on the vertical one.
    QCOMPARE(view.heightForWidth(80), 40);
    QCOMPARE(view.measuredSize(), QSizeF(80.0, 40.0));

    QCOMPARE(view.heightForWidth(240), 40);
    QCOMPARE(view.measuredSize(), QSizeF(240.0, 40.0));
}

void TestView::arrangeSetsGeometry()
{
    BoxView view;

    view.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(80.0));
    view.arrange(QRect(10, 20, 200, 80));

    QCOMPARE(view.geometry(), QRect(10, 20, 200, 80));
    QCOMPARE(view.contentRect(), QRectF(0.0, 0.0, 200.0, 80.0));
    QCOMPARE(view.size(), QSize(200, 80));

    // A second arrange with the same rectangle must still run, because the
    // children may have changed size without the parent changing shape.
    view.arrange(QRect(10, 20, 200, 80));
    QCOMPARE(view.geometry(), QRect(10, 20, 200, 80));
}

void TestView::paintsThemeSurfaceAndCustomBackground()
{
    BoxView view;
    view.resize(120, 40);
    showWidget(view);

    const QColor surface = md::Theme::baselineLight().colours().surface;

    // Level0, no explicit colour: the background is the theme surface.
    const QImage plain = view.grab().toImage();
    QVERIFY(!plain.isNull());
    QCOMPARE(pixelAt(plain, QPointF(60.0, 20.0)).rgb(), surface.rgb());

    // An explicit container colour wins over the tonal surface.
    view.setBackgroundColour(Qt::red);
    QCOMPARE(pixelAt(view.grab().toImage(), QPointF(60.0, 20.0)).rgb(),
             QColor(Qt::red).rgb());

    // Clearing it restores the tonal surface.
    view.setBackgroundColour(QColor());
    QCOMPARE(pixelAt(view.grab().toImage(), QPointF(60.0, 20.0)).rgb(), surface.rgb());
}

void TestView::tonalElevationChangesSurface()
{
    BoxView flat;
    flat.resize(120, 40);
    showWidget(flat);

    BoxView raised;
    raised.setElevation(md::Elevation::Level3);
    raised.resize(120, 40);
    showWidget(raised);

    const md::ColorScheme& colours = md::Theme::baselineLight().colours();
    const QColor flatCentre = pixelAt(flat.grab().toImage(), QPointF(60.0, 20.0));
    const QColor raisedCentre = pixelAt(raised.grab().toImage(), QPointF(60.0, 20.0));

    QCOMPARE(flatCentre.rgb(), colours.surface.rgb());
    QCOMPARE(raisedCentre.rgb(),
             colours.surfaceAtElevation(md::Elevation::Level3).rgb());
    QVERIFY(raisedCentre.rgb() != flatCentre.rgb());
    QVERIFY(raised.elevation() == md::Elevation::Level3);
}

void TestView::pointerStateTransitions()
{
    BoxView view;
    view.resize(120, 40);
    showWidget(view);

    md::PointerEvent enter(md::EventType::PointerEnter, QPointF(10.0, 10.0),
                           QPointF(10.0, 10.0), Qt::NoButton, Qt::NoButton,
                           Qt::NoModifier);
    QVERIFY(view.onEvent(enter));
    QVERIFY(view.state().testFlag(md::ViewState::Hovered));

    md::PointerEvent press(md::EventType::PointerDown, QPointF(10.0, 10.0),
                           QPointF(10.0, 10.0), Qt::LeftButton, Qt::LeftButton,
                           Qt::NoModifier);
    QVERIFY(view.onEvent(press));
    QVERIFY(view.state().testFlag(md::ViewState::Pressed));

    md::PointerEvent release(md::EventType::PointerUp, QPointF(10.0, 10.0),
                             QPointF(10.0, 10.0), Qt::LeftButton, Qt::NoButton,
                             Qt::NoModifier);
    QVERIFY(view.onEvent(release));
    QVERIFY(!view.state().testFlag(md::ViewState::Pressed));

    md::PointerEvent leave(md::EventType::PointerLeave, QPointF(10.0, 10.0),
                           QPointF(10.0, 10.0), Qt::NoButton, Qt::NoButton,
                           Qt::NoModifier);
    QVERIFY(view.onEvent(leave));
    QVERIFY(!view.state().testFlag(md::ViewState::Hovered));

    // The Qt path: the handler converts, dispatches and accepts.
    sendMouse(view, QEvent::MouseButtonPress, QPointF(60.0, 20.0), Qt::LeftButton,
              Qt::LeftButton);
    QVERIFY(view.state().testFlag(md::ViewState::Pressed));

    sendMouse(view, QEvent::MouseButtonRelease, QPointF(60.0, 20.0), Qt::LeftButton,
              Qt::NoButton);
    QVERIFY(!view.state().testFlag(md::ViewState::Pressed));
}

void TestView::keyboardActivation()
{
    BoxView view;
    view.resize(120, 40);
    showWidget(view);

    // Space is an activation key: press sets Pressed, release clears it.
    QKeyEvent spacePress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&view, &spacePress);
    QVERIFY(view.state().testFlag(md::ViewState::Pressed));

    QKeyEvent spaceRelease(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&view, &spaceRelease);
    QVERIFY(!view.state().testFlag(md::ViewState::Pressed));

    // A non-activation key must not press the view; it is left to Qt so that
    // shortcuts and traversal keep working.
    QKeyEvent letterPress(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QApplication::sendEvent(&view, &letterPress);
    QVERIFY(!view.state().testFlag(md::ViewState::Pressed));
}

void TestView::unhandledFrameworkEventsRemainIgnored()
{
    BoxView view;
    md::Event none(md::EventType::None);
    QVERIFY(!view.onEvent(none));
    QVERIFY(!none.isAccepted());

    md::WheelEvent wheel(QPoint(0, 120), QPoint(), QPointF(), Qt::NoModifier);
    QVERIFY(!view.onEvent(wheel));
    QVERIFY(!wheel.isAccepted());

    QKeyEvent letter(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QApplication::sendEvent(&view, &letter);
    QVERIFY(!letter.isAccepted());
    QVERIFY(!view.state().testFlag(md::ViewState::Pressed));
}

void TestView::updateRequestsCoalesce()
{
    md::schedule::TaskQueue& queue = md::schedule::TaskQueue::main();
    queue.flush();

    BoxView view;

    // A burst of state changes schedules exactly one repaint for this view:
    // requestUpdate() posts under a per-instance coalescing key.
    view.setState(md::ViewState::Hovered, true);
    view.setState(md::ViewState::Pressed, true);
    view.setState(md::ViewState::Pressed, false);
    QCOMPARE(queue.pending(md::schedule::Priority::UiUpdate), 1);

    queue.flush();
    QCOMPARE(queue.pending(md::schedule::Priority::UiUpdate), 0);
}

void TestView::disabledViewIgnoresInput()
{
    BoxView view;
    view.resize(120, 40);
    showWidget(view);

    view.setEnabled(false);
    QVERIFY(!view.state().testFlag(md::ViewState::Enabled));

    // A disabled view rejects the dispatch and keeps its idle state.
    md::PointerEvent press(md::EventType::PointerDown, QPointF(60.0, 20.0),
                           QPointF(60.0, 20.0), Qt::LeftButton, Qt::LeftButton,
                           Qt::NoModifier);
    QVERIFY(!view.onEvent(press));
    QVERIFY(!view.state().testFlag(md::ViewState::Pressed));

    // And it still paints the plain surface, with no interaction overlay.
    const QColor surface = md::Theme::baselineLight().colours().surface;
    QCOMPARE(pixelAt(view.grab().toImage(), QPointF(60.0, 20.0)).rgb(), surface.rgb());

    view.setEnabled(true);
    QVERIFY(view.state().testFlag(md::ViewState::Enabled));
}

void TestView::rippleDarkensSurface()
{
    BoxView view;
    view.resize(120, 40);
    showWidget(view);
    QTest::qWait(20);

    const QColor plain = pixelAt(view.grab().toImage(), QPointF(60.0, 20.0));

    sendMouse(view, QEvent::MouseButtonPress, QPointF(60.0, 20.0), Qt::LeftButton,
              Qt::LeftButton);
    QVERIFY(view.state().testFlag(md::ViewState::Pressed));

    // Let the press ripple grow and the state layer settle.
    QTest::qWait(150);

    const QColor pressed = pixelAt(view.grab().toImage(), QPointF(60.0, 20.0));
    QVERIFY2(brightness(pressed) < brightness(plain),
             "the press ripple and the state layer must darken the centre");

    // Releasing clears Pressed; the one-shot ripple expands, fades and is
    // removed, so the surface returns to its plain colour.
    sendMouse(view, QEvent::MouseButtonRelease, QPointF(60.0, 20.0), Qt::LeftButton,
              Qt::NoButton);
    QVERIFY(!view.state().testFlag(md::ViewState::Pressed));

    QTest::qWait(1200);
    const QColor settled = pixelAt(view.grab().toImage(), QPointF(60.0, 20.0));
    QCOMPARE(settled.rgb(), plain.rgb());
}

void TestView::groupArrangesChildren()
{
    // Declaration order matters: the children are QWidgets parented to the
    // group, so they must be destroyed before it.
    BoxGroup group;
    BoxView first;
    BoxView second;

    group.addView(&first);
    group.addView(&second);

    QCOMPARE(group.views().size(), qsizetype(2));
    QVERIFY(first.parentWidget() == &group);
    QVERIFY(second.parentWidget() == &group);

    group.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(100.0));
    QCOMPARE(group.measuredSize(), QSizeF(200.0, 100.0));

    group.arrange(QRect(0, 0, 200, 100));
    QCOMPARE(first.geometry(), QRect(0, 0, 60, 40));
    QCOMPARE(second.geometry(), QRect(20, 10, 60, 40));

    group.removeView(&second);
    QCOMPARE(group.views().size(), qsizetype(1));
    QVERIFY(second.parentWidget() == nullptr);
}

void TestView::groupTransferRemovesOldMembership()
{
    BoxGroup first;
    BoxGroup second;
    BoxView child;
    first.addView(&child);
    second.addView(&child);

    QVERIFY(first.views().isEmpty());
    QCOMPARE(second.views(), QList<md::View*>({&child}));
    QCOMPARE(child.parentWidget(), static_cast<QWidget*>(&second));
}

void TestView::groupPaintsSecondChildOverFirst()
{
    BoxGroup group;
    BoxView first;
    BoxView second;

    first.setFillColour(Qt::red);
    second.setFillColour(Qt::blue);

    group.addView(&first);
    group.addView(&second);

    // Arrange before showing: the layout work happens while the group is
    // invisible, and showing it must not disturb the child geometry.
    group.resize(200, 100);
    group.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(100.0));
    group.arrange(QRect(0, 0, 200, 100));

    showWidget(group);

    const QImage image = group.grab().toImage();
    QVERIFY(!image.isNull());

    // (10, 10) is covered only by the first child; (30, 20) is covered by
    // both, and the second one is painted later, so it wins.
    QCOMPARE(pixelAt(image, QPointF(10.0, 10.0)).rgb(), QColor(Qt::red).rgb());
    QCOMPARE(pixelAt(image, QPointF(30.0, 20.0)).rgb(), QColor(Qt::blue).rgb());
}

void TestView::parentTargetAndPointMapping()
{
    BoxGroup group;
    BoxView child;

    group.addView(&child);
    child.arrange(QRect(20, 10, 60, 40));

    QVERIFY(child.parentTarget() == &group);
    QVERIFY(group.parentTarget() == nullptr);

    // The group's (25, 15) is the child's (5, 5).
    QCOMPARE(child.mapPointFrom(group, QPointF(25.0, 15.0)), QPointF(5.0, 5.0));

    // Mapping from itself is the identity.
    QCOMPARE(child.mapPointFrom(child, QPointF(7.0, 8.0)), QPointF(7.0, 8.0));
}

QTEST_MAIN(TestView)
#include "tst_view.moc"
