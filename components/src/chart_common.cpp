// Periodicity — ChartBase implementation (framework.md §5.9, §7).
//
// The base paints the chart furniture and delegates the marks to paintPlot().
// The transform lives in an owned MathematicsCoordination widget. That widget
// is hidden — it is never part of the visual tree's paint pass — but it stays a
// real child so Theme::forWidget() resolves the same ThemeScope as the chart;
// the base installs its plot rectangle and calls its paintFrame() directly.
//
// Layout is computed, not cached: plotRect() measures the current theme's
// fonts, the title, the legend and the axis gutters every time it is asked.
// That is a handful of QFontMetricsF calls per paint and it keeps the geometry
// correct after a theme switch, a font-scale change or a resize without an
// invalidation protocol.

#include <md/components/chart_common.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QSizeF>
#include <QSizePolicy>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace md {

namespace {

constexpr qreal kChartMarginDp = 12.0;
constexpr qreal kChartGapDp = 6.0;

constexpr qreal kAxisTickLengthDp = 4.0;
constexpr qreal kAxisLabelGapDp = 4.0;
constexpr qreal kMinimumYAxisGutterDp = 32.0;
constexpr qreal kMinimumXAxisGutterDp = 20.0;
constexpr qreal kRightGutterDp = 8.0;

constexpr qreal kPreferredChartWidthDp = 360.0;
constexpr qreal kPreferredChartHeightDp = 260.0;

constexpr qreal kLegendSwatchDp = 8.0;
constexpr qreal kLegendGapDp = 6.0;
constexpr qreal kLegendEntryGapDp = 16.0;

constexpr qreal kHitSlopDp = 6.0;
constexpr qreal kMinimumHitSlopPx = 4.0;

// The step between two consecutive ticks, or `fallback` for a list that has
// fewer than two entries.
qreal tickStepOf(const QVector<qreal>& ticks, qreal fallback)
{
    if (ticks.size() >= 2) {
        const qreal step = ticks.at(1) - ticks.at(0);
        if (step > 0.0 && std::isfinite(step))
            return step;
    }

    return fallback > 0.0 && std::isfinite(fallback) ? fallback : 1.0;
}

} // namespace

QRectF ChartSeries::boundingRect() const
{
    bool found = false;
    qreal minimumX = 0.0;
    qreal minimumY = 0.0;
    qreal maximumX = 0.0;
    qreal maximumY = 0.0;

    for (const QPointF& point : points) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
            continue;

        if (!found) {
            minimumX = maximumX = point.x();
            minimumY = maximumY = point.y();
            found = true;
        } else {
            minimumX = std::min(minimumX, point.x());
            minimumY = std::min(minimumY, point.y());
            maximumX = std::max(maximumX, point.x());
            maximumY = std::max(maximumY, point.y());
        }
    }

    if (!found)
        return QRectF();

    return QRectF(QPointF(minimumX, minimumY), QPointF(maximumX, maximumY));
}

ChartBase::ChartBase(QWidget* parent)
    : View(parent)
    , math_(new MathematicsCoordination(this))
{
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // The transform is a composition helper, not a child surface: it must not
    // paint over the chart and must not intercept a mouse event. Hiding it also
    // keeps it out of Qt's child paint pass; the base calls paintFrame() with
    // its own painter instead.
    math_->hide();
    math_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    connect(math_, &MathematicsCoordination::transformChanged, this, [this] {
        requestUpdate();
    });
}

ChartBase::~ChartBase() = default;

void ChartBase::setTitle(const QString& text)
{
    if (title_ == text)
        return;

    title_ = text;
    requestUpdate();
}

QString ChartBase::title() const
{
    return title_;
}

void ChartBase::setLegendVisible(bool visible)
{
    if (legendVisible_ == visible)
        return;

    legendVisible_ = visible;
    requestUpdate();
}

bool ChartBase::isLegendVisible() const noexcept
{
    return legendVisible_;
}

void ChartBase::setAxesVisible(bool visible)
{
    if (axesVisible_ == visible)
        return;

    axesVisible_ = visible;
    requestUpdate();
}

bool ChartBase::isAxesVisible() const noexcept
{
    return axesVisible_;
}

void ChartBase::setSeries(QVector<ChartSeries> seriesList)
{
    series_ = std::move(seriesList);

    bool found = false;
    qreal minimumX = 0.0;
    qreal minimumY = 0.0;
    qreal maximumX = 0.0;
    qreal maximumY = 0.0;

    for (const ChartSeries& entry : series_) {
        for (const QPointF& point : entry.points) {
            if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
                continue;

            if (!found) {
                minimumX = maximumX = point.x();
                minimumY = maximumY = point.y();
                found = true;
            } else {
                minimumX = std::min(minimumX, point.x());
                minimumY = std::min(minimumY, point.y());
                maximumX = std::max(maximumX, point.x());
                maximumY = std::max(maximumY, point.y());
            }
        }
    }

    if (found)
        math_->setDataBounds(QPointF(minimumX, minimumY), QPointF(maximumX, maximumY));
    else
        math_->setDataBounds(QPointF(0.0, 0.0), QPointF(1.0, 1.0));

    requestUpdate();
}

const QVector<ChartSeries>& ChartBase::series() const noexcept
{
    return series_;
}

QVector<QColor> ChartBase::chartPalette() const
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();

    // A categorical palette is not an M3 token role set, so it is assembled
    // from the roles the scheme already defines: the four accents, the
    // high-contrast on-container roles (readable in both brightnesses) and the
    // outline/fixed-dim tones. The fixed roles are constant across light and
    // dark, which keeps late entries distinguishable in both schemes.
    const QColor roles[] = {
        colours.primary,
        colours.tertiary,
        colours.error,
        colours.secondary,
        colours.onPrimaryContainer,
        colours.onTertiaryContainer,
        colours.onSecondaryContainer,
        colours.outline,
        colours.primaryFixedDim,
        colours.tertiaryFixedDim,
        colours.secondaryFixedDim,
        colours.inversePrimary,
    };

    QVector<QColor> palette;
    palette.reserve(12);

    for (const QColor& role : roles) {
        if (role.isValid())
            palette.append(role);
    }

    // A ColorScheme is only required to carry a usable core palette, so a
    // partial theme can leave fewer than eight valid roles. Derive the missing
    // entries as tonal steps of primary; never fall back to a literal colour.
    const QColor base = colours.primary.isValid() ? colours.primary : colours.onSurface;

    if (base.isValid()) {
        int lighter = 120;
        while (palette.size() < 8 && lighter <= 300) {
            palette.append(base.lighter(lighter));
            lighter += 20;
        }

        int darker = 120;
        while (palette.size() < 8 && darker <= 300) {
            palette.append(base.darker(darker));
            darker += 20;
        }
    }

    // The loops above terminate, but keep the documented minimum an invariant
    // of the return value even for a pathological QColor.
    while (palette.size() < 8)
        palette.append(QColor(0, 0, 0));

    return palette;
}

QColor ChartBase::seriesColour(int index) const
{
    if (index < 0 || index >= series_.size())
        return QColor();

    const QColor explicitColour = series_.at(index).colour;
    if (explicitColour.isValid())
        return explicitColour;

    const QVector<QColor> palette = chartPalette();
    if (palette.isEmpty())
        return QColor();

    return palette.at(index % palette.size());
}

QRectF ChartBase::innerRect() const
{
    const QRectF content = contentRect();
    if (!content.isValid() || content.width() <= 0.0 || content.height() <= 0.0)
        return QRectF();

    const qreal margin = Theme::forWidget(this).density().dp(kChartMarginDp);
    const QRectF inner = content.adjusted(margin, margin, -margin, -margin);

    return inner.isValid() && inner.width() > 0.0 && inner.height() > 0.0
        ? inner
        : QRectF();
}

bool ChartBase::legendHasEntries() const
{
    for (const ChartSeries& entry : series_) {
        if (!entry.name.isEmpty())
            return true;
    }

    return false;
}

QRectF ChartBase::legendRect() const
{
    if (!legendVisible_ || !legendHasEntries())
        return QRectF();

    const QRectF inner = innerRect();
    if (inner.isEmpty())
        return QRectF();

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    qreal top = inner.top();

    if (!title_.isEmpty()) {
        const QFontMetricsF titleMetrics(
            Font::resolve(this, theme.type().titleMedium, density));
        top += titleMetrics.height() + density.dp(kChartGapDp);
    }

    const QFontMetricsF legendMetrics(
        Font::resolve(this, theme.type().labelSmall, density));
    const qreal height = legendMetrics.height();

    if (top + height > inner.bottom())
        return QRectF();

    return QRectF(inner.left(), top, inner.width(), height);
}

QRectF ChartBase::plotRect() const
{
    const QRectF inner = innerRect();
    if (inner.isEmpty()) {
        math_->setPlotArea(QRectF());
        return QRectF();
    }

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    qreal top = inner.top();

    if (!title_.isEmpty()) {
        const QFontMetricsF titleMetrics(
            Font::resolve(this, theme.type().titleMedium, density));
        top += titleMetrics.height() + density.dp(kChartGapDp);
    }

    if (const QRectF legend = legendRect(); !legend.isEmpty())
        top = legend.bottom() + density.dp(kChartGapDp);

    qreal leftGutter = 0.0;
    qreal bottomGutter = 0.0;
    qreal rightGutter = 0.0;

    if (axesVisible_) {
        const QFont tickFont = Font::resolve(this, theme.type().labelSmall, density);
        const QFontMetricsF tickMetrics(tickFont);
        const qreal tickLength = density.dp(kAxisTickLengthDp);
        const qreal labelGap = density.dp(kAxisLabelGapDp);

        // Measure the Y gutters from the fitted range: that is where a fit
        // animation is heading, and using it keeps the plot rectangle stable
        // while visibleBounds() interpolates.
        const QRectF fitted = math_->fittedBounds();
        const QVector<qreal> yValues = MathematicsCoordination::niceTicks(
            fitted.top(), fitted.bottom(), math_->tickCount());
        const qreal yStep = tickStepOf(yValues, fitted.height());

        qreal widestYLabel = 0.0;
        for (qreal value : yValues) {
            widestYLabel = std::max(
                widestYLabel,
                tickMetrics.horizontalAdvance(
                    MathematicsCoordination::tickLabel(value, yStep)));
        }

        leftGutter = std::max<qreal>(density.dp(kMinimumYAxisGutterDp),
                                     widestYLabel + tickLength + labelGap);
        bottomGutter = std::max<qreal>(density.dp(kMinimumXAxisGutterDp),
                                       tickMetrics.height() + tickLength + labelGap);

        if (!math_->yAxisLabel().isEmpty()) {
            const QFontMetricsF axisMetrics(
                Font::resolve(this, theme.type().labelMedium, density));
            leftGutter += axisMetrics.height() + labelGap;
        }

        if (!math_->xAxisLabel().isEmpty()) {
            const QFontMetricsF axisMetrics(
                Font::resolve(this, theme.type().labelMedium, density));
            bottomGutter += axisMetrics.height() + labelGap;
        }

        rightGutter = density.dp(kRightGutterDp);
    }

    const qreal plotLeft = inner.left() + leftGutter;
    const qreal plotTop = top;
    const qreal plotRight = inner.right() - rightGutter;
    const qreal plotBottom = inner.bottom() - bottomGutter;

    if (plotRight <= plotLeft || plotBottom <= plotTop) {
        math_->setPlotArea(QRectF());
        return QRectF();
    }

    const QRectF area(plotLeft, plotTop, plotRight - plotLeft, plotBottom - plotTop);

    // Computing the rectangle is also the moment its transform is installed.
    // A chart may therefore call math()->toPixel() from a mouse handler (or
    // from plotRect() itself) without waiting for a paint.
    math_->setPlotArea(area);

    return area;
}

void ChartBase::paintTitle(QPainter& painter)
{
    if (title_.isEmpty())
        return;

    const QRectF inner = innerRect();
    if (inner.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFont font = Font::resolve(this, theme.type().titleMedium, density);
    const QFontMetricsF metrics(font);

    painter.save();
    painter.setFont(font);
    painter.setPen(theme.colours().onSurface);

    const QString text = metrics.elidedText(title_, Qt::ElideRight, inner.width());
    const QRectF titleRect(inner.left(), inner.top(), inner.width(), metrics.height());
    painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, text);

    painter.restore();
}

void ChartBase::drawLegend(QPainter& painter, const QRectF& rect)
{
    if (!legendHasEntries() || rect.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFont font = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF metrics(font);
    const QVector<QColor> palette = chartPalette();

    const qreal swatch = density.dp(kLegendSwatchDp);
    const qreal gap = density.dp(kLegendGapDp);
    const qreal entryGap = density.dp(kLegendEntryGapDp);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setFont(font);

    qreal x = rect.left();

    for (int index = 0; index < series_.size(); ++index) {
        const ChartSeries& entry = series_.at(index);
        if (entry.name.isEmpty())
            continue;

        const QColor colour = entry.colour.isValid()
            ? entry.colour
            : (palette.isEmpty() ? theme.colours().primary
                                 : palette.at(index % palette.size()));
        const qreal textWidth = metrics.horizontalAdvance(entry.name);
        const qreal entryWidth = swatch + gap + textWidth;

        if (x + entryWidth > rect.right() + 0.5)
            break;

        const QRectF swatchRect(x, rect.center().y() - swatch * 0.5, swatch, swatch);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colour);
        painter.drawRoundedRect(swatchRect, swatch * 0.25, swatch * 0.25);

        painter.setPen(theme.colours().onSurfaceVariant);
        const QRectF textRect(x + swatch + gap, rect.top(), textWidth + 1.0, rect.height());
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, entry.name);

        x += entryWidth + entryGap;
    }

    painter.restore();
}

bool ChartBase::hasData() const noexcept
{
    for (const ChartSeries& entry : series_) {
        for (const QPointF& point : entry.points) {
            if (std::isfinite(point.x()) && std::isfinite(point.y()))
                return true;
        }
    }

    return false;
}

void ChartBase::paintEmptyState(QPainter& painter, const QRectF& plot)
{
    if (plot.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFont font = Font::resolve(this, theme.type().labelMedium, density);

    painter.save();
    painter.setFont(font);
    painter.setPen(theme.colours().onSurfaceVariant);
    painter.drawText(plot, Qt::AlignCenter,
                     localized(QStringLiteral("chart.noData"), QStringLiteral("No data")));
    painter.restore();
}

void ChartBase::measureOverride(MeasureSpec width, MeasureSpec height)
{
    try {
        const Density& density = Theme::forWidget(this).density();
        const qreal preferredWidth = density.dp(kPreferredChartWidthDp);
        const qreal preferredHeight = density.dp(kPreferredChartHeightDp);

        setMeasuredSize(QSizeF(width.resolve(preferredWidth), height.resolve(preferredHeight)));
    } catch (const std::exception& error) {
        qWarning("md::ChartBase::measureOverride failed: %s", error.what());
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    } catch (...) {
        qWarning("md::ChartBase::measureOverride failed with an unknown exception");
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    }
}

void ChartBase::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    painter.save();
    try {
        const QRectF plot = plotRect();

        paintTitle(painter);

        if (legendVisible_ && legendHasEntries()) {
            const QRectF legend = legendRect();
            if (!legend.isEmpty())
                drawLegend(painter, legend);
        }

        if (!plot.isEmpty()) {
            if (axesVisible_)
                math_->paintFrame(painter);

            if (hasData())
                paintPlot(painter, plot);
            else
                paintEmptyState(painter, plot);
        }
    } catch (const std::exception& error) {
        qWarning("md::ChartBase::paintContent failed: %s", error.what());
    } catch (...) {
        qWarning("md::ChartBase::paintContent failed with an unknown exception");
    }
    painter.restore();
}

bool ChartBase::hitTest(QPointF position, int* seriesIndex, int* pointIndex)
{
    if (seriesIndex != nullptr)
        *seriesIndex = -1;
    if (pointIndex != nullptr)
        *pointIndex = -1;

    if (!std::isfinite(position.x()) || !std::isfinite(position.y()))
        return false;

    const QRectF plot = plotRect();
    if (plot.isEmpty())
        return false;

    const Density& density = Theme::forWidget(this).density();
    const qreal slop = std::max<qreal>(kMinimumHitSlopPx, density.dp(kHitSlopDp));
    const qreal slopSquared = slop * slop;

    qreal nearest = slopSquared;
    int nearestSeries = -1;
    int nearestPoint = -1;

    for (int seriesAt = 0; seriesAt < series_.size(); ++seriesAt) {
        const QVector<QPointF>& points = series_.at(seriesAt).points;

        for (int pointAt = 0; pointAt < points.size(); ++pointAt) {
            const QPointF data = points.at(pointAt);
            if (!std::isfinite(data.x()) || !std::isfinite(data.y()))
                continue;

            const QPointF pixel = math_->toPixel(data);
            const qreal dx = pixel.x() - position.x();
            const qreal dy = pixel.y() - position.y();
            const qreal distance = dx * dx + dy * dy;

            // The first point within the slop is accepted; afterwards only a
            // strictly closer point replaces it, so ties keep the lowest
            // series and then the lowest point index.
            if (nearestSeries < 0 ? distance <= nearest : distance < nearest) {
                nearest = distance;
                nearestSeries = seriesAt;
                nearestPoint = pointAt;
            }
        }
    }

    if (nearestSeries < 0)
        return false;

    if (seriesIndex != nullptr)
        *seriesIndex = nearestSeries;
    if (pointIndex != nullptr)
        *pointIndex = nearestPoint;

    return true;
}

MathematicsCoordination* ChartBase::math() const noexcept
{
    return math_;
}

} // namespace md
