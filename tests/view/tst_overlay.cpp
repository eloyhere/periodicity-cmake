// Tests for the window-level overlay host (md/view/overlay.h).
//
// The suite is offscreen and deterministic. Each test owns its top-level
// window (a plain QWidget), so the per-window Overlay is created and destroyed
// inside the test and no application event filter survives into the next one.
//
// The assertions target the window-level contract only: the stack order is the
// z order, the top surface owns input, the content root is disabled exactly
// while a surface asks for it, focus is captured once and restored once, and
// teardown works when a surface or the whole window disappears while pushed.

#include <md/view/overlay.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QTest>
#include <QWidget>

namespace {

// Counts the events a widget below the top surface must never receive, and the
// ones the top surface must keep receiving.
class Probe final : public QWidget
{
public:
    explicit Probe(QWidget* parent = nullptr)
        : QWidget(parent)
    {
    }

    int presses = 0;
    int keys = 0;
    int overrides = 0;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        ++presses;
        QWidget::mousePressEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        ++keys;
        QWidget::keyPressEvent(event);
    }

    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ShortcutOverride)
            ++overrides;
        return QWidget::event(event);
    }
};

void sendPress(QWidget& target, const QPointF& local = QPointF(5.0, 5.0))
{
    QMouseEvent press(QEvent::MouseButtonPress, local,
                      QPointF(target.mapToGlobal(local.toPoint())), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&target, &press);
}

void sendKey(QWidget& target, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QApplication::sendEvent(&target, &press);
}

void sendShortcutOverride(QWidget& target, int key)
{
    QKeyEvent press(QEvent::ShortcutOverride, key, Qt::ControlModifier);
    QApplication::sendEvent(&target, &press);
}

md::Overlay::Options optionsFor(bool blockInput, bool disableContent,
                                bool restoreFocus = true)
{
    md::Overlay::Options options;
    options.blockInput = blockInput;
    options.disableContent = disableContent;
    options.restoreFocus = restoreFocus;
    options.fillWindow = false;
    return options;
}

} // namespace

class TestOverlay : public QObject
{
    Q_OBJECT

private slots:
    void forWidgetIsPerWindowAndParentedToIt();
    void pushPopOrderAndTop();
    void nestedPushIsTheZOrder();
    void inputIsBlockedBelowTheTop();
    void contentIsDisabledOnlyWhileASurfaceAsks();
    void alreadyDisabledStateSurvivesTheRestore();
    void focusIsRestoredOnTheLastPop();
    void nestedPopsRestoreOnlyOnce();
    void surfaceDestroyedWhilePushed();
    void windowDestroyedWhilePushed();
};

void TestOverlay::forWidgetIsPerWindowAndParentedToIt()
{
    QWidget first;
    first.resize(320, 240);
    first.show();

    QWidget second;
    second.resize(320, 240);
    second.show();

    md::Overlay* overlay = md::Overlay::forWidget(&first);
    QVERIFY(overlay != nullptr);
    QCOMPARE(md::Overlay::forWidget(&first), overlay);
    QCOMPARE(overlay->parent(), static_cast<QObject*>(&first));
    QCOMPARE(overlay->window(), static_cast<QWidget*>(&first));

    md::Overlay* other = md::Overlay::forWidget(&second);
    QVERIFY(other != nullptr);
    QVERIFY(other != overlay);
    QCOMPARE(other->window(), static_cast<QWidget*>(&second));

    QCOMPARE(md::Overlay::forWidget(nullptr), static_cast<md::Overlay*>(nullptr));
}

void TestOverlay::pushPopOrderAndTop()
{
    QWidget window;
    window.resize(400, 300);
    window.show();
    QTest::qWait(10);

    QWidget first(&window);
    QWidget second(&window);
    first.show();
    second.show();

    md::Overlay* overlay = md::Overlay::forWidget(&window);
    QVERIFY(overlay != nullptr);
    QCOMPARE(overlay->count(), 0);
    QVERIFY(overlay->top() == nullptr);
    QVERIFY(!overlay->blocksInput());

    QVERIFY(overlay->push(&first));
    QVERIFY(overlay->push(&second));
    QCOMPARE(overlay->count(), 2);
    QCOMPARE(overlay->top(), static_cast<QWidget*>(&second));
    QCOMPARE(overlay->depthOf(&first), 0);
    QCOMPARE(overlay->depthOf(&second), 1);
    QVERIFY(overlay->contains(&first));
    QVERIFY(overlay->contains(&second));
    QVERIFY(overlay->blocksInput());
    QVERIFY(overlay->hostWidget() != nullptr);
    QCOMPARE(overlay->hostWidget()->parentWidget(), static_cast<QWidget*>(&window));

    // A surface is not pushed twice; the stack stays one entry per surface.
    QVERIFY(!overlay->push(&first));
    QCOMPARE(overlay->count(), 2);

    QVERIFY(overlay->pop(&second));
    QCOMPARE(overlay->top(), static_cast<QWidget*>(&first));
    QCOMPARE(overlay->count(), 1);

    QVERIFY(overlay->pop(&first));
    QCOMPARE(overlay->count(), 0);
    QVERIFY(overlay->top() == nullptr);
    QCOMPARE(overlay->depthOf(&first), -1);
    QVERIFY(!overlay->pop(&first));
    QVERIFY(!overlay->blocksInput());
}

void TestOverlay::nestedPushIsTheZOrder()
{
    QWidget window;
    window.resize(400, 300);
    window.show();
    QTest::qWait(10);

    // Created in one order, pushed in another: only the push order may decide
    // which widget is on top.
    QWidget createdFirst(&window);
    createdFirst.setGeometry(0, 0, 400, 300);
    createdFirst.show();
    QWidget createdSecond(&window);
    createdSecond.setGeometry(0, 0, 400, 300);
    createdSecond.show();

    md::Overlay* overlay = md::Overlay::forWidget(&window);
    QVERIFY(overlay != nullptr);

    const QPoint centre = window.rect().center();

    QVERIFY(overlay->push(&createdFirst));
    QCOMPARE(window.childAt(centre), static_cast<QWidget*>(&createdFirst));

    QVERIFY(overlay->push(&createdSecond));
    QCOMPARE(window.childAt(centre), static_cast<QWidget*>(&createdSecond));
    QCOMPARE(overlay->top(), static_cast<QWidget*>(&createdSecond));

    // A third nested push goes above both; popping it leaves the second on
    // top without re-raising anything by hand.
    QWidget third(&window);
    third.setGeometry(0, 0, 400, 300);
    third.show();
    QVERIFY(overlay->push(&third));
    QCOMPARE(window.childAt(centre), static_cast<QWidget*>(&third));

    QVERIFY(overlay->pop(&third));
    third.hide();
    QCOMPARE(window.childAt(centre), static_cast<QWidget*>(&createdSecond));

    QVERIFY(overlay->pop(&createdSecond));
    createdSecond.hide();
    QCOMPARE(window.childAt(centre), static_cast<QWidget*>(&createdFirst));

    QVERIFY(overlay->pop(&createdFirst));
}

void TestOverlay::inputIsBlockedBelowTheTop()
{
    QWidget window;
    window.resize(400, 300);
    window.show();
    QTest::qWait(10);

    Probe content(&window);
    content.setGeometry(0, 0, 400, 300);
    content.show();

    Probe surface(&window);
    surface.setGeometry(0, 0, 400, 300);
    surface.show();

    md::Overlay* overlay = md::Overlay::forWidget(&window);
    QVERIFY(overlay != nullptr);
    QVERIFY(overlay->push(&surface, optionsFor(true, false)));

    // Everything below the top surface: pointer, key, shortcut.
    sendPress(content);
    sendKey(content, Qt::Key_A);
    sendKey(content, Qt::Key_Tab);
    sendShortcutOverride(content, Qt::Key_S);
    QCOMPARE(content.presses, 0);
    QCOMPARE(content.keys, 0);
    QCOMPARE(content.overrides, 0);

    // The top surface keeps its own events. QApplication::notify() sends a
    // synthetic ShortcutOverride before any non-spontaneous KeyPress (so a
    // recorded key still gets a chance to trigger a shortcut), so the count
    // is measured as a delta around our explicit override.
    sendPress(surface);
    sendKey(surface, Qt::Key_A);
    // QApplication::notify() sends a synthetic ShortcutOverride before a
    // non-spontaneous KeyPress, so the KeyPress above already delivered one to
    // the surface. Measuring the delta around the explicit override only keeps
    // the assertion about our filter instead of about Qt's own delivery.
    const int overridesBefore = surface.overrides;
    sendShortcutOverride(surface, Qt::Key_S);
    QCOMPARE(surface.presses, 1);
    QCOMPARE(surface.keys, 1);
    QCOMPARE(surface.overrides, overridesBefore + 1);

    // A non-blocking top surface lets the content through: this is the proof
    // that the filter's block is scoped to the stack, not a latch.
    QVERIFY(overlay->pop(&surface));
    QVERIFY(overlay->push(&surface, optionsFor(false, false)));

    sendPress(content);
    sendKey(content, Qt::Key_B);
    QCOMPARE(content.presses, 1);
    QCOMPARE(content.keys, 1);

    QVERIFY(overlay->pop(&surface));

    // After the last pop the filter is gone entirely; a plain event is
    // delivered without any Overlay in the way.
    sendPress(content);
    QCOMPARE(content.presses, 2);
}

void TestOverlay::contentIsDisabledOnlyWhileASurfaceAsks()
{
    QWidget window;
    window.resize(400, 300);
    window.show();
    QTest::qWait(10);

    QWidget root(&window);
    root.setGeometry(0, 0, 400, 200);
    root.show();

    QWidget child(&root);
    child.setGeometry(0, 0, 100, 40);
    child.show();

    QWidget first(&window);
    QWidget second(&window);
    first.show();
    second.show();

    md::Overlay* overlay = md::Overlay::forWidget(&window);
    QVERIFY(overlay != nullptr);

    // A surface that does not ask for it leaves the content alone.
    QVERIFY(overlay->push(&first, optionsFor(true, false)));
    QVERIFY(root.isEnabled());
    QVERIFY(overlay->pop(&first));

    // Outermost disable, then a nested surface that does not ask: the content
    // stays disabled until the last requesting surface is gone.
    QVERIFY(overlay->push(&first, optionsFor(true, true)));
    QVERIFY(!root.isEnabled());
    QVERIFY(!child.isEnabled());

    QVERIFY(overlay->push(&second, optionsFor(true, false)));
    QVERIFY(!root.isEnabled());
    QVERIFY(overlay->pop(&second));
    QVERIFY(!root.isEnabled());

    QVERIFY(overlay->pop(&first));
    QVERIFY(root.isEnabled());
    QVERIFY(child.isEnabled());

    // The request may also arrive after the push (the busy surface arms it
    // once its scrim is opaque) and still restores exactly once.
    QVERIFY(overlay->push(&first, optionsFor(true, false)));
    QVERIFY(root.isEnabled());
    QVERIFY(overlay->push(&second, optionsFor(true, false)));
    QVERIFY(overlay->setContentDisabled(&second, true));
    QVERIFY(!root.isEnabled());

    QVERIFY(overlay->pop(&second));
    QVERIFY(root.isEnabled()); // the only requesting surface is gone
    QVERIFY(overlay->pop(&first));
    QVERIFY(root.isEnabled());

    // setContentDisabled on a surface that is not pushed is a no-op.
    QVERIFY(!overlay->setContentDisabled(&first, true));
    QVERIFY(root.isEnabled());
}

void TestOverlay::alreadyDisabledStateSurvivesTheRestore()
{
    QWidget window;
    window.resize(400, 300);
    window.show();
    QTest::qWait(10);

    QWidget root(&window);
    root.setGeometry(0, 0, 400, 200);
    root.show();

    QWidget enabledChild(&root);
    enabledChild.show();
    QWidget disabledChild(&root);
    disabledChild.show();
    disabledChild.setEnabled(false);

    QWidget alreadyDisabledRoot(&window);
    alreadyDisabledRoot.setGeometry(0, 220, 100, 40);
    alreadyDisabledRoot.show();
    alreadyDisabledRoot.setEnabled(false);

    QWidget surface(&window);
    surface.show();

    md::Overlay* overlay = md::Overlay::forWidget(&window);
    QVERIFY(overlay != nullptr);
    QVERIFY(overlay->push(&surface, optionsFor(true, true)));

    QVERIFY(!root.isEnabled());
    QVERIFY(!enabledChild.isEnabled());
    QVERIFY(!disabledChild.isEnabled());
    QVERIFY(!alreadyDisabledRoot.isEnabled());

    QVERIFY(overlay->pop(&surface));
    QVERIFY(root.isEnabled());
    QVERIFY(enabledChild.isEnabled());
    // A descendant that was already disabled keeps its own disabled flag; it
    // must not be switched on by the root's restore.
    QVERIFY(!disabledChild.isEnabled());
    QVERIFY(!alreadyDisabledRoot.isEnabled());
}

void TestOverlay::focusIsRestoredOnTheLastPop()
{
    QWidget window;
    window.resize(400, 300);
    window.show();
    QTest::qWait(10);

    QWidget root(&window);
    root.setGeometry(0, 0, 400, 300);
    root.show();

    Probe invoker(&root);
    invoker.setGeometry(10, 10, 100, 30);
    invoker.setFocusPolicy(Qt::StrongFocus);
    invoker.show();

    Probe captured(&root);
    captured.setGeometry(10, 60, 100, 30);
    captured.setFocusPolicy(Qt::StrongFocus);
    captured.show();

    captured.setFocus(Qt::OtherFocusReason);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(&captured));

    QWidget surface(&window);
    surface.setFocusPolicy(Qt::StrongFocus);
    surface.show();

    md::Overlay* overlay = md::Overlay::forWidget(&window);
    QVERIFY(overlay != nullptr);

    md::Overlay::Options options = optionsFor(true, false);
    options.invoker = &invoker;
    QVERIFY(overlay->push(&surface, options));

    // The push does not steal focus; the surface does that itself.
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(&captured));
    surface.setFocus(Qt::OtherFocusReason);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(&surface));

    QVERIFY(overlay->pop(&surface));
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(&captured));

    // If the captured widget is no longer usable, the invoker is the fallback.
    QVERIFY(overlay->push(&surface, options));
    captured.hide();
    QVERIFY(overlay->pop(&surface));
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(&invoker));
}

void TestOverlay::nestedPopsRestoreOnlyOnce()
{
    QWidget window;
    window.resize(400, 300);
    window.show();
    QTest::qWait(10);

    QWidget root(&window);
    root.setGeometry(0, 0, 400, 300);
    root.show();

    Probe content(&root);
    content.setGeometry(10, 10, 100, 30);
    content.setFocusPolicy(Qt::StrongFocus);
    content.show();
    content.setFocus(Qt::OtherFocusReason);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(&content));

    QWidget outer(&window);
    outer.setFocusPolicy(Qt::StrongFocus);
    outer.show();
    QWidget inner(&window);
    inner.setFocusPolicy(Qt::StrongFocus);
    inner.show();

    md::Overlay* overlay = md::Overlay::forWidget(&window);
    QVERIFY(overlay != nullptr);
    QVERIFY(overlay->push(&outer, optionsFor(true, false)));
    QVERIFY(overlay->push(&inner, optionsFor(true, false)));

    outer.setFocus(Qt::OtherFocusReason);
    QVERIFY(overlay->pop(&inner));
    // The inner pop is not the last one: no focus restore.
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(&outer));

    QVERIFY(overlay->pop(&outer));
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(&content));
}

void TestOverlay::surfaceDestroyedWhilePushed()
{
    QWidget window;
    window.resize(400, 300);
    window.show();
    QTest::qWait(10);

    QWidget root(&window);
    root.setGeometry(0, 0, 400, 300);
    root.show();

    Probe content(&root);
    content.setGeometry(10, 10, 100, 30);
    content.setFocusPolicy(Qt::StrongFocus);
    content.show();
    content.setFocus(Qt::OtherFocusReason);

    auto* surface = new QWidget(&window);
    surface->show();

    md::Overlay* overlay = md::Overlay::forWidget(&window);
    QVERIFY(overlay != nullptr);
    QVERIFY(overlay->push(surface, optionsFor(true, true)));
    QCOMPARE(overlay->count(), 1);
    QVERIFY(!root.isEnabled());

    delete surface;

    // The window-level restore for a surface destroyed while pushed is queued
    // to the next event-loop turn, after ~QWidget has finished tearing the
    // widget down. The entry itself is gone immediately.
    QCOMPARE(overlay->count(), 0);
    QVERIFY(overlay->top() == nullptr);

    QCoreApplication::processEvents();

    QVERIFY(root.isEnabled());
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(&content));

    sendPress(content);
    QCOMPARE(content.presses, 1);
}

void TestOverlay::windowDestroyedWhilePushed()
{
    auto* window = new QWidget;
    window->resize(400, 300);
    window->show();

    auto* root = new QWidget(window);
    root->setGeometry(0, 0, 400, 300);
    root->show();

    auto* surface = new QWidget(window);
    surface->show();

    md::Overlay* overlay = md::Overlay::forWidget(window);
    QVERIFY(overlay != nullptr);
    QVERIFY(overlay->push(surface, optionsFor(true, true)));

    QPointer<md::Overlay> guard(overlay);
    delete window;

    // The overlay and its host were children of the window: no leak, no
    // dangling window pointer, and no application filter left behind.
    QVERIFY(guard.isNull());

    QWidget fresh;
    fresh.resize(320, 240);
    fresh.show();

    Probe content(&fresh);
    content.setGeometry(0, 0, 320, 240);
    content.show();

    sendPress(content);
    QCOMPARE(content.presses, 1);
}

QTEST_MAIN(TestOverlay)
#include "tst_overlay.moc"
