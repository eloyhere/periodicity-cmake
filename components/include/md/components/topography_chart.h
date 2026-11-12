// Periodicity — the TopographyChart component.
//
// A scalar field rendered as a topographic map. The field is sampled at the
// centres of a rows × columns lattice; marching squares turns each lattice cell
// into iso-contour segments, those segments are stitched into polylines, and the
// polylines are drawn with a reveal-prefix so the contours appear to draw on.
// Between the contours an optional filled-band raster maps the interpolated
// field onto the same sequential ramp as the colour bar.
//
// Geometry. fieldRect() is the map area, cellAt()/fieldAt() map a widget pixel to
// the nearest lattice sample and to the bilinearly interpolated value, and
// contourLines()/contourSegmentCount() expose the raw marching-squares segments
// for a level. contourLevels() returns the `levels()` interior levels, evenly
// spaced over the finite range.
//
// Caching. Contour polylines and the filled-band raster are cached by a data
// generation and the field rectangle, so a reveal animation re-renders the same
// geometry instead of recomputing marching squares every frame. The band raster
// also depends on the theme and is invalidated by onThemeChanged().
//
// Chrome. The field carries a sample grid, a border, a vertical colour bar with
// ticks at every contour level, and a hover readout of the (row, column) sample
// and its interpolated value with the optional unit. The chart has no Cartesian
// axes, so setAxesVisible(false) is set in the constructor and the base paints
// the empty state when no finite sample exists.
//
// Reveal. Bands fade in and contours draw on over the theme's Level2 motion. A
// chart that has never been shown paints finished, and reduced motion (or a
// zero-duration spec) snaps synchronously.

#pragma once

#include <md/components/chart_common.h>
#include <md/view/view.h>

#include <QImage>
#include <QLineF>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>

class QEvent;
class QMouseEvent;
class QPainter;

namespace md {

class TopographyChart : public ChartBase
{
    Q_OBJECT

public:
    explicit TopographyChart(QWidget* parent = nullptr);

    // Replaces the field with a row-major rows × columns matrix. A short value
    // list is padded with NaN, a long one is truncated, and negative dimensions
    // are clamped to zero. A NaN sample is unobserved: it is skipped by the
    // range, the contours and the bands, and fieldAt() reports NaN for a cell
    // that touches it.
    void setField(int rows, int columns, QVector<qreal> values);

    [[nodiscard]] int rows() const noexcept;
    [[nodiscard]] int columns() const noexcept;
    [[nodiscard]] const QVector<qreal>& values() const noexcept;

    // One sample, or NaN outside the field.
    [[nodiscard]] qreal value(int row, int column) const;

    // The number of interior contour levels. Clamped into 1..24 and default 7.
    // Changing it does not change the field.
    void setLevels(int count);
    [[nodiscard]] int levels() const noexcept;

    // Whether filled bands between the contours are painted. Default true.
    void setFilledBands(bool filled);
    [[nodiscard]] bool hasFilledBands() const noexcept;

    // The unit appended to the colour-bar caption and the hover readout.
    void setUnit(const QString& unit);
    [[nodiscard]] QString unit() const;

    // The interior levels the paint pass uses, evenly spaced over the finite
    // minimum and maximum. Empty for an empty field or a constant field.
    [[nodiscard]] QVector<qreal> contourLevels() const;

    // The raw marching-squares segments for one level, in ChartBase-local
    // pixels. Empty when the level is not finite, the field has fewer than two
    // rows or columns, or no finite cell crosses the level.
    [[nodiscard]] QVector<QLineF> contourLines(qreal level) const;
    [[nodiscard]] int contourSegmentCount(qreal level) const;

    // The number of raw segments over every contour level.
    [[nodiscard]] int totalContourSegments() const;

    // The map rectangle in ChartBase-local pixels, left of the colour bar.
    [[nodiscard]] QRectF fieldRect() const;

    // The nearest lattice sample to a ChartBase-local pixel, as
    // (x = column, y = row), or (-1, -1) outside the field.
    [[nodiscard]] QPoint cellAt(QPointF position) const;

    // The bilinearly interpolated field value at a ChartBase-local pixel, or
    // NaN outside the field or when the enclosing cell touches an unobserved
    // sample.
    [[nodiscard]] qreal fieldAt(QPointF position) const;

    [[nodiscard]] QPoint hoveredCell() const noexcept;
    [[nodiscard]] qreal hoveredValue() const;

    [[nodiscard]] qreal revealProgress() const noexcept;
    [[nodiscard]] bool isRevealing() const noexcept;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override;
    void paintPlot(QPainter& painter, const QRectF& plot, qreal progress) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

    // The band raster baked the theme colours; the base's theme bookkeeping
    // must run first, then the chart drops its own raster.
    void onThemeChanged() override;

private:
    struct Segment
    {
        QPointF first;
        QPointF second;
    };

    struct Layout
    {
        bool valid = false;
        QRectF field;
        QRectF bar;
        qreal barLabelWidth = 0.0;
        qreal labelGap = 0.0;
        qreal labelHeight = 0.0;
    };

    [[nodiscard]] Layout layoutFor(const QRectF& plot) const;
    [[nodiscard]] QPointF samplePoint(const QRectF& field, int row, int column) const;
    [[nodiscard]] qreal sampleNormalised(qreal u, qreal v) const;

    [[nodiscard]] bool finiteRange(qreal& minimum, qreal& maximum) const;
    [[nodiscard]] QColor rampColour(qreal fraction) const;

    [[nodiscard]] QVector<Segment> buildSegments(qreal level, const QRectF& field) const;
    [[nodiscard]] QVector<QPolygonF> buildPolylines(qreal level, const QRectF& field) const;

    void ensureContours(const QRectF& field);
    void ensureBands(const QRectF& field, qreal minimum, qreal maximum);

    [[nodiscard]] bool hasFieldValues() const noexcept;
    void syncPresenceSeries();

    void setHoveredFieldCell(int row, int column, qreal value, QPointF position);
    void clearHoveredCell();

    QVector<qreal> values_;
    int rows_ = 0;
    int columns_ = 0;

    int levelCount_ = 7;
    bool filled_ = true;
    QString unit_;

    int hoveredRow_ = -1;
    int hoveredColumn_ = -1;
    qreal hoveredValue_ = 0.0;
    QPointF hoverPosition_;

    // Cache keyed by the field/level generation and the field rectangle.
    quint64 geometryGeneration_ = 1;
    QVector<QVector<QPolygonF>> contourCache_;
    QRectF contourCacheArea_;
    quint64 contourCacheGeneration_ = 0;

    QImage bandCache_;
    QRectF bandCacheArea_;
    quint64 bandCacheGeneration_ = 0;
};

} // namespace md
