// Periodicity — the HistogramChart component.
//
// A single-distribution bar chart. The samples are either installed directly
// with setSamples() or read from the y values of every ChartSeries; the last
// data setter called wins. The samples are filtered to finite values, sorted,
// split into bins and painted as bars over the shared ChartBase plot.
//
// Binning conventions:
//   * setBinCount(0) — the default — asks the selected method to choose the
//     count. A positive count is used verbatim, clamped into 1..64.
//   * BinMethod::Auto and BinMethod::Sturges use Sturges' rule,
//     ceil(log2(n)) + 1, so 100 samples produce 8 bins.
//   * BinMethod::FreedmanDiaconis uses 2 * IQR * n^(-1/3) and falls back to
//     Sturges when the interquartile range is zero. The result is clamped into
//     1..64 as well.
//   * The bin edges are evenly spaced over [minimum, maximum]; the first lower
//     edge is the sample minimum and the last upper edge is the sample maximum,
//     so the bins always cover the data. A zero-width range (all samples equal)
//     is widened by one unit, and n < 2 uses a single bin.
//
// Density and cumulative modes:
//   * setDensity(true) divides every count by (n * binWidth), so the area of
//     the bars integrates to 1. Cumulative density is the CDF, normalised to
//     [0, 1] at the last bin; cumulative counts are not normalised.
//   * setCumulative(true) replaces each bin value with the running total.
//   * Both default to false. The exact value painted in each bin is available
//     through Bin::value; Bin::count is always the raw count.
//
// ChartBase owns the title, legend, grid, axes, tick labels and empty state.
// HistogramChart::paintPlot() draws only the bars and the value labels that
// fit above them; it never paints a second frame. The x axis is the sample
// value scale, so each bar spans one Bin range from bins(), and the y axis is
// the count, density or cumulative value installed through math().
//
// Two base-class integration details are handled here:
//   * Raw samples are not a ChartSeries, while ChartBase::hasData() is
//     series-based. setSamples() mirrors the finite samples into one unnamed
//     ChartSeries (x = y = sample) so the base calls paintPlot(); the binning
//     code reads the explicit sample vector, not the mirror.
//   * ChartBase paints its frame before paintPlot(), so the visible bounds are
//     installed from every setter (refreshTransform()) instead of from the
//     paint pass. That keeps the frame, the bars and the public bins() in one
//     stable transform.
//
// Colours come from chartPalette() (with the theme primary as the last
// resort) and every length and type size from Density::dp()/sp(). Every
// override is wrapped in try/catch so a failure cannot unwind into Qt's paint
// machinery.

#pragma once

#include <md/components/chart_common.h>
#include <md/view/view.h>

#include <QPointF>
#include <QVector>

class QPainter;
class QRectF;

namespace md {

class HistogramChart : public ChartBase
{
    Q_OBJECT

public:
    // How a bin count of zero is resolved. Auto currently follows Sturges;
    // Freedman–Diaconis is opt-in because it is more sensitive to the sample
    // distribution and therefore harder to use as a stable default.
    enum class BinMethod {
        Auto,
        Sturges,
        FreedmanDiaconis,
    };

    // One resolved bin. `lower`/`upper` are data values, `count` is the raw
    // number of samples in the bin, and `value` is the height actually drawn
    // (count, density, cumulative count or the CDF).
    struct Bin
    {
        qreal lower = 0.0;
        qreal upper = 0.0;
        qreal count = 0.0;
        qreal value = 0.0;
    };

    explicit HistogramChart(QWidget* parent = nullptr);

    // Installs raw samples and clears any series so the explicit sample set is
    // the only source. Non-finite values are accepted here and filtered when
    // the bins are computed.
    void setSamples(QVector<qreal> values);

    // The effective sample list: the explicit samples when setSamples() was
    // the last data call, otherwise every finite y value of every series.
    [[nodiscard]] QVector<qreal> samples() const;

    // Hides ChartBase::setSeries() to make "series are the data source" a
    // complete replacement for an earlier setSamples() call. The y values of
    // all series are pooled into one distribution.
    void setSeries(QVector<ChartSeries> seriesList);

    // Zero selects the automatic count; a positive value is clamped into
    // 1..64. A negative value is treated as zero.
    void setBinCount(int count);
    [[nodiscard]] int binCount() const noexcept;

    // The count that will be used for the current samples and settings. This
    // is the value a test or a caller should read when binCount() returned 0.
    [[nodiscard]] int effectiveBinCount() const;

    void setBinMethod(BinMethod method);
    [[nodiscard]] BinMethod binMethod() const noexcept;

    // Normalise bar heights to a density that integrates to one.
    void setDensity(bool density);
    [[nodiscard]] bool isDensity() const noexcept;

    // Replace each bar with the running total (or the CDF in density mode).
    void setCumulative(bool cumulative);
    [[nodiscard]] bool isCumulative() const noexcept;

    // Gap between adjacent bars as a fraction of the bin slot, clamped into
    // 0..0.9. The default is 0.1.
    void setBarGap(qreal gap);
    [[nodiscard]] qreal barGap() const noexcept;

    // The resolved bins for the current data and settings. Empty when there is
    // no finite sample.
    [[nodiscard]] QVector<Bin> bins() const;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintPlot(QPainter& painter, const QRectF& plot) override;

private:
    // Sorted finite samples from whichever source is active.
    [[nodiscard]] QVector<qreal> effectiveSamples() const;

    // Resolves binCount_ against the sorted samples; always returns 1..64 for
    // a non-empty sample set.
    [[nodiscard]] int resolvedBinCount(const QVector<qreal>& sorted) const;

    // Feeds explicit bounds to MathematicsCoordination once per change. The
    // chart deliberately uses a stable, non-animated viewport: the bars must
    // map identically in paint and in the public bins()/test path.
    void syncTransform(const QPointF& minimum, const QPointF& maximum);

    // Recomputes the bar range and installs it without waiting for a paint.
    // ChartBase paints its frame before paintPlot(), so the bounds must be in
    // place as soon as the data or a binning setting changes.
    void refreshTransform();

    QVector<qreal> explicitSamples_;

    int binCount_ = 0;
    BinMethod method_ = BinMethod::Auto;
    bool density_ = false;
    bool cumulative_ = false;
    qreal barGap_ = 0.1;

    QPointF boundsMin_{0.0, 0.0};
    QPointF boundsMax_{1.0, 1.0};
    bool boundsFed_ = false;
};

} // namespace md
