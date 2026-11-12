// Periodicity — VirtualTreeSelect: a tree menu over a VirtualTreeSource.
//
// The field, the menu and the interaction rules are the ones TreeSelect
// implements; the difference is where the tree comes from. A VirtualTreeSelect
// never materialises the hierarchy: it asks the source for the roots, then for
// the children of the branches the user actually expanded. A collapsed
// subtree is never read, so a source backed by a database or a path index pays
// only for what is on screen.
//
// The menu is painted by this component and raised on an md::Dropdown surface
// owned by this control: the surface does the placement and the dismissal, and
// its anchor is this control, so two tree selects never share a menu.
//
// Expansion state is keyed by the node's value (VirtualTreeSource::value()),
// not by a position, because positions are not stable across a lazily built
// tree. The source is borrowed and stays the application's.
//
// Selection rule: a node with children is a branch and is never selectable;
// activating it only toggles expansion. Only a leaf becomes the current
// selection.

#pragma once

#include <md/structure/virtual_source.h>
#include <md/view/view.h>

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

class QHideEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;
class QWheelEvent;

namespace md {

class Dropdown;

class VirtualTreeSelect : public View
{
    Q_OBJECT

public:
    using NodeId = structure::VirtualTreeSource::NodeId;

    explicit VirtualTreeSelect(QWidget* parent = nullptr);
    ~VirtualTreeSelect() override;

    // Non-owning. Replacing the source clears the expansion and the selection.
    void setSource(structure::VirtualTreeSource* source);
    [[nodiscard]] structure::VirtualTreeSource* source() const noexcept;

    void setPlaceholderText(const QString& text);
    [[nodiscard]] QString placeholderText() const;

    void setMaxVisibleRows(int rows);
    [[nodiscard]] int maxVisibleRows() const noexcept;

    // Expansion is stored per node value, so it survives a rebuild.
    [[nodiscard]] bool isExpanded(NodeId node) const;
    void setExpanded(NodeId node, bool expanded);

    // The visible rows of the lazily built tree. visibleRowCount() grows only
    // when a branch is expanded; paintedRowCount() is the menu's window.
    [[nodiscard]] int visibleRowCount() const;
    [[nodiscard]] int paintedRowCount() const;
    [[nodiscard]] NodeId rowNode(int row) const;
    [[nodiscard]] int rowDepth(int row) const;
    [[nodiscard]] QString rowLabel(int row) const;
    [[nodiscard]] bool rowHasChildren(int row) const;
    [[nodiscard]] bool rowIsExpanded(int row) const;
    [[nodiscard]] bool rowIsSelectable(int row) const;
    [[nodiscard]] bool rowIsSelected(int row) const;
    [[nodiscard]] int rowOfNode(NodeId node) const;

    // The current selection: a leaf, reported with the labels of its whole
    // path. selectedPath() is captured when the leaf is selected, so it stays
    // answerable after the ancestors are collapsed.
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] QVariant selectedValue() const;
    [[nodiscard]] QString currentText() const;
    [[nodiscard]] QStringList selectedPath() const;

    [[nodiscard]] int highlightedRow() const noexcept;
    void setHighlightedRow(int row);

    // The click/Enter action for a visible row: a branch toggles, a leaf is
    // selected and the menu closes.
    void activateRow(int row);

    [[nodiscard]] int rowAt(const QPointF& popupPoint) const;
    [[nodiscard]] QRectF rowRect(int row) const;
    [[nodiscard]] QRectF fieldRect() const;

    void showPopup();
    void hidePopup();
    [[nodiscard]] bool isPopupVisible() const;
    // The row surface: a child of the Dropdown, so its coordinates are the ones
    // rowAt() and rowRect() use.
    [[nodiscard]] QWidget* popupWidget() const noexcept;

    // The anchored surface the row widget lives in. Every control owns exactly
    // one: the menu can never be another control's, and two controls never
    // share a popup.
    [[nodiscard]] Dropdown* dropdown() const noexcept;

signals:
    void selected(const QVariant& value);
    void expandedChanged(VirtualTreeSelect::NodeId node, bool expanded);

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
        NodeId node = 0;
        int depth = 0;
        bool branch = false;
        bool expanded = false;
        QString label;
    };

    void rebuildRows();
    void appendNode(NodeId node, int depth);
    [[nodiscard]] QVariant keyOf(NodeId node) const;
    [[nodiscard]] QStringList pathForRow(int row) const;
    [[nodiscard]] int rowOfNodeInternal(NodeId node) const;
    void clampHighlight();
    void ensureHighlightVisible();
    void restoreFocus();
    void clearSelection();
    void syncPopupLayout();
    void handlePopupClosed();

    structure::VirtualTreeSource* source_ = nullptr;
    QVector<Row> rows_;
    // A vector rather than a QSet: Qt does not provide qHash(QVariant), and the
    // set of expanded branches is small. This mirrors the data layer's
    // flattenTree(), which keys expansion the same way.
    QVector<QVariant> expandedKeys_;
    bool hasSelection_ = false;
    NodeId selectedNode_ = 0;
    QVariant selectedValue_;
    QString selectedLabel_;
    QStringList selectedPath_;
    QString placeholder_ = QStringLiteral("Select an item");
    int maxVisibleRows_ = 8;
    int highlighted_ = -1;
    QWidget* popup_ = nullptr;
    Dropdown* dropdown_ = nullptr;
    bool popupOpen_ = false;
    bool pressed_ = false;
};

} // namespace md
