// Periodicity — the LaplaceChart component.
//
// The s-plane, painted from a caller-supplied spectrum. The chart is a plain
// data consumer: it never evaluates a transform and never includes the
// mathematics module. The caller samples F(s) on a rectangular grid and passes
// the three arrays to setSpectrum(); the chart only maps them to colour and
// pixels.
//
// Data contract.
//
//   sigmas  — the grid's real parts σ, ascending, uniformly spaced;
//   omegas  — the grid's imaginary parts ω, ascending, uniformly spaced;
//   values  — |sigmas| × |omegas| complex samples, row-major:
//             values[i * omegas.size() + j] is F(sigmas[i] + j·omegas[j]).
//
// Only the first and last entry of each axis define the drawing range, so a
// non-uniform grid should be resampled by the caller. A missing sample (the
// values array is shorter than the grid) is treated as not-a-number and is not
// painted; extra samples are ignored. Magnitudes are |F|; the phase readout is
// arg F in radians, in (-π, π]. Units are σ in 1/s, ω in rad/s.
//
// Field. The finite |F| range is mapped onto a sequential ramp assembled from
// the current theme's surfaceContainerHighest (zero) and primary (maximum).
// The range is normalised with a square-root transfer so small magnitudes stay
// visible next to a dominant pole. A colour bar at the right edge repeats the
// same ramp with numeric labels and the "|F(s)|" caption. The field is painted
// as one quad per grid cell, so a spectrum of a few thousand cells is cheap.
//
// Geometry. The main field, an optional σ-slice along the bottom and an
// optional ω-slice on the right share the area left of the colour bar. The
// slices show the magnitude profile through a fixed ω or σ (setSliceOmega(),
// setSliceSigma()); their default is the middle of each axis, and ω = 0 when
// zero lies in range. A dashed convergence abscissa marks the region of
// convergence. The unit-exponent grid draws a hairline at every integer σ and ω
// and emphasizes the two axes at zero. Poles are × marks and zeros are ○ marks,
// both taken from setPoles()/setZeros(); they are clipped to the field and pop
// in at the end of the reveal.
//
// Reveal. The first time the chart is shown the field sweeps in from the left,
// the slice curves draw on, and the poles and zeros pop. The animation runs for
// the theme's motion level 2 (500 ms in the baseline scheme); a reduced-motion
// theme, or a spec whose duration resolves to zero, snaps to the finished
// state. A chart that has never been shown paints finished, so an offscreen
// render is complete rather than blank.
//
// Hit-testing. sampleAt() maps a ChartBase-local pixel through fieldRect() to
// the nearest grid sample and returns (σ, ω, |F|, arg F); a position outside
// the field, a chart without data or a non-finite sample yields an invalid
// result. polePixel()/zeroPixel() expose the marker centres for tests and for
// host-side annotations.
//
// Interaction. The chart is a read-only surface, so the shared state/ripple
// layers are disabled and hover feedback is local: the sample under the pointer
// gets a crosshair and a compact readout with σ, ω, |F| and arg F. No signal is
// emitted and nothing outside the component changes.
//
// Exception safety. measureOverride(), paintPlot(), the mouse handlers and the
// lifecycle hooks catch locally, so a bad theme cannot unwind into Qt.

#pragma once

#include <md/components/chart_common.h>

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QVector>

#include <complex>
#include <limits>
#include <vector>

class QEvent;
class QMouseEvent;
class QPainter;

namespace md {

// One point of the s-plane readout. `valid` is false when the position is
// outside the field or the nearest sample is not finite.
struct LaplaceSample
{
    bool valid = false;
    double sigma = 0.0;
    double omega = 0.0;
    double magnitude = 0.0;
    double phase = 0.0;
};

class LaplaceChart : public ChartBase
{
    Q_OBJECT

public:
    explicit LaplaceChart(QWidget* parent = nullptr);
    ~LaplaceChart() override;

    LaplaceChart(const LaplaceChart&) = delete;
    LaplaceChart& operator=(const LaplaceChart&) = delete;

    // Replaces the spectrum. `sigmas` and `omegas` are ascending and uniformly
    // spaced; `values` is row-major over (σ, ω). Sizes may disagree: a short
    // values array leaves the missing cells empty. Passing an empty axis clears
    // the chart and selects the empty state.
    void setSpectrum(std::vector<double> sigmas, std::vector<double> omegas,
                     std::vector<std::complex<double>> values);

    [[nodiscard]] const std::vector<double>& sigmas() const noexcept;
    [[nodiscard]] const std::vector<double>& omegas() const noexcept;
    [[nodiscard]] const std::vector<std::complex<double>>& values() const noexcept;

    // True when both axes are non-empty and at least one sample is finite.
    [[nodiscard]] bool hasSpectrum() const noexcept;

    // The region-of-convergence abscissa, drawn as a dashed vertical line in
    // the field. The default is not-a-number, which draws nothing. A non-finite
    // value clears the marker.
    void setConvergenceAbscissa(double sigma);
    [[nodiscard]] double convergenceAbscissa() const noexcept;

    // Complex-plane markers. Poles are drawn as × and zeros as ○; entries
    // outside the field are clipped away. Setting an empty list clears the
    // markers.
    void setPoles(std::vector<std::complex<double>> poles);
    void setZeros(std::vector<std::complex<double>> zeros);
    [[nodiscard]] const std::vector<std::complex<double>>& poles() const noexcept;
    [[nodiscard]] const std::vector<std::complex<double>>& zeros() const noexcept;

    // The marker centre in ChartBase-local pixels, or an empty point when the
    // index is out of range or the field is not laid out. The centre is exactly
    // toPixel(real, imag), independent of the reveal animation.
    [[nodiscard]] QPointF polePixel(int index) const;
    [[nodiscard]] QPointF zeroPixel(int index) const;

    // Whether the σ/ω slice plots are reserved and painted. Default true.
    void setSlicesVisible(bool visible);
    [[nodiscard]] bool slicesVisible() const noexcept;

    // The fixed coordinate each slice cuts through. The defaults are the
    // middle of each axis (ω = 0 when zero is in range); setSliceSigma() and
    // setSliceOmega() override them.
    void setSliceSigma(double sigma);
    [[nodiscard]] double sliceSigma() const noexcept;
    void setSliceOmega(double omega);
    [[nodiscard]] double sliceOmega() const noexcept;

    // The unit-exponent grid: a hairline at every integer σ and ω with the axes
    // at zero emphasized. Default true.
    void setUnitGridVisible(bool visible);
    [[nodiscard]] bool showsUnitGrid() const noexcept;

    // Whether the colour bar is reserved at the right edge. Default true.
    void setColourBarVisible(bool visible);
    [[nodiscard]] bool colourBarVisible() const noexcept;

    // Geometry in ChartBase-local pixels. fieldRect() and the two slice
    // rectangles are the data areas (inside their tick labels); colourBarRect()
    // is the gradient strip. All are empty before the chart has a plot
    // rectangle or when the available area is too small.
    [[nodiscard]] QRectF fieldRect() const;
    [[nodiscard]] QRectF sigmaSliceRect() const;
    [[nodiscard]] QRectF omegaSliceRect() const;
    [[nodiscard]] QRectF colourBarRect() const;

    // Maps (σ, ω) into the field. The result is not clipped to fieldRect();
    // callers that need the visible test can intersect with fieldRect().
    [[nodiscard]] QPointF toPixel(double sigma, double omega) const;

    // The nearest grid sample to a ChartBase-local pixel, with |F| and arg F.
    [[nodiscard]] LaplaceSample sampleAt(QPointF position) const;

    // The colour the field gives a magnitude under the current range and
    // theme. The same function paints the field and the colour bar.
    [[nodiscard]] QColor colourForMagnitude(double magnitude) const;

    // The finite magnitude range the colour scale was built from. Both are zero
    // when there is no finite sample.
    [[nodiscard]] double minimumMagnitude() const noexcept;
    [[nodiscard]] double maximumMagnitude() const noexcept;

    // Nice linear ticks inside the stored axis range, at most `count` values.
    // Empty when the axis is empty or degenerate.
    [[nodiscard]] QVector<double> sigmaTicks(int count = 5) const;
    [[nodiscard]] QVector<double> omegaTicks(int count = 5) const;

    // Reveal state. revealProgress() is 1 unless the first show started a
    // running reveal; isRevealing() is true only while that run is in flight.
    [[nodiscard]] qreal revealProgress() const noexcept;
    [[nodiscard]] bool isRevealing() const noexcept;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    // Per-element reveal: the field sweeps, the slices draw on, the markers pop
    // and the convergence line extends. ChartBase supplies the eased progress
    // and owns the animation run, so this override is the whole choreography.
    void paintPlot(QPainter& painter, const QRectF& plot, qreal progress) override;
    void paintEmptyState(QPainter& painter, const QRectF& plot) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Layout
    {
        QRectF field;
        QRectF sigmaSlice;
        QRectF omegaSlice;
        QRectF colourBar;
    };

    [[nodiscard]] Layout layoutFor(const QRectF& plot) const;

    [[nodiscard]] bool hasFiniteSample() const noexcept;
    [[nodiscard]] double sampleMagnitude(int sigmaIndex, int omegaIndex) const;
    [[nodiscard]] int nearestIndex(const std::vector<double>& axis, double value) const;
    [[nodiscard]] double effectiveSliceSigma() const;
    [[nodiscard]] double effectiveSliceOmega() const;

    // Paints the field cells, the unit grid, the axes and their labels, the
    // convergence line, the slice curves and the markers, in that order.
    void paintField(QPainter& painter, const Layout& layout, qreal progress);
    void paintUnitGrid(QPainter& painter, const QRectF& field);
    void paintAxes(QPainter& painter, const QRectF& field);
    void paintConvergence(QPainter& painter, const QRectF& field, qreal progress);
    void paintSlices(QPainter& painter, const Layout& layout, qreal progress);
    void paintMarkers(QPainter& painter, const QRectF& field, qreal progress);
    void paintColourBar(QPainter& painter, const QRectF& bar);
    void paintHover(QPainter& painter, const QRectF& field);
    void paintNoData(QPainter& painter, const QRectF& plot) const;

    void syncPresenceSeries();

    std::vector<double> sigmas_;
    std::vector<double> omegas_;
    std::vector<std::complex<double>> values_;
    std::vector<std::complex<double>> poles_;
    std::vector<std::complex<double>> zeros_;

    double convergenceAbscissa_ = std::numeric_limits<double>::quiet_NaN();
    double sliceSigma_ = std::numeric_limits<double>::quiet_NaN();
    double sliceOmega_ = std::numeric_limits<double>::quiet_NaN();

    double minimumMagnitude_ = 0.0;
    double maximumMagnitude_ = 0.0;

    bool slicesVisible_ = true;
    bool unitGridVisible_ = true;
    bool colourBarVisible_ = true;

    QPointF hoverPosition_;
    bool hovering_ = false;
};

} // namespace md
