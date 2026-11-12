// Periodicity — the BoxPlotChart component.
//
// ChartBase owns the title, legend, grid, axes, tick labels and empty state;
// paintPlot() draws only the boxes, whiskers, medians and outlier dots. This
// keeps every chart in the family on one frame implementation and avoids a
// second set of tick labels.
//
// Every box is derived from the finite y values of one ChartSeries. The type-7
// quantile, the 1.5 * IQR fences and the outlier classification live in
// summarise(); paintPlot(), boxRectAt() and boxAt() all read that one summary,
// so a hover can never disagree with the pixels it is testing.
//
// The transform is installed with setVisibleBounds() rather than an animated
// auto-fit, and it is refreshed from setSeries()/setHorizontal() rather than
// from paintPlot(). ChartBase paints its frame before calling paintPlot(), so
// the category/value domain has to be correct before the frame is laid out.
// Bounds are refreshed only when the data or orientation changes, which also
// keeps transformChanged() from triggering a paint loop.
//
// The value axis is always numeric and left-to-right. Colours come from the
// chart palette or the series' own colour, never from a literal; every length
// is Density::dp().

#include <md/components/box_plot_chart.h>

#include <md/components/math_coordination.h>
#include <md/theme/theme.h>

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace md {
namespace {

constexpr qreal kPreferredWidthDp = 360.0;
constexpr qreal kPreferredHeightDp = 220.0;

constexpr qreal kHoverPaddingDp = 3.0;
constexpr qreal kOutlierRadiusDp = 3.0;
constexpr qreal kMinimumRadius = 1.5;

constexpr qreal kBoxPenDp = 1.2;
constexpr qreal kMedianPenDp = 2.0;
constexpr qreal kWhiskerPenDp = 1.2;
constexpr qreal kMinimumPenWidth = 0.75;

constexpr qreal kBoxFillAlpha = 0.18;
constexpr qreal kHoverFillAlpha = 0.32;
constexpr qreal kHaloAlpha = 0.10;

constexpr qreal kValuePaddingRatio = 0.05;
constexpr qreal kDegenerateSpan = 1.0;

// Type-7 linear interpolation over a sorted sample set.
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

[[nodiscard]] BoxPlotChart::BoxSummary summarise(const QVector<qreal>& values)
{
    BoxPlotChart::BoxSummary summary;
    if (values.isEmpty())
        return summary;

    QVector<qreal> sorted = values;
    std::sort(sorted.begin(), sorted.end());

    summary.valid = true;
    summary.minimum = sorted.first();
    summary.maximum = sorted.last();
    summary.q1 = quantileOfSorted(sorted, 0.25);
    summary.median = quantileOfSorted(sorted, 0.5);
    summary.q3 = quantileOfSorted(sorted, 0.75);

    const qreal iqr = summary.q3 - summary.q1;
    qreal lowerFence = summary.q1 - 1.5 * iqr;
    qreal upperFence = summary.q3 + 1.5 * iqr;
    if (!std::isfinite(lowerFence))
        lowerFence = summary.minimum;
    if (!std::isfinite(upperFence))
        upperFence = summary.maximum;

    summary.lowerWhisker = summary.q1;
    summary.upperWhisker = summary.q3;
    for (const qreal value : sorted) {
        if (value >= lowerFence) {
            summary.lowerWhisker = value;
            break;
        }
    }
    for (qsizetype index = sorted.size(); index > 0; --index) {
        const qreal value = sorted.at(index - 1);
        if (value <= upperFence) {
            summary.upperWhisker = value;
            break;
        }
    }

    const qreal epsilon = std::max<qreal>(1e-12, std::abs(iqr) * 1e-12);
    for (const qreal value : sorted) {
        if (value < summary.lowerWhisker - epsilon
            || value > summary.upperWhisker + epsilon) {
            summary.outliers.append(value);
        }
    }

    return summary;
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

// One box, its whiskers/caps, the median and the outlier dots. The plot frame
// and all text belong to ChartBase; this helper only draws marks.
void drawBox(QPainter& painter, const QRectF& box, const BoxPlotChart::BoxSummary& summary,
             int index, int categoryCount, const PixelMapper& mapper, bool horizontal,
             bool showOutliers, bool hovered, const QColor& colour, const Theme& theme)
{
    const Density& density = theme.density();
    const qreal boxPen = std::max(kMinimumPenWidth, density.dp(kBoxPenDp));
    const qreal medianPen = std::max(kMinimumPenWidth, density.dp(kMedianPenDp));
    const qreal whiskerPen = std::max(kMinimumPenWidth, density.dp(kWhiskerPenDp));

    if (hovered) {
        QColor halo = colour;
        halo.setAlphaF(kHaloAlpha);
        const qreal haloPad = density.dp(2.0);
        painter.fillRect(box.adjusted(-haloPad, -haloPad, haloPad, haloPad), halo);
    }

    // Whiskers and caps sit behind the box.
    const qreal category = horizontal
        ? static_cast<qreal>(categoryCount - 1 - index)
        : static_cast<qreal>(index);
    QPen whiskerPenStyle(colour, whiskerPen);
    whiskerPenStyle.setCapStyle(Qt::FlatCap);
    painter.setPen(whiskerPenStyle);
    painter.setBrush(Qt::NoBrush);

    if (horizontal) {
        const qreal centerY = mapper.toPixel(QPointF(0.0, category)).y();
        const qreal lowerX = mapper.toPixel(QPointF(summary.lowerWhisker, 0.0)).x();
        const qreal upperX = mapper.toPixel(QPointF(summary.upperWhisker, 0.0)).x();
        painter.drawLine(QPointF(box.left(), centerY), QPointF(lowerX, centerY));
        painter.drawLine(QPointF(box.right(), centerY), QPointF(upperX, centerY));

        const qreal capHalf = box.height() * 0.25;
        painter.drawLine(QPointF(lowerX, centerY - capHalf),
                         QPointF(lowerX, centerY + capHalf));
        painter.drawLine(QPointF(upperX, centerY - capHalf),
                         QPointF(upperX, centerY + capHalf));
    } else {
        const qreal centerX = mapper.toPixel(QPointF(category, 0.0)).x();
        const qreal lowerY = mapper.toPixel(QPointF(0.0, summary.lowerWhisker)).y();
        const qreal upperY = mapper.toPixel(QPointF(0.0, summary.upperWhisker)).y();
        painter.drawLine(QPointF(centerX, box.bottom()), QPointF(centerX, lowerY));
        painter.drawLine(QPointF(centerX, box.top()), QPointF(centerX, upperY));

        const qreal capHalf = box.width() * 0.25;
        painter.drawLine(QPointF(centerX - capHalf, lowerY),
                         QPointF(centerX + capHalf, lowerY));
        painter.drawLine(QPointF(centerX - capHalf, upperY),
                         QPointF(centerX + capHalf, upperY));
    }

    QColor fill = colour;
    fill.setAlphaF(hovered ? kHoverFillAlpha : kBoxFillAlpha);
    QPen outline(colour, hovered ? boxPen * 1.6 : boxPen);
    outline.setJoinStyle(Qt::MiterJoin);
    painter.setPen(outline);
    painter.setBrush(fill);
    painter.drawRect(box);

    QPen medianStyle(colour, medianPen);
    medianStyle.setCapStyle(Qt::FlatCap);
    painter.setPen(medianStyle);
    if (horizontal) {
        const qreal medianX = mapper.toPixel(QPointF(summary.median, 0.0)).x();
        painter.drawLine(QPointF(medianX, box.top()), QPointF(medianX, box.bottom()));
    } else {
        const qreal medianY = mapper.toPixel(QPointF(0.0, summary.median)).y();
        painter.drawLine(QPointF(box.left(), medianY), QPointF(box.right(), medianY));
    }

    if (!showOutliers)
        return;

    const qreal radius = std::max(kMinimumRadius, density.dp(kOutlierRadiusDp));
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    for (const qreal value : summary.outliers) {
        const QPointF point = horizontal
            ? QPointF(mapper.toPixel(QPointF(value, 0.0)).x(),
                      mapper.toPixel(QPointF(0.0, category)).y())
            : QPointF(mapper.toPixel(QPointF(category, 0.0)).x(),
                      mapper.toPixel(QPointF(0.0, value)).y());
        if (std::isfinite(point.x()) && std::isfinite(point.y()))
            painter.drawEllipse(point, radius, radius);
    }
}

} // namespace

BoxPlotChart::BoxPlotChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);
    // Hover is the only pointer interaction and must work without a button.
    setMouseTracking(true);
    // The legend is left at ChartBase's default: one entry per named series
    // maps a box colour to its category name. A caller can still hide it with
    // setLegendVisible(false); this component never draws its own legend.

    // Read-only data surface: a whole-surface hover tint would misrepresent the
    // painted colours; per-chart hover is painted by the chart itself.
    setInteractionFeedbackEnabled(false);
}

void BoxPlotChart::setSeries(QVector<ChartSeries> seriesList)
{
    ChartBase::setSeries(std::move(seriesList));
    hoveredBox_ = -1;
    // setSeries() may have started a fresh auto-fit to the raw point bounds;
    // the category/value domain must win even if its numbers are unchanged.
    boundsFed_ = false;
    refreshBounds();
    requestUpdate();
}

void BoxPlotChart::setHorizontal(bool horizontal)
{
    if (horizontal_ == horizontal)
        return;

    horizontal_ = horizontal;
    hoveredBox_ = -1;
    refreshBounds();
    requestUpdate();
}

bool BoxPlotChart::isHorizontal() const noexcept
{
    return horizontal_;
}

void BoxPlotChart::setShowOutliers(bool showOutliers)
{
    if (showOutliers_ == showOutliers)
        return;

    showOutliers_ = showOutliers;
    requestUpdate();
}

bool BoxPlotChart::showsOutliers() const noexcept
{
    return showOutliers_;
}

void BoxPlotChart::setBoxWidthRatio(qreal ratio)
{
    const qreal normalized = std::isfinite(ratio) ? std::clamp(ratio, 0.1, 0.9) : 0.6;
    if (boxWidthRatio_ == normalized)
        return;

    boxWidthRatio_ = normalized;
    requestUpdate();
}

qreal BoxPlotChart::boxWidthRatio() const noexcept
{
    return boxWidthRatio_;
}

QVector<BoxPlotChart::BoxSummary> BoxPlotChart::summaries() const
{
    QVector<BoxSummary> resolved;
    resolved.reserve(series().size());

    for (const ChartSeries& item : series()) {
        QVector<qreal> values;
        values.reserve(item.points.size());
        for (const QPointF& point : item.points) {
            if (std::isfinite(point.y()))
                values.append(point.y());
        }
        resolved.append(summarise(values));
    }

    return resolved;
}

BoxPlotChart::BoxSummary BoxPlotChart::summaryAt(int index) const
{
    const QVector<BoxSummary> all = summaries();
    if (index < 0 || static_cast<qsizetype>(index) >= all.size())
        return {};

    return all.at(index);
}

int BoxPlotChart::boxAt(QPointF pixel)
{
    try {
        const QVector<BoxSummary> all = summaries();
        if (all.isEmpty() || !ensureBounds(all))
            return -1;

        const QRectF plot = plotRect();
        if (plot.isEmpty())
            return -1;

        const qreal padding = Theme::forWidget(this).density().dp(kHoverPaddingDp);
        if (!plot.adjusted(-padding, -padding, padding, padding).contains(pixel))
            return -1;

        const int count = static_cast<int>(all.size());
        for (int index = 0; index < count; ++index) {
            if (!all.at(index).valid)
                continue;

            const QRectF box = boxRectForSummary(all.at(index), index, count, plot);
            if (box.adjusted(-padding, -padding, padding, padding).contains(pixel))
                return index;
        }
    } catch (const std::exception& error) {
        qWarning("md::BoxPlotChart::boxAt failed: %s", error.what());
    } catch (...) {
        qWarning("md::BoxPlotChart::boxAt failed with an unknown exception");
    }

    return -1;
}

QRectF BoxPlotChart::boxRectAt(int index)
{
    const QVector<BoxSummary> all = summaries();
    if (index < 0 || static_cast<qsizetype>(index) >= all.size() || !all.at(index).valid)
        return {};

    if (!ensureBounds(all))
        return {};

    const QRectF plot = plotRect();
    if (plot.isEmpty())
        return {};

    return boxRectForSummary(all.at(index), index, static_cast<int>(all.size()), plot);
}

int BoxPlotChart::hoveredBox() const noexcept
{
    return hoveredBox_;
}

bool BoxPlotChart::ensureBounds(const QVector<BoxSummary>& all)
{
    qreal minimum = 0.0;
    qreal maximum = 0.0;
    bool hasData = false;

    for (const BoxSummary& summary : all) {
        if (!summary.valid)
            continue;

        if (!hasData) {
            minimum = summary.minimum;
            maximum = summary.maximum;
            hasData = true;
        } else {
            minimum = std::min(minimum, summary.minimum);
            maximum = std::max(maximum, summary.maximum);
        }
    }

    if (!hasData)
        return false;

    qreal span = maximum - minimum;
    if (!std::isfinite(span) || !(span > 0.0))
        span = kDegenerateSpan;

    qreal padding = span * kValuePaddingRatio;
    if (!std::isfinite(padding) || !(padding > 0.0))
        padding = kDegenerateSpan;

    qreal lower = minimum - padding;
    qreal upper = maximum + padding;
    if (!std::isfinite(lower) || !std::isfinite(upper) || !(upper > lower)) {
        lower = minimum;
        upper = maximum;
    }
    if (!(upper > lower))
        upper = lower + kDegenerateSpan;

    const int count = static_cast<int>(all.size());
    const qreal categoryMinimum = -0.5;
    const qreal categoryMaximum = static_cast<qreal>(count) - 0.5;

    const QPointF boundsMinimum = horizontal_
        ? QPointF(lower, categoryMinimum)
        : QPointF(categoryMinimum, lower);
    const QPointF boundsMaximum = horizontal_
        ? QPointF(upper, categoryMaximum)
        : QPointF(categoryMaximum, upper);

    syncTransform(boundsMinimum, boundsMaximum);
    return true;
}

qreal BoxPlotChart::categoryCoordinate(int index, int count) const noexcept
{
    return horizontal_
        ? static_cast<qreal>(count - 1 - index)
        : static_cast<qreal>(index);
}

QRectF BoxPlotChart::boxRectForSummary(const BoxSummary& summary, int index, int count,
                                       const QRectF& plot)
{
    if (count <= 0 || !summary.valid)
        return {};

    const qreal slot = horizontal_
        ? plot.height() / static_cast<qreal>(count)
        : plot.width() / static_cast<qreal>(count);
    if (!(slot > 0.0) || !std::isfinite(slot))
        return {};

    const qreal thickness = std::max<qreal>(1.0, slot * boxWidthRatio_);
    const PixelMapper mapper(math(), plot, boundsMin_, boundsMax_);

    if (horizontal_) {
        const qreal centerY = mapper.toPixel(
            QPointF(0.0, categoryCoordinate(index, count))).y();
        const qreal left = mapper.toPixel(QPointF(summary.q1, 0.0)).x();
        const qreal right = mapper.toPixel(QPointF(summary.q3, 0.0)).x();
        if (!std::isfinite(centerY) || !std::isfinite(left) || !std::isfinite(right))
            return {};
        return QRectF(std::min(left, right), centerY - thickness / 2.0,
                      std::max<qreal>(1.0, std::abs(right - left)), thickness);
    }

    const qreal centerX = mapper.toPixel(QPointF(categoryCoordinate(index, count), 0.0)).x();
    const qreal top = mapper.toPixel(QPointF(0.0, summary.q3)).y();
    const qreal bottom = mapper.toPixel(QPointF(0.0, summary.q1)).y();
    if (!std::isfinite(centerX) || !std::isfinite(top) || !std::isfinite(bottom))
        return {};
    return QRectF(centerX - thickness / 2.0, std::min(top, bottom), thickness,
                  std::max<qreal>(1.0, std::abs(top - bottom)));
}

void BoxPlotChart::refreshBounds()
{
    const QVector<BoxSummary> all = summaries();
    if (!all.isEmpty())
        ensureBounds(all);
}

void BoxPlotChart::syncTransform(const QPointF& minimum, const QPointF& maximum)
{
    if (boundsFed_ && minimum == boundsMin_ && maximum == boundsMax_)
        return;

    boundsMin_ = minimum;
    boundsMax_ = maximum;
    boundsFed_ = true;

    if (MathematicsCoordination* coordination = math()) {
        // setVisibleBounds() turns auto-fit off and pins the transform to the
        // rectangle used by boxRectAt().
        coordination->setVisibleBounds(minimum, maximum);
    }
}

void BoxPlotChart::setHoveredBox(int index)
{
    if (hoveredBox_ == index)
        return;

    hoveredBox_ = index;
    requestUpdate();
}

void BoxPlotChart::measureOverride(MeasureSpec width, MeasureSpec height)
{
    try {
        const Density& density = Theme::forWidget(this).density();
        setMeasuredSize(QSizeF(width.resolve(density.dp(kPreferredWidthDp)),
                               height.resolve(density.dp(kPreferredHeightDp))));
    } catch (const std::exception& error) {
        qWarning("md::BoxPlotChart::measureOverride failed: %s", error.what());
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    } catch (...) {
        qWarning("md::BoxPlotChart::measureOverride failed with an unknown exception");
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    }
}

void BoxPlotChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    if (plot.isEmpty())
        return;

    painter.save();
    try {
        const Theme theme = Theme::forWidget(this);
        const QVector<BoxSummary> all = summaries();

        bool hasBox = false;
        for (const BoxSummary& summary : all) {
            if (summary.valid) {
                hasBox = true;
                break;
            }
        }

        if (!hasBox || !ensureBounds(all)) {
            painter.restore();
            return;
        }

        const int count = static_cast<int>(all.size());
        const PixelMapper mapper(math(), plot, boundsMin_, boundsMax_);
        const qreal clipMargin = theme.density().dp(kOutlierRadiusDp + 4.0);

        painter.setClipRect(plot.adjusted(-clipMargin, -clipMargin, clipMargin, clipMargin));
        painter.setRenderHint(QPainter::Antialiasing, true);

        for (int index = 0; index < count; ++index) {
            const BoxSummary& summary = all.at(index);
            if (!summary.valid)
                continue;

            const QRectF box = boxRectForSummary(summary, index, count, plot);
            if (box.isEmpty())
                continue;

            QColor colour = seriesColour(index);
            if (!colour.isValid())
                colour = theme.colours().primary;

            drawBox(painter, box, summary, index, count, mapper, horizontal_,
                    showOutliers_, hoveredBox_ == index, colour, theme);
        }
    } catch (const std::exception& error) {
        qWarning("md::BoxPlotChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::BoxPlotChart::paintPlot failed with an unknown exception");
    }
    painter.restore();
}

void BoxPlotChart::mouseMoveEvent(QMouseEvent* event)
{
    try {
        View::mouseMoveEvent(event);
        setHoveredBox(boxAt(event->position()));
    } catch (const std::exception& error) {
        qWarning("md::BoxPlotChart::mouseMoveEvent failed: %s", error.what());
        hoveredBox_ = -1;
        update();
    } catch (...) {
        qWarning("md::BoxPlotChart::mouseMoveEvent failed with an unknown exception");
        hoveredBox_ = -1;
        update();
    }
}

void BoxPlotChart::leaveEvent(QEvent* event)
{
    try {
        View::leaveEvent(event);
        setHoveredBox(-1);
    } catch (const std::exception& error) {
        qWarning("md::BoxPlotChart::leaveEvent failed: %s", error.what());
        hoveredBox_ = -1;
        update();
    } catch (...) {
        qWarning("md::BoxPlotChart::leaveEvent failed with an unknown exception");
        hoveredBox_ = -1;
        update();
    }
}

} // namespace md
