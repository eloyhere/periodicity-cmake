// Periodicity — MenuGroup, a titled group of menu rows with one selection.
//
// A MenuGroup is what turns a pile of MenuItems into a navigation section: an
// optional 40 dp title (a design baseline section label), then the rows at 2 dp
// spacing, one selected row at most, and a keyboard cursor that walks the
// visible rows. The group is a ViewGroup: the rows are real children, so focus,
// hover and hit testing are Qt's, and the group only owns the arrangement and
// the selection.
//
// Titles can be collapsible. A collapsed group hides its rows (they stop being
// laid out and cannot be reached with the keyboard); a SubMenu in the group
// does the same for its own children, which is why navigationRows() flattens
// the group's rows *and* the children of every expanded SubMenu, and skips the
// hidden ones. Up/Down move between those rows, Home/End jump to the ends and
// Enter/Space activates the current row.
//
// Selection is single by construction. The built-in model is private to the
// group; setSelectionModel() attaches an external MenuSelectionModel so several
// views can share one index. Activating an expandable row never selects it: it
// opens, which is the design baseline navigation behaviour.

#pragma once

#include <md/view/view_group.h>

#include <QList>
#include <QString>
#include <Qt>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class MenuItem;
class SubMenu;

// The single-selection state a group can share. The model knows nothing about
// the rows; the group validates the index against its own list.
class MenuSelectionModel final : public QObject
{
    Q_OBJECT

public:
    explicit MenuSelectionModel(QObject* parent = nullptr);

    [[nodiscard]] int selectedIndex() const noexcept { return selectedIndex_; }

    // -1 clears the selection. The value is stored as-is; a group clamps it
    // against its row count when it adopts the model.
    void setSelectedIndex(int index);

signals:
    void selectedIndexChanged(int index);

private:
    int selectedIndex_ = -1;
};

class MenuGroup : public ViewGroup
{
    Q_OBJECT

public:
    explicit MenuGroup(QWidget* parent = nullptr);
    ~MenuGroup() override;

    // The section label. An empty title paints nothing even when the title row
    // is visible.
    void setTitle(const QString& title);
    [[nodiscard]] QString title() const { return title_; }
    void setTitleVisible(bool visible);
    [[nodiscard]] bool isTitleVisible() const noexcept { return titleVisible_; }

    // A collapsible group toggles from its title row and reserves a chevron
    // there. A collapsed group keeps only the title.
    void setCollapsible(bool collapsible);
    [[nodiscard]] bool isCollapsible() const noexcept { return collapsible_; }
    void setExpanded(bool expanded);
    [[nodiscard]] bool isExpanded() const noexcept { return expanded_; }
    void toggleExpanded();

    // Rows. The group reparents an item but does not own it: the widget tree
    // does, and removeItem() hands the item back detached. Adding an item that
    // is already in the group moves it to the requested index.
    int addItem(MenuItem* item);
    int insertItem(int index, MenuItem* item);
    void removeItem(MenuItem* item);
    void clearItems();
    [[nodiscard]] int count() const;
    [[nodiscard]] MenuItem* itemAt(int index) const;
    [[nodiscard]] int indexOf(const MenuItem* item) const;
    [[nodiscard]] QList<MenuItem*> items() const;
    // The row as a SubMenu when it is one, otherwise nullptr.
    [[nodiscard]] SubMenu* subMenuAt(int index) const;

    // Selection. The model is not owned; passing nullptr restores the internal
    // one. setSelectedIndex() selects an existing, non-expandable row, or
    // clears with -1.
    void setSelectionModel(MenuSelectionModel* model);
    [[nodiscard]] MenuSelectionModel* selectionModel() const;
    void setSelectedIndex(int index);
    [[nodiscard]] int selectedIndex() const;
    void clearSelection();

    // Geometry, in the group's coordinates. titleRect() is empty when no title
    // row is shown; itemRect() covers the whole row of one item.
    [[nodiscard]] QRectF titleRect() const;
    [[nodiscard]] QRectF itemRect(int index) const;
    [[nodiscard]] MenuItem* itemAtPosition(const QPointF& position) const;

    // The keyboard order: every enabled, visible row of the group plus the
    // visible children of every expanded SubMenu, in paint order.
    [[nodiscard]] QList<MenuItem*> navigationRows() const;
    // The index of the focused row in navigationRows(), or the last cursor
    // position when no row has focus. -1 before the first navigation.
    [[nodiscard]] int currentRow() const;
    [[nodiscard]] MenuItem* currentItem() const;

    // Moves the cursor `delta` rows (skipping hidden rows) and focuses it.
    // Exposed so a host can drive the group from a shortcut.
    void moveCurrent(int delta);
    void activateCurrent();

signals:
    // A leaf row was activated by click or keyboard.
    void itemActivated(int index, QString label);
    // A row inside a SubMenu was activated. `subMenuIndex` is the submenu's
    // own row index in this group.
    void childItemActivated(int subMenuIndex, int childIndex, QString label);
    void selectionChanged(int index);
    void expandedChanged(bool expanded);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void paintChildren(QPainter& painter) override;

    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void onThemeChanged() override;
    [[nodiscard]] bool focusable() const override;
    bool onEvent(Event& event) override;

private:
    [[nodiscard]] qreal titleHeight() const;
    [[nodiscard]] qreal spacingPx() const;
    [[nodiscard]] qreal naturalWidth() const;
    [[nodiscard]] qreal rowsHeight() const;
    [[nodiscard]] bool rowsVisible() const;
    [[nodiscard]] MenuItem* navigationRowAt(int index) const;
    [[nodiscard]] int focusedRow() const;
    void relayout();
    void updateRowVisibility();
    void applySelection();
    void attachItem(MenuItem* item);
    void detachItem(MenuItem* item);
    void handleActivated(MenuItem* item);
    void paintTitle(QPainter& painter);

    QList<MenuItem*> items_;
    QList<QRectF> itemRects_;
    QString title_;
    bool titleVisible_ = true;
    bool collapsible_ = false;
    bool expanded_ = true;
    MenuSelectionModel internalModel_;
    MenuSelectionModel* model_ = nullptr;
    int currentRow_ = -1;
};

} // namespace md
