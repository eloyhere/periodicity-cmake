// Periodicity — the HistogramChart component.
//
// ChartBase owns the title, legend, grid, axes, tick labels and empty state;
// paintPlot() draws only the bars and their value labels. This division is
// deliberate: the frame is shared furniture, and a subclass that painted a
// second set of tick labels would double every axis.
//
// The paint pass resolves every colour and dimension from one theme snapshot.
// Bars use the chart palette (never a literal colour), value labels use the
// labelSmall role through Density::sp(), and every gap and radius is converted
// with Density::dp(). The binning maths is shared verbatim between paintPlot()
// and bins().
//
// Bounds are installed with setVisibleBounds() rather than an animated
// auto-fit, and they are refreshed from the setters rather than from
// paintPlot(). ChartBase paints its frame before calling paintPlot(), so the
// category/value rectangle has to be correct before the frame is laid out.
// The viewport is refreshed only when the data range actually changes, so a
// paint does not repaint itself through transformChanged().
//
// Raw samples are not a ChartSeries, so setSamples() mirrors them into one
// unnamed series to keep ChartBase::hasData() true and to make the base call
// paintPlot(). The mirror has empty names (no legend) and is never read by the
// binning code; paintPlot() consumes the explicit sample vector.

#include <md/components/histogram_chart.h>

#include <md/components/math_coordination.h>
#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace md {
namespace {

constexpr qreal kPreferredWidthDp = 360.0;
constexpr qreal kPreferredHeightDp = 220.0;

constexpr int kMinimumBin = 1;
constexpr int kMaximumBin = 64;

constexpr qreal kValueLabelGapDp = 2.0;
constexpr qreal kHeadroom = 1.08;
constexpr qreal kBarAlpha = 0.9;
constexpr qreal kDegenerateHalfSpan = 0.5;

[[nodiscard]] QFont fontFor(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(QFont::Weight(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

// Type-7 linear interpolation over an already sorted sample set. Used by the
// Freedman–Diaconis rule; the box plot has the same helper because the two
// components must agree on what an interquartile range means.
[[nodiscard]] qreal quantileOfSorted(const QVector<qreal>& sorted, qreal probability)
{
    const qsizetype size = sorted.size();
    if (size <= 0)
        return 0.0;
    if (size == 1)
        return sorted.first();

    const qreal p = std::clamp(probability, 0.0, 1.0);
    const qreal h = (static_cast<qreal>(size) - 1.0) * p;
    const qsizetype lower = static_cast<qsizetype>(std::floor(h));
    const qsizetype upper = std::min<qsizetype>(lower + 1, size - 1);
    const qreal fraction = h - static_cast<qreal>(lower);
    return sorted.at(lower) + (sorted.at(upper) - sorted.at(lower)) * fraction;
}

[[nodiscard]] int sturgesBinCount(int sampleCount)
{
    if (sampleCount <= 1)
        return kMinimumBin;

    const qreal raw = std::ceil(std::log2(static_cast<qreal>(sampleCount))) + 1.0;
    if (!std::isfinite(raw))
        return kMinimumBin;

    return std::clamp(static_cast<int>(raw), kMinimumBin, kMaximumBin);
}

[[nodiscard]] int freedmanDiaconisBinCount(const QVector<qreal>& sorted)
{
    const qreal iqr = quantileOfSorted(sorted, 0.75) - quantileOfSorted(sorted, 0.25);
    if (!(iqr > 0.0) || !std::isfinite(iqr))
        return sturgesBinCount(static_cast<int>(sorted.size()));

    const qreal count = static_cast<qreal>(sorted.size());
    const qreal raw = 2.0 * iqr * std::pow(count, -1.0 / 3.0);
    if (!std::isfinite(raw) || raw <= 0.0)
        return sturgesBinCount(static_cast<int>(sorted.size()));

    const qreal bounded = std::clamp(raw, static_cast<qreal>(kMinimumBin),
                                     static_cast<qreal>(kMaximumBin));
    return std::clamp(static_cast<int>(std::ceil(bounded)), kMinimumBin, kMaximumBin);
}

// A value label with a precision that follows the magnitude: integer counts
// stay integers, densities use enough decimals to distinguish neighbouring
// bins without turning the chart into a table.
[[nodiscard]] QString valueLabel(qreal value, bool densityMode)
{
    if (!std::isfinite(value))
        return QString();

    if (!densityMode)
        return QString::number(qRound64(std::max<qreal>(0.0, value)));

    const qreal magnitude = std::abs(value);
    const int decimals = magnitude >= 10.0 ? 1 : (magnitude >= 1.0 ? 2 : 3);
    return QString::number(value, 'f', decimals);
}

[[nodiscard]] QColor barColour(const QVector<QColor>& palette, const Theme& theme)
{
    for (const QColor& colour : palette) {
        if (colour.isValid())
            return colour;
    }

    return theme.colours().primary;
}

// Maps data coordinates into widget pixels. The coordination owns the real
// transform; the interpolation branch only exists so the component remains
// paintable if it is embedded without one.
class PixelMapper
{
public:
    PixelMapper(const MathematicsCoordination* coordination, const QRectF& plot,
                const QPointF& minimum, const QPointF& maximum)
        : coordination_(coordination)
        , plot_(plot)
        , minimum_(minimum)
        , maximum_(maximum)
    {
    }

    [[nodiscard]] QPointF toPixel(const QPointF& data) const
    {
        if (coordination_ != nullptr) {
            const QPointF mapped = coordination_->toPixel(data);
            if (std::isfinite(mapped.x()) && std::isfinite(mapped.y()))
                return mapped;
        }

        const qreal xSpan = maximum_.x() - minimum_.x();
        const qreal ySpan = maximum_.y() - minimum_.y();
        const qreal x = xSpan > 0.0
            ? plot_.left() + (data.x() - minimum_.x()) / xSpan * plot_.width()
            : plot_.center().x();
        const qreal y = ySpan > 0.0
            ? plot_.bottom() - (data.y() - minimum_.y()) / ySpan * plot_.height()
            : plot_.center().y();
        return QPointF(x, y);
    }

private:
    const MathematicsCoordination* coordination_ = nullptr;
    QRectF plot_;
    QPointF minimum_;
    QPointF maximum_;
};

// Bars and their optional value labels. No grid, axis, tick or legend is
// painted here; those belong to ChartBase.
void paintBars(QPainter& painter, const QRectF& plot, const PixelMapper& mapper,
               const QVector<HistogramChart::Bin>& bins, qreal gapFraction, bool densityMode,
               const QColor& colour, const Theme& theme)
{
    const Density& density = theme.density();
    const qreal gapPad = density.dp(kValueLabelGapDp);
    const QFont labelFont = fontFor(theme, theme.type().labelSmall);
    const QFontMetricsF labelMetrics(labelFont);

    QColor fill = colour;
    fill.setAlphaF(kBarAlpha);

    painter.save();
    painter.setClipRect(plot);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setFont(labelFont);

    const qreal gap = std::clamp(gapFraction, 0.0, 0.9);

    for (const HistogramChart::Bin& bin : bins) {
        if (!(bin.value > 0.0))
            continue;

        const qreal left = mapper.toPixel(QPointF(bin.lower, 0.0)).x();
        const qreal right = mapper.toPixel(QPointF(bin.upper, 0.0)).x();
        const qreal slot = right - left;
        if (!std::isfinite(slot) || !(slot > 0.0))
            continue;

        const qreal inset = gap * slot;
        const qreal barWidth = std::max<qreal>(1.0, slot - inset);
        qreal top = mapper.toPixel(QPointF(0.0, bin.value)).y();
        if (!std::isfinite(top))
            continue;

        top = std::clamp(top, plot.top(), plot.bottom());
        const QRectF bar(left + inset / 2.0, top, barWidth, plot.bottom() - top);
        if (bar.height() <= 0.0)
            continue;

        painter.fillRect(bar, fill);

        const QString text = valueLabel(bin.value, densityMode);
        if (text.isEmpty())
            continue;

        const qreal textWidth = labelMetrics.horizontalAdvance(text);
        const qreal available = slot - inset;
        if (available < textWidth + 2.0 * gapPad)
            continue;

        const qreal labelTop = bar.top() - labelMetrics.height() - gapPad;
        if (labelTop < plot.top())
            continue;

        painter.setPen(theme.colours().onSurface);
        painter.drawText(QRectF(bar.center().x() - textWidth / 2.0, labelTop, textWidth,
                                labelMetrics.height()),
                         Qt::AlignHCenter | Qt::AlignTop, text);
    }

    painter.restore();
}

} // namespace

HistogramChart::HistogramChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(false);
    // A histogram has one distribution and therefore no legend. ChartBase
    // would otherwise reserve and paint a row for a named series.
    setLegendVisible(false);

    // Read-only data surface: a whole-surface hover tint would misrepresent the
    // painted colours; per-chart hover is painted by the chart itself.
    setInteractionFeedbackEnabled(false);
}

void HistogramChart::setSamples(QVector<qreal> values)
{
    explicitSamples_ = std::move(values);

    // ChartBase decides between paintPlot() and its empty state from the
    // series' points. Raw samples are not a series, so mirror them into one
    // (x = y = sample) to keep hasData() true; paintPlot() reads the explicit
    // sample vector, never this mirror. The empty name keeps the legend out.
    ChartSeries mirror;
    mirror.points.reserve(explicitSamples_.size());
    for (const qreal value : explicitSamples_) {
        if (std::isfinite(value))
            mirror.points.append(QPointF(value, value));
    }

    QVector<ChartSeries> seriesList;
    if (!mirror.points.isEmpty())
        seriesList.append(std::move(mirror));

    ChartBase::setSeries(std::move(seriesList));
    // setSeries() may have started a fresh auto-fit to the mirror bounds; the
    // histogram domain must win even when its numeric bounds are unchanged.
    boundsFed_ = false;
    refreshTransform();
    requestUpdate();
}

QVector<qreal> HistogramChart::samples() const
{
    return effectiveSamples();
}

void HistogramChart::setSeries(QVector<ChartSeries> seriesList)
{
    explicitSamples_.clear();
    ChartBase::setSeries(std::move(seriesList));
    boundsFed_ = false;
    refreshTransform();
    requestUpdate();
}

void HistogramChart::setBinCount(int count)
{
    const int normalized = count <= 0 ? 0 : std::clamp(count, kMinimumBin, kMaximumBin);
    if (binCount_ == normalized)
        return;

    binCount_ = normalized;
    refreshTransform();
    requestUpdate();
}

int HistogramChart::binCount() const noexcept
{
    return binCount_;
}

int HistogramChart::effectiveBinCount() const
{
    const QVector<qreal> sorted = effectiveSamples();
    return sorted.isEmpty() ? 0 : resolvedBinCount(sorted);
}

void HistogramChart::setBinMethod(BinMethod method)
{
    if (method_ == method)
        return;

    method_ = method;
    refreshTransform();
    requestUpdate();
}

HistogramChart::BinMethod HistogramChart::binMethod() const noexcept
{
    return method_;
}

void HistogramChart::setDensity(bool density)
{
    if (density_ == density)
        return;

    density_ = density;
    refreshTransform();
    requestUpdate();
}

bool HistogramChart::isDensity() const noexcept
{
    return density_;
}

void HistogramChart::setCumulative(bool cumulative)
{
    if (cumulative_ == cumulative)
        return;

    cumulative_ = cumulative;
    refreshTransform();
    requestUpdate();
}

bool HistogramChart::isCumulative() const noexcept
{
    return cumulative_;
}

void HistogramChart::setBarGap(qreal gap)
{
    const qreal normalized = std::isfinite(gap) ? std::clamp(gap, 0.0, 0.9) : 0.0;
    if (barGap_ == normalized)
        return;

    barGap_ = normalized;
    requestUpdate();
}

qreal HistogramChart::barGap() const noexcept
{
    return barGap_;
}

QVector<HistogramChart::Bin> HistogramChart::bins() const
{
    QVector<qreal> values = effectiveSamples();
    if (values.isEmpty())
        return {};

    std::sort(values.begin(), values.end());

    const int sampleCount = static_cast<int>(values.size());
    const int count = resolvedBinCount(values);

    qreal minimum = values.first();
    qreal maximum = values.last();
    if (sampleCount < 2 || !(maximum > minimum)) {
        minimum -= kDegenerateHalfSpan;
        maximum += kDegenerateHalfSpan;
    }

    if (!(maximum > minimum) || !std::isfinite(minimum) || !std::isfinite(maximum))
        return {};

    const qreal width = (maximum - minimum) / static_cast<qreal>(count);
    if (!(width > 0.0) || !std::isfinite(width))
        return {};

    QVector<qreal> counts;
    counts.resize(static_cast<qsizetype>(count));
    for (const qreal value : values) {
        const qreal rawIndex = std::floor((value - minimum) / width);
        if (!std::isfinite(rawIndex))
            continue;

        int index = static_cast<int>(rawIndex);
        index = std::clamp(index, 0, count - 1);
        counts[index] += 1.0;
    }

    QVector<Bin> resolved;
    resolved.reserve(static_cast<qsizetype>(count));

    qreal cumulative = 0.0;
    for (int index = 0; index < count; ++index) {
        const qreal lower = minimum + static_cast<qreal>(index) * width;
        const qreal upper = index == count - 1
            ? maximum
            : minimum + static_cast<qreal>(index + 1) * width;
        const qreal rawCount = counts.at(index);
        cumulative += rawCount;

        Bin bin;
        bin.lower = lower;
        bin.upper = upper;
        bin.count = rawCount;

        if (cumulative_) {
            bin.value = density_
                ? cumulative / static_cast<qreal>(sampleCount)
                : cumulative;
        } else if (density_) {
            const qreal binWidth = upper - lower;
            bin.value = binWidth > 0.0
                ? rawCount / (static_cast<qreal>(sampleCount) * binWidth)
                : 0.0;
        } else {
            bin.value = rawCount;
        }

        resolved.append(bin);
    }

    return resolved;
}

QVector<qreal> HistogramChart::effectiveSamples() const
{
    QVector<qreal> resolved;
    const auto appendFinite = [&resolved](qreal value) {
        if (std::isfinite(value))
            resolved.append(value);
    };

    if (!explicitSamples_.isEmpty()) {
        resolved.reserve(explicitSamples_.size());
        for (const qreal value : explicitSamples_)
            appendFinite(value);
        return resolved;
    }

    qsizetype total = 0;
    for (const ChartSeries& item : series())
        total += item.points.size();
    resolved.reserve(total);

    for (const ChartSeries& item : series()) {
        for (const QPointF& point : item.points)
            appendFinite(point.y());
    }

    return resolved;
}

int HistogramChart::resolvedBinCount(const QVector<qreal>& sorted) const
{
    if (sorted.isEmpty())
        return kMinimumBin;

    // One sample has no range to divide, regardless of an explicit count.
    if (sorted.size() < 2)
        return kMinimumBin;

    if (binCount_ > 0)
        return std::clamp(binCount_, kMinimumBin, kMaximumBin);

    const int sampleCount = static_cast<int>(sorted.size());

    switch (method_) {
    case BinMethod::Sturges:
    case BinMethod::Auto:
        return sturgesBinCount(sampleCount);
    case BinMethod::FreedmanDiaconis:
        return freedmanDiaconisBinCount(sorted);
    }

    return sturgesBinCount(sampleCount);
}

void HistogramChart::syncTransform(const QPointF& minimum, const QPointF& maximum)
{
    if (boundsFed_ && minimum == boundsMin_ && maximum == boundsMax_)
        return;

    boundsMin_ = minimum;
    boundsMax_ = maximum;
    boundsFed_ = true;

    if (MathematicsCoordination* coordination = math()) {
        // setVisibleBounds() also turns auto-fit off, which pins the transform
        // to the same rectangle the bars were laid out against.
        coordination->setVisibleBounds(minimum, maximum);
    }
}

void HistogramChart::refreshTransform()
{
    const QVector<Bin> resolved = bins();
    if (resolved.isEmpty())
        return;

    qreal tallest = 0.0;
    for (const Bin& bin : resolved)
        tallest = std::max(tallest, bin.value);

    const qreal yMaximum = tallest > 0.0 && std::isfinite(tallest) ? tallest : 1.0;
    qreal yTop = yMaximum * kHeadroom;
    if (!std::isfinite(yTop) || yTop <= yMaximum)
        yTop = yMaximum + 1.0;

    syncTransform(QPointF(resolved.first().lower, 0.0),
                  QPointF(resolved.last().upper, yTop));
}

void HistogramChart::measureOverride(MeasureSpec width, MeasureSpec height)
{
    try {
        const Density& density = Theme::forWidget(this).density();
        setMeasuredSize(QSizeF(width.resolve(density.dp(kPreferredWidthDp)),
                               height.resolve(density.dp(kPreferredHeightDp))));
    } catch (const std::exception& error) {
        qWarning("md::HistogramChart::measureOverride failed: %s", error.what());
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    } catch (...) {
        qWarning("md::HistogramChart::measureOverride failed with an unknown exception");
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    }
}

void HistogramChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    if (plot.isEmpty())
        return;

    painter.save();
    try {
        const Theme theme = Theme::forWidget(this);
        const QVector<Bin> resolved = bins();
        if (resolved.isEmpty()) {
            painter.restore();
            return;
        }

        qreal tallest = 0.0;
        for (const Bin& bin : resolved)
            tallest = std::max(tallest, bin.value);

        const qreal yMaximum = tallest > 0.0 && std::isfinite(tallest) ? tallest : 1.0;
        qreal yTop = yMaximum * kHeadroom;
        if (!std::isfinite(yTop) || yTop <= yMaximum)
            yTop = yMaximum + 1.0;

        const QPointF minimum(resolved.first().lower, 0.0);
        const QPointF maximum(resolved.last().upper, yTop);
        syncTransform(minimum, maximum);

        const PixelMapper mapper(math(), plot, boundsMin_, boundsMax_);
        paintBars(painter, plot, mapper, resolved, barGap_, density_,
                  barColour(chartPalette(), theme), theme);
    } catch (const std::exception& error) {
        qWarning("md::HistogramChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::HistogramChart::paintPlot failed with an unknown exception");
    }
    painter.restore();
}

} // namespace md
