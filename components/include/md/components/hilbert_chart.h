// Periodicity — the HilbertChart component.
//
// Two views of one analytic signal. setSignal() takes a uniformly sampled real
// signal x[n]; the component computes its discrete Hilbert transform q[n]
// locally, then paints:
//
//   * a phase portrait — q(t) against x(t), traced as a Lissajous figure and
//     coloured by the instantaneous phase, with the current sample marked; and
//   * an envelope/phase panel — the instantaneous envelope |z(t)| as a filled
//     curve and the unwrapped instantaneous phase φ(t) on a secondary axis.
//
// The larger panel follows setMode(): Quadrature emphasises the phase portrait,
// Envelope emphasises |z|/φ. Both panels are always painted, so the mode only
// changes their proportions, not the information on screen.
//
// The local transform. With N samples and X[k] the discrete Fourier transform of
// x, the analytic signal is
//
//   Z[0] = X[0], Z[N/2] = X[N/2] (even N), Z[k] = 2·X[k] for 0 < k < N/2,
//   Z[k] = 0 for k > N/2,
//
// and z[n] = IDFT(Z)[n] = x[n] + j·q[n]. The imaginary part is the quadrature
// q, |z| is the envelope and the unwrapped arg z is the instantaneous phase.
// Both transforms are evaluated directly, in O(N²) time and O(N) memory, once
// per setSignal(). There is no FFT dependency and no numerical library; N in the
// low thousands is the intended range, and the cost is paid only when the
// signal changes.
//
// Data contract. x is the caller's plain array of samples at t = n·dt with
// dt = sampleInterval() (1.0 by default). Fewer than four samples clears the
// analytic data and selects the empty state. The signal and the derived arrays
// are exposed by const reference so a host can annotate or export exactly what
// the chart painted.
//
// Geometry. The two panels share plotRect() minus a phase-colour bar on the
// right. quadratureRect() and envelopeRect() are their data areas in
// ChartBase-local pixels; the phase portrait is square, so q against x uses the
// same scale on both axes. Axis captions and ticks are painted by the component
// (the base's Cartesian frame is switched off), and all colours come from the
// theme: seriesColour(0) for the trajectory and envelope, secondary for the
// phase curve, and primary/tertiary/secondary for the phase colour ramp that
// both the portrait and the colour bar use.
//
// Reveal. The first show traces the portrait and sweeps the envelope in over
// the theme's motion level 2. Reduced motion, or a zero-duration spec, snaps to
// the finished state; a chart that was never shown paints finished.
//
// Hit-testing. sampleAt() returns the nearest sample and its (t, x, q, |z|, φ)
// while the pointer is over either data panel, and an invalid result outside
// them. The readout is local to the component; the shared interaction layers are
// disabled because this is a read-only surface.
//
// Exception safety. measureOverride(), paintPlot(), the transform and the mouse
// handlers catch locally.

#pragma once

#include <md/components/chart_common.h>

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QVector>

#include <vector>

class QEvent;
class QMouseEvent;
class QPainter;

namespace md {

enum class HilbertMode {
    Quadrature,
    Envelope,
};

// One sample of the readout. `valid` is false outside the two data panels.
struct HilbertSample
{
    bool valid = false;
    int index = -1;
    double time = 0.0;
    double value = 0.0;
    double quadrature = 0.0;
    double envelope = 0.0;
    double phase = 0.0;
};

class HilbertChart : public ChartBase
{
    Q_OBJECT

public:
    explicit HilbertChart(QWidget* parent = nullptr);
    ~HilbertChart() override;

    HilbertChart(const HilbertChart&) = delete;
    HilbertChart& operator=(const HilbertChart&) = delete;

    // Replaces the signal and recomputes the analytic arrays. Fewer than four
    // samples empties the chart; the input is copied.
    void setSignal(std::vector<double> x);

    [[nodiscard]] const std::vector<double>& signal() const noexcept;
    [[nodiscard]] const std::vector<double>& quadrature() const noexcept;
    [[nodiscard]] const std::vector<double>& envelope() const noexcept;

    // The unwrapped instantaneous phase in radians, one entry per sample.
    [[nodiscard]] const std::vector<double>& phase() const noexcept;
    [[nodiscard]] bool hasSignal() const noexcept;

    // The sample interval used by the time axis and the hover readout. Default
    // 1.0; a non-finite or non-positive value falls back to 1.0.
    void setSampleInterval(double interval);
    [[nodiscard]] double sampleInterval() const noexcept;

    // Which panel receives the larger share of plotRect(). Default Quadrature.
    void setMode(HilbertMode mode);
    [[nodiscard]] HilbertMode mode() const noexcept;

    // Data rectangles in ChartBase-local pixels. Both are empty before the
    // chart has a usable plot rectangle.
    [[nodiscard]] QRectF quadratureRect() const;
    [[nodiscard]] QRectF envelopeRect() const;

    // The phase-colour bar at the right edge of plotRect() and its visibility.
    // The bar repeats colourForPhase() with the padded phase range as labels.
    [[nodiscard]] QRectF colourBarRect() const;
    void setColourBarVisible(bool visible);
    [[nodiscard]] bool colourBarVisible() const noexcept;

    // Maps a quadrature-space point into the phase portrait. `x` is horizontal
    // and `q` vertical.
    [[nodiscard]] QPointF toQuadraturePixel(double x, double quadrature) const;

    // Maps (time, envelope) into the envelope panel.
    [[nodiscard]] QPointF toEnvelopePixel(double time, double envelope) const;

    // The nearest sample to a ChartBase-local pixel inside either data panel.
    [[nodiscard]] HilbertSample sampleAt(QPointF position) const;

    // The colour the phase portrait gives an instantaneous phase, interpolated
    // between the theme's primary, tertiary and secondary roles over the
    // signal's phase range.
    [[nodiscard]] QColor colourForPhase(double phase) const;

    // Nice ticks: time over [0, (N-1)·dt], envelope over [0, max |z|], phase
    // over its padded range.
    [[nodiscard]] QVector<double> timeTicks(int count = 5) const;
    [[nodiscard]] QVector<double> amplitudeTicks(int count = 5) const;
    [[nodiscard]] QVector<double> phaseTicks(int count = 3) const;

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
        QRectF quadratureBox;
        QRectF quadrature;
        QRectF envelopeBox;
        QRectF envelope;
        QRectF colourBar;
    };

    [[nodiscard]] Layout layoutFor(const QRectF& plot) const;
    [[nodiscard]] bool hasAnalyticData() const noexcept;
    [[nodiscard]] double maximumEnvelope() const;
    [[nodiscard]] double maximumQuadratureAmplitude() const;
    [[nodiscard]] double phaseMinimum() const;
    [[nodiscard]] double phaseMaximum() const;

    // Recomputes quadrature_, envelope_, phase_ and the phase range from
    // signal_. See the class comment for the transform.
    void computeAnalyticSignal();

    void paintQuadrature(QPainter& painter, const QRectF& rect, qreal progress);
    void paintEnvelope(QPainter& painter, const QRectF& rect, qreal progress);
    void paintPanelCaption(QPainter& painter, const QRectF& box, const QString& text);
    void paintColourBar(QPainter& painter, const QRectF& bar, qreal progress);
    void paintHover(QPainter& painter, const Layout& layout);
    void paintNoData(QPainter& painter, const QRectF& plot) const;

    void syncPresenceSeries();

    std::vector<double> signal_;
    std::vector<double> quadrature_;
    std::vector<double> envelope_;
    std::vector<double> phase_;

    double sampleInterval_ = 1.0;
    double phaseMinimum_ = 0.0;
    double phaseMaximum_ = 0.0;

    HilbertMode mode_ = HilbertMode::Quadrature;

    bool colourBarVisible_ = true;

    QPointF hoverPosition_;
    bool hovering_ = false;

};

} // namespace md
