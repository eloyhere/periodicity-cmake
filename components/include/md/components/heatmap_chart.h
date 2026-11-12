// Periodicity — the HeatmapChart component.
//
// A categorical matrix rendered as a grid of coloured cells. The finite value
// range is mapped onto a continuous scale between the theme's
// surfaceContainerHighest (the minimum) and primary (the maximum), so the
// chart needs no colour configuration and follows a theme switch automatically.
// A NaN cell is empty rather than zero: it is skipped by the range calculation
// and hatched in outlineVariant, so a missing observation cannot be mistaken
// for a low one. A matrix with no finite value paints the shared empty state
// instead of a grid.
//
// Geometry. The cells occupy plotRect() minus the label gutters: a row-label
// gutter on the reading-start side and a column-label row above the matrix.
// cellRect() exposes that geometry and cellAt() maps a widget-local pixel back
// to a (column, row) pair; both mirror the column order under
// Qt::RightToLeft, so the labels stay adjacent to the data. The logical matrix
// order is never transposed, only its screen mapping.
//
// Hover. mouseMoveEvent() resolves the cell under the pointer, stores it and
// repaints; leaveEvent() clears it. The hovered cell receives a translucent
// onSurface overlay and a 2 dp outline, both from the theme. The base View's
// whole-surface state layer is switched off in the constructor: it would tint
// every cell and the scale would no longer show the pure role colours, so the
// chart supplies its own, cell-local hover feedback instead.
//
// Exception safety. paintPlot(), mouseMoveEvent() and leaveEvent() each catch
// locally, so a failure cannot unwind into Qt's paint or event dispatch.
//
// Chart chrome. ChartBase owns the title, legend and empty state. A categorical
// matrix has no Cartesian axes, so the constructor calls setAxesVisible(false):
// plotRect() is then the full inner rectangle and the base skips the frame
// while still painting the title and legend.
//
// Data gate. ChartBase selects paintPlot()/paintEmptyState() from hasData(),
// which reads ChartBase::series(); a matrix is not a series. The component
// therefore keeps one unnamed, single-point presence series in sync with the
// matrix (one finite point while any cell is finite, no series otherwise). It
// carries no visual meaning: paintPlot() draws values(), never the presence
// point, and the empty series selects the empty state only when the matrix has
// no data.

#pragma once

#include <md/components/chart_common.h>
#include <md/view/view.h>

#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

class QEvent;
class QMouseEvent;
class QPainter;

namespace md {

class HeatmapChart : public ChartBase
{
    Q_OBJECT

public:
    explicit HeatmapChart(QWidget* parent = nullptr);

    // The matrix dimensions. Both default to zero. Growing either dimension
    // preserves the cells that remain in range and fills the new cells with
    // NaN (empty); shrinking discards the cells that fall outside. Negative
    // values are clamped to zero.
    void setRows(int rowCount);
    [[nodiscard]] int rows() const noexcept;

    void setColumns(int columnCount);
    [[nodiscard]] int columns() const noexcept;

    // Writes one cell and repaints. An out-of-range coordinate is ignored, so
    // the matrix never grows implicitly; use setRows()/setColumns() first. The
    // value may be NaN, which marks the cell empty.
    void setValue(int row, int column, qreal value);

    // The cell value, or NaN when either coordinate is out of range.
    [[nodiscard]] qreal value(int row, int column) const;

    // Replaces the whole matrix. The new dimensions become the data's: rows()
    // is the number of rows and columns() is the length of the longest row.
    // Ragged input is padded with NaN, so every stored row has exactly
    // columns() entries. Passing an empty matrix clears the heatmap.
    void setValues(QVector<QVector<qreal>> matrix);
    [[nodiscard]] const QVector<QVector<qreal>>& values() const noexcept;

    // The labels drawn beside the rows and above the columns. Missing entries
    // are simply not drawn, and an empty list reserves no gutter. Setting a
    // list never changes the matrix dimensions.
    void setRowLabels(QStringList labels);
    [[nodiscard]] QStringList rowLabels() const;
    void setColumnLabels(QStringList labels);
    [[nodiscard]] QStringList columnLabels() const;

    // Whether every finite cell prints its numeric value. Default false; the
    // text is omitted when a cell is too small to hold it legibly.
    void setShowValues(bool show);
    [[nodiscard]] bool showsValues() const noexcept;

    // Whether the cell grid and its outer border are stroked. Default true.
    void setShowGrid(bool show);
    [[nodiscard]] bool showsGrid() const noexcept;

    // The cell at a ChartBase-local pixel, as (x = column, y = row), or
    // (-1, -1) when the position is outside the cell area (including the label
    // gutters and an empty chart). The method is const; it recomputes the cell
    // geometry from plotRect().
    [[nodiscard]] QPoint cellAt(QPointF position) const;

    // The cell under the pointer as (x = column, y = row), or (-1, -1).
    [[nodiscard]] QPoint hoveredCell() const noexcept;

    // The rectangle of one cell in ChartBase-local pixels, in the same
    // mirrored mapping cellAt() uses. An empty rectangle for an out-of-range
    // cell or before the chart has a plot rectangle.
    [[nodiscard]] QRectF cellRect(int row, int column) const;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    [[nodiscard]] bool hasHeatValues() const noexcept;

    // Mirrors "the matrix has at least one finite cell" onto the base's data
    // gate. See the class comment: the presence series has no visual meaning.
    void syncPresenceSeries();

    // The cell area inside `plot`: plotRect() in normal use. Empty when the
    // chart has no matrix or the plot is too small for the gutters.
    [[nodiscard]] QRectF cellAreaFor(const QRectF& plot) const;

    // The width reserved by the row labels, capped so the labels can never
    // consume the matrix. Zero when there are no row labels.
    [[nodiscard]] qreal rowLabelGutter(qreal availableWidth) const;

    void setHoveredCell(int row, int column);

    QVector<QVector<qreal>> values_;

    QStringList rowLabels_;
    QStringList columnLabels_;

    int rows_ = 0;
    int columns_ = 0;

    bool showValues_ = false;
    bool showGrid_ = true;

    int hoveredRow_ = -1;
    int hoveredColumn_ = -1;
};

} // namespace md
