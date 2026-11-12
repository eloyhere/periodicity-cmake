// Regression tests for the keyboard focus indicator and for chained mention
// triggers.
//
// Focus: a widget's painter is clipped to the widget rectangle. The focus ring
// used to be stroked 2 dp outside the shape, so a focused widget showed only
// the fragments of the ring's rounded corner arcs that fell back inside the
// bounds: a 1 px dot in each corner (and, on a pill, clipped arcs that read as
// a broken outline). The ring now lies inside the bounds; these tests pin the
// two visible properties that distinguishes the fix from the bug: nothing is
// painted outside the widget, the exact corners stay background, and a
// continuous 2 dp secondary band hugs the inside of every edge. Following M3,
// the ring is a keyboard indicator: a focus gained by a mouse press relies on
// the state layer alone.
//
// Mention: every '@' on a word boundary starts a trigger. A second '@' retargets
// the single popup instead of stacking a new one, so "@alice @bob" can be typed
// and each mention can be committed in turn. A '@' inside a word stays literal,
// deleting the trigger closes the list, and synthetic key events do not hang.

#include <md/components/cascader.h>
#include <md/components/checkbox.h>
#include <md/components/mention.h>
#include <md/theme/theme.h>
#include <md/tokens/shape.h>
#include <md/view/view.h>

#include <QColor>
#include <QImage>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QWidget>

namespace {

constexpr int kDarkThreshold = 160;
constexpr int kColourTolerance = 70;

// A host with a known white background: the pixel under a widget corner that
// the widget's rounded shape does not cover is then unambiguous.
class PlainHost final : public QWidget
{
public:
    explicit PlainHost(QWidget* parent = nullptr)
        : QWidget(parent)
    {
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::white);
    }
};

// A bare View: enough to exercise the focus-ring stage without a component's
// own content getting in the way.
class FocusProbe final : public md::View
{
public:
    explicit FocusProbe(QWidget* parent = nullptr)
        : View(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setShape(md::Shape::medium());
        setBackgroundColour(QColor(0xF7, 0xF2, 0xFA));
    }

protected:
    void measureOverride(md::MeasureSpec width, md::MeasureSpec height) override
    {
        setMeasuredSize(QSizeF(width.resolve(160.0), height.resolve(100.0)));
    }

    void paintContent(QPainter&, const QRectF&) override {}
};

QPoint imagePoint(const QPoint& hostPoint, qreal ratio)
{
    return QPoint(qRound(hostPoint.x() * ratio), qRound(hostPoint.y() * ratio));
}

QColor colourAt(const QImage& image, const QPoint& hostPoint, qreal ratio)
{
    return image.pixelColor(imagePoint(hostPoint, ratio));
}

bool nearColour(const QColor& lhs, const QColor& rhs, int tolerance = kColourTolerance)
{
    return qAbs(lhs.red() - rhs.red()) <= tolerance
        && qAbs(lhs.green() - rhs.green()) <= tolerance
        && qAbs(lhs.blue() - rhs.blue()) <= tolerance;
}

// True when any pixel of the rectangle is darker than the threshold. The four
// corner dots were drawn in the secondary role (lightness ~102), while the
// white page and the focused state layer stay above 200.
bool anyDark(const QImage& image, const QPoint& topLeft, int width, int height,
             qreal ratio)
{
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const QColor pixel = colourAt(image, topLeft + QPoint(x, y), ratio);
            if (pixel.lightness() < kDarkThreshold)
                return true;
        }
    }
    return false;
}

// The 1 px frame around a rectangle, expressed as a rectangle of samples.
bool frameIsClean(const QImage& image, const QRect& hostRect, qreal ratio)
{
    const int left = hostRect.left() - 1;
    const int top = hostRect.top() - 1;
    const int right = hostRect.right() + 1;
    const int bottom = hostRect.bottom() + 1;

    for (int x = left; x <= right; ++x) {
        if (colourAt(image, QPoint(x, top), ratio).lightness() < kDarkThreshold
            || colourAt(image, QPoint(x, bottom), ratio).lightness() < kDarkThreshold) {
            return false;
        }
    }
    for (int y = top; y <= bottom; ++y) {
        if (colourAt(image, QPoint(left, y), ratio).lightness() < kDarkThreshold
            || colourAt(image, QPoint(right, y), ratio).lightness() < kDarkThreshold) {
            return false;
        }
    }
    return true;
}

void showHost(PlainHost& host)
{
    host.show();
    host.activateWindow();
    // The test only needs a mapped window; offscreen platforms always expose.
    (void)QTest::qWaitForWindowExposed(&host);
}

} // namespace

class TestFocusAndMention : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void checkboxFocusLeavesNoDarkCorner();
    void cascaderFocusLeavesNoDarkCorner();
    void focusRingIsInsideAndContinuous();
    void focusRingOnlyForKeyboardFocus();

    void mentionFirstTriggerFiltersAndCommits();
    void mentionSecondTriggerAfterCommit();
    void mentionTwoMentionsTextAndCursor();
    void mentionSecondTriggerDoesNotStack();
    void mentionDeletingTriggerClosesPopup();
    void mentionCursorInTheMiddleRetriggers();
    void mentionKeyClicksAtDoesNotHang();

private:
    md::Mention* createMention(QWidget* parent);
};

void TestFocusAndMention::init()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestFocusAndMention::cleanup()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

md::Mention* TestFocusAndMention::createMention(QWidget* parent)
{
    auto* mention = new md::Mention(parent);
    mention->setGeometry(20, 20, 560, 200);
    mention->setCandidates({QStringLiteral("alice"), QStringLiteral("bob"),
                            QStringLiteral("grace"), QStringLiteral("linus")});
    return mention;
}

void TestFocusAndMention::checkboxFocusLeavesNoDarkCorner()
{
    PlainHost host;
    host.resize(180, 140);

    auto* box = new md::CheckBox(&host);
    box->setGeometry(40, 40, 48, 48);
    box->setChecked(true);
    showHost(host);

    box->setFocus(Qt::TabFocusReason);
    QVERIFY(box->hasFocus());
    QTest::qWait(10);

    const QImage image = host.grab().toImage();
    const qreal ratio = image.devicePixelRatio();
    const QPoint origin = box->mapTo(&host, QPoint(0, 0));

    // The exact corners are outside the rounded shape. The old outside ring
    // left its corner-arc fragments there; the inside ring cannot reach them.
    QVERIFY(!anyDark(image, origin, 2, 2, ratio));
    QVERIFY(!anyDark(image, origin + QPoint(46, 0), 2, 2, ratio));
    QVERIFY(!anyDark(image, origin + QPoint(0, 46), 2, 2, ratio));
    QVERIFY(!anyDark(image, origin + QPoint(46, 46), 2, 2, ratio));

    // The widget paints nothing outside its own rectangle.
    QVERIFY(frameIsClean(image, QRect(origin, QSize(48, 48)), ratio));

    // The 2 dp secondary ring is still visible just inside the left edge.
    const QColor secondary = md::Theme::baselineLight().colours().secondary;
    QVERIFY(nearColour(colourAt(image, origin + QPoint(1, 24), ratio), secondary));
}

void TestFocusAndMention::cascaderFocusLeavesNoDarkCorner()
{
    PlainHost host;
    host.resize(420, 240);

    auto* cascader = new md::Cascader(&host);
    cascader->setGeometry(30, 30, 360, 110);

    md::CascaderNode root;
    root.label = QStringLiteral("Root");
    md::CascaderNode child;
    child.label = QStringLiteral("Child");
    root.children.append(child);
    cascader->setLevels({root});

    showHost(host);

    cascader->setFocus(Qt::TabFocusReason);
    QVERIFY(cascader->hasFocus());
    QTest::qWait(10);

    const QImage image = host.grab().toImage();
    const qreal ratio = image.devicePixelRatio();
    const QPoint origin = cascader->mapTo(&host, QPoint(0, 0));

    QVERIFY(!anyDark(image, origin, 2, 2, ratio));
    QVERIFY(!anyDark(image, origin + QPoint(358, 0), 2, 2, ratio));
    QVERIFY(!anyDark(image, origin + QPoint(0, 108), 2, 2, ratio));
    QVERIFY(!anyDark(image, origin + QPoint(358, 108), 2, 2, ratio));

    QVERIFY(frameIsClean(image, QRect(origin, QSize(360, 110)), ratio));

    const QColor secondary = md::Theme::baselineLight().colours().secondary;
    QVERIFY(nearColour(colourAt(image, origin + QPoint(1, 55), ratio), secondary));
}

void TestFocusAndMention::focusRingIsInsideAndContinuous()
{
    PlainHost host;
    host.resize(260, 200);

    auto* probe = new FocusProbe(&host);
    probe->setGeometry(50, 50, 160, 100);
    showHost(host);

    QTest::qWait(10);
    const QImage unfocused = host.grab().toImage();

    probe->setFocus(Qt::TabFocusReason);
    QVERIFY(probe->hasFocus());
    QTest::qWait(10);
    const QImage focused = host.grab().toImage();

    const qreal ratio = focused.devicePixelRatio();
    const QPoint origin = probe->mapTo(&host, QPoint(0, 0));
    const QRect hostRect(origin, QSize(160, 100));
    const QColor secondary = md::Theme::baselineLight().colours().secondary;

    // A continuous band on all four edges: the old clipped ring had dark
    // pixels only near the corners, so any of these four probes failed.
    QVERIFY(nearColour(colourAt(focused, origin + QPoint(1, 50), ratio), secondary));
    QVERIFY(nearColour(colourAt(focused, origin + QPoint(158, 50), ratio), secondary));
    QVERIFY(nearColour(colourAt(focused, origin + QPoint(80, 1), ratio), secondary));
    QVERIFY(nearColour(colourAt(focused, origin + QPoint(80, 98), ratio), secondary));

    // The corners are not painted, so no corner dot survives.
    QVERIFY(!anyDark(focused, origin, 2, 2, ratio));
    QVERIFY(!anyDark(focused, origin + QPoint(158, 0), 2, 2, ratio));
    QVERIFY(!anyDark(focused, origin + QPoint(0, 98), 2, 2, ratio));
    QVERIFY(!anyDark(focused, origin + QPoint(158, 98), 2, 2, ratio));

    // Focusing must not paint outside the widget: the frame is identical.
    for (int x = hostRect.left() - 1; x <= hostRect.right() + 1; ++x) {
        QCOMPARE(colourAt(focused, QPoint(x, hostRect.top() - 1), ratio),
                 colourAt(unfocused, QPoint(x, hostRect.top() - 1), ratio));
        QCOMPARE(colourAt(focused, QPoint(x, hostRect.bottom() + 1), ratio),
                 colourAt(unfocused, QPoint(x, hostRect.bottom() + 1), ratio));
    }
    for (int y = hostRect.top() - 1; y <= hostRect.bottom() + 1; ++y) {
        QCOMPARE(colourAt(focused, QPoint(hostRect.left() - 1, y), ratio),
                 colourAt(unfocused, QPoint(hostRect.left() - 1, y), ratio));
        QCOMPARE(colourAt(focused, QPoint(hostRect.right() + 1, y), ratio),
                 colourAt(unfocused, QPoint(hostRect.right() + 1, y), ratio));
    }
}

void TestFocusAndMention::focusRingOnlyForKeyboardFocus()
{
    PlainHost host;
    host.resize(300, 260);

    auto* probe = new FocusProbe(&host);
    probe->setGeometry(50, 50, 160, 100);

    // A second focusable view lets the test move the focus away and then hand
    // it back with a keyboard reason; focusing an already-focused widget does
    // not deliver a new focusInEvent.
    auto* other = new FocusProbe(&host);
    other->setGeometry(50, 190, 120, 30);

    showHost(host);
    QTest::qWait(10);

    const QPoint origin = probe->mapTo(&host, QPoint(0, 0));
    const QRect probeRect(origin, QSize(160, 100));
    const QColor secondary = md::Theme::baselineLight().colours().secondary;

    // M3: a mouse press focuses the component but draws no outline; the state
    // layer is the feedback. This is the "Button gets a black outline on
    // click" report. Move the focus away first: clicking an already-focused
    // widget does not deliver a new focusInEvent.
    other->setFocus(Qt::OtherFocusReason);
    QVERIFY(other->hasFocus());

    QTest::mouseClick(probe, Qt::LeftButton, Qt::NoModifier, QPoint(80, 50));
    QVERIFY(probe->hasFocus());
    QTest::qWait(10);

    const QImage clicked = host.grab().toImage();
    const qreal clickedRatio = clicked.devicePixelRatio();
    QVERIFY(colourAt(clicked, origin + QPoint(1, 50), clickedRatio).lightness()
            > kDarkThreshold);
    QVERIFY(!anyDark(clicked, origin, 2, 2, clickedRatio));
    QVERIFY(frameIsClean(clicked, probeRect, clickedRatio));

    // Keyboard focus draws the clean inside ring again.
    other->setFocus(Qt::TabFocusReason);
    QVERIFY(other->hasFocus());
    probe->setFocus(Qt::TabFocusReason);
    QVERIFY(probe->hasFocus());
    QTest::qWait(10);

    const QImage tabbed = host.grab().toImage();
    const qreal tabbedRatio = tabbed.devicePixelRatio();
    QVERIFY(nearColour(colourAt(tabbed, origin + QPoint(1, 50), tabbedRatio), secondary));
    QVERIFY(!anyDark(tabbed, origin, 2, 2, tabbedRatio));
    QVERIFY(frameIsClean(tabbed, probeRect, tabbedRatio));
}

void TestFocusAndMention::mentionFirstTriggerFiltersAndCommits()
{
    PlainHost host;
    host.resize(640, 280);
    auto* mention = createMention(&host);
    showHost(host);

    QTextEdit* editor = mention->editor();
    editor->setFocus(Qt::TabFocusReason);
    QVERIFY(editor->hasFocus());

    QTest::keyClicks(editor, QStringLiteral("@"));
    QVERIFY(mention->isCandidatePopupVisible());
    QCOMPARE(mention->visibleCandidates().size(), 4);

    QTest::keyClicks(editor, QStringLiteral("al"));
    QCOMPARE(mention->visibleCandidates(), QStringList{QStringLiteral("alice")});

    QTest::keyClick(editor, Qt::Key_Return);
    QVERIFY(!mention->isCandidatePopupVisible());
    QCOMPARE(mention->plainText(), QStringLiteral("@alice"));
    QCOMPARE(mention->mentionCount(), 1);
}

void TestFocusAndMention::mentionSecondTriggerAfterCommit()
{
    PlainHost host;
    host.resize(640, 280);
    auto* mention = createMention(&host);
    showHost(host);

    QTextEdit* editor = mention->editor();
    editor->setFocus(Qt::TabFocusReason);
    QVERIFY(editor->hasFocus());

    QTest::keyClicks(editor, QStringLiteral("@al"));
    QVERIFY(mention->isCandidatePopupVisible());
    QTest::keyClick(editor, Qt::Key_Return);
    QCOMPARE(mention->mentionCount(), 1);

    // The space closes the first list; the fresh '@' opens a second one.
    QTest::keyClicks(editor, QStringLiteral(" @"));
    QVERIFY(mention->isCandidatePopupVisible());
    QCOMPARE(mention->visibleCandidates().size(), 4);
    QCOMPARE(mention->mentionCount(), 1);

    QTest::keyClicks(editor, QStringLiteral("bo"));
    QCOMPARE(mention->visibleCandidates(), QStringList{QStringLiteral("bob")});

    QTest::keyClick(editor, Qt::Key_Return);
    QVERIFY(!mention->isCandidatePopupVisible());
    QCOMPARE(mention->mentionCount(), 2);
    QCOMPARE(mention->plainText(), QStringLiteral("@alice @bob"));

    // A newline is a word boundary as well: the trigger opens on a fresh line.
    mention->setText(QString());
    QTest::keyClicks(editor, QStringLiteral("note"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, QStringLiteral("@"));
    QVERIFY(mention->isCandidatePopupVisible());
    QTest::keyClick(editor, Qt::Key_Escape);
}

void TestFocusAndMention::mentionTwoMentionsTextAndCursor()
{
    PlainHost host;
    host.resize(640, 280);
    auto* mention = createMention(&host);
    showHost(host);

    QTextEdit* editor = mention->editor();
    editor->setFocus(Qt::TabFocusReason);
    QVERIFY(editor->hasFocus());

    QTest::keyClicks(editor, QStringLiteral("@al"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, QStringLiteral(" @bo"));
    QTest::keyClick(editor, Qt::Key_Return);

    QCOMPARE(mention->plainText(), QStringLiteral("@alice @bob"));

    const QList<md::MentionSpan> spans = mention->mentions();
    QCOMPARE(spans.size(), 2);
    QCOMPARE(spans.at(0).start, 0);
    QCOMPARE(spans.at(0).text, QStringLiteral("@alice"));
    QCOMPARE(spans.at(1).start, 7);
    QCOMPARE(spans.at(1).text, QStringLiteral("@bob"));

    // The document holds two atomic nodes separated by a space, so the caret
    // sits at the end of the document.
    QCOMPARE(editor->textCursor().position(),
             editor->document()->characterCount() - 1);
}

void TestFocusAndMention::mentionSecondTriggerDoesNotStack()
{
    PlainHost host;
    host.resize(640, 280);
    auto* mention = createMention(&host);
    showHost(host);

    QTextEdit* editor = mention->editor();
    editor->setFocus(Qt::TabFocusReason);
    QVERIFY(editor->hasFocus());

    // A '@' inside a word is literal: it must not create a second trigger and
    // the caret's own signal closes the first list.
    QTest::keyClicks(editor, QStringLiteral("@"));
    QVERIFY(mention->isCandidatePopupVisible());
    QTest::keyClicks(editor, QStringLiteral("@"));
    QVERIFY(!mention->isCandidatePopupVisible());
    QCOMPARE(mention->plainText(), QStringLiteral("@@"));

    // Exactly one list widget exists: a new trigger retargets it instead of
    // constructing another popup.
    QCOMPARE(mention->findChildren<QListWidget*>().size(), 1);

    // A candidate with a space keeps the first list open across the space, so
    // this exercises the boundary '@' while a popup really is visible.
    mention->setText(QString());
    mention->setCandidates({QStringLiteral("alice smith"), QStringLiteral("bob")});

    QTest::keyClicks(editor, QStringLiteral("@alice "));
    QVERIFY(mention->isCandidatePopupVisible());
    QCOMPARE(mention->visibleCandidates(),
             QStringList{QStringLiteral("alice smith")});

    QTest::keyClicks(editor, QStringLiteral("@"));
    QVERIFY(mention->isCandidatePopupVisible());
    QCOMPARE(mention->visibleCandidates().size(), 2);
    QCOMPARE(mention->findChildren<QListWidget*>().size(), 1);

    QTest::keyClicks(editor, QStringLiteral("bo"));
    QCOMPARE(mention->visibleCandidates(), QStringList{QStringLiteral("bob")});
    QTest::keyClick(editor, Qt::Key_Return);
    QVERIFY(!mention->isCandidatePopupVisible());
    QCOMPARE(mention->plainText(), QStringLiteral("@alice @bob"));
}

void TestFocusAndMention::mentionDeletingTriggerClosesPopup()
{
    PlainHost host;
    host.resize(640, 280);
    auto* mention = createMention(&host);
    showHost(host);

    QTextEdit* editor = mention->editor();
    editor->setFocus(Qt::TabFocusReason);
    QVERIFY(editor->hasFocus());

    QTest::keyClicks(editor, QStringLiteral("@al"));
    QVERIFY(mention->isCandidatePopupVisible());

    // Delete the '@' itself, not the query: the list closes in textChanged,
    // before the caret notification can arrive.
    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(1);
    editor->setTextCursor(cursor);
    QTest::keyClick(editor, Qt::Key_Backspace);
    QVERIFY(!mention->isCandidatePopupVisible());

    // Deleting every character of a fresh trigger has the same effect.
    mention->setText(QString());
    QTest::keyClicks(editor, QStringLiteral("@"));
    QVERIFY(mention->isCandidatePopupVisible());
    QTest::keyClick(editor, Qt::Key_Backspace);
    QVERIFY(!mention->isCandidatePopupVisible());
}

void TestFocusAndMention::mentionCursorInTheMiddleRetriggers()
{
    PlainHost host;
    host.resize(640, 280);
    auto* mention = createMention(&host);
    showHost(host);

    QTextEdit* editor = mention->editor();
    editor->setFocus(Qt::TabFocusReason);
    QVERIFY(editor->hasFocus());

    QTest::keyClicks(editor, QStringLiteral("hello world"));
    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(6); // after "hello ", before "world"
    editor->setTextCursor(cursor);

    QTest::keyClicks(editor, QStringLiteral("@"));
    QVERIFY(mention->isCandidatePopupVisible());
    QCOMPARE(mention->plainText(), QStringLiteral("hello @world"));

    QTest::keyClick(editor, Qt::Key_Escape);
    QVERIFY(!mention->isCandidatePopupVisible());
}

void TestFocusAndMention::mentionKeyClicksAtDoesNotHang()
{
    PlainHost host;
    host.resize(640, 280);
    auto* mention = createMention(&host);
    showHost(host);

    QTextEdit* editor = mention->editor();
    editor->setFocus(Qt::TabFocusReason);
    QVERIFY(editor->hasFocus());

    // QTest::keyClicks drives the same key path the freeze used to wedge. The
    // event loop must return, and the popup state must stay stable across a
    // wait instead of flipping or blocking in refreshPopup().
    QTest::keyClicks(editor, QStringLiteral("@"));
    QVERIFY(mention->isCandidatePopupVisible());
    const bool open = mention->isCandidatePopupVisible();
    QTest::qWait(50);
    QCOMPARE(mention->isCandidatePopupVisible(), open);
    QVERIFY(mention->isVisible());

    // A literal second '@' must also return, and the editor stays responsive.
    QTest::keyClicks(editor, QStringLiteral("@"));
    QTest::qWait(20);
    QCOMPARE(mention->plainText(), QStringLiteral("@@"));
    QVERIFY(mention->isVisible());

    // And a trigger after a space still opens, proving the loop is live.
    QTest::keyClicks(editor, QStringLiteral(" @"));
    QVERIFY(mention->isCandidatePopupVisible());
    QTest::qWait(20);
    QVERIFY(mention->isCandidatePopupVisible());
}

QTEST_MAIN(TestFocusAndMention)
#include "tst_focus_and_mention.moc"
