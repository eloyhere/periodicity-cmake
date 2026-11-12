// Tests for the dropdown surface behind the three select controls
// (components/tree_select.h, virtual_tree_select.h, virtual_select.h).
//
// The three reports this file is written against are one bug seen three ways:
// a menu that was placed by hand from a rectangle in the wrong coordinate
// space ended up far away from its field, and because it stayed open and
// raised it could be clicked where another control's field was — a click then
// appeared to open "the other" control's menu.
//
// The contract these tests pin is therefore ownership first and geometry
// second:
//   * every control owns its own md::Dropdown, whose anchor is that control;
//   * only the control that was clicked has an open surface;
//   * the visible panel starts at the field's global left edge, is exactly as
//     wide as the field, and sits one anchor gap below it — or above it when
//     the screen has no room below;
//   * the surface follows the window and the field, and closes on Escape and on
//     a press outside both.
//
// Everything runs offscreen against the baseline light theme. The controls are
// children of a real, shown top-level widget because their surfaces are
// children of that window rather than Qt::Popup windows.

#include <QApplication>
#include <QFont>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QStandardItemModel>
#include <QTest>
#include <QVariantList>
#include <QWidget>

#include <md/components/dropdown.h>
#include <md/components/tree_select.h>
#include <md/components/virtual_select.h>
#include <md/components/virtual_tree_select.h>
#include <md/structure/virtual_source.h>
#include <md/theme/theme.h>

#include <cmath>
#include <functional>

namespace {

using NodeId = md::structure::VirtualTreeSource::NodeId;

constexpr int kFieldWidth = 240;
constexpr int kFieldHeight = 56;

// Sizes a widget through the framework's measurement and arrangement hooks.
template<class Widget>
void arrangeField(Widget& widget)
{
    widget.measure(md::MeasureSpec::exactly(qreal(kFieldWidth)),
                   md::MeasureSpec::exactly(qreal(kFieldHeight)));
    widget.arrange(QRect(0, 0, kFieldWidth, kFieldHeight));
}

// The field's rectangle in global coordinates: what the surface is anchored to.
QRect globalField(QWidget& field)
{
    return QRect(field.mapToGlobal(QPoint(0, 0)), field.size());
}

// The gap the Dropdown inserts between the anchor and the panel, in pixels. It
// is a theme measurement, so it is read from the same place the component reads
// it instead of being hard-coded.
qreal anchorGap(QWidget& field)
{
    return md::Theme::forWidget(&field).density().dp(4.0);
}

// The three geometry rules, for a surface that reports itself open. `field` is
// the control's own field rectangle, which for these controls is the whole
// widget.
void verifyAnchored(QWidget& field, md::Dropdown* dropdown)
{
    QVERIFY(dropdown != nullptr);
    QVERIFY(dropdown->isOpen());

    const QRect anchor = globalField(field);
    const QRectF panel = dropdown->popupGlobalRect();
    QVERIFY(panel.isValid());
    QVERIFY(!panel.isEmpty());

    // Horizontal: the panel is the field's column, not the screen's.
    QVERIFY2(std::abs(panel.left() - qreal(anchor.left())) <= 1.0,
             qPrintable(QStringLiteral("panel left %1, field left %2")
                            .arg(panel.left())
                            .arg(anchor.left())));
    QVERIFY2(std::abs(panel.width() - qreal(anchor.width())) <= 1.0,
             qPrintable(QStringLiteral("panel width %1, field width %2")
                            .arg(panel.width())
                            .arg(anchor.width())));

    // Vertical: one anchor gap below the field, or one gap above it after the
    // flip. The tolerance covers the panel's integer rounding.
    const qreal gap = anchorGap(field);
    if (dropdown->effectivePlacement() == md::Dropdown::Placement::Above) {
        const qreal expectedBottom = qreal(anchor.top()) - gap;
        QVERIFY2(std::abs(panel.bottom() - expectedBottom) <= 2.0,
                 qPrintable(QStringLiteral("panel bottom %1, field top %2")
                                .arg(panel.bottom())
                                .arg(anchor.top())));
        QVERIFY(panel.top() < qreal(anchor.top()));
    } else {
        QCOMPARE(dropdown->effectivePlacement(), md::Dropdown::Placement::Below);
        const qreal expectedTop = qreal(anchor.top() + anchor.height()) + gap;
        QVERIFY2(std::abs(panel.top() - expectedTop) <= 2.0,
                 qPrintable(QStringLiteral("panel top %1, field bottom %2")
                                .arg(panel.top())
                                .arg(anchor.top() + anchor.height())));
    }
}

// A flat tree: every root is a leaf, so a single Enter selects one.
class FlatTreeSource final : public md::structure::VirtualTreeSource
{
public:
    FlatTreeSource()
    {
        roots_ = {1, 2, 3};
        labels_ = {
            {1, QStringLiteral("Plants")},
            {2, QStringLiteral("Minerals")},
            {3, QStringLiteral("Gases")},
        };
    }

    [[nodiscard]] int rootCount() const override { return static_cast<int>(roots_.size()); }

    [[nodiscard]] NodeId rootAt(int index) const override { return roots_.at(index); }

    [[nodiscard]] int childCount(NodeId) const override { return 0; }

    [[nodiscard]] NodeId childAt(NodeId, int) const override { return 0; }

    [[nodiscard]] QVariant value(NodeId node) const override
    {
        return QVariant::fromValue<qulonglong>(static_cast<qulonglong>(node));
    }

    [[nodiscard]] QString label(NodeId node) const override { return labels_.value(node); }

private:
    QVector<NodeId> roots_;
    QHash<NodeId, QString> labels_;
};

// Three rows, enough to make a panel that is clearly taller than one row and
// clearly shorter than a screen.
md::structure::ListSource makeListSource()
{
    return md::structure::ListSource(
        QVariantList{QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral("Gamma")});
}

} // namespace

class TestSelectPopup : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void everyControlOwnsItsOwnDropdown();
    void onlyTheClickedInstanceIsOpen();
    void panelIsAnchoredToTheField();
    void panelFollowsTheWindowAndTheField();
    void panelFlipsAboveWhenThereIsNoRoomBelow();
    void escapeAndOutsidePressClose();
    void keyboardSelectionWritesTheFieldBack();
};

void TestSelectPopup::initTestCase()
{
    // Pin the theme so the dp measurements do not depend on the platform.
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestSelectPopup::everyControlOwnsItsOwnDropdown()
{
    QWidget window;
    window.resize(760, 420);

    QStandardItemModel model;
    for (const QString& label : {QStringLiteral("Fruit"), QStringLiteral("Wood"),
                                 QStringLiteral("Metal")}) {
        model.appendRow(new QStandardItem(label));
    }

    FlatTreeSource treeSource;
    md::structure::ListSource listSource = makeListSource();

    md::TreeSelect tree(&window);
    tree.setModel(&model);
    md::VirtualTreeSelect virtualTree(&window);
    virtualTree.setSource(&treeSource);
    md::VirtualSelect list(&window);
    list.setSource(&listSource);

    arrangeField(tree);
    arrangeField(virtualTree);
    arrangeField(list);

    window.show();

    // A surface exists as soon as the control has something to show, and its
    // anchor is the control itself — never another control's field.
    tree.showPopup();
    virtualTree.showPopup();
    list.showPopup();

    QVERIFY(tree.dropdown() != nullptr);
    QVERIFY(virtualTree.dropdown() != nullptr);
    QVERIFY(list.dropdown() != nullptr);

    QCOMPARE(tree.dropdown()->anchor(), static_cast<QWidget*>(&tree));
    QCOMPARE(virtualTree.dropdown()->anchor(), static_cast<QWidget*>(&virtualTree));
    QCOMPARE(list.dropdown()->anchor(), static_cast<QWidget*>(&list));

    // Three surfaces, three distinct objects, three distinct row widgets.
    QVERIFY(tree.dropdown() != virtualTree.dropdown());
    QVERIFY(tree.dropdown() != list.dropdown());
    QVERIFY(virtualTree.dropdown() != list.dropdown());

    // The row widget a test hits with rowRect() lives inside its own surface.
    QCOMPARE(static_cast<QWidget*>(tree.dropdown()->content()), tree.popupWidget());
    QCOMPARE(static_cast<QWidget*>(virtualTree.dropdown()->content()), virtualTree.popupWidget());
    QCOMPARE(static_cast<QWidget*>(list.dropdown()->content()), list.popupWidget());
}

void TestSelectPopup::onlyTheClickedInstanceIsOpen()
{
    QWidget window;
    window.resize(760, 700);

    QStandardItemModel model;
    for (const QString& label : {QStringLiteral("Fruit"), QStringLiteral("Wood"),
                                 QStringLiteral("Metal")}) {
        model.appendRow(new QStandardItem(label));
    }

    FlatTreeSource treeSource;
    md::structure::ListSource listSource = makeListSource();

    // Two instances of each control: the report was that clicking one of them
    // opened the other one's menu, which needs two siblings to show up.
    md::TreeSelect treeA(&window);
    md::TreeSelect treeB(&window);
    md::VirtualTreeSelect virtualTreeA(&window);
    md::VirtualTreeSelect virtualTreeB(&window);
    md::VirtualSelect listA(&window);
    md::VirtualSelect listB(&window);

    for (md::TreeSelect* select : {&treeA, &treeB}) {
        select->setModel(&model);
        arrangeField(*select);
    }
    for (md::VirtualTreeSelect* select : {&virtualTreeA, &virtualTreeB}) {
        select->setSource(&treeSource);
        arrangeField(*select);
    }
    for (md::VirtualSelect* select : {&listA, &listB}) {
        select->setSource(&listSource);
        arrangeField(*select);
    }

    // The fields are spaced so that no field sits under another control's open
    // panel: a press inside a panel is meant to act on the panel, not to
    // dismiss it, so an overlapping layout would test the wrong rule.
    treeA.move(20, 20);
    treeB.move(20, 260);
    virtualTreeA.move(400, 20);
    virtualTreeB.move(400, 260);
    listA.move(20, 500);
    listB.move(400, 500);

    window.show();

    QVector<QPair<QWidget*, std::function<bool()>>> instances;
    const auto add = [&instances](QWidget& field, const std::function<bool()>& visible) {
        instances.append({&field, visible});
    };
    add(treeA, [&treeA] { return treeA.isPopupVisible(); });
    add(treeB, [&treeB] { return treeB.isPopupVisible(); });
    add(virtualTreeA, [&virtualTreeA] { return virtualTreeA.isPopupVisible(); });
    add(virtualTreeB, [&virtualTreeB] { return virtualTreeB.isPopupVisible(); });
    add(listA, [&listA] { return listA.isPopupVisible(); });
    add(listB, [&listB] { return listB.isPopupVisible(); });

    // Every control starts closed.
    for (const auto& instance : instances)
        QVERIFY(!instance.second());

    for (int clicked = 0; clicked < instances.size(); ++clicked) {
        QWidget* field = instances.at(clicked).first;
        QTest::mouseClick(field, Qt::LeftButton, Qt::NoModifier, field->rect().center());

        for (int index = 0; index < instances.size(); ++index) {
            const bool visible = instances.at(index).second();
            QVERIFY2(visible == (index == clicked),
                     qPrintable(QStringLiteral("clicked instance %1, instance %2 reported %3")
                                    .arg(clicked)
                                    .arg(index)
                                    .arg(visible ? QStringLiteral("open") : QStringLiteral("closed"))));
        }
    }
}

void TestSelectPopup::panelIsAnchoredToTheField()
{
    QWidget window;
    window.resize(760, 560);

    QStandardItemModel model;
    for (const QString& label : {QStringLiteral("Fruit"), QStringLiteral("Wood"),
                                 QStringLiteral("Metal")}) {
        model.appendRow(new QStandardItem(label));
    }

    FlatTreeSource treeSource;
    md::structure::ListSource listSource = makeListSource();

    md::TreeSelect tree(&window);
    tree.setModel(&model);
    md::VirtualTreeSelect virtualTree(&window);
    virtualTree.setSource(&treeSource);
    md::VirtualSelect list(&window);
    list.setSource(&listSource);

    arrangeField(tree);
    arrangeField(virtualTree);
    arrangeField(list);

    // The fields sit away from the window origin so that a coordinate-space
    // mistake — the local offset being applied twice — shows up as a failure
    // instead of cancelling out.
    tree.move(40, 40);
    virtualTree.move(300, 120);
    list.move(80, 300);

    window.show();

    tree.showPopup();
    verifyAnchored(tree, tree.dropdown());
    tree.hidePopup();

    virtualTree.showPopup();
    verifyAnchored(virtualTree, virtualTree.dropdown());
    virtualTree.hidePopup();

    list.showPopup();
    verifyAnchored(list, list.dropdown());
    list.hidePopup();
}

void TestSelectPopup::panelFollowsTheWindowAndTheField()
{
    QWidget window;
    window.resize(600, 420);

    md::structure::ListSource listSource = makeListSource();

    md::VirtualSelect list(&window);
    list.setSource(&listSource);
    arrangeField(list);
    list.move(40, 40);

    window.move(60, 60);
    window.show();

    list.showPopup();
    const QRectF before = list.dropdown()->popupGlobalRect();
    verifyAnchored(list, list.dropdown());
    list.hidePopup();

    // Moving the window moves the field's global rectangle, and the reopened
    // panel has to move with it.
    window.move(window.x() + 120, window.y() + 70);
    list.showPopup();
    const QRectF after = list.dropdown()->popupGlobalRect();
    QVERIFY2(std::abs(after.left() - before.left() - 120.0) <= 2.0,
             qPrintable(QStringLiteral("panel moved by %1, window by 120")
                            .arg(after.left() - before.left())));
    QVERIFY2(std::abs(after.top() - before.top() - 70.0) <= 2.0,
             qPrintable(QStringLiteral("panel moved by %1, window by 70")
                            .arg(after.top() - before.top())));
    verifyAnchored(list, list.dropdown());
    list.hidePopup();

    // Moving the field inside the window with the panel open re-anchors it: the
    // surface listens to its anchor, not only to the window.
    list.showPopup();
    list.move(160, 120);
    verifyAnchored(list, list.dropdown());
    list.hidePopup();
}

void TestSelectPopup::panelFlipsAboveWhenThereIsNoRoomBelow()
{
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    if (available.height() < 400) {
        QSKIP("the offscreen screen is too short for a below/above fit test");
    }

    QWidget window;
    window.resize(kFieldWidth + 40, kFieldHeight);

    md::structure::ListSource listSource = makeListSource();

    md::VirtualSelect list(&window);
    list.setSource(&listSource);
    arrangeField(list);
    list.move(0, 0);
    window.show();

    // Leave the field a few pixels above the bottom of the screen: a three-row
    // panel cannot fit below it, but there is room above.
    window.move(available.left() + 20, available.bottom() - kFieldHeight - 20);

    list.showPopup();
    auto* dropdown = list.dropdown();
    QVERIFY(dropdown != nullptr);
    QVERIFY(dropdown->isOpen());
    QCOMPARE(dropdown->effectivePlacement(), md::Dropdown::Placement::Above);

    const QRect anchor = globalField(list);
    const QRectF panel = dropdown->popupGlobalRect();
    QVERIFY(!panel.isEmpty());
    QVERIFY(panel.bottom() <= qreal(anchor.top()));
    verifyAnchored(list, dropdown);
}

void TestSelectPopup::escapeAndOutsidePressClose()
{
    QWidget window;
    window.resize(600, 420);

    md::structure::ListSource listSource = makeListSource();

    md::VirtualSelect list(&window);
    list.setSource(&listSource);
    arrangeField(list);
    list.move(20, 20);
    window.show();

    // Escape belongs to the surface: it closes whatever had the focus inside the
    // menu without touching the selection.
    list.showPopup();
    QVERIFY(list.isPopupVisible());
    QWidget* content = list.dropdown()->content();
    QVERIFY(content != nullptr);
    QTest::keyClick(content, Qt::Key_Escape);
    QVERIFY(!list.isPopupVisible());
    QVERIFY(!list.dropdown()->isOpen());

    // A press outside both the panel and the field dismisses it.
    list.showPopup();
    QVERIFY(list.isPopupVisible());
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(520, 380));
    QVERIFY(!list.isPopupVisible());
    QVERIFY(!list.dropdown()->isOpen());

    // A press on the field itself is a toggle, not an outside press.
    const QPoint centre = list.fieldRect().center().toPoint();
    QTest::mouseClick(&list, Qt::LeftButton, Qt::NoModifier, centre);
    QVERIFY(list.isPopupVisible());
    QTest::mouseClick(&list, Qt::LeftButton, Qt::NoModifier, centre);
    QVERIFY(!list.isPopupVisible());
}

void TestSelectPopup::keyboardSelectionWritesTheFieldBack()
{
    QWidget window;
    window.resize(520, 420);

    QStandardItemModel model;
    for (const QString& label : {QStringLiteral("Fruit"), QStringLiteral("Wood"),
                                 QStringLiteral("Metal")}) {
        model.appendRow(new QStandardItem(label));
    }

    FlatTreeSource treeSource;
    md::structure::ListSource listSource = makeListSource();

    md::TreeSelect tree(&window);
    tree.setModel(&model);
    md::VirtualTreeSelect virtualTree(&window);
    virtualTree.setSource(&treeSource);
    md::VirtualSelect list(&window);
    list.setSource(&listSource);

    arrangeField(tree);
    arrangeField(virtualTree);
    arrangeField(list);
    tree.move(20, 20);
    virtualTree.move(20, 140);
    list.move(20, 260);
    window.show();

    // TreeSelect: Down opens with the first row highlighted, Enter selects it.
    QTest::keyClick(&tree, Qt::Key_Down);
    QVERIFY(tree.isPopupVisible());
    QCOMPARE(tree.highlightedRow(), 0);
    QTest::keyClick(tree.popupWidget(), Qt::Key_Return);
    QVERIFY(!tree.isPopupVisible());
    QCOMPARE(tree.currentText(), QStringLiteral("Fruit"));

    // VirtualTreeSelect follows the same rule.
    QTest::keyClick(&virtualTree, Qt::Key_Down);
    QVERIFY(virtualTree.isPopupVisible());
    QCOMPARE(virtualTree.highlightedRow(), 0);
    QTest::keyClick(virtualTree.popupWidget(), Qt::Key_Return);
    QVERIFY(!virtualTree.isPopupVisible());
    QCOMPARE(virtualTree.currentText(), QStringLiteral("Plants"));

    // And VirtualSelect commits the highlighted row on Enter.
    QTest::keyClick(&list, Qt::Key_Down);
    QVERIFY(list.isPopupVisible());
    QCOMPARE(list.highlightedPosition(), 0);
    QTest::keyClick(list.popupWidget(), Qt::Key_Return);
    QVERIFY(!list.isPopupVisible());
    QCOMPARE(list.currentText(), QStringLiteral("Alpha"));
    QCOMPARE(list.currentValue().toString(), QStringLiteral("Alpha"));
}

QTEST_MAIN(TestSelectPopup)

#include "tst_select_popup.moc"
