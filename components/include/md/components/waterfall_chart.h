// Periodicity — the WaterfallChart component.
//
// A stack of spectra over time: one trace per frame, each drawn against a
// frequency axis and raised by a fixed depth offset so the newest trace sits on
// the shared level axis and older traces recede behind it. The traces are
// painted far-to-near (the painter's algorithm), with a translucent fill under
// each spectrum and a progressively fainter stroke, which is what gives the
// stack its depth without a 3D projection.
//
// Data. setTraces() replaces the stack with dB spectra; trace 0 is the oldest
// and the last trace is the newest. Ragged traces are accepted and each trace
// is mapped across its own bin count, so a caller may change resolution between
// frames. The frequency axis is the linear band 0..sampleRate/2, the level axis
// is the range from setDbRange(), and both labels and tick values come from the
// shared MathematicsCoordination frame (ChartBase paints it).
//
// Depth. setDepth() is the fraction of the plot height the whole stack may
// recede into (clamped into 0..0.9; default 0.32). The remaining height is the
// amplitude band; a trace at the bottom of the dB range sits on its baseline and
// one at the top reaches the top of its amplitude band, so a full-scale newest
// trace touches the top of the plot at the default depth. The baselines are
// slightly translucent, which keeps the depth readable when neighbouring
// traces overlap.
//
// Hit testing. traceAt() resolves a widget-local pixel to the nearest painted
// spectrum (and its frequency and level). tracePoint() returns the pixel of one
// (trace, bin) pair, so a caller can place a marker or drive a test with the
// exact same geometry the paint pass uses.
//
// Reveal. The chart's shared ChartAnimation raises the traces from their
// baselines in a short cascade on first appearance and on a later data change;
// reduced motion settles the stack immediately. A chart that was never shown
// paints finished.
//
// Data gate. A matrix of traces is not a ChartSeries, so the component keeps
// one unnamed presence point in sync with "at least one finite level" (the same
// technique HeatmapChart uses) to select paintPlot() over the base empty state.
//
// Exception safety. paintPlot(), traceAt() and the reveal hooks catch locally,
// so a broken theme cannot unwind into Qt's paint loop.

#pragma once

#include <md/components/chart_common.h>

#include <QPointF>
#include <QRectF>
#include <QVector>

#include <vector>

class QPainter;

namespace md {

class WaterfallChart : public ChartBase
{
    Q_OBJECT

public:
    explicit WaterfallChart(QWidget* parent = nullptr);

    // Replaces the stack with dB spectra, oldest first. A trace may be empty;
    // an entirely empty stack clears the chart. Non-finite values are gaps and
    // are not painted.
    void setTraces(std::vector<std::vector<double>> traces);
    [[nodiscard]] const std::vector<std::vector<double>>& traces() const noexcept;

    [[nodiscard]] int traceCount() const noexcept;

    // The number of bins in the longest trace. Zero for an empty stack.
    [[nodiscard]] int binCount() const noexcept;

    // The acquisition sample rate. The frequency axis runs from 0 to
    // sampleRate/2. A non-finite or non-positive value is rejected and leaves
    // the previous rate untouched. Default 48 kHz.
    void setSampleRate(double sampleRate);
    [[nodiscard]] double sampleRate() const noexcept;
    [[nodiscard]] double nyquistFrequency() const noexcept;

    // The level window in dB. Values outside it are clamped for the layout.
    // The range is normalised and a degenerate range is widened to 1 dB.
    // Default -100..0 dB.
    void setDbRange(double minimumDb, double maximumDb);
    [[nodiscard]] double minimumDb() const noexcept;
    [[nodiscard]] double maximumDb() const noexcept;

    // How far the whole stack recedes, as a fraction of the plot height. The
    // value is clamped into 0..0.9. Default 0.32.
    void setDepth(qreal fraction);
    [[nodiscard]] qreal depth() const noexcept;

    // The tick values the shared frame paints. frequencyTicks() is in Hz and
    // levelTicks() in dB; both follow the current visible bounds.
    [[nodiscard]] QVector<qreal> frequencyTicks() const;
    [[nodiscard]] QVector<qreal> levelTicks() const;

    // The pixel of one bin of one trace, after its baseline and depth offset.
    // An empty point for an out-of-range trace/bin or before the chart has a
    // plot rectangle.
    [[nodiscard]] QPointF tracePoint(int traceIndex, int bin) const;

    // The painted trace nearest to `position`, plus its frequency (Hz) and raw
    // level (dB, without the depth offset). Returns false outside the plot or
    // when no painted spectrum is within the hit slop; the out parameters are
    // left untouched then.
    [[nodiscard]] bool traceAt(QPointF position, int* traceIndex = nullptr,
                               double* frequency = nullptr, double* level = nullptr) const;

    // 0..1 cascade progress of the chart's shared ChartAnimation. 1 before the
    // first visible paint and 1 immediately after a reduced-motion paint.
    [[nodiscard]] qreal revealProgress() const noexcept;
    [[nodiscard]] bool isRevealing() const noexcept;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override;
    void paintPlot(QPainter& painter, const QRectF& plot, qreal progress) override;

private:
    [[nodiscard]] bool hasLevels() const noexcept;
    void syncPresenceSeries();
    void updateViewport();

    // The baseline of one trace and the height of the amplitude band, in
    // ChartBase-local pixels. Trace 0 is the oldest and sits highest.
    [[nodiscard]] qreal baselineFor(const QRectF& plot, int traceIndex) const;
    [[nodiscard]] qreal amplitudeHeightFor(const QRectF& plot) const;

    // The y pixel of `levelDb` on one trace, after the reveal scaling.
    [[nodiscard]] qreal traceY(const QRectF& plot, int traceIndex, double levelDb,
                               qreal reveal) const;

    // The level of one bin, or NaN when the bin does not exist.
    [[nodiscard]] qreal levelAt(int traceIndex, int bin) const;

    std::vector<std::vector<double>> traces_;
    double sampleRate_ = 48000.0;
    double dbMinimum_ = -100.0;
    double dbMaximum_ = 0.0;
    qreal depth_ = 0.32;
};

} // namespace md
