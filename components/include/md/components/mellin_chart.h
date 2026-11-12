// Periodicity — the MellinChart component.
//
// The Mellin transform F(s) = ∫ f(t) t^(s-1) dt is sampled by the caller on the
// vertical line s = c + jt and painted here as a magnitude field over the
// (c, t) plane. The component is a plain data consumer: it never evaluates the
// integral and never includes the mathematics module.
//
// Data contract.
//
//   c      — the real parts Re(s), ascending and uniformly spaced;
//   t      — the imaginary parts Im(s), ascending and uniformly spaced;
//   values — |c| × |t| complex samples, row-major:
//            values[i * t.size() + j] is F(c[i] + j·t[j]).
//
// Only the first and last entries of each axis define the drawing range, so a
// non-uniform grid should be resampled by the caller. A missing sample is
// treated as not-a-number and is not painted; extra samples are ignored.
// Magnitudes are |F| in the caller's units; the phase readout is arg F in
// radians, in (-π, π]. The critical line is the vertical c = criticalLine.
//
// Axis. The real axis is linear. The imaginary axis is linear by default and
// logarithmic with setLogT(true); in log mode only t > 0 is drawn and the ticks
// are labelled as powers of ten. The log switch changes both the mapping and
// the tick generator, and the tick labels keep reporting the actual t.
//
// Field and colour. The finite |F| range maps onto a sequential ramp between the
// theme's surfaceContainerHighest and primary, with primaryContainer as the
// middle stop and a square-root transfer. The colour bar on the right repeats
// the ramp with numeric labels. The critical line is a dashed tertiary line
// with a "Re(s) = c" caption; it is clipped to the field.
//
// Reveal. The first show sweeps the field in from the left and draws the
// critical line on; the animation uses the theme's motion level 2. Reduced
// motion, or a zero-duration spec, snaps to the finished state. A chart that
// was never shown paints finished.
//
// Hit-testing. sampleAt() maps a ChartBase-local pixel inside fieldRect() to the
// nearest sample and reports (c, t, |F|, arg F); an outside position, an empty
// chart or a non-finite sample is invalid. Hover paints a crosshair and a
// compact readout; the shared interaction layers are disabled because this is a
// read-only surface.
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

// One point of the (c, t) readout. `valid` is false outside the field or when
// the nearest sample is not finite.
struct MellinSample
{
    bool valid = false;
    double c = 0.0;
    double t = 0.0;
    double magnitude = 0.0;
    double phase = 0.0;
};

class MellinChart : public ChartBase
{
    Q_OBJECT

public:
    explicit MellinChart(QWidget* parent = nullptr);
    ~MellinChart() override;

    MellinChart(const MellinChart&) = delete;
    MellinChart& operator=(const MellinChart&) = delete;

    // Replaces the spectrum over s = c + jt. `c` and `t` are ascending and
    // uniformly spaced; `values` is row-major over (c, t). A short values array
    // leaves the missing cells empty; an empty axis clears the chart.
    void setSpectrum(std::vector<double> c, std::vector<double> t,
                     std::vector<std::complex<double>> values);

    [[nodiscard]] const std::vector<double>& realAxis() const noexcept;
    [[nodiscard]] const std::vector<double>& imaginaryAxis() const noexcept;
    [[nodiscard]] const std::vector<std::complex<double>>& values() const noexcept;
    [[nodiscard]] bool hasSpectrum() const noexcept;

    // The critical line c = criticalLine. The default is not-a-number, which
    // draws no line; a non-finite value clears it.
    void setCriticalLine(double c);
    [[nodiscard]] double criticalLine() const noexcept;
    [[nodiscard]] bool hasCriticalLine() const noexcept;

    // Whether the imaginary axis is logarithmic. Default false. In log mode
    // samples with t <= 0 are skipped and the ticks are labelled as powers of
    // ten.
    void setLogT(bool logarithmic);
    [[nodiscard]] bool isLogT() const noexcept;

    // Whether the colour bar is reserved at the right edge. Default true.
    void setColourBarVisible(bool visible);
    [[nodiscard]] bool colourBarVisible() const noexcept;

    // Geometry in ChartBase-local pixels. fieldRect() is the data area inside
    // the tick labels and colourBarRect() is the gradient strip; both are empty
    // before the chart has a usable plot rectangle.
    [[nodiscard]] QRectF fieldRect() const;
    [[nodiscard]] QRectF colourBarRect() const;

    // Maps (c, t) into the field. In log mode a non-positive t yields an empty
    // point. The result is not clipped to fieldRect().
    [[nodiscard]] QPointF toPixel(double c, double t) const;

    // The nearest grid sample to a ChartBase-local pixel.
    [[nodiscard]] MellinSample sampleAt(QPointF position) const;

    // The colour the field gives a magnitude under the current range and theme.
    [[nodiscard]] QColor colourForMagnitude(double magnitude) const;

    [[nodiscard]] double minimumMagnitude() const noexcept;
    [[nodiscard]] double maximumMagnitude() const noexcept;

    // Nice ticks inside the stored axis range. In log mode tTicks() returns
    // powers of ten (and a linear fallback when the range spans less than one
    // decade).
    [[nodiscard]] QVector<double> cTicks(int count = 5) const;
    [[nodiscard]] QVector<double> tTicks(int count = 5) const;

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
        QRectF field;
        QRectF colourBar;
    };

    [[nodiscard]] Layout layoutFor(const QRectF& plot) const;
    [[nodiscard]] bool hasFiniteSample() const noexcept;
    [[nodiscard]] double sampleMagnitude(int cIndex, int tIndex) const;
    [[nodiscard]] int nearestIndex(const std::vector<double>& axis, double value) const;

    // The vertical fraction of `t` in the current axis mode, in 0..1. Returns
    // a negative value for a t that cannot be mapped (log mode, t <= 0).
    [[nodiscard]] double verticalFraction(double t) const;
    [[nodiscard]] double valueFromVerticalFraction(double fraction) const;
    [[nodiscard]] QString formatTTick(double t) const;

    void paintField(QPainter& painter, const QRectF& field, qreal progress);
    void paintGrid(QPainter& painter, const QRectF& field);
    void paintAxes(QPainter& painter, const QRectF& field);
    void paintCriticalLine(QPainter& painter, const QRectF& field, qreal progress);
    void paintColourBar(QPainter& painter, const QRectF& bar);
    void paintHover(QPainter& painter, const QRectF& field);
    void paintNoData(QPainter& painter, const QRectF& plot) const;

    void syncPresenceSeries();

    std::vector<double> cs_;
    std::vector<double> ts_;
    std::vector<std::complex<double>> values_;

    double criticalLine_ = std::numeric_limits<double>::quiet_NaN();
    double minimumMagnitude_ = 0.0;
    double maximumMagnitude_ = 0.0;

    bool logT_ = false;
    bool colourBarVisible_ = true;

    QPointF hoverPosition_;
    bool hovering_ = false;

};

} // namespace md
