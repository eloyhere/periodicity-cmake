// Periodicity — Table, a two-dimensional view backed by any QAbstractItemModel.
//
// Table is the grid counterpart of List: the caller owns the model, Table owns
// selection, hover, keyboard navigation, scrolling and painting. No cell is
// ever instantiated as a widget — the paint pass walks only the rows and
// columns that intersect the viewport, so a model with ten thousand rows costs
// the same as one with twenty.
//
// The header row is painted from QAbstractItemModel::headerData(Horizontal);
// an empty header title is simply an empty header cell. Rows use a uniform
// height (48 dp by default) and columns a uniform default width (120 dp), with
// setColumnWidth() overriding individual columns. Selection is a single
// current cell (or, in SingleRow mode, the whole current row); the current
// cell is what the arrow keys move.
//
// The table observes dataChanged, rowsInserted, rowsRemoved, columnsInserted,
// columnsRemoved, modelReset and layoutChanged, so the current cell and the
// scroll offsets stay valid when the model changes underneath it. The model is
// never owned: setModel(nullptr) is the documented way to detach it before the
// model is destroyed.

#pragma once

#include <md/view/view.h>

#include <QHash>
#include <Qt>

class QAbstractItemModel;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QPointF;
class QRectF;
class QResizeEvent;
class QWheelEvent;

namespace md {

class Table : public View
{
    Q_OBJECT

public:
    // How the current cell is presented. NoSelection keeps the current cell for
    // keyboard navigation but paints no selection layer; SingleCell paints the
    // current cell; SingleRow paints the whole current row.
    enum class SelectionMode {
        NoSelection,
        SingleCell,
        SingleRow,
    };
    Q_ENUM(SelectionMode)

    explicit Table(QWidget* parent = nullptr);

    // The model is not owned. Passing nullptr empties the table and clears the
    // selection.
    void setModel(QAbstractItemModel* model);
    [[nodiscard]] QAbstractItemModel* model() const;

    // The header row is 48 dp high when visible and takes no space otherwise.
    void setHeadersVisible(bool visible);
    [[nodiscard]] bool headersVisible() const noexcept;

    // The uniform row height, in dp. The design baseline table row is 48 dp.
    void setRowHeight(qreal dp);
    [[nodiscard]] qreal rowHeight() const noexcept;

    // The width of one column, in dp. The default applies to columns that were
    // never given an explicit width.
    void setColumnWidth(int column, qreal widthDp);
    [[nodiscard]] qreal columnWidth(int column) const;
    void setDefaultColumnWidth(qreal widthDp);
    [[nodiscard]] qreal defaultColumnWidth() const noexcept;

    [[nodiscard]] int rowCount() const;
    [[nodiscard]] int columnCount() const;

    void setSelectionMode(SelectionMode mode);
    [[nodiscard]] SelectionMode selectionMode() const noexcept;

    // The current cell, or (-1, -1) when there is none. setCurrentCell() clamps
    // to the model's bounds and scrolls the cell into view.
    [[nodiscard]] int currentRow() const noexcept;
    [[nodiscard]] int currentColumn() const noexcept;
    void setCurrentCell(int row, int column);
    void clearSelection();

    // Vertical and horizontal scrolling, in device-independent pixels from the
    // top-left of the table body.
    [[nodiscard]] qreal scrollOffset() const noexcept;
    void setScrollOffset(qreal offset);
    [[nodiscard]] qreal horizontalScrollOffset() const noexcept;
    void setHorizontalScrollOffset(qreal offset);
    void scrollToRow(int row);
    void scrollToCell(int row, int column);

    // The numbers the paint pass iterates over. firstVisibleRow() is the row at
    // the top edge; visibleRowCount() includes the partially visible row at the
    // bottom, exactly like List.
    [[nodiscard]] int firstVisibleRow() const;
    [[nodiscard]] int visibleRowCount() const;
    [[nodiscard]] int firstVisibleColumn() const;
    [[nodiscard]] int visibleColumnCount() const;

    // Geometry in widget coordinates. A row or cell outside the viewport still
    // reports a valid rectangle that is simply clipped away.
    [[nodiscard]] QRectF rowRect(int row) const;
    [[nodiscard]] QRectF cellRect(int row, int column) const;
    [[nodiscard]] QRectF columnRect(int column) const;
    [[nodiscard]] QRectF headerRect() const;

    // The row viewport: the content rect below the header. The paint pass
    // clips the rows to it, so a partially scrolled first row can never paint
    // over the header, and rowRect() places row 0 exactly at its top edge.
    [[nodiscard]] QRectF bodyRect() const;

signals:
    void cellClicked(int row, int column);
    void doubleClicked(int row, int column);
    void currentCellChanged(int row, int column);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void arrangeOverride(const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    [[nodiscard]] qreal rowHeightPx() const;
    [[nodiscard]] qreal headerHeightPx() const;
    [[nodiscard]] qreal columnWidthPx(int column) const;
    [[nodiscard]] qreal columnX(int column) const;
    [[nodiscard]] qreal totalColumnWidth() const;
    [[nodiscard]] qreal viewportWidth() const;
    [[nodiscard]] qreal viewportHeight() const;
    [[nodiscard]] qreal maximumScroll() const;
    [[nodiscard]] qreal maximumHorizontalScroll() const;
    [[nodiscard]] qreal scrollbarWidthPx() const;

    [[nodiscard]] QRectF verticalThumbRect() const;
    [[nodiscard]] QRectF horizontalThumbRect() const;
    [[nodiscard]] bool onVerticalScrollbar(const QPointF& position) const;
    [[nodiscard]] bool onHorizontalScrollbar(const QPointF& position) const;
    void updateVerticalScroll(qreal pointerY);
    void updateHorizontalScroll(qreal pointerX);
    void clampScroll();
    void ensureCellVisible(int row, int column);

    [[nodiscard]] int rowAtPosition(const QPointF& position) const;
    [[nodiscard]] int columnAtPosition(const QPointF& position) const;

    void connectModel();
    void resetCurrent(bool notify);

    void paintHeader(QPainter& painter, const QRectF& content);
    void paintCell(QPainter& painter, int row, int column, const QRectF& rect,
                   bool selected, bool hovered);
    void paintScrollbars(QPainter& painter, const QRectF& content);

    QAbstractItemModel* model_ = nullptr;
    QHash<int, qreal> columnWidthsDp_;

    SelectionMode selectionMode_ = SelectionMode::SingleCell;
    int currentRow_ = -1;
    int currentColumn_ = -1;
    int hoveredRow_ = -1;
    int hoveredColumn_ = -1;
    int pressedRow_ = -1;
    int pressedColumn_ = -1;

    bool headersVisible_ = true;
    bool draggingVertical_ = false;
    bool draggingHorizontal_ = false;
    qreal dragGrab_ = 0.0;

    qreal rowHeightDp_ = 48.0;
    qreal defaultColumnWidthDp_ = 120.0;
    qreal scrollOffset_ = 0.0;
    qreal horizontalScrollOffset_ = 0.0;
};

} // namespace md
