// Periodicity — VirtualTable, a table over an on-demand data source.
//
// A VirtualTable never copies the data it shows. Its source
// (md::structure::VirtualTableSource) answers "how many rows", "how many
// columns", "the cell at (row, column)" and "the header of column"; the table
// caches only the two counts and asks for the cells that are about to be
// painted. A source with ten thousand rows therefore costs one screen of work,
// which is what visibleRowCount()/visibleColumnCount() demonstrate and what a
// counting source can assert in a test.
//
// The source is not owned and — because VirtualTableSource is a plain
// interface, not a QObject — the table cannot observe its destruction. Call
// setSource(nullptr) before the source dies. setSource() caches the counts;
// call refresh() when an already-installed source changes its geometry.
//
// Columns can be frozen: the first `frozenColumns()` columns stay pinned at the
// left edge while the rest scroll horizontally underneath them. The paint pass
// is split into a frozen region and a scrolling region, so a cell in the
// scrolling region is only queried when it is actually exposed.

#pragma once

#include <md/view/view.h>

#include <QHash>
#include <Qt>

class QFont;
class QFontMetricsF;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QPointF;
class QRectF;
class QResizeEvent;
class QWheelEvent;

namespace md::structure {
class VirtualTableSource;
}

namespace md {

class ColorScheme;

class VirtualTable : public View
{
    Q_OBJECT

public:
    enum class SelectionMode {
        NoSelection,
        SingleCell,
        SingleRow,
    };
    Q_ENUM(SelectionMode)

    explicit VirtualTable(QWidget* parent = nullptr);

    // The source is not owned. Passing nullptr clears the cached geometry and
    // the selection. The counts are read once; call refresh() after the source
    // changes its row or column count.
    void setSource(structure::VirtualTableSource* source);
    [[nodiscard]] structure::VirtualTableSource* source() const;
    void refresh();

    [[nodiscard]] int rowCount() const;
    [[nodiscard]] int columnCount() const;

    // The presentation text of one cell, asked from the source on demand. Out
    // of range indices yield an empty string and never reach the source.
    [[nodiscard]] QString cellText(int row, int column) const;

    void setHeadersVisible(bool visible);
    [[nodiscard]] bool headersVisible() const noexcept;

    void setRowHeight(qreal dp);
    [[nodiscard]] qreal rowHeight() const noexcept;

    void setColumnWidth(int column, qreal widthDp);
    [[nodiscard]] qreal columnWidth(int column) const;
    void setDefaultColumnWidth(qreal widthDp);
    [[nodiscard]] qreal defaultColumnWidth() const noexcept;

    // Rows kept checked around the viewport. The paint pass queries the
    // prefetched rows as well, so the next scroll step does not start on an
    // empty band. Zero disables prefetching.
    void setPrefetchRows(int rows);
    [[nodiscard]] int prefetchRows() const noexcept;

    // The first N columns stay pinned at the left edge while the remaining
    // columns scroll horizontally.
    void setFrozenColumns(int columns);
    [[nodiscard]] int frozenColumns() const noexcept;

    void setSelectionMode(SelectionMode mode);
    [[nodiscard]] SelectionMode selectionMode() const noexcept;

    [[nodiscard]] int currentRow() const noexcept;
    [[nodiscard]] int currentColumn() const noexcept;
    void setCurrentCell(int row, int column);
    void clearSelection();

    [[nodiscard]] qreal scrollOffset() const noexcept;
    void setScrollOffset(qreal offset);
    [[nodiscard]] qreal horizontalScrollOffset() const noexcept;
    void setHorizontalScrollOffset(qreal offset);
    void scrollToRow(int row);
    void scrollToCell(int row, int column);

    // The numbers the paint pass works from. They are derived from the viewport
    // and the uniform row height, never from the source's row count, so they
    // stay bounded for a source of any size.
    [[nodiscard]] int firstVisibleRow() const;
    [[nodiscard]] int visibleRowCount() const;
    [[nodiscard]] int firstVisibleColumn() const;
    [[nodiscard]] int visibleColumnCount() const;

    // Geometry in widget coordinates. columnRect() reports the unpinned
    // position of a scrolling column (it may lie under the frozen region, which
    // is clipped at paint time).
    [[nodiscard]] QRectF rowRect(int row) const;
    [[nodiscard]] QRectF cellRect(int row, int column) const;
    [[nodiscard]] QRectF columnRect(int column) const;
    [[nodiscard]] QRectF headerRect() const;

    // The row viewport: the content rect below the header. Both the pinned and
    // the scrolling band are clipped to it, so the frozen column and the rows
    // that scroll under it share one top edge and never paint over the header.
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
    [[nodiscard]] qreal frozenWidthPx() const;
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

    void paintHeader(QPainter& painter, const QRectF& content);
    void paintHeaderCell(QPainter& painter, int column, const QRectF& rect,
                         const QFont& font, const QFontMetricsF& metrics, qreal padding,
                         const ColorScheme& colours);
    void paintCell(QPainter& painter, int row, int column, const QRectF& rect,
                   bool selected, bool hovered);
    void paintScrollbars(QPainter& painter, const QRectF& content);

    structure::VirtualTableSource* source_ = nullptr;
    QHash<int, qreal> columnWidthsDp_;
    int rowCount_ = 0;
    int columnCount_ = 0;

    SelectionMode selectionMode_ = SelectionMode::SingleCell;
    int currentRow_ = -1;
    int currentColumn_ = -1;
    int hoveredRow_ = -1;
    int hoveredColumn_ = -1;
    int pressedRow_ = -1;
    int pressedColumn_ = -1;
    int prefetchRows_ = 1;
    int frozenColumns_ = 0;

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
