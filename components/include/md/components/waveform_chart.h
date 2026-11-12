// Periodicity — the WaveformChart component.
//
// A time-domain oscillogram with an optional min/max envelope band and an RMS
// curve. The raw samples are stored as amplitude values; the chart chooses the
// time unit from the signal length (milliseconds below one second, seconds
// above) and installs a symmetric amplitude range around zero, so the zero line
// is always the vertical centre of the plot and a clipped or offset waveform is
// immediately visible.
//
// Rendering. A short signal (at most roughly two samples per pixel column) is
// stroked as a polyline, so its shape stays exact. A long signal is reduced to
// one vertical min/max segment per pixel column, which is what makes a dense
// waveform look solid without painting millions of line segments. The optional
// envelope band is filled between two supplied curves, and the RMS line is
// computed per block of setRmsWindow() samples. All three marks share the same
// reveal clip, so the first show draws the waveform in from left to right.
//
// Axes. The shared ChartBase frame paints the grid and the axes. The x axis is
// labelled "Time (ms)" or "Time (s)" and its values are in that unit; the y axis
// is "Amplitude". timeTicks() and amplitudeTicks() expose exactly the values the
// frame paints.
//
// Hit testing. sampleAt() maps a widget-local pixel to the nearest sample time
// and its amplitude; samplePoint() returns the pixel of one sample index. The
// base ChartBase::hitTest() also works, because the component installs a
// decimated copy of the signal as its (single) data series.
//
// Data gate. The decimated signal series is what selects paintPlot() over the
// empty state, so an empty signal paints the shared "No data" state with the
// axes still visible.
//
// Reveal. The chart's shared ChartAnimation reveals the marks left to right on
// first appearance and on a later data change; reduced motion settles the whole
// chart immediately, and a chart that was never shown paints finished.
//
// Exception safety. paintPlot() catches locally, so a broken theme cannot
// unwind into Qt's paint loop.

#pragma once

#include <md/components/chart_common.h>

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <vector>

class QPainter;

namespace md {

class WaveformChart : public ChartBase
{
    Q_OBJECT

public:
    explicit WaveformChart(QWidget* parent = nullptr);

    // Replaces the signal. A non-finite or non-positive sample rate is rejected
    // and leaves the previous rate in place; the default is 44.1 kHz. Values
    // are stored verbatim; non-finite samples become gaps in the polyline.
    void setSamples(std::vector<double> samples, double sampleRate = 44100.0);

    [[nodiscard]] const std::vector<double>& samples() const noexcept;
    [[nodiscard]] double sampleRate() const noexcept;

    // Signal length in seconds; zero for an empty signal.
    [[nodiscard]] double duration() const noexcept;

    // The largest absolute sample or envelope value, used for the symmetric
    // amplitude range.
    [[nodiscard]] double peakAmplitude() const noexcept;

    // The optional min/max envelope band. Both lists must be non-empty to
    // enable the band; the shorter list bounds the painted band, and each entry
    // is spread across the full signal duration, so the envelope may come from a
    // different analysis block size than the samples. Passing an empty list
    // clears the band.
    void setEnvelope(std::vector<double> minima, std::vector<double> maxima);
    [[nodiscard]] bool hasEnvelope() const noexcept;
    [[nodiscard]] const std::vector<double>& envelopeMinima() const noexcept;
    [[nodiscard]] const std::vector<double>& envelopeMaxima() const noexcept;

    // The per-block RMS curve. On by default. Turning it off removes the curve
    // and its legend entry; the cached values are kept.
    void setShowRms(bool show);
    [[nodiscard]] bool showsRms() const noexcept;

    // Samples per RMS block. Values below one are clamped to one. Recomputes
    // the cached curve. Default 256.
    void setRmsWindow(int samples);
    [[nodiscard]] int rmsWindow() const noexcept;

    // The cached block RMS values, in order. Empty when the signal is empty.
    [[nodiscard]] const std::vector<double>& rmsValues() const noexcept;

    // True when the time axis is labelled in milliseconds (duration below one
    // second). The unit is chosen from duration(), not from the caller.
    [[nodiscard]] bool usesMilliseconds() const noexcept;

    // The tick values the frame paints, in the axis unit (ms or s) for time and
    // in amplitude units for y.
    [[nodiscard]] QVector<qreal> timeTicks() const;
    [[nodiscard]] QVector<qreal> amplitudeTicks() const;
    [[nodiscard]] QString timeAxisLabel() const;

    // The pixel of one raw sample, in ChartBase-local coordinates. An empty
    // point for an out-of-range index or before the chart has a plot rectangle.
    [[nodiscard]] QPointF samplePoint(int index) const;

    // Resolves a widget-local pixel to the nearest sample: the time in seconds
    // (not axis units) and the sample amplitude. Returns false outside the plot
    // rectangle or on non-finite input; out parameters are untouched then.
    [[nodiscard]] bool sampleAt(QPointF position, double* time = nullptr,
                                double* amplitude = nullptr) const;

    // 0..1 horizontal progress of the chart's shared ChartAnimation. 1 before
    // the first visible paint and 1 immediately after a reduced-motion paint.
    [[nodiscard]] qreal revealProgress() const noexcept;
    [[nodiscard]] bool isRevealing() const noexcept;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override;
    void paintPlot(QPainter& painter, const QRectF& plot, qreal progress) override;

private:
    void rebuildSeries();
    void rebuildRms();
    void recomputePeak();
    void updateViewport();

    std::vector<double> samples_;
    std::vector<double> envelopeMinima_;
    std::vector<double> envelopeMaxima_;
    std::vector<double> rms_;

    double sampleRate_ = 44100.0;
    double peak_ = 0.0;
    int rmsWindow_ = 256;
    bool showRms_ = true;
};

} // namespace md
