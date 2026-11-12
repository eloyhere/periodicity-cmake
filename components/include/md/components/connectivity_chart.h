// Periodicity — the ConnectivityChart component.
//
// A square connectivity matrix rendered as a labelled adjacency grid. Rows and
// columns carry the same node names, and every finite cell is coloured on a
// diverging scale that is neutral at zero: negative values move towards primary,
// positive values towards error, and zero takes the neutral surface-container
// role. The scale is therefore readable as a signed relationship (inhibition
// versus excitation, deficit versus surplus) rather than as a one-sided ramp.
//
// Range. setRange(min, max) pins the colour scale; without an explicit range the
// effective range is derived from the finite cells and always contains zero. The
// colour bar on the right shows the effective range with a tick at zero, and the
// status line above the grid reports the matrix size and whether the matrix is
// symmetric within a tolerance. isSymmetric() exposes the same test.
//
// Geometry. plotRect() is divided into a status row, a column-label row, the
// square cell area and a colour-bar gutter. cellRect() and cellAt() expose the
// cell mapping, and both are stable before the first paint because they derive
// the geometry from plotRect() on demand. The cells stay square, centred in the
// available area, so a wide widget does not stretch the matrix.
//
// Hover. mouseMoveEvent() resolves the cell under the pointer and paints a
// cell-local outline plus a readout of the node names, the (row, column) index
// and the value with its unit. The base View's whole-surface state layer is
// switched off in the constructor: it would tint the matrix and corrupt the
// colour reading. leaveEvent() clears the readout.
//
// Reveal. The rows fill in over the theme's Level2 motion. A chart that has never
// been shown paints finished, so an offscreen render or QWidget::grab() is
// complete; reduced motion, and a zero-duration spec, snap synchronously.
//
// Data gate. ChartBase selects paintPlot()/paintEmptyState() from hasData(),
// which reads ChartBase::series(); a matrix is not a series. The component keeps
// one unnamed, single-point presence series in sync with the matrix, exactly as
// HeatmapChart does. The presence point is never painted.

#pragma once

#include <md/components/chart_common.h>
#include <md/view/view.h>

#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <utility>

class QEvent;
class QMouseEvent;
class QPainter;

namespace md {

class ConnectivityChart : public ChartBase
{
    Q_OBJECT

public:
    explicit ConnectivityChart(QWidget* parent = nullptr);

    // Replaces the matrix. The dimension is the larger of the name count and
    // the row count; names and rows are padded so the stored matrix is square.
    // Missing cells are NaN (not observed) and are skipped by the range, the
    // symmetry test and the colour scale. A name that is missing is generated
    // from the node index.
    void setMatrix(QStringList names, QVector<QVector<qreal>> values);

    // The edge length of the square matrix; zero for an empty matrix.
    [[nodiscard]] int size() const noexcept;

    [[nodiscard]] QStringList names() const;
    [[nodiscard]] const QVector<QVector<qreal>>& matrix() const noexcept;

    // One cell, or NaN when either index is outside the matrix.
    [[nodiscard]] qreal value(int row, int column) const;

    // True when the stored data is square. The component always normalizes to a
    // square matrix, so this is true exactly when size() > 0.
    [[nodiscard]] bool isSquare() const noexcept;

    // True when the matrix is square and every finite cell equals its mirror
    // within `tolerance`. A NaN cell matches only another non-finite cell, so an
    // unobserved edge can never make the matrix look asymmetric.
    [[nodiscard]] bool isSymmetric(qreal tolerance = 1e-9) const;

    // Pins the diverging colour scale. The bounds are ordered, and a degenerate
    // pair is widened into a small non-zero span. clearRange() removes the
    // pinning and returns to the automatic range.
    void setRange(qreal minimum, qreal maximum);
    void clearRange();
    [[nodiscard]] bool hasExplicitRange() const noexcept;

    // The effective colour-scale bounds: the explicit range when set, otherwise
    // the finite cell range extended to include zero.
    [[nodiscard]] qreal rangeMinimum() const;
    [[nodiscard]] qreal rangeMaximum() const;

    // The unit appended to the readout and printed above the colour bar. Empty
    // by default, in which case the bar carries the neutral caption "Value".
    void setUnit(const QString& unit);
    [[nodiscard]] QString unit() const;

    // The rectangle of one cell in ChartBase-local pixels, or an empty
    // rectangle when the index is out of range or the plot is too small.
    [[nodiscard]] QRectF cellRect(int row, int column) const;

    // The cell at a ChartBase-local pixel, as (x = column, y = row), or
    // (-1, -1) outside the cell area.
    [[nodiscard]] QPoint cellAt(QPointF position) const;

    // The cell under the pointer as (x = column, y = row), or (-1, -1).
    [[nodiscard]] QPoint hoveredCell() const noexcept;

    // The value under the pointer, or NaN when no cell is hovered.
    [[nodiscard]] qreal hoveredValue() const;

    // The row/column/value text the hover readout paints, empty when no cell is
    // hovered. Exposed so the readout can be asserted without OCR.
    [[nodiscard]] QString hoverLabel() const;

    // 0..1 row-fill progress. 1 before the first show and immediately after a
    // reduced-motion show.
    [[nodiscard]] qreal revealProgress() const noexcept;
    [[nodiscard]] bool isRevealing() const noexcept;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override;
    void paintPlot(QPainter& painter, const QRectF& plot, qreal progress) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // The resolved geometry of one paint/hit-test pass. `matrix` is the square
    // cell area, `bar` the colour-bar track; both are in ChartBase-local pixels.
    struct Geometry
    {
        QRectF matrix;
        QRectF bar;
        qreal cell = 0.0;
        qreal labelGap = 0.0;
        qreal labelHeight = 0.0;
        qreal barLabelWidth = 0.0;
    };

    [[nodiscard]] Geometry geometryFor(const QRectF& plot) const;
    [[nodiscard]] QRectF cellRectFor(const Geometry& geometry, int row, int column) const;
    [[nodiscard]] QColor colourFor(qreal value, qreal minimum, qreal maximum) const;
    [[nodiscard]] std::pair<qreal, qreal> automaticRange() const;

    [[nodiscard]] bool hasFiniteCells() const noexcept;
    void syncPresenceSeries();

    void setHoveredCell(int row, int column);

    QStringList names_;
    QVector<QVector<qreal>> values_;

    int size_ = 0;

    bool hasRange_ = false;
    qreal rangeMinimum_ = 0.0;
    qreal rangeMaximum_ = 1.0;

    QString unit_;

    int hoveredRow_ = -1;
    int hoveredColumn_ = -1;
    QPointF hoverPosition_;
};

} // namespace md
