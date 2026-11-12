// Periodicity — the Cascader component (framework.md §5.9).
//
// A hierarchical selector that reveals one column per level. Clicking a node
// with children appends that node to the path and opens the next column;
// clicking a node without children completes the path. Selecting a different
// node at an earlier level discards every deeper choice, which is the whole
// contract of a cascader expressed in one place (selectItem).
//
// The columns are painted inline rather than in a popup, which keeps the
// component a plain measured View: no top-level windows, no focus traps, and
// the same public geometry helpers (columnRect, itemRect) serve painting,
// hit-testing and the tests. Keyboard input mirrors the pointer: Left/Right
// move the active column, Up/Down move inside it and Enter commits.

#pragma once

#include <md/view/view.h>

#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <utility>

class QKeyEvent;
class QMouseEvent;
class QPainter;

namespace md {

// One node of the hierarchy. A node may be a leaf (empty children) or an
// expandable parent; `value` is opaque to the component and is what
// currentValue()/leafActivated() report.
struct CascaderNode
{
    QString label;
    QVariant value;
    QList<CascaderNode> children;

    CascaderNode() = default;
    CascaderNode(QString labelText, QVariant nodeValue = {},
                 QList<CascaderNode> childNodes = {})
        : label(std::move(labelText))
        , value(std::move(nodeValue))
        , children(std::move(childNodes))
    {
    }
};

class Cascader : public View
{
    Q_OBJECT

public:
    explicit Cascader(QWidget* parent = nullptr);

    // Replaces the hierarchy. The current path is reset without emitting
    // pathChanged(); the next selection starts the signal sequence again.
    void setLevels(const QList<CascaderNode>& levels);
    [[nodiscard]] const QList<CascaderNode>& levels() const noexcept { return levels_; }

    // The selected labels from the root down (for example {"Zhejiang",
    // "Hangzhou"}). currentValue() is the value of the deepest selected node;
    // hasCompletePath() tells whether that node is a leaf.
    [[nodiscard]] QStringList currentPath() const;
    [[nodiscard]] QVariantList currentValues() const;
    [[nodiscard]] QVariant currentValue() const;
    [[nodiscard]] bool hasCompletePath() const;

    // The number of visible columns and the column that receives the keyboard
    // cursor. itemCount(column) is the number of nodes the column shows.
    [[nodiscard]] int columnCount() const;
    [[nodiscard]] int activeColumn() const noexcept { return activeColumn_; }
    [[nodiscard]] int cursorIndex() const noexcept { return cursorIndex_; }
    [[nodiscard]] int itemCount(int column) const;

    // Geometry in widget coordinates; empty when the column or index does not
    // exist.
    [[nodiscard]] QRectF columnRect(int column) const;
    [[nodiscard]] QRectF itemRect(int column, int index) const;

    // Selects the node at (column, index). Selecting a parent opens the next
    // column and drops any deeper selection; a leaf completes the path. Emits
    // pathChanged() when the path actually changes and leafActivated() when the
    // new node is a leaf. Returns false when the coordinates are invalid.
    bool selectItem(int column, int index);
    void clearSelection();

signals:
    void pathChanged(const QStringList& path);
    void leafActivated(const QVariant& value);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] QRectF contentArea() const;
    [[nodiscard]] const QList<CascaderNode>* nodesAt(int column) const;
    [[nodiscard]] const CascaderNode* nodeAt(int column, int index) const;
    void moveActiveColumn(int delta);
    void moveCursor(int delta);

    QList<CascaderNode> levels_;
    QList<int> indices_;
    int activeColumn_ = 0;
    int cursorIndex_ = 0;
    QPointF pressed_{-1.0, -1.0};
};

} // namespace md
