// Periodicity — the BarChart component.
//
// ChartBase owns the furniture: the title, the legend, the grid, the axes, the
// tick marks and the tick labels are painted by the base before paintPlot() is
// called, and the base paints the empty state instead of paintPlot() when
// hasData() is false. This file therefore paints only bars, the selection ring
// and the optional value labels; it deliberately paints no axis of its own.
//
// The one thing the base cannot know is a bar chart's domain. Bars grow from
// zero, so setSeries(), setStacked() and setOrientation() snap the value range
// outwards to whole nice ticks that contain zero and install it on math() with
// auto-fit off. The base then paints the frame from that exact range, and the
// bars, the gridlines and the tick labels share one transform. The category axis
// is given the slot indices 0..categoryCount as its data range, so the tick
// labels along that axis name category boundaries rather than a meaningless
// fallback span.
//
// Geometry is computed, not cached: barRectangle() derives a bar from the
// category slot, the grouping mode and the value axis, and barRect() rebuilds
// the same rectangle from plotRect() with the linear form of the transform. The
// paint pass uses the coordination, which is installed with exactly the same
// range, so the two agree.
//
// Every override that runs component code catches, and every colour is a theme
// role or a series/palette colour: no colour is hard-coded and no pixel size
// assumes a particular display scale.

#include <md/components/bar_chart.h>

#include <md/components/math_coordination.h>
#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QSizeF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace md {
namespace {

constexpr qreal kPreferredWidthDp = 380.0;
constexpr qreal kPreferredHeightDp = 240.0;

constexpr int kTickCount = 5;
constexpr qreal kValueLabelPaddingDp = 6.0;
constexpr qreal kMinimumBarExtent = 0.5;
constexpr qreal kSelectionPenDp = 1.5;

[[nodiscard]] QFont fontFor(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(QFont::Weight(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

// Picks whichever of the surface/on-surface pair contrasts more with `fill`.
// The pair is a contrast pair in both brightnesses, so the rule works for a
// light and a dark theme without hard-coding white or black.
[[nodiscard]] QColor readableOn(const QColor& fill, const ColorScheme& colours)
{
    const qreal fillLightness = fill.lightnessF();
    const qreal againstSurface = std::abs(fillLightness - colours.surface.lightnessF());
    const qreal againstOnSurface = std::abs(fillLightness - colours.onSurface.lightnessF());
    return againstSurface >= againstOnSurface ? colours.surface : colours.onSurface;
}

// A compact value label: one decimal only while the number is small, and no
// trailing ".0", so an integer axis does not read as a row of zeroes.
[[nodiscard]] QString barValueLabel(qreal value)
{
    QString text = QString::number(value, 'f', 1);
    if (text.endsWith(QStringLiteral(".0")))
        text.chop(2);
    return text;
}

// A category's own colour when it has one, otherwise the palette entry for the
// bar position, so the same position in every group shares a colour.
[[nodiscard]] QColor barColour(const ChartSeries& category, const QVector<QColor>& palette,
                               qsizetype pointIndex, const Theme& theme)
{
    if (category.colour.isValid())
        return category.colour;

    if (!palette.isEmpty()) {
        const QColor colour = palette.at(pointIndex % palette.size());
        if (colour.isValid())
            return colour;
    }

    return theme.colours().primary;
}

// The value → pixel transform for one axis range. The coordination is the
// authoritative transform, installed with exactly this range by
// feedMathematics(); the linear branch exists for a chart without a
// coordination and for the frame in which it is still animating elsewhere.
struct ValueAxis
{
    MathematicsCoordination* coordination = nullptr;
    QRectF plot;
    qreal minimum = 0.0;
    qreal maximum = 1.0;
    bool horizontal = false;

    [[nodiscard]] qreal toPixel(qreal value) const
    {
        const qreal span = maximum - minimum;
        if (coordination != nullptr && span > 0.0) {
            const QRectF visible = coordination->visibleBounds();
            const qreal tolerance = std::max<qreal>(1e-9, std::abs(span) * 1e-6);
            const qreal visibleMinimum = horizontal ? visible.left() : visible.top();
            const qreal visibleMaximum = horizontal ? visible.right() : visible.bottom();
            if (std::abs(visibleMinimum - minimum) <= tolerance
                && std::abs(visibleMaximum - maximum) <= tolerance) {
                const QPointF mapped = horizontal ? coordination->toPixel(QPointF(value, 0.0))
                                                  : coordination->toPixel(QPointF(0.0, value));
                const qreal result = horizontal ? mapped.x() : mapped.y();
                if (std::isfinite(result))
                    return result;
            }
        }

        if (!(span > 0.0))
            return horizontal ? plot.left() : plot.bottom();

        const qreal ratio = (value - minimum) / span;
        return horizontal ? plot.left() + ratio * plot.width()
                          : plot.bottom() - ratio * plot.height();
    }
};

// Everything a bar rectangle needs that is not data: the plot rectangle, the
// orientation, the grouping mode and the category count.
struct BarLayout
{
    QRectF plot;
    bool horizontal = false;
    bool stacked = false;
    qreal groupGap = 0.25;
    qreal minimum = 0.0;
    qreal maximum = 1.0;
    bool mirror = false;
    int categories = 0;
};

// The rectangle of bar `pointIndex` inside `categoryIndex`. The category axis
// contributes the slot and the thickness; the value axis contributes the extent
// from the zero line (or from the accumulated stack) to the value.
[[nodiscard]] QRectF barRectangle(const BarLayout& layout, int categoryIndex,
                                  const ChartSeries& category, int pointIndex,
                                  const ValueAxis& axis)
{
    const int bars = static_cast<int>(category.points.size());
    if (categoryIndex < 0 || pointIndex < 0 || pointIndex >= bars || layout.categories <= 0)
        return {};

    const qreal value = category.points.at(static_cast<qsizetype>(pointIndex)).y();
    if (!std::isfinite(value))
        return {};

    const qreal length = layout.horizontal ? layout.plot.height() : layout.plot.width();
    if (!(length > 0.0))
        return {};

    const qreal slot = length / static_cast<qreal>(layout.categories);
    const qreal gap = std::clamp(layout.groupGap, 0.0, 0.9);
    const qreal inner = slot * (1.0 - gap);

    const qreal nearEdge = layout.horizontal ? layout.plot.top() : layout.plot.left();
    const qreal farEdge = layout.horizontal ? layout.plot.top() + length : layout.plot.left() + length;

    const qreal slotStart = layout.mirror
        ? farEdge - static_cast<qreal>(categoryIndex + 1) * slot
        : nearEdge + static_cast<qreal>(categoryIndex) * slot;
    const qreal innerStart = slotStart + (slot - inner) / 2.0;

    const qreal thickness = layout.stacked ? inner : inner / static_cast<qreal>(bars);
    qreal categoryStart = innerStart;
    if (!layout.stacked) {
        const int barIndex = layout.mirror ? bars - 1 - pointIndex : pointIndex;
        categoryStart = innerStart + static_cast<qreal>(barIndex) * thickness;
    }

    // Stacked bars grow away from the zero line, each sign on its own side.
    qreal from = 0.0;
    qreal to = value;
    if (layout.stacked) {
        qreal positive = 0.0;
        qreal negative = 0.0;
        for (int index = 0; index < pointIndex; ++index) {
            const qreal previous = category.points.at(static_cast<qsizetype>(index)).y();
            if (!std::isfinite(previous))
                continue;
            if (previous >= 0.0)
                positive += previous;
            else
                negative += previous;
        }
        if (to >= 0.0) {
            from = positive;
            to = positive + to;
        } else {
            from = negative;
            to = negative + to;
        }
    }

    const qreal fromPixel = axis.toPixel(from);
    const qreal toPixel = axis.toPixel(to);
    const qreal low = std::min(fromPixel, toPixel);
    const qreal high = std::max(fromPixel, toPixel);

    if (layout.horizontal)
        return QRectF(low, categoryStart, high - low, thickness);
    return QRectF(categoryStart, low, thickness, high - low);
}

// The step of the nice tick ladder for a range, taken from niceTicks() so the
// chart uses exactly the repository's tick algorithm. The step then snaps the
// axis outwards to whole ticks.
[[nodiscard]] qreal niceAxisStep(qreal minimum, qreal maximum, int count)
{
    const QVector<qreal> ticks = MathematicsCoordination::niceTicks(minimum, maximum, count);
    qreal step = 0.0;
    if (ticks.size() >= 2)
        step = std::abs(ticks.at(1) - ticks.at(0));

    if (!(step > 0.0) || !std::isfinite(step)) {
        const qreal span = std::abs(maximum - minimum);
        step = span > 0.0 ? span / static_cast<qreal>(std::max(1, count - 1)) : 1.0;
    }
    return step;
}

} // namespace

BarChart::BarChart(QWidget* parent)
    : ChartBase(parent)
{
    // A data surface, not a control: no keyboard focus, and the shared state and
    // ripple layers would wash over the bars. Clicks are still delivered to the
    // overrides below.
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(false);
    setInteractionFeedbackEnabled(false);
}

void BarChart::setSeries(QVector<ChartSeries> series)
{
    ChartBase::setSeries(std::move(series));

    // The stored pair described a bar that may not exist any more.
    selectedSeries_ = -1;
    selectedPoint_ = -1;
    pressedSeries_ = -1;
    pressedPoint_ = -1;

    // The value range depends on the data; install it before the next frame so
    // the base paints the frame from the same transform as the bars.
    fed_ = false;
    recomputeAxis();
    feedMathematics();
    requestUpdate();
}

void BarChart::setOrientation(Orientation orientation)
{
    if (orientation_ == orientation)
        return;

    orientation_ = orientation;
    // The value axis changes sides: swap the roles in the installed bounds.
    fed_ = false;
    feedMathematics();
    requestUpdate();
}

BarChart::Orientation BarChart::orientation() const noexcept
{
    return orientation_;
}

void BarChart::setStacked(bool stacked)
{
    if (stacked_ == stacked)
        return;

    stacked_ = stacked;
    recomputeAxis();
    fed_ = false;
    feedMathematics();
    requestUpdate();
}

bool BarChart::isStacked() const noexcept
{
    return stacked_;
}

void BarChart::setShowValues(bool show)
{
    if (showValues_ == show)
        return;

    showValues_ = show;
    requestUpdate();
}

bool BarChart::showsValues() const noexcept
{
    return showValues_;
}

void BarChart::setGroupGap(qreal ratio)
{
    if (!std::isfinite(ratio))
        return;

    const qreal clamped = std::clamp(ratio, 0.0, 0.9);
    if (groupGap_ == clamped)
        return;

    groupGap_ = clamped;
    requestUpdate();
}

qreal BarChart::groupGap() const noexcept
{
    return groupGap_;
}

void BarChart::setBarRadius(qreal dp)
{
    if (!std::isfinite(dp))
        return;

    const qreal clamped = std::max<qreal>(0.0, dp);
    if (barRadiusDp_ == clamped)
        return;

    barRadiusDp_ = clamped;
    requestUpdate();
}

qreal BarChart::barRadius() const noexcept
{
    return barRadiusDp_;
}

void BarChart::setSelectedBar(int seriesIndex, int pointIndex)
{
    const int selectedCategory = seriesIndex >= 0 ? seriesIndex : -1;
    const int selectedBar = selectedCategory >= 0 && pointIndex >= 0 ? pointIndex : -1;
    if (selectedSeries_ == selectedCategory && selectedPoint_ == selectedBar)
        return;

    selectedSeries_ = selectedCategory;
    selectedPoint_ = selectedBar;
    requestUpdate();
}

int BarChart::selectedSeries() const noexcept
{
    return selectedSeries_;
}

int BarChart::selectedPoint() const noexcept
{
    return selectedPoint_;
}

bool BarChart::hasBarData() const noexcept
{
    return hasData();
}

int BarChart::categoryCount() const noexcept
{
    return static_cast<int>(series().size());
}

bool BarChart::rightToLeft() const
{
    return layoutDirection() == Qt::RightToLeft;
}

bool BarChart::mirrorCategories() const
{
    return orientation_ == Orientation::Vertical && rightToLeft();
}

QRectF BarChart::plotOrContentRect() const
{
    QRectF plot = plotRect();
    if (!plot.isValid())
        plot = contentRect();
    return plot;
}

void BarChart::recomputeAxis()
{
    if (!hasData()) {
        axisMinimum_ = 0.0;
        axisMaximum_ = 1.0;
        return;
    }

    qreal minimum = 0.0;
    qreal maximum = 0.0;
    bool hasValues = false;

    const QVector<ChartSeries>& categories = series();
    if (stacked_) {
        for (const ChartSeries& category : categories) {
            qreal positive = 0.0;
            qreal negative = 0.0;
            bool any = false;
            for (const QPointF& point : category.points) {
                const qreal value = point.y();
                if (!std::isfinite(value))
                    continue;
                any = true;
                if (value >= 0.0)
                    positive += value;
                else
                    negative += value;
            }
            if (!any)
                continue;

            hasValues = true;
            minimum = std::min(minimum, negative);
            maximum = std::max(maximum, positive);
        }
    } else {
        for (const ChartSeries& category : categories) {
            for (const QPointF& point : category.points) {
                const qreal value = point.y();
                if (!std::isfinite(value))
                    continue;
                hasValues = true;
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
        }
    }

    if (!hasValues) {
        axisMinimum_ = 0.0;
        axisMaximum_ = 1.0;
        return;
    }

    // Zero is always on the axis, so a bar's sign is readable from its bar alone.
    minimum = std::min(minimum, 0.0);
    maximum = std::max(maximum, 0.0);
    if (!(maximum > minimum))
        maximum = minimum + 1.0;

    const qreal step = niceAxisStep(minimum, maximum, kTickCount);
    axisMinimum_ = std::floor(minimum / step) * step;
    axisMaximum_ = std::ceil(maximum / step) * step;
    if (!(axisMaximum_ > axisMinimum_))
        axisMaximum_ = axisMinimum_ + step;
}

void BarChart::feedMathematics()
{
    MathematicsCoordination* coordination = math();
    if (coordination == nullptr || !hasData())
        return;

    // The category axis carries slot indices, not data values, so its ticks name
    // category boundaries; the value axis carries the snapped bar range. The base
    // installs the plot area on every paint, so only the bounds are installed
    // here.
    const bool horizontal = orientation_ == Orientation::Horizontal;
    const qreal categories = static_cast<qreal>(std::max(1, categoryCount()));
    const QPointF minimumPoint = horizontal ? QPointF(axisMinimum_, 0.0)
                                            : QPointF(0.0, axisMinimum_);
    const QPointF maximumPoint = horizontal ? QPointF(axisMaximum_, categories)
                                            : QPointF(categories, axisMaximum_);
    if (fed_ && fedMinimum_ == minimumPoint && fedMaximum_ == maximumPoint)
        return;

    // The bar chart owns its range: auto-fit would choose a different window and
    // animate the bars away from the frame the base just painted.
    // setVisibleBounds() installs the exact range and stops any fit in flight.
    coordination->setAutoFit(false);
    coordination->setDataBounds(minimumPoint, maximumPoint);
    coordination->setVisibleBounds(minimumPoint, maximumPoint);

    fedMinimum_ = minimumPoint;
    fedMaximum_ = maximumPoint;
    fed_ = true;
}

QRectF BarChart::barRect(int seriesIndex, int pointIndex) const
{
    const QVector<ChartSeries>& categories = series();
    if (seriesIndex < 0 || static_cast<qsizetype>(seriesIndex) >= categories.size())
        return {};

    const ChartSeries& category = categories.at(static_cast<qsizetype>(seriesIndex));
    if (pointIndex < 0 || static_cast<qsizetype>(pointIndex) >= category.points.size())
        return {};

    const QRectF plot = plotOrContentRect();
    if (!plot.isValid())
        return {};

    const ValueAxis axis{nullptr, plot, axisMinimum_, axisMaximum_,
                         orientation_ == Orientation::Horizontal};
    const BarLayout layout{plot, axis.horizontal, stacked_, groupGap_, axisMinimum_,
                           axisMaximum_, mirrorCategories(), static_cast<int>(categories.size())};
    return barRectangle(layout, seriesIndex, category, pointIndex, axis);
}

bool BarChart::barAt(QPointF pixel, int* seriesIndex, int* pointIndex) const
{
    const QVector<ChartSeries>& categories = series();
    for (qsizetype categoryIndex = 0; categoryIndex < categories.size(); ++categoryIndex) {
        const ChartSeries& category = categories.at(categoryIndex);
        for (qsizetype barIndex = 0; barIndex < category.points.size(); ++barIndex) {
            const QRectF rect = barRect(static_cast<int>(categoryIndex),
                                        static_cast<int>(barIndex));
            if (!rect.isValid() || !rect.contains(pixel))
                continue;

            if (seriesIndex != nullptr)
                *seriesIndex = static_cast<int>(categoryIndex);
            if (pointIndex != nullptr)
                *pointIndex = static_cast<int>(barIndex);
            return true;
        }
    }
    return false;
}

void BarChart::measureOverride(MeasureSpec width, MeasureSpec height)
{
    try {
        const Density& density = Theme::forWidget(this).density();
        setMeasuredSize(QSizeF(width.resolve(density.dp(kPreferredWidthDp)),
                               height.resolve(density.dp(kPreferredHeightDp))));
    } catch (const std::exception& error) {
        qWarning("md::BarChart::measureOverride failed: %s", error.what());
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    } catch (...) {
        qWarning("md::BarChart::measureOverride failed with an unknown exception");
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    }
}

void BarChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    if (plot.isEmpty())
        return;

    painter.save();
    try {
        const Theme theme = Theme::forWidget(this);
        const Density& density = theme.density();

        const QVector<ChartSeries>& categories = series();
        const ValueAxis axis{math(), plot, axisMinimum_, axisMaximum_,
                             orientation_ == Orientation::Horizontal};
        const BarLayout layout{plot, axis.horizontal, stacked_, groupGap_, axisMinimum_,
                               axisMaximum_, mirrorCategories(),
                               static_cast<int>(categories.size())};

        const QVector<QColor> palette = chartPalette();
        const QFont valueFont = fontFor(theme, theme.type().labelSmall);
        const QFontMetricsF valueMetrics(valueFont);
        const qreal radiusLimit = std::max<qreal>(0.0, density.dp(barRadiusDp_));
        const qreal penWidth = std::max<qreal>(1.0, density.dp(kSelectionPenDp));
        const qreal padding = density.dp(kValueLabelPaddingDp);

        painter.save();
        painter.setClipRect(plot);
        painter.setRenderHint(QPainter::Antialiasing, true);

        for (qsizetype categoryIndex = 0; categoryIndex < categories.size(); ++categoryIndex) {
            const ChartSeries& category = categories.at(categoryIndex);
            for (qsizetype pointIndex = 0; pointIndex < category.points.size(); ++pointIndex) {
                const qreal value = category.points.at(pointIndex).y();
                if (!std::isfinite(value))
                    continue;

                const QRectF rect = barRectangle(layout, static_cast<int>(categoryIndex),
                                                 category, static_cast<int>(pointIndex), axis);
                if (!rect.isValid() || rect.width() <= kMinimumBarExtent
                    || rect.height() <= kMinimumBarExtent)
                    continue;

                const QColor colour = barColour(category, palette, pointIndex, theme);
                const qreal radius = std::min(radiusLimit,
                                              std::min(rect.width(), rect.height()) / 2.0);

                painter.setPen(Qt::NoPen);
                painter.setBrush(colour);
                if (radius > 0.01)
                    painter.drawRoundedRect(rect, radius, radius);
                else
                    painter.drawRect(rect);

                const bool selected = static_cast<int>(categoryIndex) == selectedSeries_
                    && static_cast<int>(pointIndex) == selectedPoint_;
                if (selected && rect.width() > penWidth * 2.0
                    && rect.height() > penWidth * 2.0) {
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(theme.colours().onSurface, penWidth));
                    painter.drawRoundedRect(rect.adjusted(penWidth / 2.0, penWidth / 2.0,
                                                          -penWidth / 2.0, -penWidth / 2.0),
                                            radius, radius);
                }

                if (!showValues_)
                    continue;

                const QString text = barValueLabel(value);
                const qreal textWidth = valueMetrics.horizontalAdvance(text);
                const qreal textHeight = valueMetrics.height();

                if (axis.horizontal) {
                    if (rect.height() < textHeight || rect.width() < textWidth + padding)
                        continue;

                    painter.setFont(valueFont);
                    painter.setPen(readableOn(colour, theme.colours()));
                    const bool positive = value >= 0.0;
                    painter.drawText(rect.adjusted(padding / 2.0, 0.0, -padding / 2.0, 0.0),
                                     (positive ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                                     text);
                } else {
                    if (rect.width() < textWidth + padding || rect.height() < textHeight)
                        continue;

                    painter.setFont(valueFont);
                    painter.setPen(readableOn(colour, theme.colours()));
                    const bool positive = value >= 0.0;
                    painter.drawText(rect.adjusted(0.0, padding / 2.0, 0.0, -padding / 2.0),
                                     Qt::AlignHCenter | (positive ? Qt::AlignTop : Qt::AlignBottom),
                                     text);
                }
            }
        }

        painter.restore();
    } catch (const std::exception& error) {
        qWarning("md::BarChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::BarChart::paintPlot failed with an unknown exception");
    }
    painter.restore();
}

void BarChart::mousePressEvent(QMouseEvent* event)
{
    try {
        View::mousePressEvent(event);

        pressedSeries_ = -1;
        pressedPoint_ = -1;
        int series = -1;
        int point = -1;
        if (barAt(event->position(), &series, &point)) {
            pressedSeries_ = series;
            pressedPoint_ = point;
        }
    } catch (const std::exception& error) {
        qWarning("md::BarChart::mousePressEvent failed: %s", error.what());
        pressedSeries_ = -1;
        pressedPoint_ = -1;
    } catch (...) {
        qWarning("md::BarChart::mousePressEvent failed with an unknown exception");
        pressedSeries_ = -1;
        pressedPoint_ = -1;
    }
}

void BarChart::mouseReleaseEvent(QMouseEvent* event)
{
    try {
        View::mouseReleaseEvent(event);

        int series = -1;
        int point = -1;
        const bool hit = barAt(event->position(), &series, &point);
        const bool activated = hit && series == pressedSeries_ && point == pressedPoint_;

        pressedSeries_ = -1;
        pressedPoint_ = -1;

        if (activated) {
            setSelectedBar(series, point);
            emit barActivated(series, point);
        }
    } catch (const std::exception& error) {
        qWarning("md::BarChart::mouseReleaseEvent failed: %s", error.what());
        pressedSeries_ = -1;
        pressedPoint_ = -1;
    } catch (...) {
        qWarning("md::BarChart::mouseReleaseEvent failed with an unknown exception");
        pressedSeries_ = -1;
        pressedPoint_ = -1;
    }
}

} // namespace md
