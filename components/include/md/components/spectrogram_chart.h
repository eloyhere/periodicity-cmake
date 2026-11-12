// Periodicity — the SpectrogramChart component.
//
// A time-frequency matrix painted as a heat map with a calibrated colour bar.
// The matrix is stored as rows = frequency bins and columns = time frames, so
// one column is one analysis window. setFrames() takes levels that are already
// in dB; setMagnitude() takes linear magnitudes together with the acquisition
// geometry and converts each cell with 20·log10, which is the usual way a
// spectrum analyser presents a short-time Fourier transform.
//
// Axes. The chart paints its own frame instead of the shared Cartesian frame
// because a spectrogram has a frequency axis that is optionally logarithmic and
// a colour bar that shares the frequency gutter. Time runs along x in seconds,
// frequency runs along y in Hz, and the colour bar carries the level in dB.
// setLogFrequency(true) switches the frequency axis to a logarithmic mapping;
// frequency ticks then fall on the 1/2/5 ladder and stay readable across the
// usual audio range. Tick values and labels are exposed through frequencyTicks()
// / frequencyTickLabel() and timeTicks() / timeTickLabel(), so a caller can
// label a legend or a tooltip with exactly the text the chart paints.
//
// Colour. The level range is set with setDbRange(); every cell maps linearly
// onto a deterministic five-stop ramp assembled from theme colour roles. The
// ramp and the colour bar use the same stops, so a cell colour can always be
// read back from the bar without a lookup table.
//
// Hover. The chart tracks the pointer over the matrix and paints a crosshair
// plus a compact readout of (time, frequency, level). sampleAt() exposes the
// same mapping without a mouse event, which is what a tooltip, a test or a
// linked cursor can use. Hover is presentation only; nothing about the data
// changes.
//
// Reveal. The first visible paint grows the matrix from the first column to the
// last through the chart's shared ChartAnimation (reveal on first appearance,
// update on a later data change). Reduced motion settles the whole chart
// immediately, and an offscreen render of a chart that was never shown paints
// the finished image.
//
// Data gate. ChartBase selects paintPlot()/paintEmptyState() from hasData(),
// which reads ChartBase::series(); a matrix is not a series. The component
// therefore keeps one unnamed presence point in sync with the matrix (one point
// while any cell is finite, no series otherwise). paintPlot() never paints it.
//
// Exception safety. paintPlot(), paintEmptyState(), the mouse handlers and the
// reveal hooks catch component exceptions, so a broken theme cannot unwind into
// Qt's paint loop.

#pragma once

#include <md/components/chart_common.h>

#include <QPoint>
#include <QRectF>
#include <QString>
#include <QVector>

#include <vector>

class QEvent;
class QMouseEvent;
class QPainter;

namespace md {

class SpectrogramChart : public ChartBase
{
    Q_OBJECT

public:
    explicit SpectrogramChart(QWidget* parent = nullptr);

    // Replaces the matrix with precomputed dB levels: rows are frequency bins
    // (bin 0 is DC), columns are time frames. Ragged input is accepted; missing
    // cells are treated as empty (no level, no painted colour). An empty matrix
    // clears the chart. Values are left exactly as given.
    void setFrames(std::vector<std::vector<double>> frames);
    [[nodiscard]] const std::vector<std::vector<double>>& frames() const noexcept;

    // Replaces the matrix with linear magnitudes gathered with `sampleRate`
    // (Hz) and `hopSize` (samples between consecutive frames). Every finite
    // positive magnitude becomes 20·log10(m); a zero or negative magnitude is
    // left empty rather than clamped to a misleading finite level. The frame
    // size is inferred from the bin count as 2·(bins - 1), which is the usual
    // real-FFT layout with the Nyquist bin last.
    void setMagnitude(const std::vector<std::vector<double>>& matrix, double sampleRate,
                      double hopSize);

    // True when the stored matrix came from setMagnitude() and its cells are
    // linear magnitudes. setFrames() and setMagnitude() both replace the data.
    [[nodiscard]] bool isMagnitudeScale() const noexcept;

    [[nodiscard]] int frequencyBins() const noexcept;
    [[nodiscard]] int frameCount() const noexcept;

    // The acquisition geometry. `sampleRate` must be finite and positive; a
    // rejected value leaves the previous one untouched. `hopSize` is clamped to
    // at least one sample. The defaults (48 kHz, 512 samples) make an
    // unconfigured setFrames() chart display a plausible time axis; call
    // setMagnitude() or both setters when the real geometry is known.
    void setSampleRate(double sampleRate);
    [[nodiscard]] double sampleRate() const noexcept;
    void setHopSize(double hopSize);
    [[nodiscard]] double hopSize() const noexcept;

    // The displayed level window in dB. Values outside it are clamped to the
    // ramp's ends. The range is normalised (the smaller value becomes the
    // minimum) and a degenerate range is widened to 1 dB so the ramp always has
    // a span. Default -100..0 dB.
    void setDbRange(double minimumDb, double maximumDb);
    [[nodiscard]] double minimumDb() const noexcept;
    [[nodiscard]] double maximumDb() const noexcept;

    // Map the frequency axis logarithmically instead of linearly. Default
    // false. In log mode the axis starts at the first non-DC bin and ticks on
    // the 1/2/5 × 10^k ladder; DC and any bin below that start share the bottom
    // edge.
    void setLogFrequency(bool logarithmic);
    [[nodiscard]] bool isLogFrequency() const noexcept;

    // Whether the level colour bar and its dB labels are painted. Default true.
    void setColourBarVisible(bool visible);
    [[nodiscard]] bool isColourBarVisible() const noexcept;

    // The time span of the matrix, in seconds, and the highest analysed
    // frequency, in Hz. Both are zero when the geometry or the matrix is empty.
    [[nodiscard]] double duration() const noexcept;
    [[nodiscard]] double nyquistFrequency() const noexcept;

    // The tick values the painted axes use. Both are ordered and inside their
    // axis domain. frequencyTicks() seeds from the linearly spaced FFT bins even
    // in log mode, so the returned values are real bin frequencies.
    [[nodiscard]] QVector<qreal> timeTicks() const;
    [[nodiscard]] QVector<qreal> frequencyTicks() const;

    // The exact label text for one axis tick. frequencyTickLabel() uses Hz
    // below 1 kHz and a compact "k" suffix above it ("500", "2k", "15k").
    [[nodiscard]] QString timeTickLabel(qreal seconds) const;
    [[nodiscard]] QString frequencyTickLabel(qreal hertz) const;

    // The matrix rectangle (frequency × time), in widget-local pixels. Empty
    // before the chart has a plot rectangle. The colour bar sits to its right.
    [[nodiscard]] QRectF matrixRect() const;
    [[nodiscard]] QRectF colourBarRect() const;

    // The rectangle of one cell in widget-local pixels. An empty rectangle for
    // an out-of-range cell or before the chart has geometry.
    [[nodiscard]] QRectF cellRect(int bin, int frame) const;

    // The cell under a widget-local pixel, as (x = frame, y = bin), or (-1, -1)
    // outside the matrix. Mirrors the mapping sampleAt() uses.
    [[nodiscard]] QPoint cellAt(QPointF position) const;

    // Resolves the matrix cell under `position` to its centre time, the bin's
    // centre frequency and the cell level in dB. Returns false (and leaves the
    // out parameters untouched) outside the matrix or on non-finite input.
    [[nodiscard]] bool sampleAt(QPointF position, double* time = nullptr,
                                double* frequency = nullptr, double* levelDb = nullptr) const;

    // The hovered cell and its resolved values. hasHover() is false outside the
    // matrix; the other getters then return the last cleared values (zero).
    [[nodiscard]] bool hasHover() const noexcept;
    [[nodiscard]] int hoveredBin() const noexcept;
    [[nodiscard]] int hoveredFrame() const noexcept;
    [[nodiscard]] double hoveredTime() const noexcept;
    [[nodiscard]] double hoveredFrequency() const noexcept;
    [[nodiscard]] double hoveredLevelDb() const noexcept;

    // 0..1 progress of the chart's shared ChartAnimation. 1 before the first
    // visible paint and 1 immediately after a reduced-motion paint.
    [[nodiscard]] qreal revealProgress() const noexcept;
    [[nodiscard]] bool isRevealing() const noexcept;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override;
    void paintPlot(QPainter& painter, const QRectF& plot, qreal progress) override;
    void paintEmptyState(QPainter& painter, const QRectF& plot) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Layout
    {
        QRectF data;
        QRectF colourBar;
    };

    [[nodiscard]] Layout layoutFor(const QRectF& plot) const;

    // Shared paint body: the frame, grid, labels and colour bar, plus the cells
    // when `drawData` is true. `progress` is the revealed fraction of the
    // columns; paintEmptyState() passes 1 so the axes are always complete.
    void paintScene(QPainter& painter, const QRectF& plot, bool drawData, qreal progress);

    // The centre frequency of one bin, in Hz.
    [[nodiscard]] double binFrequency(int bin) const noexcept;

    // The level of one cell in dB, converting a linear magnitude when the data
    // came from setMagnitude(). A missing or non-finite cell returns NaN.
    [[nodiscard]] qreal levelDb(int bin, int frame) const;

    [[nodiscard]] bool hasSpectrogramValues() const noexcept;
    void syncPresenceSeries();

    // The lower end of a logarithmic frequency axis: the first non-DC bin.
    [[nodiscard]] double frequencyFloor() const noexcept;

    // Maps a frequency to a 0..1 fraction of the matrix height (0 at the bottom
    // edge, 1 at the top). Clamped into the axis domain.
    [[nodiscard]] qreal frequencyFraction(double hertz) const;

    void setHoveredCell(int bin, int frame);

    std::vector<std::vector<double>> frames_;
    bool magnitudeScale_ = false;

    double sampleRate_ = 48000.0;
    double hopSize_ = 512.0;
    double dbMinimum_ = -100.0;
    double dbMaximum_ = 0.0;
    bool logFrequency_ = false;
    bool colourBarVisible_ = true;

    int hoveredBin_ = -1;
    int hoveredFrame_ = -1;
    double hoveredTime_ = 0.0;
    double hoveredFrequency_ = 0.0;
    double hoveredLevelDb_ = 0.0;
};

} // namespace md
