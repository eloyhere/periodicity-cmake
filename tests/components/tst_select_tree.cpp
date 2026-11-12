// Tests for the tree and virtual selectors (components/tree_select.h,
// virtual_tree_select.h, virtual_select.h).
//
// Everything runs offscreen against the baseline light theme. The controls are
// children of a real, shown top-level widget because their menus are children
// of that window rather than Qt::Popup windows: a shown window is what gives
// the menu a visible geometry to open into.
//
// The tests cover the two rules the selectors are built around — a branch is
// never selectable and a virtual source is only read where it is visible — and
// the keyboard, pointer, formatting and filtering paths that implement them.

#include <QApplication>
#include <QCoreApplication>
#include <QHash>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTest>
#include <QVariant>
#include <QVariantList>
#include <QWheelEvent>
#include <QWidget>

#include <md/components/tree_select.h>
#include <md/components/virtual_select.h>
#include <md/components/virtual_tree_select.h>
#include <md/structure/virtual_source.h>
#include <md/theme/theme.h>

#include <memory>

namespace {

using NodeId = md::structure::VirtualTreeSource::NodeId;

constexpr NodeId kPlants = 1;
constexpr NodeId kTrees = 2;
constexpr NodeId kFlowers = 3;
constexpr NodeId kMinerals = 4;
constexpr NodeId kRocks = 5;
constexpr NodeId kGems = 6;
constexpr NodeId kOak = 7;
constexpr NodeId kPine = 8;

// QSignalSpy stores a QVariant parameter as a nested QVariant; unwrap it.
QVariant unwrap(const QVariant& value)
{
    if (value.metaType().id() == QMetaType::QVariant)
        return value.value<QVariant>();

    return value;
}

// Sizes a widget through the framework's measurement and arrangement hooks.
template<class Widget>
void arrangeWidget(Widget& widget, int width = 320, int height = 56)
{
    widget.measure(md::MeasureSpec::exactly(qreal(width)),
                   md::MeasureSpec::exactly(qreal(height)));
    widget.arrange(QRect(0, 0, width, height));
}

// Renders a widget synchronously so the paint path really runs.
QImage renderWidget(QWidget& widget)
{
    const QSize size = widget.size();
    QImage image(std::max(1, size.width()), std::max(1, size.height()),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    widget.render(&painter);
    painter.end();

    return image;
}

// The tree the virtual selectors read. The counters are the point: a collapsed
// subtree must never be asked for its children.
class CountingTreeSource final : public md::structure::VirtualTreeSource
{
public:
    CountingTreeSource()
    {
        roots_ = {kPlants, kMinerals};
        labels_ = {
            {kPlants, QStringLiteral("Plants")},   {kTrees, QStringLiteral("Trees")},
            {kFlowers, QStringLiteral("Flowers")}, {kMinerals, QStringLiteral("Minerals")},
            {kRocks, QStringLiteral("Rocks")},     {kGems, QStringLiteral("Gems")},
            {kOak, QStringLiteral("Oak")},         {kPine, QStringLiteral("Pine")},
        };
        children_ = {
            {kPlants, {kTrees, kFlowers}},
            {kTrees, {kOak, kPine}},
            {kMinerals, {kRocks, kGems}},
        };
    }

    [[nodiscard]] int rootCount() const override
    {
        ++rootCountCalls;
        return static_cast<int>(roots_.size());
    }

    [[nodiscard]] NodeId rootAt(int index) const override
    {
        ++rootAtCalls;
        if (index < 0 || index >= roots_.size())
            return 0;
        return roots_.at(index);
    }

    [[nodiscard]] int childCount(NodeId parent) const override
    {
        ++childCountCalls;
        return static_cast<int>(children_.value(parent).size());
    }

    [[nodiscard]] NodeId childAt(NodeId parent, int index) const override
    {
        ++childAtCalls;
        ++childAtCallsPerNode[parent];

        const QVector<NodeId> children = children_.value(parent);
        if (index < 0 || index >= children.size())
            return 0;
        return children.at(index);
    }

    [[nodiscard]] QVariant value(NodeId node) const override
    {
        ++valueCalls;
        return labels_.value(node);
    }

    [[nodiscard]] QString label(NodeId node) const override
    {
        ++labelCalls;
        return labels_.value(node);
    }

    mutable int rootCountCalls = 0;
    mutable int rootAtCalls = 0;
    mutable int childCountCalls = 0;
    mutable int childAtCalls = 0;
    mutable int valueCalls = 0;
    mutable int labelCalls = 0;
    mutable QHash<NodeId, int> childAtCallsPerNode;

private:
    QVector<NodeId> roots_;
    QHash<NodeId, QString> labels_;
    QHash<NodeId, QVector<NodeId>> children_;
};

// A flat list that counts what the menu reads from it. It implements the
// interface directly because ListSource is a final convenience type.
class CountingListSource final : public md::structure::VirtualSource
{
public:
    CountingListSource() = default;
    explicit CountingListSource(QVariantList values)
        : values_(std::move(values))
    {
    }

    [[nodiscard]] int count() const override
    {
        return static_cast<int>(values_.size());
    }

    [[nodiscard]] QVariant item(int index) const override
    {
        ++itemCalls;
        if (index < 0 || index >= values_.size())
            return {};
        return values_.at(index);
    }

    [[nodiscard]] QString label(int index) const override
    {
        ++labelCalls;
        if (index < 0 || index >= values_.size())
            return {};
        return values_.at(index).toString();
    }

    mutable int itemCalls = 0;
    mutable int labelCalls = 0;

private:
    QVariantList values_;
};

// The 2-level model the TreeSelect tests use: two branches, three leaves.
std::unique_ptr<QStandardItemModel> makeFruitModel()
{
    auto model = std::make_unique<QStandardItemModel>();

    auto* fruit = new QStandardItem(QStringLiteral("Fruit"));
    fruit->setData(QStringLiteral("fruit"), Qt::UserRole);
    auto* apple = new QStandardItem(QStringLiteral("Apple"));
    apple->setData(QStringLiteral("apple"), Qt::UserRole);
    auto* banana = new QStandardItem(QStringLiteral("Banana"));
    banana->setData(QStringLiteral("banana"), Qt::UserRole);
    fruit->appendRow(apple);
    fruit->appendRow(banana);

    auto* vegetables = new QStandardItem(QStringLiteral("Vegetables"));
    vegetables->setData(QStringLiteral("vegetables"), Qt::UserRole);
    auto* carrot = new QStandardItem(QStringLiteral("Carrot"));
    carrot->setData(QStringLiteral("carrot"), Qt::UserRole);
    vegetables->appendRow(carrot);

    model->appendRow(fruit);
    model->appendRow(vegetables);
    return model;
}

} // namespace

class TestSelectTree final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void treeSelectBranchesAreNotSelectable();
    void treeSelectLeafSelectionAndPath();
    void treeSelectKeyboardAndEscape();
    void treeSelectFormatsThroughAdapter();

    void virtualTreeSelectReadsOnlyVisibleRows();
    void virtualTreeSelectSelectionAndKeyboard();

    void virtualSelectInstantiatesOnlyVisibleRows();
    void virtualSelectKeyboardAndFilter();
    void virtualSelectScrolls();
};

void TestSelectTree::initTestCase()
{
    // Pin the theme so the dp measurements do not depend on the platform.
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestSelectTree::treeSelectBranchesAreNotSelectable()
{
    const auto model = makeFruitModel();

    QWidget window;
    window.resize(520, 520);

    md::TreeSelect select(&window);
    select.setModel(model.get());
    arrangeWidget(select);

    // Collapsed, the tree shows its two roots only.
    QCOMPARE(select.visibleRowCount(), 2);
    QCOMPARE(select.paintedRowCount(), 2);
    QVERIFY(!select.hasSelection());
    QVERIFY(!select.selectedIndex().isValid());
    QCOMPARE(select.currentText(), select.placeholderText());

    window.show();
    select.show();

    // The painted field is the control's own: clicking it toggles the menu.
    const QPoint fieldCentre = select.fieldRect().center().toPoint();
    QTest::mouseClick(&select, Qt::LeftButton, Qt::NoModifier, fieldCentre);
    QVERIFY(select.isPopupVisible());
    QTest::mouseClick(&select, Qt::LeftButton, Qt::NoModifier, fieldCentre);
    QVERIFY(!select.isPopupVisible());

    QSignalSpy selectedSpy(&select, &md::TreeSelect::selected);

    select.showPopup();
    QVERIFY(select.isPopupVisible());
    QVERIFY(select.popupWidget() != nullptr);
    QCOMPARE(select.visibleRowCount(), 2);

    // A click on a branch only expands it: no selection, no signal.
    QWidget* popup = select.popupWidget();
    QTest::mouseClick(popup, Qt::LeftButton, Qt::NoModifier,
                      select.rowRect(0).center().toPoint());
    QCOMPARE(select.visibleRowCount(), 4);
    QVERIFY(select.isExpanded(model->index(0, 0)));
    QVERIFY(!select.hasSelection());
    QVERIFY(!select.selectedIndex().isValid());
    QCOMPARE(static_cast<int>(selectedSpy.count()), 0);

    // A second click collapses it again; the rule is symmetric.
    QTest::mouseClick(popup, Qt::LeftButton, Qt::NoModifier,
                      select.rowRect(0).center().toPoint());
    QCOMPARE(select.visibleRowCount(), 2);
    QVERIFY(!select.isExpanded(model->index(0, 0)));
    QVERIFY(!select.hasSelection());
    QCOMPARE(static_cast<int>(selectedSpy.count()), 0);

    // Enter on a branch expands it just like a click does. The clicks left the
    // focus row on "Fruit", so Down steps to the second root first.
    QCOMPARE(select.highlightedRow(), 0);
    QTest::keyClick(popup, Qt::Key_Down);
    QCOMPARE(select.highlightedRow(), 1);
    QTest::keyClick(popup, Qt::Key_Return);
    QCOMPARE(select.visibleRowCount(), 3);
    QVERIFY(select.isExpanded(model->index(1, 0)));
    QVERIFY(!select.hasSelection());
    QCOMPARE(static_cast<int>(selectedSpy.count()), 0);

    // The menu window is bounded by setMaxVisibleRows; the focus row scrolls
    // into it and the rows outside the window are not painted at all.
    select.setMaxVisibleRows(1);
    QCOMPARE(select.paintedRowCount(), 1);
    int paintedRows = 0;
    for (int row = 0; row < select.visibleRowCount(); ++row) {
        if (!select.rowRect(row).isEmpty())
            ++paintedRows;
    }
    QCOMPARE(paintedRows, 1);
    QVERIFY(!select.rowRect(select.highlightedRow()).isEmpty());

    // A press outside the field and the menu dismisses it.
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(400, 400));
    QVERIFY(!select.isPopupVisible());
}

void TestSelectTree::treeSelectLeafSelectionAndPath()
{
    const auto model = makeFruitModel();

    QWidget window;
    window.resize(520, 520);

    md::TreeSelect select(&window);
    select.setModel(model.get());
    arrangeWidget(select);
    window.show();
    select.show();

    QSignalSpy selectedSpy(&select, &md::TreeSelect::selected);

    select.setExpanded(model->index(0, 0), true);
    QCOMPARE(select.visibleRowCount(), 4);

    select.showPopup();
    QVERIFY(select.isPopupVisible());

    // Row 1 is the first leaf of the expanded branch.
    QCOMPARE(select.rowLabel(1), QStringLiteral("Apple"));
    QTest::mouseClick(select.popupWidget(), Qt::LeftButton, Qt::NoModifier,
                      select.rowRect(1).center().toPoint());

    QVERIFY(!select.isPopupVisible());
    QVERIFY(select.hasSelection());
    QVERIFY(select.selectedIndex().isValid());

    // The selection is the model index itself, which is what a caller needs to
    // map back into its own data.
    QCOMPARE(select.adapter()->mapToSource(select.selectedIndex()),
             model->index(0, 0, model->index(0, 0)));
    QCOMPARE(select.selectedValue().toString(), QStringLiteral("apple"));
    QCOMPARE(select.currentText(), QStringLiteral("Apple"));
    QCOMPARE(select.selectedPath(),
             QStringList({QStringLiteral("Fruit"), QStringLiteral("Apple")}));

    QCOMPARE(static_cast<int>(selectedSpy.count()), 1);
    QCOMPARE(unwrap(selectedSpy.at(0).at(0)).toString(), QStringLiteral("apple"));

    // Selecting another leaf replaces the selection and announces the new value.
    select.showPopup();
    QTest::mouseClick(select.popupWidget(), Qt::LeftButton, Qt::NoModifier,
                      select.rowRect(2).center().toPoint());
    QCOMPARE(select.currentText(), QStringLiteral("Banana"));
    QCOMPARE(select.selectedValue().toString(), QStringLiteral("banana"));
    QCOMPARE(static_cast<int>(selectedSpy.count()), 2);
}

void TestSelectTree::treeSelectKeyboardAndEscape()
{
    const auto model = makeFruitModel();

    QWidget window;
    window.resize(520, 520);

    md::TreeSelect select(&window);
    select.setModel(model.get());
    arrangeWidget(select);
    window.show();
    select.show();

    // Down opens the menu and gives the tree its first focus row.
    QTest::keyClick(&select, Qt::Key_Down);
    QVERIFY(select.isPopupVisible());
    QCOMPARE(select.highlightedRow(), 0);

    // Return on a branch expands, it does not select.
    QTest::keyClick(select.popupWidget(), Qt::Key_Return);
    QCOMPARE(select.visibleRowCount(), 4);
    QVERIFY(!select.hasSelection());

    // Down moves to the leaf, Enter selects it and closes the menu.
    QTest::keyClick(select.popupWidget(), Qt::Key_Down);
    QCOMPARE(select.highlightedRow(), 1);
    QTest::keyClick(select.popupWidget(), Qt::Key_Enter);
    QVERIFY(!select.isPopupVisible());
    QCOMPARE(select.currentText(), QStringLiteral("Apple"));
    QCOMPARE(select.selectedPath(),
             QStringList({QStringLiteral("Fruit"), QStringLiteral("Apple")}));

    // Escape closes without touching the selection.
    select.showPopup();
    QVERIFY(select.isPopupVisible());
    QTest::keyClick(select.popupWidget(), Qt::Key_Escape);
    QVERIFY(!select.isPopupVisible());
    QCOMPARE(select.currentText(), QStringLiteral("Apple"));

    // Home / End, Left collapsing and Right stepping into an expanded branch.
    select.showPopup();
    QTest::keyClick(select.popupWidget(), Qt::Key_Home);
    QCOMPARE(select.highlightedRow(), 0);
    QTest::keyClick(select.popupWidget(), Qt::Key_Left);
    QCOMPARE(select.visibleRowCount(), 2);
    QTest::keyClick(select.popupWidget(), Qt::Key_Right);
    QCOMPARE(select.visibleRowCount(), 4);
    QTest::keyClick(select.popupWidget(), Qt::Key_Right);
    QCOMPARE(select.highlightedRow(), 1);
    QTest::keyClick(select.popupWidget(), Qt::Key_Left);
    QCOMPARE(select.highlightedRow(), 0);
    QTest::keyClick(select.popupWidget(), Qt::Key_End);
    QCOMPARE(select.highlightedRow(), select.visibleRowCount() - 1);
}

void TestSelectTree::treeSelectFormatsThroughAdapter()
{
    QStandardItemModel model;
    auto* fruit = new QStandardItem(QStringLiteral("Fruit"));
    fruit->setData(0.5, Qt::UserRole);
    auto* half = new QStandardItem(QStringLiteral("Half"));
    half->setData(0.25, Qt::UserRole);
    fruit->appendRow(half);
    model.appendRow(fruit);

    QWidget window;
    window.resize(520, 520);

    md::TreeSelect select(&window);
    select.setModel(&model);
    QVERIFY(select.adapter() != nullptr);
    QCOMPARE(select.adapter()->sourceModel(), &model);

    select.setFormatter([](const QVariant& value) {
        return QStringLiteral("%1 %").arg(value.toDouble() * 100.0, 0, 'f', 0);
    });

    // The formatter applies per node: a child row is formatted from its own
    // value, not from a top-level row with the same row number.
    QCOMPARE(select.rowLabel(0), QStringLiteral("50 %"));
    QCOMPARE(select.adapter()->labelFor(0.25), QStringLiteral("25 %"));

    select.setExpanded(model.index(0, 0), true);
    QCOMPARE(select.visibleRowCount(), 2);
    QCOMPARE(select.rowLabel(1), QStringLiteral("25 %"));

    arrangeWidget(select);
    window.show();
    select.show();

    select.activateRow(1);
    QVERIFY(select.hasSelection());
    QCOMPARE(select.selectedValue().toDouble(), 0.25);
    QCOMPARE(select.currentText(), QStringLiteral("25 %"));
    QCOMPARE(select.selectedPath(),
             QStringList({QStringLiteral("50 %"), QStringLiteral("25 %")}));

    // A formatter change re-presents the same selection and its path.
    select.setFormatter([](const QVariant& value) {
        return QStringLiteral("v=%1").arg(value.toDouble());
    });
    QCOMPARE(select.currentText(), QStringLiteral("v=0.25"));
    QCOMPARE(select.selectedPath(),
             QStringList({QStringLiteral("v=0.5"), QStringLiteral("v=0.25")}));

    // Without a formatter the source's display text is the label.
    md::TreeSelect plain(&window);
    plain.setModel(&model);
    QCOMPARE(plain.rowLabel(0), QStringLiteral("Fruit"));
    plain.setExpanded(model.index(0, 0), true);
    QCOMPARE(plain.rowLabel(1), QStringLiteral("Half"));
}

void TestSelectTree::virtualTreeSelectReadsOnlyVisibleRows()
{
    CountingTreeSource source;

    QWidget window;
    window.resize(520, 520);

    md::VirtualTreeSelect select(&window);
    select.setSource(&source);
    arrangeWidget(select);

    // Two roots, nothing below them read: no childAt() call at all.
    QCOMPARE(select.visibleRowCount(), 2);
    QCOMPARE(select.paintedRowCount(), 2);
    QCOMPARE(source.childAtCalls, 0);
    QCOMPARE(source.rootAtCalls, 2);
    QCOMPARE(select.rowNode(0), kPlants);
    QCOMPARE(select.rowLabel(0), QStringLiteral("Plants"));
    QCOMPARE(select.rowNode(1), kMinerals);
    QVERIFY(select.rowHasChildren(0));
    QVERIFY(!select.rowIsSelectable(0));

    // Expanding one branch reads exactly that branch's children.
    select.setExpanded(kPlants, true);
    QVERIFY(select.isExpanded(kPlants));
    QCOMPARE(select.visibleRowCount(), 4);
    QCOMPARE(source.childAtCallsPerNode.value(kPlants), 2);
    QCOMPARE(source.childAtCallsPerNode.value(kTrees), 0);
    QCOMPARE(select.rowNode(1), kTrees);
    QCOMPARE(select.rowDepth(1), 1);

    window.show();
    select.show();

    // The menu expands the nested branch on a click, and still only that branch
    // is read: Oak and Pine are never touched until "Trees" is open.
    select.showPopup();
    QVERIFY(select.isPopupVisible());
    QTest::mouseClick(select.popupWidget(), Qt::LeftButton, Qt::NoModifier,
                      select.rowRect(1).center().toPoint());

    QCOMPARE(select.visibleRowCount(), 6);
    QCOMPARE(source.childAtCallsPerNode.value(kTrees), 2);
    QCOMPARE(source.childAtCallsPerNode.value(kOak), 0);
    // Both branches on the path are open; the leaf under them is not a branch.
    QVERIFY(select.rowIsExpanded(0));
    QVERIFY(select.rowIsExpanded(1));
    QVERIFY(select.rowHasChildren(1));
    QVERIFY(!select.rowHasChildren(2));
    QCOMPARE(select.rowLabel(2), QStringLiteral("Oak"));
}

void TestSelectTree::virtualTreeSelectSelectionAndKeyboard()
{
    CountingTreeSource source;

    QWidget window;
    window.resize(520, 520);

    md::VirtualTreeSelect select(&window);
    select.setSource(&source);
    arrangeWidget(select);
    window.show();
    select.show();

    select.setExpanded(kPlants, true);
    select.setExpanded(kTrees, true);
    QCOMPARE(select.visibleRowCount(), 6);

    QSignalSpy selectedSpy(&select, &md::VirtualTreeSelect::selected);

    // A leaf is selectable; its path is the labels from the root down.
    select.activateRow(2);
    QCOMPARE(select.selectedValue().toString(), QStringLiteral("Oak"));
    QCOMPARE(select.currentText(), QStringLiteral("Oak"));
    QCOMPARE(select.selectedPath(),
             QStringList({QStringLiteral("Plants"), QStringLiteral("Trees"),
                          QStringLiteral("Oak")}));
    QCOMPARE(static_cast<int>(selectedSpy.count()), 1);

    // A branch is not: activating it collapses the branch and leaves the
    // selection, its value and its path untouched.
    select.activateRow(0);
    QVERIFY(!select.isExpanded(kPlants));
    QCOMPARE(select.visibleRowCount(), 2);
    QCOMPARE(select.selectedValue().toString(), QStringLiteral("Oak"));
    QCOMPARE(static_cast<int>(selectedSpy.count()), 1);
    // The path was captured when the leaf was selected, so it survives the
    // collapse that hid the row.
    QCOMPARE(select.selectedPath().size(), 3);

    // Keyboard on a fresh control: open, expand, step in, expand, select.
    CountingTreeSource keySource;
    md::VirtualTreeSelect keys(&window);
    keys.setSource(&keySource);
    arrangeWidget(keys);
    keys.show();

    QTest::keyClick(&keys, Qt::Key_Down);
    QVERIFY(keys.isPopupVisible());
    QCOMPARE(keys.highlightedRow(), 0);

    QTest::keyClick(keys.popupWidget(), Qt::Key_Return);
    QCOMPARE(keys.visibleRowCount(), 4);
    QVERIFY(!keys.hasSelection());

    QTest::keyClick(keys.popupWidget(), Qt::Key_Right);
    QCOMPARE(keys.highlightedRow(), 1);
    QTest::keyClick(keys.popupWidget(), Qt::Key_Return);
    QCOMPARE(keys.visibleRowCount(), 6);

    QTest::keyClick(keys.popupWidget(), Qt::Key_Down);
    QCOMPARE(keys.highlightedRow(), 2);
    QTest::keyClick(keys.popupWidget(), Qt::Key_Enter);
    QVERIFY(!keys.isPopupVisible());
    QCOMPARE(keys.selectedValue().toString(), QStringLiteral("Oak"));
    QCOMPARE(keys.selectedPath(),
             QStringList({QStringLiteral("Plants"), QStringLiteral("Trees"),
                          QStringLiteral("Oak")}));

    // Escape leaves the selection alone.
    keys.showPopup();
    QTest::keyClick(keys.popupWidget(), Qt::Key_Escape);
    QVERIFY(!keys.isPopupVisible());
    QCOMPARE(keys.currentText(), QStringLiteral("Oak"));
}

void TestSelectTree::virtualSelectInstantiatesOnlyVisibleRows()
{
    QVariantList values;
    values.reserve(1000);
    for (int row = 0; row < 1000; ++row)
        values.append(QStringLiteral("Row %1").arg(row));

    CountingListSource source(values);

    QWidget window;
    window.resize(520, 520);

    md::VirtualSelect select(&window);
    select.setSource(&source);
    select.setMaxVisibleRows(8);
    arrangeWidget(select);

    QCOMPARE(select.itemCount(), 1000);
    QCOMPARE(select.visibleRowCount(), 0); // the menu is closed

    window.show();
    select.show();

    select.showPopup();
    QVERIFY(select.isPopupVisible());
    QCOMPARE(select.visibleRowCount(), 8);
    QCOMPARE(select.firstVisibleRow(), 0);
    QCOMPARE(select.maxVisibleRows(), 8);

    // Opening asks for the painted window plus a small prefetch margin — a
    // dozen labels, not a thousand, and no values at all.
    QVERIFY(source.labelCalls <= 16);
    QCOMPARE(source.itemCalls, 0);

    const QImage image = renderWidget(*select.popupWidget());
    QCOMPARE(image.size(), select.popupWidget()->size());
    QVERIFY(source.labelCalls <= 16);
    QCOMPARE(source.itemCalls, 0);

    // Geometry is the painted window, not the source.
    QVERIFY(!select.rowRect(0).isEmpty());
    QVERIFY(!select.rowRect(7).isEmpty());
    QVERIFY(select.rowRect(8).isEmpty());
    QCOMPARE(select.rowAt(select.rowRect(3).center()), 3);
    QCOMPARE(select.rowAt(QPointF(-5.0, -5.0)), -1);
    QCOMPARE(select.labelAt(0), QStringLiteral("Row 0"));
    QCOMPARE(select.sourceIndexAt(0), 0);
    QVERIFY(select.visibleRowCount() < source.count());
}

void TestSelectTree::virtualSelectKeyboardAndFilter()
{
    md::structure::ListSource source(
        QVariantList{QStringLiteral("Alpha"), QStringLiteral("Alpine"),
                     QStringLiteral("Beta"), QStringLiteral("Gamma")});

    QWidget window;
    window.resize(520, 520);

    md::VirtualSelect select(&window);
    select.setSource(&source);
    arrangeWidget(select);
    window.show();
    select.show();

    QSignalSpy selectedSpy(&select, &md::VirtualSelect::selected);
    QSignalSpy changedSpy(&select, &md::VirtualSelect::currentChanged);

    select.showPopup();
    QCOMPARE(select.visibleRowCount(), 4);
    QCOMPARE(select.currentIndex(), -1);
    QCOMPARE(select.highlightedPosition(), -1);

    // Down gives the menu a highlight and takes the selection with it.
    QTest::keyClick(select.popupWidget(), Qt::Key_Down);
    QCOMPARE(select.highlightedPosition(), 0);
    QCOMPARE(select.currentIndex(), 0);
    QCOMPARE(select.currentText(), QStringLiteral("Alpha"));
    QVERIFY(changedSpy.count() >= 1);

    QTest::keyClick(select.popupWidget(), Qt::Key_End);
    QCOMPARE(select.currentIndex(), 3);
    QCOMPARE(select.currentText(), QStringLiteral("Gamma"));

    QTest::keyClick(select.popupWidget(), Qt::Key_Home);
    QCOMPARE(select.currentIndex(), 0);

    QTest::keyClick(select.popupWidget(), Qt::Key_Down);
    QTest::keyClick(select.popupWidget(), Qt::Key_Enter);
    QVERIFY(!select.isPopupVisible());
    QCOMPARE(select.currentValue().toString(), QStringLiteral("Alpine"));
    QCOMPARE(static_cast<int>(selectedSpy.count()), 1);

    // A filter narrows the order without changing the selection.
    select.setFilterable(true);
    QVERIFY(select.isFilterable());
    QVERIFY(select.editor() != nullptr);

    select.showPopup();
    QCOMPARE(select.visibleRowCount(), 4);
    QTest::keyClicks(select.editor(), QStringLiteral("ga"));

    QCOMPARE(select.filteredCount(), 1);
    QCOMPARE(select.visibleRowCount(), 1);
    QCOMPARE(select.labelAt(0), QStringLiteral("Gamma"));
    QCOMPARE(select.currentValue().toString(), QStringLiteral("Alpine"));

    QTest::keyClick(select.editor(), Qt::Key_Return);
    QVERIFY(!select.isPopupVisible());
    QCOMPARE(select.currentValue().toString(), QStringLiteral("Gamma"));
    QCOMPARE(static_cast<int>(selectedSpy.count()), 2);

    // Filtering is case-insensitive, and Escape discards it.
    select.showPopup();
    QTest::keyClicks(select.editor(), QStringLiteral("AL"));
    QCOMPARE(select.filteredCount(), 2);
    QCOMPARE(select.labelAt(0), QStringLiteral("Alpha"));
    QCOMPARE(select.labelAt(1), QStringLiteral("Alpine"));

    QTest::keyClick(select.editor(), Qt::Key_Escape);
    QVERIFY(!select.isPopupVisible());
    QCOMPARE(select.currentValue().toString(), QStringLiteral("Gamma"));
    QCOMPARE(select.filteredCount(), 4);
}

void TestSelectTree::virtualSelectScrolls()
{
    QVariantList values;
    values.reserve(1000);
    for (int row = 0; row < 1000; ++row)
        values.append(QStringLiteral("Row %1").arg(row));

    md::structure::ListSource source(values);

    QWidget window;
    window.resize(520, 520);

    md::VirtualSelect select(&window);
    select.setSource(&source);
    select.setMaxVisibleRows(8);
    arrangeWidget(select);
    window.show();
    select.show();

    select.showPopup();
    QCOMPARE(select.firstVisibleRow(), 0);
    QCOMPARE(select.visibleRowCount(), 8);

    QWidget* popup = select.popupWidget();

    // The wheel scrolls the window and never touches the selection.
    QWheelEvent wheelDown(QPointF(20.0, 20.0), QPointF(20.0, 20.0), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(popup, &wheelDown);
    QCOMPARE(select.firstVisibleRow(), 3);
    QCOMPARE(select.currentIndex(), -1);

    // PageDown walks the highlight a page at a time and keeps it visible.
    QTest::keyClick(popup, Qt::Key_PageDown);
    QCOMPARE(select.currentIndex(), 7);
    QTest::keyClick(popup, Qt::Key_PageDown);
    QCOMPARE(select.currentIndex(), 15);
    QVERIFY(select.firstVisibleRow() > 3);
    const int afterPages = select.firstVisibleRow();

    // The scrollbar is draggable: a press jumps, a move drags, a release ends.
    const QRectF bar = select.scrollBarRect();
    QVERIFY(!bar.isEmpty());

    const qreal x = bar.center().x();
    const QPoint pressPoint(qRound(x), qRound(bar.top() + bar.height() * 0.5));
    QTest::mousePress(popup, Qt::LeftButton, Qt::NoModifier, pressPoint);
    QVERIFY(select.firstVisibleRow() > afterPages);

    const qreal dragY = bar.top() + bar.height() * 0.8;
    QMouseEvent drag(QEvent::MouseMove, QPointF(x, dragY),
                     popup->mapToGlobal(QPointF(x, dragY)), Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    const int afterPress = select.firstVisibleRow();
    QCoreApplication::sendEvent(popup, &drag);
    QVERIFY(select.firstVisibleRow() > afterPress);

    QTest::mouseRelease(popup, Qt::LeftButton, Qt::NoModifier, pressPoint);

    // Home and End move the selection across the whole source, scrolling with it.
    QTest::keyClick(popup, Qt::Key_End);
    QCOMPARE(select.currentIndex(), 999);
    QCOMPARE(select.firstVisibleRow(), 992);
    QTest::keyClick(popup, Qt::Key_Home);
    QCOMPARE(select.currentIndex(), 0);
    QCOMPARE(select.firstVisibleRow(), 0);

    // A press outside the menu and the field dismisses it.
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(400, 400));
    QVERIFY(!select.isPopupVisible());
}

QTEST_MAIN(TestSelectTree)

#include "tst_select_tree.moc"
