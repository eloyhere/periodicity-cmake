// Periodicity — the PolarPlotChart component.
//
// A directivity or radiation pattern: one value per angle, drawn on a polar
// grid. 0 degrees is at twelve o'clock and the angle grows clockwise, which is
// the convention antenna and loudspeaker measurements use. The radius carries
// the magnitude (linear) or the level relative to the strongest sample
// (decibels); the angle carries the direction.
//
// Data. setAngles() takes an angle vector in degrees and a matching value
// vector. The vectors are paired by index, truncated to the shorter length,
// stripped of non-finite samples and sorted by the normalized angle. A value
// is read as a magnitude, so its absolute value is used. setComplexResponse()
// is the convenience form for a complex response: sample i is placed at
// startDegrees + i × 360/N and its absolute value is the magnitude, which
// matches how a directivity measurement is usually stored.
//
// Radial scale. RadialScale::Linear normalizes the radius against the largest
// finite magnitude, so the strongest sample touches the outer ring. The ticks
// are nice 0..maximum values. RadialScale::Db maps 20·log10(value / maximum)
// from setRadialFloorDb() (default −40 dB) at the centre to 0 dB at the outer
// ring; the ticks are the dB values, so a pattern with a −20 dB side lobe is
// read directly. A non-positive maximum collapses every sample to the centre,
// and the linear axis falls back to a 0..1 scale so the frame stays drawable.
//
// Mirror. setMirror(true) draws a second copy reflected about the 0°–180°
// axis, which turns a measured half pattern into a full directivity plot. The
// mirrored copy is slightly dimmer so the measured half stays readable; it is
// presentation only and is not part of pointForIndex() or nearestIndex().
//
// Grid. Concentric circles mark the radial ticks, eight spokes mark the 45
// degree directions, and the angle labels are placed outside the outer ring.
// Radial tick labels run down the 0 degree spoke to the right of the vertical
// axis, where they cannot collide with the 0 degree angle label.
//
// Reveal. The first show sweeps the pattern clockwise from 0 degrees over the
// theme's level-2 motion: a growing wedge clips the fill, stroke and markers.
// A reduced-motion theme snaps synchronously, and a never-shown chart paints
// finished so an offscreen render is complete.
//
// Empty state. Without a drawable sample the base paints a localized
// "No directivity data" placeholder.
//
// Exception safety. paintPlot(), paintEmptyState() and the reveal hooks catch
// component exceptions locally.

#pragma once

#include <md/components/chart_common.h>

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <complex>
#include <vector>

class QPainter;

namespace md {

class PolarPlotChart : public ChartBase
{
    Q_OBJECT

public:
    // How the radius carries the value.
    enum class RadialScale {
        Linear, // magnitude / maximum
        Db,     // 20·log10(magnitude / maximum), floor at setRadialFloorDb()
    };
    Q_ENUM(RadialScale)

    explicit PolarPlotChart(QWidget* parent = nullptr);

    // Replaces the pattern. Angles are normalized into [0, 360) and the pairs
    // are sorted by angle; non-finite pairs are dropped. values are magnitudes,
    // so a negative value is used as its absolute value.
    void setAngles(std::vector<double> degrees, std::vector<double> values);

    // Builds the pattern from a complex response: sample i is at
    // startDegrees + i × 360/N and its magnitude is std::abs(response[i]).
    // An empty response clears the chart.
    void setComplexResponse(std::vector<std::complex<double>> response,
                            double startDegrees = 0.0);

    [[nodiscard]] const std::vector<double>& angles() const noexcept;
    [[nodiscard]] const std::vector<double>& values() const noexcept;

    [[nodiscard]] int pointCount() const noexcept;
    [[nodiscard]] double angleAt(int index) const noexcept;
    [[nodiscard]] double valueAt(int index) const noexcept;

    // The largest finite magnitude, or 1.0 when every sample is zero. This is
    // the value the outer ring represents.
    [[nodiscard]] double radialMaximum() const noexcept;

    // The fraction of the radius one sample maps to, in 0..1. This is the
    // value after the linear or dB normalization; a zero maximum maps every
    // sample to the centre.
    [[nodiscard]] double radialFraction(int index) const;

    void setRadialScale(RadialScale scale);
    [[nodiscard]] RadialScale radialScale() const noexcept;

    // Centre of the decibel radial axis, clamped into [−200, −1]. Default
    // −40 dB. Ignored by RadialScale::Linear.
    void setRadialFloorDb(double decibels);
    [[nodiscard]] double radialFloorDb() const noexcept;

    // Reflect the pattern about the 0°–180° axis. Off by default.
    void setMirror(bool mirror);
    [[nodiscard]] bool isMirrored() const noexcept;

    // The circle the pattern is drawn in, in ChartBase-local pixels: a square
    // centred in plotRect(), inset for the outside angle labels. Empty when the
    // widget is too small.
    [[nodiscard]] QRectF polarRect() const;
    [[nodiscard]] QPointF centre() const;
    [[nodiscard]] qreal radius() const;

    // The settled pixel of one sample. A null QPointF for an out-of-range
    // index.
    [[nodiscard]] QPointF pointForIndex(int index) const;

    // The sample nearest to a ChartBase-local pixel within the density-scaled
    // slop, or −1. The mirrored copy is not part of the result.
    [[nodiscard]] int nearestIndex(QPointF position) const;

    // Radius ticks in axis units: nice magnitudes for RadialScale::Linear,
    // −floor..0 dB for RadialScale::Db. The outer ring is the last value.
    [[nodiscard]] QVector<double> radialTicks() const;
    [[nodiscard]] QStringList radialTickLabels() const;

    // The eight 45-degree directions used for the spokes and labels.
    [[nodiscard]] QVector<double> angleTicks() const;
    [[nodiscard]] QStringList angleTickLabels() const;

    [[nodiscard]] QString radialAxisTitle() const;
    [[nodiscard]] QString angleAxisTitle() const;

    // 0..1 sweep progress. 1 before the first show and after a reduced-motion
    // show.
    [[nodiscard]] qreal revealProgress() const noexcept;
    [[nodiscard]] bool isRevealing() const noexcept;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot, qreal progress) override;
    void paintEmptyState(QPainter& painter, const QRectF& plot) override;

private:
    [[nodiscard]] bool hasPattern() const noexcept;
    [[nodiscard]] double radialAxisMinimum() const;
    [[nodiscard]] double radialAxisMaximum() const;
    [[nodiscard]] double radialFractionForValue(double value) const;

    void rebuildSeries();
    void paintPlotAt(QPainter& painter, const QRectF& plot, qreal progress);

    [[nodiscard]] QPointF polarPoint(double degrees, qreal radiusPixels) const;

    std::vector<double> angles_;
    std::vector<double> values_;

    RadialScale scale_ = RadialScale::Linear;
    double radialFloorDb_ = -40.0;
    bool mirror_ = false;
};

} // namespace md
