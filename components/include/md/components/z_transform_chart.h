// Periodicity — the ZTransformChart component.
//
// The z-plane. H(z) is sampled by the caller on a rectangular grid in
// z = Re + j·Im and painted here as a magnitude field with the unit circle,
// poles and zeros. The component is a plain data consumer: it never evaluates
// the transform and never includes the mathematics module.
//
// Data contract.
//
//   re     — the grid's real parts, ascending and uniformly spaced;
//   im     — the grid's imaginary parts, ascending and uniformly spaced;
//   values — |re| × |im| complex samples, row-major:
//            values[i * im.size() + j] is H(re[i] + j·im[j]).
//
// Only the first and last entries of each axis define the drawing range, so a
// non-uniform grid should be resampled by the caller. A missing sample is
// treated as not-a-number and is not painted; extra samples are ignored. The
// hover readout reports |z|, arg z and |H|; arg z is in (-π, π].
//
// Geometry. The plane is drawn square and centred on the origin, with the same
// scale on both axes, so the unit circle is a circle rather than an ellipse.
// The domain always contains the unit circle and grows to contain every finite
// sample (plus a small margin). setLogMagnitude(true) maps the colour scale
// through log10(1 + |H|), which keeps a low-magnitude background visible next to
// a sharp resonance; the readout and minimum/maximumMagnitude() still report
// |H| itself.
//
// Field and chrome. The finite |H| range maps onto a sequential ramp between the
// theme's surfaceContainerHighest and primary, with primaryContainer as the
// middle stop and a square-root transfer. A colour bar on the right repeats the
// ramp with numeric labels. The unit circle is a secondary-coloured ring, poles
// are × marks in the error colour, zeros are ○ marks in the primary colour, and
// both marker sets are clipped to the plane and pop in at the end of the
// reveal. Integer Re/Im grid lines come from the same nice-tick routine as the
// labelled axes.
//
// Reveal. The first show sweeps the field in from the left, draws the unit
// circle on and pops the markers; the animation uses the theme's motion level 2.
// Reduced motion, or a zero-duration spec, snaps to the finished state. A chart
// that was never shown paints finished.
//
// Hit-testing. sampleAt() maps a ChartBase-local pixel inside zPlaneRect() to the
// nearest sample. polePixel() and zeroPixel() expose marker centres. The shared
// interaction layers are disabled; hover feedback is local to the chart.
//
// Exception safety. measureOverride(), paintPlot(), the mouse handlers and the
// lifecycle hooks catch locally.

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

// One point of the z-plane readout. `valid` is false outside the plane or when
// the nearest sample is not finite.
struct ZSample
{
    bool valid = false;
    double real = 0.0;
    double imaginary = 0.0;
    double magnitude = 0.0;
    double phase = 0.0;
};

class ZTransformChart : public ChartBase
{
    Q_OBJECT

public:
    explicit ZTransformChart(QWidget* parent = nullptr);
    ~ZTransformChart() override;

    ZTransformChart(const ZTransformChart&) = delete;
    ZTransformChart& operator=(const ZTransformChart&) = delete;

    // Replaces the spectrum. `re` and `im` are ascending and uniformly spaced;
    // `values` is row-major over (Re, Im). A short values array leaves the
    // missing cells empty; an empty axis clears the chart.
    void setSpectrum(std::vector<double> re, std::vector<double> im,
                     std::vector<std::complex<double>> values);

    [[nodiscard]] const std::vector<double>& realAxis() const noexcept;
    [[nodiscard]] const std::vector<double>& imaginaryAxis() const noexcept;
    [[nodiscard]] const std::vector<std::complex<double>>& values() const noexcept;
    [[nodiscard]] bool hasSpectrum() const noexcept;

    void setPoles(std::vector<std::complex<double>> poles);
    void setZeros(std::vector<std::complex<double>> zeros);
    [[nodiscard]] const std::vector<std::complex<double>>& poles() const noexcept;
    [[nodiscard]] const std::vector<std::complex<double>>& zeros() const noexcept;

    // Marker centres in ChartBase-local pixels, or an empty point when the
    // index is out of range or the plane is not laid out.
    [[nodiscard]] QPointF polePixel(int index) const;
    [[nodiscard]] QPointF zeroPixel(int index) const;

    // The radius of the unit circle in pixels. Kept as an accessor so a test or
    // an annotation does not have to duplicate the domain computation.
    [[nodiscard]] double unitCircleRadius() const;

    // Whether the colour scale uses log10(1 + |H|). Default false. The readout
    // and minimum/maximumMagnitude() always report |H|.
    void setLogMagnitude(bool logarithmic);
    [[nodiscard]] bool isLogMagnitude() const noexcept;

    // Whether the colour bar is reserved at the right edge. Default true.
    void setColourBarVisible(bool visible);
    [[nodiscard]] bool colourBarVisible() const noexcept;

    // Geometry in ChartBase-local pixels. zPlaneRect() is the square data area
    // and colourBarRect() is the gradient strip; both are empty before the
    // chart has a usable plot rectangle.
    [[nodiscard]] QRectF zPlaneRect() const;
    [[nodiscard]] QRectF colourBarRect() const;

    // Maps (Re, Im) into the plane. The result is not clipped to zPlaneRect().
    [[nodiscard]] QPointF toPixel(double real, double imaginary) const;

    // The nearest grid sample to a ChartBase-local pixel.
    [[nodiscard]] ZSample sampleAt(QPointF position) const;

    // The colour the field gives a magnitude under the current range, transfer
    // and theme.
    [[nodiscard]] QColor colourForMagnitude(double magnitude) const;

    [[nodiscard]] double minimumMagnitude() const noexcept;
    [[nodiscard]] double maximumMagnitude() const noexcept;

    // Nice ticks over the visible real and imaginary domain.
    [[nodiscard]] QVector<double> realTicks(int count = 5) const;
    [[nodiscard]] QVector<double> imaginaryTicks(int count = 5) const;

    [[nodiscard]] qreal revealProgress() const noexcept;
    [[nodiscard]] bool isRevealing() const noexcept;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintPlot(QPainter& painter, const QRectF& plot, qreal progress) override;
    void paintEmptyState(QPainter& painter, const QRectF& plot) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Layout
    {
        QRectF plane;
        QRectF colourBar;
    };

    [[nodiscard]] Layout layoutFor(const QRectF& plot) const;
    [[nodiscard]] bool hasFiniteSample() const noexcept;
    [[nodiscard]] double sampleMagnitude(int realIndex, int imaginaryIndex) const;
    [[nodiscard]] int nearestIndex(const std::vector<double>& axis, double value) const;

    // Half the visible domain width; the domain is always [-extent, extent] on
    // both axes so the unit circle stays round.
    [[nodiscard]] double domainExtent() const;
    [[nodiscard]] double plottedMagnitude(double magnitude) const;

    void paintField(QPainter& painter, const QRectF& plane, qreal progress);
    void paintGrid(QPainter& painter, const QRectF& plane);
    void paintUnitCircle(QPainter& painter, const QRectF& plane, qreal progress);
    void paintAxes(QPainter& painter, const QRectF& plane);
    void paintMarkers(QPainter& painter, const QRectF& plane, qreal progress);
    void paintColourBar(QPainter& painter, const QRectF& bar);
    void paintHover(QPainter& painter, const QRectF& plane);
    void paintNoData(QPainter& painter, const QRectF& plot) const;

    void syncPresenceSeries();

    std::vector<double> re_;
    std::vector<double> im_;
    std::vector<std::complex<double>> values_;
    std::vector<std::complex<double>> poles_;
    std::vector<std::complex<double>> zeros_;

    double minimumMagnitude_ = 0.0;
    double maximumMagnitude_ = 0.0;

    bool logMagnitude_ = false;
    bool colourBarVisible_ = true;

    QPointF hoverPosition_;
    bool hovering_ = false;

};

} // namespace md
