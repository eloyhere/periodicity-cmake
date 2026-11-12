// Periodicity — the BodeChart component.
//
// A Bode plot in one component: the magnitude response in decibels above and
// the phase response in degrees below, sharing a single frequency axis. The
// two plot areas are stacked with a small gap; each keeps its own y grid and
// tick labels, while the vertical frequency grid runs through both so a
// feature can be read across the pair by eye.
//
// Magnitude. setResponse() takes decibels directly, because a transfer function
// is usually already expressed in dB. The axis is fitted to the finite data
// with a 1/2/5 × 10^k-style dB step (20, 40 or 60 dB), so the grid stays stable
// while the trace moves.
//
// Phase. The phase vector is wrapped into −180..180 degrees for both painting
// and storage. A wrap is drawn as a break in the polyline, never as a vertical
// stroke, so a +180 → −180 transition reads as the same angle. The phase axis
// is fixed to −180..180 with 90 degree ticks.
//
// Frequency. Linear or logarithmic (the default, because a Bode plot is
// conventionally logarithmic). Logarithmic ticks are decades with 2× and 5×
// minor grid lines; a range narrower than one decade falls back to the linear
// nice-tick set, so the axis is never unlabelled.
//
// Frame. Both areas are painted by this component rather than by the shared
// Cartesian transform, for the same reason SpectrumChart paints its own frame:
// a logarithmic frequency axis cannot be expressed by a linear data-to-pixel
// mapping. The nice-tick and label helpers are still taken from
// MathematicsCoordination, so the numeric style stays consistent.
//
// Reveal. The first show sweeps both traces in from the left over the theme's
// level-2 motion. The frames stay complete during the sweep. A reduced-motion
// theme snaps synchronously, and a never-shown chart paints finished so an
// offscreen render is complete.
//
// Empty state. Without finite response data the base paints a localized
// "No frequency response" placeholder.
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

#include <vector>

class QPainter;

namespace md {

class BodeChart : public ChartBase
{
    Q_OBJECT

public:
    enum class FrequencyAxis {
        Linear,
        Logarithmic,
    };
    Q_ENUM(FrequencyAxis)

    explicit BodeChart(QWidget* parent = nullptr);

    // Replaces both traces. The three vectors are paired by index and truncated
    // to the shortest; non-finite samples and negative frequencies are dropped,
    // and the sequence is sorted by frequency. The phase values are wrapped
    // into −180..180. An empty result clears the chart.
    void setResponse(std::vector<double> frequencies, std::vector<double> magnitudeDb,
                     std::vector<double> phaseDegrees);

    [[nodiscard]] const std::vector<double>& frequencies() const noexcept;
    [[nodiscard]] const std::vector<double>& magnitudesDb() const noexcept;
    [[nodiscard]] const std::vector<double>& phasesDegrees() const noexcept;

    [[nodiscard]] int pointCount() const noexcept;
    [[nodiscard]] double frequencyAt(int index) const noexcept;
    [[nodiscard]] double magnitudeDbAt(int index) const noexcept;
    [[nodiscard]] double phaseDegAt(int index) const noexcept;

    // Linear frequency, or logarithmic (the default).
    void setFrequencyAxis(FrequencyAxis axis);
    [[nodiscard]] FrequencyAxis frequencyAxis() const noexcept;

    // The two plot areas in ChartBase-local pixels. Both share the same x
    // range; magnitudeRect() is above phaseRect().
    [[nodiscard]] QRectF magnitudeRect() const;
    [[nodiscard]] QRectF phaseRect() const;

    // The nearest response sample to a ChartBase-local pixel, or −1. The
    // nearest area is chosen by the y coordinate first, then the nearest sample
    // inside that area within the density-scaled slop.
    [[nodiscard]] int nearestIndex(QPointF position) const;

    // The settled pixel of one sample in one of the two areas. A null QPointF
    // for an out-of-range index or a sample the current frequency axis does not
    // show (a DC sample on a logarithmic axis).
    [[nodiscard]] QPointF magnitudePoint(int index) const;
    [[nodiscard]] QPointF phasePoint(int index) const;

    // Axis observability, shared with the paint pass and with tests.
    [[nodiscard]] QVector<double> frequencyTicks() const;
    [[nodiscard]] QStringList frequencyTickLabels() const;
    [[nodiscard]] QVector<double> magnitudeTicks() const;
    [[nodiscard]] QStringList magnitudeTickLabels() const;
    [[nodiscard]] QVector<double> phaseTicks() const;
    [[nodiscard]] QStringList phaseTickLabels() const;
    [[nodiscard]] QString xAxisTitle() const;
    [[nodiscard]] QString magnitudeAxisTitle() const;
    [[nodiscard]] QString phaseAxisTitle() const;

    // 0..1 sweep progress. 1 before the first show and after a reduced-motion
    // show.
    [[nodiscard]] qreal revealProgress() const noexcept;
    [[nodiscard]] bool isRevealing() const noexcept;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot, qreal progress) override;
    void paintEmptyState(QPainter& painter, const QRectF& plot) override;

private:
    struct Layout
    {
        QRectF magnitude;
        QRectF phase;
        qreal leftGutter = 0.0;
        qreal bottomGutter = 0.0;
        qreal areaGap = 0.0;
        bool valid = false;
    };

    struct Interval
    {
        double minimum = 0.0;
        double maximum = 1.0;
    };

    [[nodiscard]] bool hasResponse() const noexcept;
    [[nodiscard]] Interval frequencyRange() const;
    [[nodiscard]] Interval magnitudeRange() const;
    [[nodiscard]] double magnitudeStep() const;

    [[nodiscard]] Layout computeLayout(const QRectF& plot) const;
    [[nodiscard]] qreal mapFrequency(double hertz, const Layout& layout,
                                     const Interval& range) const;
    [[nodiscard]] qreal mapMagnitude(double decibels, const Layout& layout,
                                     const Interval& range) const;
    [[nodiscard]] qreal mapPhase(double degrees, const Layout& layout) const;
    [[nodiscard]] double unmapFrequency(qreal x, const Layout& layout,
                                        const Interval& range) const;

    void rebuildSeries();
    void drawFrame(QPainter& painter, const Layout& layout, const Theme& theme);
    void paintPlotAt(QPainter& painter, const QRectF& plot, qreal progress);

    std::vector<double> frequencies_;
    std::vector<double> magnitudesDb_;
    std::vector<double> phasesDeg_;

    FrequencyAxis frequencyAxis_ = FrequencyAxis::Logarithmic;
};

} // namespace md
