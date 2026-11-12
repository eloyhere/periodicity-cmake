// Periodicity — TreeSelect: a model-driven tree menu behind a filled field.
//
// The closed control is a Material filled field painted by this component: the
// current label, the placeholder and the chevron. Opening it raises a menu on
// the control's own md::Dropdown surface: the surface is a child of the window
// — not a Qt::Popup — so the menu shares the window's theme scope, belongs to
// the same top-level and never spawns a second window. The surface owns the
// placement (below the field, flipped above when the screen has no room, edges
// clamped to the screen) and the dismissal; Escape, a click outside the field
// and a click outside the panel all dismiss it.
//
// Every control owns exactly one surface, and that surface's anchor is the
// control itself, so a menu can never appear for another control's field.
//
// Data is borrowed, never owned. setModel() installs the application's model on
// the owned Adapter (components/model.h), which is the single presentation
// bridge: a formatter changes what a row reads, never the value a row stores.
//
// The menu is a tree. The control keeps a flattened list of the rows that are
// currently visible, rebuilt when a branch is expanded or collapsed; a model
// that changes structure rebuilds it as well.
//
// Selection rule (the same rule VirtualTreeSelect implements): a node with
// children is a branch, and a branch is never selectable. Activating a branch
// only toggles its expansion — with the mouse and with Enter alike. Only a leaf
// becomes the current selection, which is what selectedIndex(), selectedValue(),
// currentText() and selectedPath() report, and what selected() announces.

#pragma once

#include <md/components/model.h>
#include <md/view/view.h>

#include <QModelIndex>
#include <QPersistentModelIndex>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

class QAbstractItemModel;
class QHideEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class Dropdown;

class TreeSelect : public View
{
    Q_OBJECT

public:
    explicit TreeSelect(QWidget* parent = nullptr);
    ~TreeSelect() override;

    // Non-owning. Replacing the model resets the expansion, the selection and
    // the highlight; the previous model is never deleted.
    void setModel(QAbstractItemModel* model);
    [[nodiscard]] QAbstractItemModel* sourceModel() const noexcept;

    // The presentation bridge this control owns. Its formatter is applied per
    // node, and its value role is the role a node's value is read from.
    [[nodiscard]] Adapter* adapter() const noexcept;
    void setFormatter(Adapter::Formatter formatter);

    void setPlaceholderText(const QString& text);
    [[nodiscard]] QString placeholderText() const;

    // The maximum number of rows the menu paints before it scrolls. The menu
    // still reports every visible tree row through visibleRowCount().
    void setMaxVisibleRows(int rows);
    [[nodiscard]] int maxVisibleRows() const noexcept;

    // The current selection. Only a leaf can be selected; selectedIndex() is
    // therefore always a leaf index or an invalid index.
    [[nodiscard]] QModelIndex selectedIndex() const;
    [[nodiscard]] QVariant selectedValue() const;
    [[nodiscard]] QString currentText() const;
    // The labels from the root down to the selected leaf, presented exactly as
    // the menu draws them (the Adapter formatter included).
    [[nodiscard]] QStringList selectedPath() const;
    [[nodiscard]] bool hasSelection() const;

    // `index` is accepted from this control's Adapter as well as from the
    // source model: an identity proxy maps them row for row, so a caller never
    // has to know which of the two it is holding.
    bool isExpanded(const QModelIndex& index) const;
    void setExpanded(const QModelIndex& index, bool expanded);

    // The menu's row model: a row is one visible tree row, with the current
    // expansion applied. paintedRowCount() is the window of rows the menu
    // paints; visibleRowCount() is every visible tree row.
    [[nodiscard]] int visibleRowCount() const;
    [[nodiscard]] int paintedRowCount() const;
    [[nodiscard]] int rowDepth(int row) const;
    [[nodiscard]] QString rowLabel(int row) const;
    [[nodiscard]] bool rowHasChildren(int row) const;
    [[nodiscard]] bool rowIsExpanded(int row) const;
    [[nodiscard]] bool rowIsSelectable(int row) const;
    [[nodiscard]] bool rowIsSelected(int row) const;
    [[nodiscard]] QModelIndex rowIndex(int row) const;

    [[nodiscard]] int highlightedRow() const noexcept;
    void setHighlightedRow(int row);

    // The click/Enter action for a visible row: a branch toggles, a leaf is
    // selected and the menu closes.
    void activateRow(int row);

    // Menu geometry. Rows are measured in the menu widget's own coordinates so
    // that a test or a custom delegate can hit-test exactly what is drawn.
    [[nodiscard]] int rowAt(const QPointF& popupPoint) const;
    [[nodiscard]] QRectF rowRect(int row) const;
    [[nodiscard]] QRectF fieldRect() const;

    void showPopup();
    void hidePopup();
    [[nodiscard]] bool isPopupVisible() const;
    // The row surface: a child of the Dropdown, so its coordinates are the
    // ones rowAt() and rowRect() use.
    [[nodiscard]] QWidget* popupWidget() const noexcept;

    // The anchored surface the row widget lives in. Every control owns exactly
    // one: the menu can never be another control's, and two controls never
    // share a popup.
    [[nodiscard]] Dropdown* dropdown() const noexcept;

signals:
    void selected(const QVariant& value);
    void expandedChanged(const QModelIndex& index, bool expanded);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void onThemeChanged() override;
    [[nodiscard]] bool focusable() const override;

private:
    struct Row
    {
        QModelIndex index;
        int depth = 0;
        bool branch = false;
        bool expanded = false;
    };

    void rebuildRows();
    void appendRows(const QModelIndex& parent, int depth, const QString& parentPath);
    void handleStructureChanged();
    void handleModelReset();

    [[nodiscard]] QString pathOf(const QModelIndex& index) const;
    // Accepts both an index of this control's Adapter and an index of the
    // source model; an identity proxy maps them row for row, but Qt's proxy
    // assertions do not, so the control normalises before asking the Adapter
    // anything.
    [[nodiscard]] QModelIndex proxyIndexFor(const QModelIndex& index) const;
    [[nodiscard]] QString labelForIndex(const QModelIndex& index) const;
    [[nodiscard]] QString sourceTextFor(const QModelIndex& index) const;
    [[nodiscard]] QVariant valueForIndex(const QModelIndex& index) const;
    [[nodiscard]] int rowOfIndex(const QModelIndex& index) const;

    void clampHighlight();
    void ensureHighlightVisible();
    void restoreFocus();
    void applySelection(const QModelIndex& index);
    void syncPopupLayout();
    void handlePopupClosed();

    Adapter* adapter_ = nullptr;
    QVector<Row> rows_;
    QSet<QString> expandedPaths_;
    QPersistentModelIndex selected_;
    QString placeholder_ = QStringLiteral("Select an item");
    int maxVisibleRows_ = 8;
    int highlighted_ = -1;
    QWidget* popup_ = nullptr;
    Dropdown* dropdown_ = nullptr;
    bool popupOpen_ = false;
    bool pressed_ = false;
    bool formatterSet_ = false;
};

} // namespace md
