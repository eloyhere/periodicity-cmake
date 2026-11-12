// Periodicity — the CoherenceChart component.
//
// A magnitude-squared coherence plot. Coherence is a dimensionless quantity in
// [0, 1]: 1 means the two signals are perfectly linearly related at that
// frequency, 0 means they are not. This component presents that contract
// directly — the y axis is fixed to 0..1, values outside the interval are
// clamped, and an optional significance threshold shades the significant
// region instead of leaving the reader to compare against an invisible line.
//
// Data. setCoherence() takes the frequency vector and the coherence vector
// separately. They are paired by index, truncated to the shorter length,
// sorted by frequency and stripped of non-finite samples, so the paint pass and
// binAt() can rely on a monotone frequency sequence. Frequencies below zero are
// dropped; a coherence value outside [0, 1] is clamped. A frequency axis can be
// linear (the default) or logarithmic, with the same decade ticks and fallback
// rules as SpectrumChart.
//
// Significance band. With the default threshold of 0.5, the region from the
// threshold to 1.0 is shaded with a translucent primary wash and the threshold
// itself is a dashed line labelled with its value at the right edge of the plot.
// The band is chrome, not data: it is always painted in full, even while the
// reveal sweep is still running.
//
// Frame. The component owns its frame for the same reason SpectrumChart does:
// a logarithmic frequency axis is not a linear data-to-pixel mapping. Grid,
// axes, ticks, labels and axis titles are painted in paintPlot(); ChartBase
// remains the parent surface and paints the title, legend and empty state.
// setAxesVisible(false) gives the component the full inner rectangle.
//
// Fill. The area under the coherence trace is filled with the series colour at
// low alpha, so a high-coherence band reads as a solid region while a noisy
// floor stays light. The fill and the trace grow in from the left during the
// first reveal; the frame and band do not move.
//
// Legend. The base legend lists the coherence colour and the significance
// threshold, so a black-and-white print still distinguishes the data from the
// shaded region.
//
// Empty state. Without a finite pair the base paints a themed, localized
// "No coherence data" label instead of the frame.
//
// Exception safety. paintPlot(), paintEmptyState(), the hit-test helper and the
// reveal hooks catch component exceptions locally.

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

class CoherenceChart : public ChartBase
{
    Q_OBJECT

public:
    enum class FrequencyAxis {
        Linear,
        Logarithmic,
    };
    Q_ENUM(FrequencyAxis)

    explicit CoherenceChart(QWidget* parent = nullptr);

    // Replaces the coherence data. The vectors are paired by index, truncated
    // to the shorter length, sorted by frequency and cleaned: non-finite pairs
    // and negative frequencies are dropped, and values outside [0, 1] are
    // clamped. An empty result clears the chart.
    void setCoherence(std::vector<double> frequencies, std::vector<double> coherence);

    [[nodiscard]] const std::vector<double>& frequencies() const noexcept;
    [[nodiscard]] const std::vector<double>& coherence() const noexcept;

    // The number of plotted samples, or zero when the chart is empty.
    [[nodiscard]] int sampleCount() const noexcept;

    // Frequency and coherence of one sample. Out-of-range indices yield 0.
    [[nodiscard]] double frequencyAt(int index) const noexcept;
    [[nodiscard]] double coherenceAt(int index) const noexcept;

    // The significance threshold drawn as a dashed line, clamped into 0..1.
    // Default 0.5. The region at or above it is shaded.
    void setThreshold(double threshold);
    [[nodiscard]] double threshold() const noexcept;

    // Linear (the default) or logarithmic frequency mapping.
    void setFrequencyAxis(FrequencyAxis axis);
    [[nodiscard]] FrequencyAxis frequencyAxis() const noexcept;

    // The data rectangle in ChartBase-local pixels, after the axis gutters.
    [[nodiscard]] QRectF dataRect() const;

    // The nearest sample to a ChartBase-local pixel, or −1 outside the data
    // rectangle. The search is by frequency, which is correct on a logarithmic
    // axis where samples are not evenly spaced in pixels.
    [[nodiscard]] int binAt(QPointF position) const;

    // The settled pixel of one sample. A null QPointF for an out-of-range
    // index or a sample the current axis does not show (a DC sample on a
    // logarithmic axis).
    [[nodiscard]] QPointF pointForIndex(int index) const;

    // Axis observability, shared with the paint pass and with tests.
    [[nodiscard]] QVector<double> frequencyTicks() const;
    [[nodiscard]] QStringList frequencyTickLabels() const;
    [[nodiscard]] QVector<double> coherenceTicks() const;
    [[nodiscard]] QStringList coherenceTickLabels() const;
    [[nodiscard]] QString xAxisTitle() const;
    [[nodiscard]] QString yAxisTitle() const;

    // The text of the threshold badge, e.g. "0.50".
    [[nodiscard]] QString thresholdLabel() const;

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
        QRectF data;
        qreal leftGutter = 0.0;
        qreal bottomGutter = 0.0;
        bool valid = false;
    };

    struct Interval
    {
        double minimum = 0.0;
        double maximum = 1.0;
    };

    [[nodiscard]] bool hasSamples() const noexcept;
    [[nodiscard]] Interval frequencyRange() const;
    [[nodiscard]] Layout computeLayout(const QRectF& plot) const;
    [[nodiscard]] qreal mapFrequency(double hertz, const Layout& layout,
                                     const Interval& range) const;
    [[nodiscard]] qreal mapCoherence(double value, const Layout& layout) const;
    [[nodiscard]] double unmapFrequency(qreal x, const Layout& layout,
                                        const Interval& range) const;
    [[nodiscard]] int nearestSample(double hertz) const;

    void rebuildSeries();
    void drawFrame(QPainter& painter, const Layout& layout, const Theme& theme);
    void paintPlotAt(QPainter& painter, const QRectF& plot, qreal progress);

    std::vector<double> frequencies_;
    std::vector<double> coherence_;

    double threshold_ = 0.5;
    FrequencyAxis frequencyAxis_ = FrequencyAxis::Linear;
};

} // namespace md
