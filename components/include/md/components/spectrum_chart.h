// Periodicity — the SpectrumChart component.
//
// A one-sided magnitude spectrum with an optional phase trace. The data is a
// plain magnitude vector plus the sample rate it was measured at: bin i is the
// frequency i × (sampleRate / 2) / (count − 1), so the first bin is DC and the
// last is the Nyquist frequency. Nothing is assumed about how the magnitudes
// were produced — a windowed FFT, a filter bank or a synthetic test vector are
// all valid inputs.
//
// Axes. The chart owns its frame rather than reusing ChartBase's Cartesian
// frame, because a logarithmic frequency axis is not a linear data-to-pixel
// mapping. The frame is painted by this class: grid lines, axes, tick marks,
// tick labels and the axis titles all live in paintPlot(). ChartBase is still
// the parent surface: it paints the title, the legend and the empty state, and
// setAxesVisible(false) gives this class the full inner rectangle to lay the
// two-axis frame out in. Every length goes through Density::dp(); every colour
// is a theme role or a palette entry from ChartBase::seriesColour(). Tick
// values are generated with MathematicsCoordination::niceTicks() and formatted
// with MathematicsCoordination::tickLabel(), so the numeric style matches the
// shared transform.
//
// Magnitude axis. Scale::Linear maps magnitudes from zero to the largest finite
// magnitude. Scale::Db maps 20·log10(magnitude) — decibels relative to a
// full-scale magnitude of 1.0 — from setDbFloor() (default −120 dB) to the next
// 20 dB multiple at or above the data. A zero or negative magnitude is drawn at
// the floor.
//
// Frequency axis. FrequencyAxis::Linear spans 0 Hz to Nyquist.
// FrequencyAxis::Logarithmic spans the first positive bin to Nyquist and paints
// decade grid lines (with 2× and 5× minors). A range narrower than a decade
// falls back to linear ticks, so the axis is never empty.
//
// Phase axis. setPhase() attaches one phase value per magnitude bin. Phase is
// drawn against a right-hand axis fixed to −180..180 degrees, wrapped into that
// interval, with the trace broken where the wrap would otherwise produce a
// vertical jump. An empty phase vector removes the axis.
//
// Peaks. Peak markers are the interior local maxima above the noise floor,
// labelled with their frequency. At most setMaxPeakLabels() labels are painted
// (the strongest first) so a dense spectrum stays readable; peakIndices()
// reports the complete set in descending magnitude order.
//
// Cursor. The chart is a read-only data surface (setInteractionFeedbackEnabled(false),
// no focus) but tracks the pointer: a vertical crosshair snaps to the nearest
// bin and a readout reports its frequency, linear magnitude and level in dB.
// setCursorFrequency() drives the same readout from code. binAt() maps a pixel
// to the nearest bin for a host that wants its own tooltip.
//
// Reveal animation. The first show sweeps the trace in from left to right over
// the theme's level-2 motion. The sweep is presentation only: binAt(),
// pointForBin(), peakIndices() and every accessor describe the settled
// geometry. A reduced-motion theme (or a zero-duration spec) snaps to the
// finished frame synchronously, and a chart that was never shown paints
// finished, so a QWidget::grab() or an offscreen render is complete.
//
// Empty state. With no finite magnitude the base paints a themed, localized
// "No spectrum data" label instead of the frame, so a fresh chart has a usable
// placeholder.
//
// Exception safety. paintPlot(), paintEmptyState(), the mouse handlers and the
// reveal hooks catch component exceptions locally; nothing unwinds into Qt's
// paint or event loop.

#pragma once

#include <md/components/chart_common.h>

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <vector>

class QMouseEvent;
class QPainter;

namespace md {

class SpectrumChart : public ChartBase
{
    Q_OBJECT

public:
    // How the magnitude axis maps magnitudes to pixels.
    enum class Scale {
        Linear, // magnitude from zero to the largest finite value
        Db,     // 20·log10(magnitude), from setDbFloor() to the next 20 dB step
    };
    Q_ENUM(Scale)

    // How the frequency axis maps hertz to pixels.
    enum class FrequencyAxis {
        Linear,
        Logarithmic,
    };
    Q_ENUM(FrequencyAxis)

    explicit SpectrumChart(QWidget* parent = nullptr);

    // Replaces the spectrum. Bin i is at i × (sampleRate / 2) / (count − 1).
    // A non-finite or non-positive sample rate, or an empty vector, clears the
    // chart. Non-finite magnitudes are skipped when painting and never enter
    // the peak or hit-test result. The previous phase vector and cursor are
    // cleared because they described bins that no longer exist.
    void setMagnitudes(std::vector<double> magnitudes, double sampleRate);

    [[nodiscard]] const std::vector<double>& magnitudes() const noexcept;
    [[nodiscard]] double sampleRate() const noexcept;

    // The number of bins, or zero when the chart is empty.
    [[nodiscard]] int binCount() const noexcept;

    // Nyquist frequency, sampleRate / 2 (zero when empty).
    [[nodiscard]] double nyquistFrequency() const noexcept;

    // Frequency of a bin in Hz. An out-of-range bin yields 0.
    [[nodiscard]] double frequencyAt(int bin) const noexcept;

    // The raw magnitude of a bin; 0 for an out-of-range or non-finite bin.
    [[nodiscard]] double magnitudeAt(int bin) const noexcept;

    // 20·log10(magnitudeAt(bin)), clamped to the current dB floor. A zero or
    // negative magnitude reports the floor. This is defined for both scales,
    // because the cursor readout always reports the level.
    [[nodiscard]] double decibelsAt(int bin) const noexcept;

    // Linear magnitude axis, or the decibel axis described above.
    void setScale(Scale scale);
    [[nodiscard]] Scale scale() const noexcept;

    // Bottom of the dB axis in decibels. The value is clamped into
    // [−300, −1]; the default is −120. Ignored by Scale::Linear.
    void setDbFloor(double decibels);
    [[nodiscard]] double dbFloor() const noexcept;

    // Linear (the default) or logarithmic frequency mapping.
    void setFrequencyAxis(FrequencyAxis axis);
    [[nodiscard]] FrequencyAxis frequencyAxis() const noexcept;

    // The optional phase trace, in degrees, one value per magnitude bin. Values
    // are wrapped into −180..180 for painting; a missing tail is skipped. An
    // empty vector removes the right-hand axis and the legend entry.
    void setPhase(std::vector<double> degrees);
    [[nodiscard]] const std::vector<double>& phase() const noexcept;
    [[nodiscard]] bool hasPhase() const noexcept;
    void clearPhase();

    // Peak markers and labels. Enabled by default; the floor is relative to
    // the total dB span and defaults to −60 dB, so low-level ripple is not
    // labelled as a peak.
    void setShowPeaks(bool show);
    [[nodiscard]] bool showsPeaks() const noexcept;
    void setPeakFloorDb(double decibels);
    [[nodiscard]] double peakFloorDb() const noexcept;
    void setMaxPeakLabels(int count);
    [[nodiscard]] int maxPeakLabels() const noexcept;

    // Interior local maxima, strongest first. Empty when there are fewer than
    // three finite bins. A peak must be strictly above its left neighbour, at
    // least as high as its right neighbour, positive and within peakFloorDb()
    // of the strongest finite bin.
    [[nodiscard]] std::vector<int> peakIndices() const;

    // Cursor: the bin the crosshair is on, or a nearest bin to a frequency.
    // setCursorFrequency() clamps into the displayed range; clearCursor()
    // removes the crosshair. A pixel outside the data rectangle yields binAt()
    // == −1.
    void setCursorFrequency(double hertz);
    void clearCursor();
    [[nodiscard]] bool hasCursor() const noexcept;
    [[nodiscard]] int cursorBin() const noexcept;
    [[nodiscard]] double cursorFrequency() const noexcept;

    // The readout text the next paint would draw next to the cursor, in the
    // form "frequency  |  linear magnitude  |  level dB"; empty without a
    // cursor.
    [[nodiscard]] QString cursorReadout() const;

    // The data rectangle in ChartBase-local pixels: the plot rectangle after
    // the axis gutters. Empty when the widget is too small for the labels.
    [[nodiscard]] QRectF dataRect() const;

    // The nearest bin to a ChartBase-local pixel, or −1 outside the data
    // rectangle. The nearest-bin search is by frequency, so it is correct on a
    // logarithmic axis where bins are not evenly spaced in pixels.
    [[nodiscard]] int binAt(QPointF position) const;

    // The settled pixel of one bin: math used by the paint pass and by a host
    // that wants to anchor a tooltip. A null QPointF (0, 0) for an
    // out-of-range bin, a non-finite magnitude, or a bin the current axis does
    // not show (the DC bin on a logarithmic axis).
    [[nodiscard]] QPointF pointForBin(int bin) const;

    // Axis observability, used by the paint pass and by tests. The tick values
    // and their exact label strings are public so an annotation can use the
    // same numbers the axis shows.
    [[nodiscard]] QVector<double> frequencyTicks() const;
    [[nodiscard]] QStringList frequencyTickLabels() const;
    [[nodiscard]] QVector<double> magnitudeTicks() const;
    [[nodiscard]] QStringList magnitudeTickLabels() const;
    [[nodiscard]] QString xAxisTitle() const;
    [[nodiscard]] QString yAxisTitle() const;
    [[nodiscard]] QString phaseAxisTitle() const;

    // 0..1 sweep progress. 1 before the first show and 1 immediately after a
    // reduced-motion show.
    [[nodiscard]] qreal revealProgress() const noexcept;
    [[nodiscard]] bool isRevealing() const noexcept;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot, qreal progress) override;
    void paintEmptyState(QPainter& painter, const QRectF& plot) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // The pixel geometry of one frame: the data rectangle plus the gutters the
    // labels and axis titles occupy. `valid` is false when the widget cannot
    // fit the frame.
    struct Layout
    {
        QRectF data;
        qreal leftGutter = 0.0;
        qreal rightGutter = 0.0;
        qreal bottomGutter = 0.0;
        bool valid = false;
    };

    // A closed interval used for one axis mapping.
    struct Interval
    {
        double minimum = 0.0;
        double maximum = 1.0;
    };

    [[nodiscard]] bool hasMagnitudes() const noexcept;
    [[nodiscard]] double displayValueAt(int bin) const;
    [[nodiscard]] Interval valueRange() const;
    [[nodiscard]] Interval frequencyRange() const;

    [[nodiscard]] Layout computeLayout(const QRectF& plot) const;
    [[nodiscard]] qreal mapFrequency(double hertz, const Layout& layout,
                                     const Interval& range) const;
    [[nodiscard]] qreal mapValue(double value, const Layout& layout,
                                 const Interval& range) const;
    [[nodiscard]] double unmapFrequency(qreal x, const Layout& layout,
                                        const Interval& range) const;
    [[nodiscard]] int nearestBin(double hertz) const;

    void rebuildSeries();
    void drawFrame(QPainter& painter, const Layout& layout, const Theme& theme);
    void paintPlotAt(QPainter& painter, const QRectF& plot, qreal progress);

    // Fields.
    std::vector<double> magnitudes_;
    std::vector<double> phase_;
    double sampleRate_ = 0.0;

    Scale scale_ = Scale::Linear;
    double dbFloor_ = -120.0;
    FrequencyAxis frequencyAxis_ = FrequencyAxis::Linear;

    bool showPeaks_ = true;
    double peakFloorDb_ = -60.0;
    int maxPeakLabels_ = 3;

    int cursorBin_ = -1;
};

} // namespace md
