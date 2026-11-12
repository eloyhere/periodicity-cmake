// Periodicity — the EpicycleChart component.
//
// A closed curve is decomposed into a small set of rotating vectors and then
// drawn as the chain of circles those vectors trace: the classic Fourier
// epicycle construction. setTarget() supplies the samples in order around the
// closed curve; the chart takes their discrete Fourier transform and keeps the
// `harmonics` largest positive/negative frequency pairs. The first vector is the
// curve's centroid, so the chain starts at the centre of the plot and the final
// tip reproduces the target.
//
// Geometry. epicycleRadii() reports the radius of every kept term in data units,
// and epicycleRadiusSum() is their sum — the maximum distance the chain can
// reach from the centroid and therefore the quantity the projection uses to fit
// the drawing into plotRect(). targetPoint() maps a target sample to widget
// pixels and targetPointAt() is its inverse hit test for hover.
//
// Trace. The reveal runs over the theme's Level2 motion. Its progress turns the
// chain by speed() full revolutions and records the tip, so the trace grows as
// the arms rotate instead of appearing all at once. A chart that has never been
// shown paints the finished trace, and reduced motion (or a zero-duration spec)
// snaps synchronously.
//
// Chrome. A dashed concentric grid and a scale bar give the drawing a spatial
// reference; a faint dashed outline shows the target curve, and the hover
// readout names the sample index and its data coordinates with the optional
// unit. The chart has no Cartesian axes, so setAxesVisible(false) is set in the
// constructor and the base's empty state is used when no finite sample exists.
//
// Data gate. ChartBase selects paintPlot()/paintEmptyState() from hasData(); the
// target samples are stored directly as one unnamed ChartSeries, so the base's
// predicate needs no separate presence marker.

#pragma once

#include <md/components/chart_common.h>
#include <md/view/view.h>

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <vector>

class QEvent;
class QMouseEvent;
class QPainter;

namespace md {

class EpicycleChart : public ChartBase
{
    Q_OBJECT

public:
    explicit EpicycleChart(QWidget* parent = nullptr);

    // Replaces the target curve. The samples are interpreted in order around
    // the closed curve; non-finite points are dropped and the last sample is
    // connected back to the first by the Fourier reconstruction. The harmonic
    // decomposition is rebuilt immediately.
    void setTarget(std::vector<QPointF> points);
    [[nodiscard]] const std::vector<QPointF>& target() const noexcept;

    // The number of finite target samples currently stored.
    [[nodiscard]] int sampleCount() const noexcept;

    // The number of positive harmonics requested. The effective count is capped
    // at (sampleCount() - 1) / 2, and zero-radius terms are omitted. Default 8.
    void setHarmonics(int count);
    [[nodiscard]] int harmonics() const noexcept;

    // Whether the rotating circles and arms are painted. Default true.
    void setShowCircles(bool show);
    [[nodiscard]] bool showsCircles() const noexcept;

    // Full revolutions the chain makes over the whole reveal. The value is
    // clamped into 0.25..8; values below 1 still complete one loop so the trace
    // always closes. Default 1.
    void setSpeed(double speed);
    [[nodiscard]] double speed() const noexcept;

    // The unit appended to the hover readout and the scale bar. Empty by
    // default.
    void setUnit(const QString& unit);
    [[nodiscard]] QString unit() const;

    // The radius of every kept epicycle, in data units, largest first. The sum
    // is the chain's reach and is exposed separately for the geometry test.
    [[nodiscard]] QVector<qreal> epicycleRadii() const;
    [[nodiscard]] qreal epicycleRadiusSum() const;

    // A target sample in ChartBase-local pixels, or a null point when the index
    // is out of range or the chart has no room to paint.
    [[nodiscard]] QPointF targetPoint(int index) const;

    // The target sample nearest to a ChartBase-local pixel, or -1. `radius` is
    // the hit slop in pixels; a non-positive value selects the density-scaled
    // default.
    [[nodiscard]] int targetPointAt(QPointF position, qreal radius = -1.0) const;

    [[nodiscard]] int hoveredTargetPoint() const noexcept;

    // The currently painted trace in ChartBase-local pixels. At revealProgress()
    // 1 it is the complete trace for the configured speed.
    [[nodiscard]] QPolygonF tracePath() const;

    [[nodiscard]] qreal revealProgress() const noexcept;
    [[nodiscard]] bool isRevealing() const noexcept;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override;
    void paintPlot(QPainter& painter, const QRectF& plot, qreal progress) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // One complex Fourier term in polar form. `frequency` is a signed sample
    // frequency, so the pair +k/-k of a real drawing is handled by two entries.
    struct Term
    {
        int frequency = 0;
        qreal radius = 0.0;
        qreal phase = 0.0;
    };

    struct Projection
    {
        bool valid = false;
        QPointF centre;
        qreal scale = 0.0;
        qreal gridRadius = 0.0;
    };

    void rebuildHarmonics();
    void rebuildDisplaySeries();

    [[nodiscard]] Projection projectionFor(const QRectF& plot) const;
    [[nodiscard]] QPointF toPixel(const Projection& projection, const QPointF& data) const;

    // The reconstructed point at `sampleTime`, in data coordinates.
    [[nodiscard]] QPointF sampleAt(qreal sampleTime) const;
    [[nodiscard]] QPolygonF buildTrace(const Projection& projection, qreal progress) const;

    [[nodiscard]] qreal radiusBound() const;
    [[nodiscard]] QPointF centroid() const noexcept;

    void setHoveredTargetPoint(int index);

    std::vector<QPointF> target_;
    QVector<Term> terms_;
    QPointF centroid_;
    qreal extent_ = 0.0;

    int harmonics_ = 8;
    bool showCircles_ = true;
    double speed_ = 1.0;
    QString unit_;

    int hoveredPoint_ = -1;
    QPointF hoverPosition_;
};

} // namespace md
