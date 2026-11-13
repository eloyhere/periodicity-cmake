// Unit tests for the display and navigation components (framework.md §14).
//
// The tests are offscreen and deterministic: the application theme is pinned to
// the baseline light scheme, widgets are sized with arrange() (the framework's
// layout entry point) instead of a real layout, and pointer/key input is
// delivered with QApplication::sendEvent, exactly as tests/components already
// does for the basic widgets.
//
// The assertions target what the components promise rather than their pixels:
// a typography role resolves to the matching type token, a list selection moves
// with the arrow keys, a recycler realises only the visible window and reuses
// the widgets after a scroll, a marquee starts its timer only when the text
// overflows, and so on.

#include <QAbstractListModel>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QImage>
#include <QKeyEvent>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QRegion>
#include <QSignalSpy>
#include <QSize>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QTest>
#include <QVariant>
#include <QWheelEvent>
#include <QWidget>

#include <md/components/banner.h>
#include <md/components/button.h>
#include <md/components/guide.h>
#include <md/components/list.h>
#include <md/components/marquee.h>
#include <md/components/popup.h>
#include <md/components/recycler.h>
#include <md/components/tab.h>
#include <md/components/tabbar.h>
#include <md/components/text.h>
#include <md/components/typography.h>
#include <md/theme/theme.h>
#include <md/tokens/color_scheme.h>
#include <md/tokens/type_scale.h>

namespace {

// A flat model with two view types and mutation helpers, so one fixture covers
// selection, recycling and the model-change notifications.
class StringModel : public QAbstractListModel
{
public:
    enum Role {
        ViewTypeRole = Qt::UserRole + 1,
    };

    explicit StringModel(int count, QObject* parent = nullptr)
        : QAbstractListModel(parent)
    {
        rows_.reserve(count);
        for (int row = 0; row < count; ++row)
            rows_.append(QStringLiteral("Row %1").arg(row));
    }

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(rows_.size());
    }

    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0
            || index.row() >= static_cast<int>(rows_.size())) {
            return {};
        }

        switch (role) {
        case Qt::DisplayRole:
            return rows_.at(index.row());
        case ViewTypeRole:
            return index.row() % 2;
        default:
            return {};
        }
    }

    void appendRow(const QString& text)
    {
        const int row = static_cast<int>(rows_.size());
        beginInsertRows(QModelIndex(), row, row);
        rows_.append(text);
        endInsertRows();
    }

    void removeLastRow()
    {
        const int row = static_cast<int>(rows_.size()) - 1;
        if (row < 0)
            return;

        beginRemoveRows(QModelIndex(), row, row);
        rows_.removeLast();
        endRemoveRows();
    }

    void renameRow(int row, const QString& text)
    {
        if (row < 0 || row >= static_cast<int>(rows_.size()))
            return;

        rows_[row] = text;
        emit dataChanged(index(row, 0), index(row, 0), {Qt::DisplayRole});
    }

private:
    QStringList rows_;
};

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

int paintedPixels(const QImage& image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) != 0)
                ++count;
        }
    }

    return count;
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

class TestDisplayControls : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void typographyRoles();
    void typographyWrapsAndPaints();

    void listSelectionAndKeyboard();
    void listClicksAndScrolling();
    void listSurvivesItsDelegateBeingDestroyed();
    void firstVisibleRowStaysInsideModel();

    void recyclerVirtualisation();
    void recyclerReactsToModelChanges();
    void removedSelectionEmitsInvalidIndex();

    void popUpOpensClosesAndDismisses();

    void tabBarSelectionAndSignal();

    void bannerActionsAndClose();

    void marqueeScrollsOnlyWhenOverflowing();

    void guideStepProgression();
};

void TestDisplayControls::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestDisplayControls::typographyRoles()
{
    const md::TypeScale& scale = md::Theme::applicationTheme().type();

    struct Case
    {
        md::Typography::TypeRole role;
        qreal sizeSp;
    };

    const Case cases[] = {
        {md::Typography::TypeRole::DisplayLarge, scale.displayLarge.sizeSp},
        {md::Typography::TypeRole::DisplayMedium, scale.displayMedium.sizeSp},
        {md::Typography::TypeRole::DisplaySmall, scale.displaySmall.sizeSp},
        {md::Typography::TypeRole::HeadlineLarge, scale.headlineLarge.sizeSp},
        {md::Typography::TypeRole::HeadlineMedium, scale.headlineMedium.sizeSp},
        {md::Typography::TypeRole::HeadlineSmall, scale.headlineSmall.sizeSp},
        {md::Typography::TypeRole::TitleLarge, scale.titleLarge.sizeSp},
        {md::Typography::TypeRole::TitleMedium, scale.titleMedium.sizeSp},
        {md::Typography::TypeRole::TitleSmall, scale.titleSmall.sizeSp},
        {md::Typography::TypeRole::BodyLarge, scale.bodyLarge.sizeSp},
        {md::Typography::TypeRole::BodyMedium, scale.bodyMedium.sizeSp},
        {md::Typography::TypeRole::BodySmall, scale.bodySmall.sizeSp},
        {md::Typography::TypeRole::LabelLarge, scale.labelLarge.sizeSp},
        {md::Typography::TypeRole::LabelMedium, scale.labelMedium.sizeSp},
        {md::Typography::TypeRole::LabelSmall, scale.labelSmall.sizeSp},
    };

    for (const Case& entry : cases) {
        md::Typography label(QStringLiteral("Ag"), entry.role);
        QVERIFY(label.role() == entry.role);
        QCOMPARE(label.textStyle().sizeSp, entry.sizeSp);
        QVERIFY(label.textStyle().weight == md::Typography::styleFor(entry.role, scale).weight);
        QVERIFY(label.sizeHint().height() > 0);
    }

    // The scale is ordered: display is larger than label.
    md::Typography display(QStringLiteral("Ag"), md::Typography::TypeRole::DisplayLarge);
    md::Typography small(QStringLiteral("Ag"), md::Typography::TypeRole::LabelSmall);
    QVERIFY(display.textStyle().sizeSp > small.textStyle().sizeSp);
    QVERIFY(display.sizeHint().height() > small.sizeHint().height());

    // An explicit style wins until a role is set again.
    md::TextStyle custom = display.textStyle();
    custom.sizeSp = 11.0;
    display.setTextStyle(custom);
    QCOMPARE(display.textStyle().sizeSp, 11.0);
    display.setRole(md::Typography::TypeRole::DisplayLarge);
    QCOMPARE(display.textStyle().sizeSp, scale.displayLarge.sizeSp);

    // Text's four role names map onto the full scale.
    small.setRole(md::Text::TextRole::Caption);
    QVERIFY(small.role() == md::Typography::TypeRole::BodySmall);
    QVERIFY(small.textRole() == md::Text::TextRole::Caption);

    small.setRole(md::Text::TextRole::Title);
    QVERIFY(small.role() == md::Typography::TypeRole::TitleMedium);
}

void TestDisplayControls::typographyWrapsAndPaints()
{
    md::Typography title(QStringLiteral("Typography"),
                         md::Typography::TypeRole::HeadlineSmall);
    title.setAlignment(Qt::AlignCenter | Qt::AlignVCenter);
    title.resize(title.sizeHint());

    QVERIFY(title.sizeHint().width() > 0);
    QVERIFY(paintedPixels(renderWidget(title)) > 0);

    md::Typography wrapped(QStringLiteral("The quick brown fox jumps over the lazy dog"),
                           md::Typography::TypeRole::BodyLarge);
    wrapped.setWordWrap(true);

    const int narrow = wrapped.heightForWidth(80);
    const int wide = wrapped.heightForWidth(400);
    QVERIFY(narrow > wide);
    QVERIFY(wide > 0);

    // A custom colour is painted, not the role's default.
    md::Typography coloured(QStringLiteral("X"), md::Typography::TypeRole::BodyMedium);
    coloured.setColour(md::Theme::applicationTheme().colours().error);
    coloured.resize(48, 48);

    const QImage image = renderWidget(coloured);
    QVERIFY(paintedPixels(image) > 0);
}

void TestDisplayControls::listSelectionAndKeyboard()
{
    StringModel model(30);

    md::List list;
    list.setModel(&model);
    list.setRowHeight(40.0);
    list.arrange(QRect(0, 0, 240, 200));

    QCOMPARE(list.rowCount(), 30);
    QCOMPARE(list.visibleRowCount(), 6);
    QCOMPARE(list.firstVisibleRow(), 0);

    QSignalSpy currentSpy(&list, &md::List::currentIndexChanged);

    list.setCurrentIndex(list.indexAt(2));
    QCOMPARE(list.currentRow(), 2);
    QCOMPARE(currentSpy.count(), 1);

    sendKey(list, Qt::Key_Down);
    QCOMPARE(list.currentRow(), 3);

    sendKey(list, Qt::Key_Up);
    QCOMPARE(list.currentRow(), 2);

    sendKey(list, Qt::Key_Home);
    QCOMPARE(list.currentRow(), 0);

    sendKey(list, Qt::Key_End);
    QCOMPARE(list.currentRow(), 29);
    QVERIFY(list.scrollOffset() > 0.0);

    // End scrolls the last row into view.
    QVERIFY(list.rowRect(29).bottom() <= 200.0);
    QVERIFY(list.rowRect(29).top() >= 0.0);

    // Clearing notifies with an invalid index.
    list.clearSelection();
    QCOMPARE(list.currentRow(), -1);
    QCOMPARE(list.currentIndex().isValid(), false);
}

void TestDisplayControls::listClicksAndScrolling()
{
    StringModel model(20);

    md::List list;
    list.setModel(&model);
    list.setRowHeight(40.0);
    list.arrange(QRect(0, 0, 240, 200));

    QSignalSpy clickSpy(&list, &md::List::clicked);
    QSignalSpy doubleSpy(&list, &md::List::doubleClicked);

    clickAt(list, QPointF(30.0, 20.0));
    QCOMPARE(clickSpy.count(), 1);
    QCOMPARE(list.currentRow(), 0);
    QCOMPARE(clickSpy.at(0).at(0).toModelIndex().row(), 0);

    sendMouse(list, QEvent::MouseButtonDblClick, QPointF(30.0, 20.0), Qt::LeftButton,
              Qt::LeftButton);
    QCOMPARE(doubleSpy.count(), 1);

    // A click on a scrolled row selects that row, not the first visible one.
    list.setScrollOffset(80.0);
    QCOMPARE(list.firstVisibleRow(), 2);
    clickAt(list, QPointF(30.0, 20.0));
    QCOMPARE(list.currentRow(), 2);

    // The wheel scrolls without changing the selection.
    QWheelEvent wheel(QPointF(120.0, 100.0), QPointF(120.0, 100.0), QPoint(), QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&list, &wheel);
    QVERIFY(list.scrollOffset() > 80.0);
    QCOMPARE(list.currentRow(), 2);

    // A model reset clears the selection instead of keeping a stale row.
    list.setModel(nullptr);
    QCOMPARE(list.rowCount(), 0);
    QCOMPARE(list.currentRow(), -1);
}

void TestDisplayControls::recyclerVirtualisation()
{
    StringModel model(100);

    md::Recycler recycler;
    recycler.setRowHeight(40.0);
    recycler.setPrefetchRows(2);
    recycler.arrange(QRect(0, 0, 300, 300));
    recycler.setModel(&model);

    QCOMPARE(recycler.rowCount(), 100);

    const int initial = recycler.instantiatedItemCount();
    QVERIFY(initial >= 10);
    QVERIFY(initial <= 14);
    QVERIFY(initial < recycler.rowCount());
    QCOMPARE(recycler.createdItemCount(), initial);
    QCOMPARE(recycler.pooledItemCount(), 0);

    // Scrolling realises a new window and reuses most of the old widgets.
    recycler.scrollToRow(50);
    QVERIFY(recycler.scrollOffset() > 0.0);
    QVERIFY(recycler.instantiatedItemCount() <= 14);
    QVERIFY(recycler.createdItemCount() <= initial + 4);
    QVERIFY(recycler.reusedItemCount() > 0);

    const int createdAfterScroll = recycler.createdItemCount();

    // Scrolling back does not create a second set of widgets either.
    recycler.scrollToRow(0);
    QCOMPARE(recycler.createdItemCount(), createdAfterScroll);
    QVERIFY(recycler.reusedItemCount() > 0);

    QSignalSpy currentSpy(&recycler, &md::Recycler::currentIndexChanged);
    recycler.setCurrentIndex(recycler.indexAtRow(3));
    QCOMPARE(recycler.currentRow(), 3);
    QCOMPARE(currentSpy.count(), 1);

    sendKey(recycler, Qt::Key_Down);
    QCOMPARE(recycler.currentRow(), 4);

    sendKey(recycler, Qt::Key_End);
    QCOMPARE(recycler.currentRow(), 99);
    QVERIFY(recycler.scrollOffset() > 0.0);
}

void TestDisplayControls::recyclerReactsToModelChanges()
{
    StringModel model(20);

    md::Recycler recycler;
    recycler.setRowHeight(40.0);
    recycler.arrange(QRect(0, 0, 300, 300));
    recycler.setModel(&model);

    QCOMPARE(recycler.rowCount(), 20);

    model.appendRow(QStringLiteral("New row"));
    QCOMPARE(recycler.rowCount(), 21);

    model.renameRow(0, QStringLiteral("Renamed row"));
    QCOMPARE(recycler.rowCount(), 21);

    recycler.setCurrentIndex(recycler.indexAtRow(20));
    QCOMPARE(recycler.currentRow(), 20);

    model.removeLastRow();
    QCOMPARE(recycler.rowCount(), 20);
    QCOMPARE(recycler.currentRow(), -1);

    model.removeLastRow();
    QCOMPARE(recycler.rowCount(), 19);
    QVERIFY(recycler.instantiatedItemCount() > 0);
    QVERIFY(recycler.instantiatedItemCount() <= 14);
}

void TestDisplayControls::popUpOpensClosesAndDismisses()
{
    QWidget host;
    host.resize(400, 300);

    QWidget anchor(&host);
    anchor.setGeometry(20, 20, 100, 40);
    host.show();

    md::PopUp popup(&host);
    md::Text content(QStringLiteral("Anchored content"));
    popup.setContent(&content);
    popup.setAnchor(&anchor);

    QSignalSpy openedSpy(&popup, &md::PopUp::opened);
    QSignalSpy closedSpy(&popup, &md::PopUp::closed);

    popup.open();
    QVERIFY(popup.isOpen());
    QVERIFY(popup.isVisible());
    QCOMPARE(openedSpy.count(), 1);
    QVERIFY(popup.effectivePlacement() == md::PopUp::Placement::Below);
    QVERIFY(!popup.bodyRect().isEmpty());
    QVERIFY(!popup.contentArea().isEmpty());
    QVERIFY(popup.geometry().top() >= anchor.geometry().bottom());

    // A press outside the surface closes it.
    sendMouse(host, QEvent::MouseButtonPress, QPointF(360.0, 260.0), Qt::LeftButton,
              Qt::LeftButton);
    QVERIFY(!popup.isOpen());
    QCOMPARE(closedSpy.count(), 1);

    // Escape closes it as well.
    popup.open();
    QVERIFY(popup.isOpen());

    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&host, &escape);
    QVERIFY(!popup.isOpen());
    QCOMPARE(closedSpy.count(), 2);

    // A press inside the surface keeps it open.
    popup.open();
    QVERIFY(popup.isOpen());
    sendMouse(popup, QEvent::MouseButtonPress, QPointF(10.0, 10.0), Qt::LeftButton,
              Qt::LeftButton);
    QVERIFY(popup.isOpen());
    QCOMPARE(closedSpy.count(), 2);
}

void TestDisplayControls::tabBarSelectionAndSignal()
{
    md::TabBar bar;
    bar.setTabs({md::Tab(QStringLiteral("Home")), md::Tab(QStringLiteral("Search")),
                 md::Tab(QStringLiteral("Profile"))});
    bar.setIndicatorAnimationEnabled(false);
    bar.arrange(QRect(0, 0, 300, 48));

    QCOMPARE(bar.count(), 3);
    QCOMPARE(bar.currentIndex(), -1);
    QVERIFY(bar.indicatorRect().isEmpty());

    QSignalSpy changedSpy(&bar, &md::TabBar::currentChanged);

    bar.setCurrentIndex(1);
    QCOMPARE(bar.currentIndex(), 1);
    QCOMPARE(changedSpy.count(), 1);

    const QRectF indicator = bar.indicatorRect();
    QVERIFY(!indicator.isEmpty());
    QVERIFY(bar.tabRect(1).contains(indicator.center()));
    QCOMPARE(indicator.height(),
             md::Theme::applicationTheme().density().dp(3.0));

    // Clicking a tab selects it and notifies once.
    clickAt(bar, bar.tabRect(2).center());
    QCOMPARE(bar.currentIndex(), 2);
    QCOMPARE(changedSpy.count(), 2);

    sendKey(bar, Qt::Key_Left);
    QCOMPARE(bar.currentIndex(), 1);
    QCOMPARE(changedSpy.count(), 3);

    // A disabled tab is skipped by the keyboard.
    bar.setTabEnabled(2, false);
    sendKey(bar, Qt::Key_Right);
    QCOMPARE(bar.currentIndex(), 1);
    QCOMPARE(changedSpy.count(), 3);

    // Indicators follow the selection.
    bar.setCurrentIndex(0);
    QVERIFY(bar.tabRect(0).contains(bar.indicatorRect().center()));
}

void TestDisplayControls::bannerActionsAndClose()
{
    md::Banner banner;
    banner.setTitle(QStringLiteral("Update available"));
    banner.setDescription(QStringLiteral("A newer version of the app is ready."));
    banner.setPrimaryAction(QStringLiteral("Update"));
    banner.setSecondaryAction(QStringLiteral("Later"));
    banner.arrange(QRect(0, 0, 480, 80));
    banner.show();

    QVERIFY(!banner.primaryButton()->isHidden());
    QVERIFY(!banner.secondaryButton()->isHidden());
    QVERIFY(!banner.primaryButton()->geometry().isEmpty());

    md::Banner::Action lastAction = md::Banner::Action::None;
    int actionCount = 0;
    connect(&banner, &md::Banner::actionClicked, this,
            [&](md::Banner::Action action) {
                lastAction = action;
                ++actionCount;
            });

    QSignalSpy closedSpy(&banner, &md::Banner::closed);

    clickAt(*banner.primaryButton(), QPointF(24.0, 24.0));
    QCOMPARE(actionCount, 1);
    QVERIFY(lastAction == md::Banner::Action::Primary);

    clickAt(*banner.secondaryButton(), QPointF(24.0, 24.0));
    QCOMPARE(actionCount, 2);
    QVERIFY(lastAction == md::Banner::Action::Secondary);

    // Hiding and showing are animated; both end in the matching state.
    banner.hideAnimated();
    QTRY_VERIFY(!banner.isVisible());

    banner.showAnimated();
    QTRY_COMPARE(banner.revealProgress(), 1.0);
    QVERIFY(banner.isVisible());

    clickAt(banner, banner.closeButtonRect().center());
    QCOMPARE(closedSpy.count(), 1);

    // The close animation ends in the hidden state.
    QTRY_VERIFY(!banner.isVisible());

    // An empty action hides its button.
    banner.setSecondaryAction(QString());
    banner.showAnimated();
    QTRY_COMPARE(banner.revealProgress(), 1.0);
    QVERIFY(banner.secondaryButton()->isHidden());
}

void TestDisplayControls::marqueeScrollsOnlyWhenOverflowing()
{
    md::Marquee marquee;
    marquee.setText(QStringLiteral("A very long line that cannot possibly fit"));
    marquee.setSpeed(60.0);
    marquee.setPauseOnHover(false);
    marquee.resize(120, 24);
    marquee.show();

    QVERIFY(marquee.isOverflowing());
    QVERIFY(marquee.contentWidth() > 120.0);

    marquee.start();
    QVERIFY(marquee.isRunning());

    QTest::qWait(160);
    QVERIFY(marquee.scrollOffset() > 0.0);

    marquee.setPaused(true);
    QVERIFY(!marquee.isRunning());

    marquee.setPaused(false);
    QVERIFY(marquee.isRunning());

    marquee.stop();
    QVERIFY(!marquee.isRunning());
    QCOMPARE(marquee.scrollOffset(), 0.0);

    // Hover pauses a running marquee when the option is enabled. A Leave is
    // delivered first because showing a top-level widget under the offscreen
    // platform synthesises an Enter for the virtual cursor at (0, 0).
    marquee.setPauseOnHover(true);
    QEvent clearEnter(QEvent::Leave);
    QApplication::sendEvent(&marquee, &clearEnter);

    marquee.start();
    QVERIFY(marquee.isRunning());

    QEnterEvent enter(QPointF(4.0, 4.0), QPointF(4.0, 4.0), QPointF(4.0, 4.0));
    QApplication::sendEvent(&marquee, &enter);
    QVERIFY(!marquee.isRunning());

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&marquee, &leave);
    QVERIFY(marquee.isRunning());
    marquee.stop();

    // A label that fits never starts the timer.
    md::Marquee shortText(QStringLiteral("Hi"));
    shortText.resize(200, 24);
    shortText.show();

    QVERIFY(!shortText.isOverflowing());
    shortText.start();
    QVERIFY(!shortText.isRunning());

    QTest::qWait(80);
    QCOMPARE(shortText.scrollOffset(), 0.0);
}

void TestDisplayControls::guideStepProgression()
{
    QWidget host;
    host.resize(500, 400);

    QWidget first(&host);
    first.setGeometry(40, 60, 100, 32);

    QWidget second(&host);
    second.setGeometry(240, 220, 120, 40);

    host.show();

    md::GuideView guide(&host);
    guide.setSteps({md::GuideStep(&first, QStringLiteral("Step one"),
                                  QStringLiteral("The first explanation.")),
                    md::GuideStep(&second, QStringLiteral("Step two"),
                                  QStringLiteral("The second explanation."))});

    QSignalSpy changedSpy(&guide, &md::GuideView::stepChanged);
    QSignalSpy finishedSpy(&guide, &md::GuideView::finished);
    QSignalSpy skippedSpy(&guide, &md::GuideView::skipped);

    guide.start();
    QVERIFY(guide.isActive());
    QCOMPARE(guide.currentStep(), 0);
    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(!guide.highlightRect().isEmpty());
    QVERIFY(guide.highlightRect().contains(QRectF(first.geometry())));
    QVERIFY(!guide.bubbleRect().isEmpty());

    guide.next();
    QCOMPARE(guide.currentStep(), 1);
    QCOMPARE(changedSpy.count(), 2);
    QVERIFY(guide.highlightRect().contains(QRectF(second.geometry())));

    guide.previous();
    QCOMPARE(guide.currentStep(), 0);
    QCOMPARE(changedSpy.count(), 3);

    guide.next();
    guide.next();
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(!guide.isActive());
    QVERIFY(!guide.isVisible());

    // Moving a target while the tour is running re-positions the highlight.
    guide.start();
    QCOMPARE(guide.currentStep(), 0);
    const QRectF before = guide.highlightRect();
    QVERIFY(!before.isEmpty());

    first.setGeometry(120, 100, 100, 32);
    QCoreApplication::processEvents();
    QVERIFY(guide.highlightRect() != before);

    guide.skip();
    QCOMPARE(skippedSpy.count(), 1);
    QVERIFY(!guide.isActive());
}

QTEST_MAIN(TestDisplayControls)
#include "tst_display_controls.moc"

// ---------------------------------------------------------------------------
// Regression tests (appended): the row window and the selection must survive a
// model that shrinks, and a removed selection must be reported.
// ---------------------------------------------------------------------------

namespace {

// StringModel has no reset helper, and a reset is the other way a selection
// disappears. No Q_OBJECT: the fixture needs no signals of its own.
class ResettableModel : public QAbstractListModel
{
public:
    explicit ResettableModel(int count, QObject* parent = nullptr)
        : QAbstractListModel(parent)
    {
        resetTo(count);
    }

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(rows_.size());
    }

    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0
            || index.row() >= static_cast<int>(rows_.size())) {
            return {};
        }

        return role == Qt::DisplayRole ? QVariant(rows_.at(index.row())) : QVariant();
    }

    void resetTo(int count)
    {
        beginResetModel();
        rows_.clear();
        for (int row = 0; row < count; ++row)
            rows_.append(QStringLiteral("Row %1").arg(row));
        endResetModel();
    }

private:
    QStringList rows_;
};

} // namespace

void TestDisplayControls::listSurvivesItsDelegateBeingDestroyed()
{
    StringModel model(8);

    md::List list;
    list.setModel(&model);
    list.setRowHeight(40.0);
    list.arrange(QRect(0, 0, 240, 200));

    auto* delegate = new QStyledItemDelegate;
    list.setItemDelegate(delegate);
    QCOMPARE(list.itemDelegate(), delegate);

    // The delegate is shared and not owned by the List. Destroying it must
    // clear the List's pointer instead of leaving a dangling one.
    delete delegate;
    QVERIFY(list.itemDelegate() == nullptr);

    // Painting now falls back to the default row; it must not call the dead
    // delegate. The result matches a list that never had a delegate.
    md::List plain;
    plain.setModel(&model);
    plain.setRowHeight(40.0);
    plain.arrange(QRect(0, 0, 240, 200));

    const QImage painted = renderWidget(list);
    QVERIFY(paintedPixels(painted) > 0);
    QVERIFY(painted == renderWidget(plain));
}

void TestDisplayControls::firstVisibleRowStaysInsideModel()
{
    StringModel model(10);

    // A collapsed viewport still has a scroll range that covers the whole
    // model, so ensureVisible() may scroll to `rowCount * rowHeight`.
    md::List collapsed;
    collapsed.setModel(&model);
    collapsed.setRowHeight(40.0);
    collapsed.arrange(QRect(0, 0, 240, 0));
    collapsed.scrollToRow(9);

    QCOMPARE(collapsed.firstVisibleRow(), 0);
    QVERIFY(collapsed.firstVisibleRow() < collapsed.rowCount());

    // A viewport shorter than one row reports the last row, never rowCount().
    md::List narrow;
    narrow.setModel(&model);
    narrow.setRowHeight(40.0);
    narrow.arrange(QRect(0, 0, 240, 10));
    narrow.scrollToRow(9);

    QCOMPARE(narrow.firstVisibleRow(), 9);
    QVERIFY(narrow.firstVisibleRow() < narrow.rowCount());
}

void TestDisplayControls::removedSelectionEmitsInvalidIndex()
{
    StringModel listModel(10);

    md::List list;
    list.setModel(&listModel);
    list.setRowHeight(40.0);
    list.arrange(QRect(0, 0, 240, 200));
    list.setCurrentIndex(list.indexAt(5));
    QCOMPARE(list.currentRow(), 5);

    QSignalSpy listSpy(&list, &md::List::currentIndexChanged);

    // A removal below the selection keeps it and emits nothing.
    listModel.removeLastRow();
    QCOMPARE(list.currentRow(), 5);
    QVERIFY(list.currentIndex().isValid());
    QCOMPARE(listSpy.count(), 0);

    // Removing the selected row clears it and reports the invalid index.
    list.setCurrentIndex(list.indexAt(8)); // the model now has 9 rows
    QCOMPARE(listSpy.count(), 1);          // the explicit selection above
    listModel.removeLastRow();
    QCOMPARE(list.currentRow(), -1);
    QCOMPARE(listSpy.count(), 2);
    QVERIFY(!listSpy.last().at(0).toModelIndex().isValid());

    StringModel recyclerModel(20);

    md::Recycler recycler;
    recycler.setRowHeight(40.0);
    recycler.arrange(QRect(0, 0, 300, 300));
    recycler.setModel(&recyclerModel);
    recycler.setCurrentIndex(recycler.indexAtRow(19));
    QCOMPARE(recycler.currentRow(), 19);

    QSignalSpy recyclerSpy(&recycler, &md::Recycler::currentIndexChanged);
    recyclerModel.removeLastRow();
    QCOMPARE(recycler.currentRow(), -1);
    QCOMPARE(recyclerSpy.count(), 1);
    QVERIFY(!recyclerSpy.last().at(0).toModelIndex().isValid());

    // A model reset goes through the same notification.
    ResettableModel resetModel(4);

    md::Recycler resetRecycler;
    resetRecycler.setRowHeight(40.0);
    resetRecycler.arrange(QRect(0, 0, 300, 300));
    resetRecycler.setModel(&resetModel);
    resetRecycler.setCurrentIndex(resetModel.index(1, 0));
    QCOMPARE(resetRecycler.currentRow(), 1);

    QSignalSpy resetSpy(&resetRecycler, &md::Recycler::currentIndexChanged);
    resetModel.resetTo(2);
    QCOMPARE(resetRecycler.currentRow(), -1);
    QCOMPARE(resetSpy.count(), 1);
    QVERIFY(!resetSpy.last().at(0).toModelIndex().isValid());

    // List reports a reset the same way.
    ResettableModel listResetModel(3);

    md::List resetList;
    resetList.setModel(&listResetModel);
    resetList.setRowHeight(40.0);
    resetList.arrange(QRect(0, 0, 240, 200));
    resetList.setCurrentIndex(listResetModel.index(2, 0));
    QCOMPARE(resetList.currentRow(), 2);

    QSignalSpy listResetSpy(&resetList, &md::List::currentIndexChanged);
    listResetModel.resetTo(1);
    QCOMPARE(resetList.currentRow(), -1);
    QCOMPARE(listResetSpy.count(), 1);
    QVERIFY(!listResetSpy.last().at(0).toModelIndex().isValid());
    QVERIFY(!resetList.currentIndex().isValid());
}
