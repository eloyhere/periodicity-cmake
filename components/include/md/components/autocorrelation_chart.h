// Periodicity — the AutocorrelationChart component.
//
// A stem plot of a normalised autocorrelation sequence. The x axis is the lag,
// in samples or milliseconds (setLagUnit()), and the y axis is fixed to the
// normalised -1..1 range, so every chart has the same visual scale and a
// periodicity peak can be compared across signals by eye.
//
// Data. setValues() installs a sequence directly (lag = index); setSamples()
// computes the mean-removed, normalised autocorrelation  r[k] =
// Σ (x[i]-μ)(x[i+k]-μ) / Σ (x[i]-μ)²  up to setMaxLag() (512 lags by default,
// or the whole signal when shorter). The result is deterministic and stays
// inside ±1. A constant signal has no variance: it yields r[0] = 1 and zero
// elsewhere rather than a division by zero.
//
// Threshold band. A translucent band spans ±threshold() (default 0.5), with
// dashed edges, behind the stems. It marks the region a peak should leave to be
// treated as significant; the band is presentation only and does not filter the
// data.
//
// Rendering. Every lag is a stem from the zero baseline to its value, with a
// small tip marker when the stems are far enough apart to read individually.
// The chart's shared ChartAnimation grows the stems from the baseline on first
// appearance and on a later data change; reduced motion settles the plot
// immediately, and a chart that was never shown paints finished.
//
// Hit testing. stemAt() maps a widget-local pixel to the nearest stem tip and
// stemPoint() returns the exact pixel of one lag; ChartBase::hitTest() also
// works because the component installs the sequence as its data series.
//
// Exception safety. paintPlot(), stemAt() and the reveal hooks catch locally,
// so a broken theme cannot unwind into Qt's paint loop.

#pragma once

#include <md/components/chart_common.h>

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <vector>

class QPainter;

namespace md {

class AutocorrelationChart : public ChartBase
{
    Q_OBJECT

public:
    enum class LagUnit
    {
        Samples,
        Milliseconds
    };
    Q_ENUM(LagUnit)

    explicit AutocorrelationChart(QWidget* parent = nullptr);

    // Replaces the sequence directly. Index = lag; values outside -1..1 are
    // preserved but clamped when painted. An empty sequence clears the chart.
    void setValues(std::vector<double> values);
    [[nodiscard]] const std::vector<double>& values() const noexcept;

    // Computes the normalised autocorrelation of `samples` and installs it as
    // the sequence. A non-finite or non-positive sample rate is rejected and
    // leaves the previous rate untouched. The original samples are kept, so
    // setMaxLag() can recompute at another lag limit.
    void setSamples(std::vector<double> samples, double sampleRate = 44100.0);
    [[nodiscard]] const std::vector<double>& samples() const noexcept;
    [[nodiscard]] double sampleRate() const noexcept;

    // The lag limit used by setSamples(): 0 means automatic (the signal length,
    // capped at 512 lags). A positive value is clamped to the signal length at
    // computation time. Changing it recomputes the sequence when samples are
    // available; it does not truncate a sequence installed by setValues().
    void setMaxLag(int maxLag);
    [[nodiscard]] int maxLag() const noexcept;

    // The number of lags currently displayed minus one. Zero when empty.
    [[nodiscard]] int effectiveMaxLag() const noexcept;

    // The significance band half-width, clamped into 0..1. Default 0.5. A zero
    // threshold paints no band.
    void setThreshold(qreal threshold);
    [[nodiscard]] qreal threshold() const noexcept;

    // Milliseconds lags are derived from sampleRate() (1000 / rate per sample).
    // Default Samples.
    void setLagUnit(LagUnit unit);
    [[nodiscard]] LagUnit lagUnit() const noexcept;

    // The tick values the shared frame paints: lags in the current unit and the
    // normalised correlation values.
    [[nodiscard]] QVector<qreal> lagTicks() const;
    [[nodiscard]] QVector<qreal> valueTicks() const;
    [[nodiscard]] QString lagAxisLabel() const;

    // The lag value of one index in the current axis unit.
    [[nodiscard]] double lagForIndex(int index) const;

    // The pixel of one stem tip (the settled value, not the reveal-scaled one).
    // An empty point for an out-of-range index or before the chart has a plot
    // rectangle.
    [[nodiscard]] QPointF stemPoint(int index) const;

    // The nearest stem tip to `position`, within the hit slop. Returns the
    // index, the lag in axis units and the stored value. False outside the plot
    // or when no stem is near; out parameters are untouched then.
    [[nodiscard]] bool stemAt(QPointF position, int* index = nullptr,
                              double* lag = nullptr, double* value = nullptr) const;

    // 0..1 stem-growth progress of the chart's shared ChartAnimation. 1 before
    // the first visible paint and 1 immediately after a reduced-motion paint.
    [[nodiscard]] qreal revealProgress() const noexcept;
    [[nodiscard]] bool isRevealing() const noexcept;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override;
    void paintPlot(QPainter& painter, const QRectF& plot, qreal progress) override;

private:
    void rebuildSeries();
    void updateViewport();
    void recomputeFromSamples();
    [[nodiscard]] double lagScale() const noexcept;

    std::vector<double> values_;
    std::vector<double> samples_;

    double sampleRate_ = 44100.0;
    int maxLag_ = 0;
    qreal threshold_ = 0.5;
    LagUnit lagUnit_ = LagUnit::Samples;
    bool derivedFromSamples_ = false;
};

} // namespace md
