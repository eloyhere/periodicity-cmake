// Periodicity — ChartBase implementation (framework.md §5.9, §7).
//
// The base paints the chart furniture and delegates the marks to paintPlot().
// The transform lives in an owned MathematicsCoordination widget. That widget
// is hidden — it is never part of the visual tree's paint pass — but it stays a
// real child so Theme::forWidget() resolves the same ThemeScope as the chart;
// the base installs its plot rectangle and calls its paintFrame() directly.
//
// Two layers. The furniture — title, legend, grid, axes, tick marks and tick
// labels, plus whatever a chart adds in paintStaticLayer() — is rendered once
// into a device-pixel-ratio-aware QPixmap and re-blitted. The data marks are
// painted every frame by paintPlot(). The static layer is keyed by the widget
// size, the device pixel ratio, the theme revision, the content and plot
// rectangles, the visible axis range, the legend signature and the
// title/legend/axes flags, so a plain repaint (a hover, an animation frame, a
// resize that has not happened yet) never rebuilds it. See the cache rules in
// chart_common.h.
//
// Layout is cached, not recomputed per frame. plotRect() re-derives the inner,
// legend and plot rectangles only when the size, theme revision, fitted range,
// tick count, axis-label presence or a title/legend/axes flag changed; the
// measured fonts, the categorical palette and the resolved series colours are
// cached the same way.
//
// Allocation discipline. The paint path allocates nothing: it reads the cached
// layout, blits the cached pixmap, fills one background rectangle and calls
// paintPlot(). A chart that maps data itself precomputes its QPainterPaths and
// label strings in updateChartCache(), which runs at most once per
// invalidateChartCache().
//
// Animation. The chart owns one Animator and one ChartAnimation value. The value
// asks for a run through the private ChartAnimation::Driver interface;
// startChartAnimation() resolves the motion spec, refuses to start an animator
// when the chart or the theme asks for reduced motion, and otherwise feeds every
// frame into animation_ and a repaint. The reveal starts on the first paint of a
// visible chart (startRevealIfNeeded()), and setSeries() restarts it in Update
// mode once that first paint happened. Animation frames are throttled to roughly
// 60 Hz; the static layer is reused untouched while the run is in flight.

#include <md/components/chart_common.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QSizeF>
#include <QSizePolicy>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

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

// The animation repaint cap: roughly one frame per 16 ms. The animator already
// ticks near that rate; the cap protects the paint path from a burst of values
// (a retarget, a slow frame catch-up) without ever dropping the final frame.
constexpr qint64 kMinimumAnimationFrameMs = 15;

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

    // The chart paints every pixel of its content rectangle itself (the full
    // background fill in paintContent()), so Qt must not erase or composite a
    // transparent layer underneath it.
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    // The transform is a composition helper, not a child surface: it must not
    // paint over the chart and must not intercept a mouse event. Hiding it also
    // keeps it out of Qt's child paint pass; the base calls paintFrame() with
    // its own painter instead.
    math_->hide();
    math_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    connect(math_, &MathematicsCoordination::transformChanged, this, [this] {
        // A ranged change moves every data mark; the chart-side pixel cache is
        // stale. The static layer decides for itself whether its visible range
        // key changed, so it is not invalidated here.
        chartCacheValid_ = false;
        requestUpdate();
    });

    // The animation value asks this chart to play its runs. The attachment is
    // re-established by setAnimation(); a copied value never keeps it.
    animation_.attach(this);

    // The value callback already settles the state on the final 1.0; the signal
    // covers a completion that arrives without a last frame (a zero-length
    // retarget), so isRunning() is never left true on a finished run.
    connect(&animator_, &animation::Animator::finished, this, [this] {
        animation_.finishRun();
        requestUpdate();
    });
}

ChartBase::~ChartBase() = default;

void ChartBase::setTitle(const QString& text)
{
    if (title_ == text)
        return;

    title_ = text;
    layoutValid_ = false;
    staticLayerValid_ = false;
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
    layoutValid_ = false;
    staticLayerValid_ = false;
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
    layoutValid_ = false;
    staticLayerValid_ = false;
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

    // The palette mapping may now cover a different number of entries, and the
    // chart-side pixel cache definitely no longer matches the data.
    seriesColoursValid_ = false;
    chartCacheValid_ = false;

    // The static layer carries the legend names and swatches. New data values
    // alone do not change it, so only a different legend signature invalidates
    // it — a live series update keeps the furniture cache warm.
    const QString previousLegend = legendSignature_;
    refreshLegendSignature();
    if (legendSignature_ != previousLegend)
        staticLayerValid_ = false;

    // New data after the chart appeared is a morph: re-reveal the marks from 0
    // with the shorter Update spec. Before the first reveal there is nothing to
    // morph from, and the pending reveal already covers the new data.
    if (revealStarted_)
        animation_.play(ChartAnimation::PlayMode::Update);

    requestUpdate();
}

const QVector<ChartSeries>& ChartBase::series() const noexcept
{
    return series_;
}

void ChartBase::setAnimation(ChartAnimation animation)
{
    // The old run cannot survive a configuration replacement: its value belongs
    // to the previous state. Stop it and adopt the caller's snapshot instead.
    animator_.stop();
    animation_ = std::move(animation);
    animation_.attach(this);
    requestUpdate();
}

ChartAnimation& ChartBase::animation() noexcept
{
    return animation_;
}

const ChartAnimation& ChartBase::animation() const noexcept
{
    return animation_;
}

void ChartBase::setAnimated(bool animated)
{
    if (animated_ == animated)
        return;

    animated_ = animated;

    if (!animated_) {
        // Settle immediately: an animated(false) chart is always at its final
        // frame, including while it is still on screen when the flag flips.
        animator_.stop();
        animation_.finishRun();
    }

    requestUpdate();
}

bool ChartBase::isAnimated() const noexcept
{
    return animated_;
}

void ChartBase::invalidateStaticLayer()
{
    staticLayerValid_ = false;
    requestUpdate();
}

bool ChartBase::isStaticLayerCached() const noexcept
{
    return staticLayerValid_ && !staticLayer_.isNull();
}

quint64 ChartBase::staticLayerBuilds() const noexcept
{
    return staticLayerBuilds_;
}

void ChartBase::invalidateChartCache()
{
    chartCacheValid_ = false;
    requestUpdate();
}

const QVector<QColor>& ChartBase::chartPalette() const
{
    if (!paletteValid_)
        rebuildPalette();

    return palette_;
}

void ChartBase::rebuildPalette() const
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();

    // A categorical palette is not a design baseline token role set, so it is
    // assembled from the roles the scheme already defines: the four accents, the
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

    palette_.clear();
    palette_.reserve(12);

    for (const QColor& role : roles) {
        if (role.isValid())
            palette_.append(role);
    }

    // A ColorScheme is only required to carry a usable core palette, so a
    // partial theme can leave fewer than eight valid roles. Derive the missing
    // entries as tonal steps of primary; never fall back to a literal colour.
    const QColor base = colours.primary.isValid() ? colours.primary : colours.onSurface;

    if (base.isValid()) {
        int lighter = 120;
        while (palette_.size() < 8 && lighter <= 300) {
            palette_.append(base.lighter(lighter));
            lighter += 20;
        }

        int darker = 120;
        while (palette_.size() < 8 && darker <= 300) {
            palette_.append(base.darker(darker));
            darker += 20;
        }
    }

    // The loops above terminate, but keep the documented minimum an invariant
    // of the return value even for a pathological QColor.
    while (palette_.size() < 8)
        palette_.append(QColor(0, 0, 0));

    paletteValid_ = true;
}

void ChartBase::rebuildSeriesColours() const
{
    if (!paletteValid_)
        rebuildPalette();

    seriesColours_.resize(series_.size());

    for (qsizetype index = 0; index < series_.size(); ++index) {
        const QColor explicitColour = series_.at(index).colour;
        if (explicitColour.isValid())
            seriesColours_[index] = explicitColour;
        else if (!palette_.isEmpty())
            seriesColours_[index] = palette_.at(index % palette_.size());
        else
            seriesColours_[index] = QColor();
    }

    seriesColoursValid_ = true;
}

QColor ChartBase::seriesColour(int index) const
{
    if (index < 0 || index >= series_.size())
        return QColor();

    if (!seriesColoursValid_)
        rebuildSeriesColours();

    return index < seriesColours_.size() ? seriesColours_.at(index) : QColor();
}

void ChartBase::refreshLegendSignature()
{
    // The legend is part of the static layer, so its content is part of that
    // layer's key. The signature is built on a data change, never per frame.
    legendSignature_.clear();

    for (const ChartSeries& entry : series_) {
        legendSignature_ += entry.name;
        legendSignature_ += QChar(0x1f);
        if (entry.colour.isValid())
            legendSignature_ += entry.colour.name();
        legendSignature_ += QChar(0x1e);
    }
}

void ChartBase::refreshFonts() const
{
    if (fontsRevision_ == themeRevision_)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    titleFont_ = Font::resolve(this, theme.type().titleMedium, density);
    legendFont_ = Font::resolve(this, theme.type().labelSmall, density);
    emptyStateFont_ = Font::resolve(this, theme.type().labelMedium, density);
    fontsRevision_ = themeRevision_;
}

const QFont& ChartBase::titleFont() const
{
    refreshFonts();
    return titleFont_;
}

const QFont& ChartBase::legendFont() const
{
    refreshFonts();
    return legendFont_;
}

const QFont& ChartBase::emptyStateFont() const
{
    refreshFonts();
    return emptyStateFont_;
}

void ChartBase::rebuildLayout() const
{
    const QRectF content = contentRect();

    layoutSize_ = size();
    layoutThemeRevision_ = themeRevision_;
    layoutTitle_ = title_;
    layoutLegendVisible_ = legendVisible_;
    layoutAxesVisible_ = axesVisible_;
    layoutFittedBounds_ = math_->fittedBounds();
    layoutTickCount_ = math_->tickCount();
    layoutHasXLabel_ = !math_->xAxisLabel().isEmpty();
    layoutHasYLabel_ = !math_->yAxisLabel().isEmpty();
    layoutValid_ = true;

    layoutInner_ = QRectF();
    layoutLegend_ = QRectF();
    layoutPlot_ = QRectF();

    if (!content.isValid() || content.width() <= 0.0 || content.height() <= 0.0)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    const qreal margin = density.dp(kChartMarginDp);
    const QRectF inner = content.adjusted(margin, margin, -margin, -margin);
    if (!inner.isValid() || inner.width() <= 0.0 || inner.height() <= 0.0)
        return;

    layoutInner_ = inner;

    const qreal gap = density.dp(kChartGapDp);

    qreal top = inner.top();
    if (!title_.isEmpty()) {
        const QFontMetricsF titleMetrics(titleFont());
        top += titleMetrics.height() + gap;
    }

    if (legendVisible_ && legendHasEntries()) {
        const QFontMetricsF legendMetrics(legendFont());
        const qreal height = legendMetrics.height();
        if (top + height <= inner.bottom()) {
            layoutLegend_ = QRectF(inner.left(), top, inner.width(), height);
            top = layoutLegend_.bottom() + gap;
        }
    }

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
        const QVector<qreal> yValues = MathematicsCoordination::niceTicks(
            layoutFittedBounds_.top(), layoutFittedBounds_.bottom(), layoutTickCount_);
        const qreal yStep = tickStepOf(yValues, layoutFittedBounds_.height());

        qreal widestYLabel = 0.0;
        for (qreal value : yValues) {
            widestYLabel = std::max(
                widestYLabel,
                tickMetrics.horizontalAdvance(MathematicsCoordination::tickLabel(value, yStep)));
        }

        leftGutter = std::max<qreal>(density.dp(kMinimumYAxisGutterDp),
                                     widestYLabel + tickLength + labelGap);
        bottomGutter = std::max<qreal>(density.dp(kMinimumXAxisGutterDp),
                                       tickMetrics.height() + tickLength + labelGap);

        if (layoutHasYLabel_) {
            const QFontMetricsF axisMetrics(
                Font::resolve(this, theme.type().labelMedium, density));
            leftGutter += axisMetrics.height() + labelGap;
        }

        if (layoutHasXLabel_) {
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

    if (plotRight <= plotLeft || plotBottom <= plotTop)
        return;

    layoutPlot_ = QRectF(plotLeft, plotTop, plotRight - plotLeft, plotBottom - plotTop);
}

QRectF ChartBase::innerRect() const
{
    static_cast<void>(plotRect());
    return layoutInner_;
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

    static_cast<void>(plotRect());
    return layoutLegend_;
}

QRectF ChartBase::plotRect() const
{
    if (!layoutValid_
        || layoutSize_ != size()
        || layoutThemeRevision_ != themeRevision_
        || layoutTitle_ != title_
        || layoutLegendVisible_ != legendVisible_
        || layoutAxesVisible_ != axesVisible_
        || layoutFittedBounds_ != math_->fittedBounds()
        || layoutTickCount_ != math_->tickCount()
        || layoutHasXLabel_ != !math_->xAxisLabel().isEmpty()
        || layoutHasYLabel_ != !math_->yAxisLabel().isEmpty()) {
        rebuildLayout();
    }

    // Computing the rectangle is also the moment its transform is installed.
    // A chart may therefore call math()->toPixel() from a mouse handler (or
    // from plotRect() itself) without waiting for a paint. The call is a pair of
    // assignments: it does not allocate and does not emit transformChanged().
    math_->setPlotArea(layoutPlot_);

    return layoutPlot_;
}

void ChartBase::paintTitle(QPainter& painter)
{
    if (title_.isEmpty())
        return;

    const QRectF inner = innerRect();
    if (inner.isEmpty())
        return;

    const QFont& font = titleFont();
    const QFontMetricsF metrics(font);
    const Theme& theme = Theme::forWidget(this);

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
    const QFont& font = legendFont();
    const QFontMetricsF metrics(font);

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

        QColor colour = seriesColour(index);
        if (!colour.isValid())
            colour = theme.colours().primary;

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

void ChartBase::paintPlot(QPainter& painter, const QRectF& plot)
{
    // The base has no marks of its own. A chart implements this overload (the
    // settled frame) or the progress overload below.
    Q_UNUSED(painter);
    Q_UNUSED(plot);
}

void ChartBase::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    const qreal eased = std::clamp(progress, 0.0, 1.0);

    // Nothing is visible at the start of the run: skip the whole pass instead
    // of painting fully transparent marks.
    if (eased <= 0.0)
        return;

    if (eased >= 1.0) {
        // The finished frame is painted exactly as before the animation model
        // existed: no opacity change and no extra clip, so a chart that paints
        // edge markers outside the plot keeps them.
        paintPlot(painter, plot);
        return;
    }

    // Mid-reveal the marks are faded and confined to the data area. The clip is
    // intersected with whatever the paint pipeline installed, which is why this
    // is the chart's clip handed to the plot rather than a replacement for it.
    painter.save();
    painter.setOpacity(painter.opacity() * eased);
    painter.setClipRect(plot, Qt::IntersectClip);
    paintPlot(painter, plot);
    painter.restore();
}

void ChartBase::onAnimationProgress(qreal progress)
{
    Q_UNUSED(progress);
}

void ChartBase::paintStaticLayer(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(painter);
    Q_UNUSED(content);
}

void ChartBase::updateChartCache()
{
}

void ChartBase::paintEmptyState(QPainter& painter, const QRectF& plot)
{
    if (plot.isEmpty())
        return;

    // The label is resolved once per language change, not per paint.
    if (!emptyStateTextValid_) {
        emptyStateText_ =
            localized(QStringLiteral("chart.noData"), QStringLiteral("No data"));
        emptyStateTextValid_ = true;
    }

    const Theme& theme = Theme::forWidget(this);

    painter.save();
    painter.setFont(emptyStateFont());
    painter.setPen(theme.colours().onSurfaceVariant);
    painter.drawText(plot, Qt::AlignCenter, emptyStateText_);
    painter.restore();
}

void ChartBase::resizeEvent(QResizeEvent* event)
{
    View::resizeEvent(event);

    // The next paint would detect both changes through their keys; marking them
    // here keeps isStaticLayerCached() truthful between the resize and the
    // paint.
    layoutValid_ = false;
    staticLayerValid_ = false;
    chartCacheValid_ = false;
}

void ChartBase::ensureChartCache()
{
    if (chartCacheValid_)
        return;

    // updateChartCache() may throw; the paint pass already has an exception
    // boundary, and the flag stays false so the next paint retries.
    updateChartCache();
    chartCacheValid_ = true;
}

void ChartBase::ensureStaticLayer(const QRectF& content, const QRectF& plot)
{
    const qreal ratio = devicePixelRatioF();
    const QRectF visibleBounds = math_->visibleBounds();

    const bool matches = staticLayerValid_
        && !staticLayer_.isNull()
        && staticLayerSize_ == size()
        && staticLayerDevicePixelRatio_ == ratio
        && staticLayerThemeRevision_ == themeRevision_
        && staticLayerContent_ == content
        && staticLayerPlot_ == plot
        && staticLayerVisibleBounds_ == visibleBounds
        && staticLayerTitle_ == title_
        && staticLayerLegendSignature_ == legendSignature_
        && staticLayerLegendVisible_ == legendVisible_
        && staticLayerAxesVisible_ == axesVisible_;

    if (matches)
        return;

    rebuildStaticLayer(content, plot);

    staticLayerSize_ = size();
    staticLayerDevicePixelRatio_ = ratio;
    staticLayerThemeRevision_ = themeRevision_;
    staticLayerContent_ = content;
    staticLayerPlot_ = plot;
    staticLayerVisibleBounds_ = visibleBounds;
    staticLayerTitle_ = title_;
    staticLayerLegendSignature_ = legendSignature_;
    staticLayerLegendVisible_ = legendVisible_;
    staticLayerAxesVisible_ = axesVisible_;
    staticLayerValid_ = true;
    ++staticLayerBuilds_;
}

void ChartBase::rebuildStaticLayer(const QRectF& content, const QRectF& plot)
{
    staticLayer_ = QPixmap();

    if (size().isEmpty())
        return;

    const qreal ratio = devicePixelRatioF();
    const QSize pixelSize(std::max(1, qRound(size().width() * ratio)),
                          std::max(1, qRound(size().height() * ratio)));

    QPixmap pixmap(pixelSize);
    pixmap.fill(Qt::transparent);

    {
        QPainter layerPainter(&pixmap);
        layerPainter.scale(ratio, ratio);

        // Text and curves read best with antialiasing; the straight grid and
        // axis lines are drawn by paintFrame(), whose one-time render makes the
        // hint immaterial to the frame budget.
        layerPainter.setRenderHint(QPainter::Antialiasing, false);
        layerPainter.setRenderHint(QPainter::TextAntialiasing, true);

        paintTitle(layerPainter);

        if (legendVisible_ && legendHasEntries()) {
            const QRectF legend = legendRect();
            if (!legend.isEmpty())
                drawLegend(layerPainter, legend);
        }

        if (axesVisible_ && !plot.isEmpty())
            math_->paintFrame(layerPainter);

        // Chart-specific furniture (a colour bar, fixed annotations) belongs in
        // the same cached layer, never in the per-frame plot pass.
        paintStaticLayer(layerPainter, content);
    }

    staticLayer_ = pixmap;
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

// --- Post-process antialiasing ------------------------------------------------
//
// The filters below operate on the finished plot image, which the base renders
// at widget resolution into an ARGB32 pre-multiplied buffer. They exist because
// the raster engine only offers on/off coverage antialiasing: a chart that wants
// a sharper result at a lower fill cost than 4x supersampling can filter the
// image afterwards instead. Jittered accumulation is built in the same place: it
// renders the marks several times rather than filtering a single pass.

namespace {

inline int lumaOf(QRgb pixel)
{
    return (77 * qRed(pixel) + 150 * qGreen(pixel) + 29 * qBlue(pixel)) >> 8;
}

// Mixes two premultiplied pixels; `weight` is the share of `second`, 0..256.
inline QRgb lerpPremultiplied(QRgb first, QRgb second, int weight)
{
    const int inverse = 256 - weight;
    return qRgba((qRed(first) * inverse + qRed(second) * weight) >> 8,
                 (qGreen(first) * inverse + qGreen(second) * weight) >> 8,
                 (qBlue(first) * inverse + qBlue(second) * weight) >> 8,
                 (qAlpha(first) * inverse + qAlpha(second) * weight) >> 8);
}

// The luminance plane of a premultiplied image. The morphological filters read
// each neighbour several times while they trace spans, so they materialise the
// 8-bit luminance once instead of re-deriving it from the pixels.
class LumaPlane
{
public:
    explicit LumaPlane(const QImage& image)
        : width_(image.width())
        , height_(image.height())
        , values_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_))
    {
        for (int y = 0; y < height_; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            for (int x = 0; x < width_; ++x)
                values_[index(x, y)] = lumaOf(row[x]);
        }
    }

    [[nodiscard]] int at(int x, int y) const noexcept { return values_[index(x, y)]; }

private:
    [[nodiscard]] std::size_t index(int x, int y) const noexcept
    {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_)
             + static_cast<std::size_t>(x);
    }

    int width_ = 0;
    int height_ = 0;
    std::vector<int> values_;
};

inline QRgb pixelAt(const QImage& image, int x, int y)
{
    return reinterpret_cast<const QRgb*>(image.constScanLine(y))[x];
}

inline void setPixelAt(QImage& image, int x, int y, QRgb value)
{
    reinterpret_cast<QRgb*>(image.scanLine(y))[x] = value;
}

// FXAA-family filter: find pixels whose 4-neighbourhood spans a luminance range
// beyond `threshold` and blend them towards the neighbourhood average. Flat
// interior pixels and flat background are left untouched, so text and solid
// fills keep their weight.
void filterFxaa(QImage& image, int threshold = 12)
{
    const int width = image.width();
    const int height = image.height();
    if (width < 3 || height < 3)
        return;

    const QImage source = image.copy();

    for (int y = 1; y < height - 1; ++y) {
        const QRgb* rowUp = reinterpret_cast<const QRgb*>(source.constScanLine(y - 1));
        const QRgb* row = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        const QRgb* rowDown = reinterpret_cast<const QRgb*>(source.constScanLine(y + 1));
        QRgb* out = reinterpret_cast<QRgb*>(image.scanLine(y));

        for (int x = 1; x < width - 1; ++x) {
            const int centre = lumaOf(row[x]);
            const int north = lumaOf(rowUp[x]);
            const int south = lumaOf(rowDown[x]);
            const int west = lumaOf(row[x - 1]);
            const int east = lumaOf(row[x + 1]);

            const int lowest = std::min(std::min(north, south), std::min(west, east));
            const int highest = std::max(std::max(north, south), std::max(west, east));
            if (highest - lowest < threshold)
                continue;

            const QRgb blended = qRgb((qRed(row[x]) * 4 + qRed(rowUp[x]) + qRed(rowDown[x])
                                       + qRed(row[x - 1]) + qRed(row[x + 1])) / 8,
                                      (qGreen(row[x]) * 4 + qGreen(rowUp[x]) + qGreen(rowDown[x])
                                       + qGreen(row[x - 1]) + qGreen(row[x + 1])) / 8,
                                      (qBlue(row[x]) * 4 + qBlue(rowUp[x]) + qBlue(rowDown[x])
                                       + qBlue(row[x - 1]) + qBlue(row[x + 1])) / 8);
            out[x] = qRgba(qRed(blended), qGreen(blended), qBlue(blended), qAlpha(row[x]));
        }
    }
}

// Fast morphological filter, MLAA family: locate horizontal and vertical edges,
// measure the run length of each edge, then blend the two pixels that straddle
// the edge in proportion to the run. Short runs (the usual stair steps of a
// shallow diagonal) get a half-pixel blend; long runs stay crisp.
void filterFmaa(QImage& image, int threshold = 24)
{
    const int width = image.width();
    const int height = image.height();
    if (width < 3 || height < 3)
        return;

    const QImage source = image.copy();
    constexpr int kMaxRun = 4;

    // Horizontal edges: a vertical luminance step between (x, y) and (x+1, y).
    for (int y = 0; y < height; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        QRgb* out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width - 1; ++x) {
            if (std::abs(lumaOf(row[x + 1]) - lumaOf(row[x])) < threshold)
                continue;

            int run = 1;
            while (run < kMaxRun && x + run < width - 1
                   && std::abs(lumaOf(row[x + 1 + run]) - lumaOf(row[x + run])) >= threshold)
                ++run;

            const int weight = std::min(run, kMaxRun) * 32;
            const auto mix = [weight](QRgb a, QRgb b) {
                return qRgb((qRed(a) * (64 - weight) + qRed(b) * weight) / 64,
                            (qGreen(a) * (64 - weight) + qGreen(b) * weight) / 64,
                            (qBlue(a) * (64 - weight) + qBlue(b) * weight) / 64);
            };
            out[x + 1] = qRgba(qRed(mix(row[x], row[x + 1])), qGreen(mix(row[x], row[x + 1])),
                               qBlue(mix(row[x], row[x + 1])), qAlpha(row[x + 1]));
        }
    }

    // Vertical edges: the same idea on columns. The horizontal pass already
    // softened their crossings, so re-read the image for this one.
    const QImage columns = image.copy();
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height - 1; ++y) {
            const QRgb upper = columns.pixel(x, y);
            const QRgb lower = columns.pixel(x, y + 1);
            if (std::abs(lumaOf(lower) - lumaOf(upper)) < threshold)
                continue;

            int run = 1;
            while (run < kMaxRun && y + run < height - 1
                   && std::abs(lumaOf(columns.pixel(x, y + 1 + run))
                               - lumaOf(columns.pixel(x, y + run))) >= threshold)
                ++run;

            const int weight = std::min(run, kMaxRun) * 32;
            const QRgb mixed = qRgb((qRed(upper) * (64 - weight) + qRed(lower) * weight) / 64,
                                    (qGreen(upper) * (64 - weight) + qGreen(lower) * weight) / 64,
                                    (qBlue(upper) * (64 - weight) + qBlue(lower) * weight) / 64);
            QRgb* target = reinterpret_cast<QRgb*>(image.scanLine(y + 1));
            target[x] = qRgba(qRed(mixed), qGreen(mixed), qBlue(mixed), qAlpha(lower));
        }
    }
}

// MLAA: classify each edge span as step/corner/cap and blend the straddling
// pair with trapezoid weights; omits the reference filter's Z/U area tables.
void filterMlaa(QImage& image, int threshold = 20)
{
    const int width = image.width();
    const int height = image.height();
    if (width < 3 || height < 3)
        return;

    const QImage source = image.copy();
    const LumaPlane luma(source);

    // A pair edge is the boundary between two adjacent pixels. The sign records
    // which side is brighter, so a span never mixes a rising edge with a falling
    // one.
    const auto horizontalEdge = [&](int x, int y) -> qint8 {
        if (x < 0 || x >= width || y < 0 || y >= height - 1)
            return 0;
        const int delta = luma.at(x, y + 1) - luma.at(x, y);
        return delta >= threshold ? qint8(1) : delta <= -threshold ? qint8(-1) : qint8(0);
    };
    const auto verticalEdge = [&](int x, int y) -> qint8 {
        if (x < 0 || x >= width - 1 || y < 0 || y >= height)
            return 0;
        const int delta = luma.at(x + 1, y) - luma.at(x, y);
        return delta >= threshold ? qint8(1) : delta <= -threshold ? qint8(-1) : qint8(0);
    };

    constexpr int kMaxSpan = 16;

    // Horizontal pass: a run of horizontal edges is a near-horizontal boundary.
    // The two rows straddle it; a perpendicular edge at either end turns the
    // trapezoid into a corner or a step.
    for (int y = 0; y + 1 < height; ++y) {
        int x = 0;
        while (x < width) {
            const qint8 sign = horizontalEdge(x, y);
            if (sign == 0) {
                ++x;
                continue;
            }
            int end = x + 1;
            while (end < width && horizontalEdge(end, y) == sign)
                ++end;

            const int span = end - x;
            // A span on the image border, or one longer than the search limit,
            // has no classified end: treat it as a straight line and leave it.
            if (span <= kMaxSpan && x > 0 && end < width) {
                const bool leftCrossing = verticalEdge(x - 1, y) != 0
                                       || verticalEdge(x - 1, y + 1) != 0;
                const bool rightCrossing = verticalEdge(end - 1, y) != 0
                                        || verticalEdge(end - 1, y + 1) != 0;
                for (int i = 0; i < span; ++i) {
                    // Trapezoid position across the span: the share of the
                    // diagonal boundary that falls inside this column.
                    const int position = ((2 * i + 1) * 256) / (2 * span);
                    int coverage = position;
                    if (leftCrossing && rightCrossing) {
                        coverage = position;              // step: full trapezoid
                    } else if (leftCrossing) {
                        coverage = (256 - position) / 2;  // corner at the left
                    } else if (rightCrossing) {
                        coverage = position / 2;          // corner at the right
                    } else {
                        coverage = 64;                    // loose cap
                    }

                    const int column = x + i;
                    const QRgb top = pixelAt(source, column, y);
                    const QRgb bottom = pixelAt(source, column, y + 1);
                    setPixelAt(image, column, y,
                               lerpPremultiplied(top, bottom, (256 - coverage) / 2));
                    setPixelAt(image, column, y + 1,
                               lerpPremultiplied(bottom, top, coverage / 2));
                }
            }
            x = end;
        }
    }

    // Vertical pass on the image the horizontal pass produced.
    const QImage columns = image.copy();
    for (int x = 0; x + 1 < width; ++x) {
        int y = 0;
        while (y < height) {
            const qint8 sign = verticalEdge(x, y);
            if (sign == 0) {
                ++y;
                continue;
            }
            int end = y + 1;
            while (end < height && verticalEdge(x, end) == sign)
                ++end;

            const int span = end - y;
            if (span <= kMaxSpan && y > 0 && end < height) {
                const bool topCrossing = horizontalEdge(x, y - 1) != 0
                                      || horizontalEdge(x + 1, y - 1) != 0;
                const bool bottomCrossing = horizontalEdge(x, end - 1) != 0
                                         || horizontalEdge(x + 1, end - 1) != 0;
                for (int i = 0; i < span; ++i) {
                    const int position = ((2 * i + 1) * 256) / (2 * span);
                    int coverage = position;
                    if (topCrossing && bottomCrossing) {
                        coverage = position;
                    } else if (topCrossing) {
                        coverage = (256 - position) / 2;
                    } else if (bottomCrossing) {
                        coverage = position / 2;
                    } else {
                        coverage = 64;
                    }

                    const int row = y + i;
                    const QRgb left = pixelAt(columns, x, row);
                    const QRgb right = pixelAt(columns, x + 1, row);
                    setPixelAt(image, x, row,
                               lerpPremultiplied(left, right, (256 - coverage) / 2));
                    setPixelAt(image, x + 1, row,
                               lerpPremultiplied(right, left, coverage / 2));
                }
            }
            y = end;
        }
    }
}

// SMAA core (area-based, not the full reference): 3x3 luma masks build edge
// spans and analytic trapezoid areas blend the darker pixel; omits the textures.
void filterSmaa(QImage& image, int threshold = 18)
{
    const int width = image.width();
    const int height = image.height();
    if (width < 3 || height < 3)
        return;

    const QImage source = image.copy();
    const LumaPlane luma(source);

    // Per-pixel 3x3 tests: a pixel sits on a vertical edge when the luminance
    // left and right of it differs, and on a horizontal edge when the luminance
    // above and below differs.
    const auto verticalMask = [&](int x, int y) {
        return std::abs(luma.at(std::min(width - 1, x + 1), y)
                        - luma.at(std::max(0, x - 1), y))
            >= threshold;
    };
    const auto horizontalMask = [&](int x, int y) {
        return std::abs(luma.at(x, std::min(height - 1, y + 1))
                        - luma.at(x, std::max(0, y - 1)))
            >= threshold;
    };

    // An edge pair exists where both pixels of the pair see the same 3x3
    // gradient. This keeps the mask from turning one thin line into two
    // parallel edges.
    const auto verticalEdge = [&](int x, int y) -> qint8 {
        if (x < 0 || x >= width - 1 || y < 0 || y >= height)
            return 0;
        if (!verticalMask(x, y) || !verticalMask(x + 1, y))
            return 0;
        const int delta = luma.at(x + 1, y) - luma.at(x, y);
        return delta >= threshold ? qint8(1) : delta <= -threshold ? qint8(-1) : qint8(0);
    };
    const auto horizontalEdge = [&](int x, int y) -> qint8 {
        if (x < 0 || x >= width || y < 0 || y >= height - 1)
            return 0;
        if (!horizontalMask(x, y) || !horizontalMask(x, y + 1))
            return 0;
        const int delta = luma.at(x, y + 1) - luma.at(x, y);
        return delta >= threshold ? qint8(1) : delta <= -threshold ? qint8(-1) : qint8(0);
    };

    constexpr int kMaxSpan = 16;

    // The darker pixel of a pair receives the brighter colour; the weight is the
    // area of the brighter region inside it, up to a quarter at a span centre.
    const auto blendDarker = [](QImage& target, int darkX, int darkY, int brightX,
                                int brightY, int weight) {
        const QRgb dark = pixelAt(target, darkX, darkY);
        const QRgb bright = pixelAt(target, brightX, brightY);
        setPixelAt(target, darkX, darkY, lerpPremultiplied(dark, bright, weight));
    };

    for (int y = 0; y + 1 < height; ++y) {
        int x = 0;
        while (x < width) {
            const qint8 sign = horizontalEdge(x, y);
            if (sign == 0) {
                ++x;
                continue;
            }
            int end = x + 1;
            while (end < width && horizontalEdge(end, y) == sign)
                ++end;

            const int span = end - x;
            if (span <= kMaxSpan && x > 0 && end < width) {
                for (int i = 0; i < span; ++i) {
                    const int coverage = ((2 * i + 1) * 128) / (2 * span);
                    const int column = x + i;
                    if (sign > 0) {
                        blendDarker(image, column, y, column, y + 1, coverage);
                    } else {
                        blendDarker(image, column, y + 1, column, y, coverage);
                    }
                }
            }
            x = end;
        }
    }

    // Vertical pass; the horizontal pass softened the crossings, so read its
    // result before writing.
    const QImage columns = image.copy();
    for (int x = 0; x + 1 < width; ++x) {
        int y = 0;
        while (y < height) {
            const qint8 sign = verticalEdge(x, y);
            if (sign == 0) {
                ++y;
                continue;
            }
            int end = y + 1;
            while (end < height && verticalEdge(x, end) == sign)
                ++end;

            const int span = end - y;
            if (span <= kMaxSpan && y > 0 && end < height) {
                for (int i = 0; i < span; ++i) {
                    const int coverage = ((2 * i + 1) * 128) / (2 * span);
                    const int row = y + i;
                    if (sign > 0) {
                        blendDarker(image, x, row, x + 1, row, coverage);
                    } else {
                        blendDarker(image, x + 1, row, x, row, coverage);
                    }
                }
            }
            y = end;
        }
    }
}

// CMAA: blend only edges whose four-sample cross profile is monotone; omits span
// tracing, so text strokes and corners keep their weight.
void filterCmaa(QImage& image, int threshold = 24)
{
    const int width = image.width();
    const int height = image.height();
    if (width < 4 || height < 4)
        return;

    const QImage source = image.copy();
    const LumaPlane luma(source);
    constexpr int kWeight = 64; // a quarter, the conservative blend

    const auto monotone = [](int a, int b, int c, int d) {
        const bool rising = a <= b && b <= c && c <= d;
        const bool falling = a >= b && b >= c && c >= d;
        return (rising || falling) && a != d;
    };

    // Horizontal edges: the profile runs through both rows and one pixel on
    // each side.
    for (int y = 1; y + 2 < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (std::abs(luma.at(x, y + 1) - luma.at(x, y)) < threshold)
                continue;
            if (!monotone(luma.at(x, y - 1), luma.at(x, y), luma.at(x, y + 1),
                          luma.at(x, y + 2)))
                continue;

            const QRgb top = pixelAt(source, x, y);
            const QRgb bottom = pixelAt(source, x, y + 1);
            setPixelAt(image, x, y, lerpPremultiplied(top, bottom, kWeight));
            setPixelAt(image, x, y + 1, lerpPremultiplied(bottom, top, kWeight));
        }
    }

    // Vertical edges use the same monotone test on the horizontal profile.
    const QImage rows = image.copy();
    for (int x = 1; x + 2 < width; ++x) {
        for (int y = 0; y < height; ++y) {
            if (std::abs(luma.at(x + 1, y) - luma.at(x, y)) < threshold)
                continue;
            if (!monotone(luma.at(x - 1, y), luma.at(x, y), luma.at(x + 1, y),
                          luma.at(x + 2, y)))
                continue;

            const QRgb left = pixelAt(rows, x, y);
            const QRgb right = pixelAt(rows, x + 1, y);
            setPixelAt(image, x, y, lerpPremultiplied(left, right, kWeight));
            setPixelAt(image, x + 1, y, lerpPremultiplied(right, left, kWeight));
        }
    }
}

// Jittered accumulation: average four rotated-grid or eight eight-queens
// samples (one per row and column, no shared diagonal); omits adaptive sampling.
template <typename DrawMarks>
QImage renderJittered(const QSize& pixelSize, qreal ratio, const QRectF& plot,
                      int sampleCount, DrawMarks&& drawMarks)
{
    static const QPointF kRotatedGrid[4] = {
        QPointF(0.375, 0.125), QPointF(0.875, 0.375),
        QPointF(0.125, 0.625), QPointF(0.625, 0.875),
    };
    static const QPointF kQueensPattern[8] = {
        QPointF(0.0625, 0.0625), QPointF(0.1875, 0.5625),
        QPointF(0.3125, 0.9375), QPointF(0.4375, 0.6875),
        QPointF(0.5625, 0.3125), QPointF(0.6875, 0.8125),
        QPointF(0.8125, 0.1875), QPointF(0.9375, 0.4375),
    };

    const bool eight = sampleCount >= 8;
    const QPointF* offsets = eight ? kQueensPattern : kRotatedGrid;
    const int count = eight ? 8 : 4;

    QImage result(pixelSize, QImage::Format_ARGB32_Premultiplied);
    result.setDevicePixelRatio(ratio);
    result.fill(Qt::transparent);
    QImage frame(pixelSize, QImage::Format_ARGB32_Premultiplied);

    for (int sample = 0; sample < count; ++sample) {
        frame.fill(Qt::transparent);
        {
            QPainter painter(&frame);
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.scale(ratio, ratio);
            painter.translate(-plot.topLeft() + offsets[sample]);
            drawMarks(painter);
        }

        // Running mean in place: after `sample` previous frames each channel is
        // their average, so the new mean is
        // (previous * sample + incoming) / (sample + 1).
        const int divisor = sample + 1;
        for (int y = 0; y < pixelSize.height(); ++y) {
            const QRgb* incoming = reinterpret_cast<const QRgb*>(frame.constScanLine(y));
            QRgb* accumulated = reinterpret_cast<QRgb*>(result.scanLine(y));
            for (int x = 0; x < pixelSize.width(); ++x) {
                const QRgb previous = accumulated[x];
                const QRgb next = incoming[x];
                accumulated[x] =
                    qRgba((qRed(previous) * sample + qRed(next)) / divisor,
                          (qGreen(previous) * sample + qGreen(next)) / divisor,
                          (qBlue(previous) * sample + qBlue(next)) / divisor,
                          (qAlpha(previous) * sample + qAlpha(next)) / divisor);
            }
        }
    }
    return result;
}

} // namespace

void ChartBase::setAntialiasing(ChartAntialiasing mode)
{
    if (antialiasing_ == mode)
        return;

    antialiasing_ = mode;
    // Only the plot pass changes: the cached furniture is painted with
    // antialiasing off and is not supersampled either way.
    invalidateChartCache();
    update();
}

ChartAntialiasing ChartBase::antialiasing() const noexcept
{
    return antialiasing_;
}

void ChartBase::paintContent(QPainter& painter, const QRectF& content)
{
    painter.save();
    try {
        const Theme& theme = Theme::forWidget(this);

        // 1. Full opaque background. WA_OpaquePaintEvent promises that no stale
        //    pixel survives a paint, so every pass starts with a complete fill.
        painter.fillRect(content, theme.colours().surface);
        painter.setRenderHint(QPainter::Antialiasing, false);

        const QRectF plot = plotRect();

        ensureChartCache();
        startRevealIfNeeded();

        // 2. The cached furniture. It is re-blitted untouched on every frame of
        //    a reveal/update run; only the plot below changes.
        ensureStaticLayer(content, plot);

        if (!staticLayer_.isNull() && !size().isEmpty()) {
            painter.drawPixmap(QRectF(QPointF(0.0, 0.0), QSizeF(size())), staticLayer_,
                               QRectF(staticLayer_.rect()));
        }

        // 3. The dynamic plot. Curves want antialiasing; the cached layer above
        //    does not pay for it again.
        if (!plot.isEmpty()) {
            const auto drawMarks = [&](QPainter& target) {
                if (hasData())
                    paintPlot(target, plot, animation_.progress());
                else
                    paintEmptyState(target, plot);
            };

            const qreal ratio = devicePixelRatioF();
            const auto pixelSizeFor = [&](int scale) {
                return (plot.size() * ratio * scale).toSize();
            };
            const auto withinRasterLimit = [](const QSize& pixelSize) {
                return !pixelSize.isEmpty() && pixelSize.width() <= 16384
                    && pixelSize.height() <= 16384;
            };
            // Beyond the raster limit the buffer would be illegal; fall back to
            // the grey raster instead of failing the paint. Every buffered
            // family below goes through this same guard.
            const auto drawGreyFallback = [&] {
                painter.setRenderHint(QPainter::Antialiasing, true);
                drawMarks(painter);
            };

            // The one dispatch over the strategy families:
            //   aliased / coverage   -> the direct painter;
            //   coverage sampling    -> Nx buffer, smooth downscale;
            //   sample accumulation  -> jittered buffer average;
            //   post-process         -> aliased buffer, then an edge filter.
            switch (antialiasing_) {
            case ChartAntialiasing::None:
            case ChartAntialiasing::Grayscale:
                painter.setRenderHint(QPainter::Antialiasing,
                                      antialiasing_ != ChartAntialiasing::None);
                drawMarks(painter);
                break;

            case ChartAntialiasing::Supersample2x:
            case ChartAntialiasing::Supersample4x: {
                const int factor =
                    antialiasing_ == ChartAntialiasing::Supersample2x ? 2 : 4;
                const QSize pixelSize = pixelSizeFor(factor);
                if (!withinRasterLimit(pixelSize)) {
                    drawGreyFallback();
                    break;
                }

                QImage buffer(pixelSize, QImage::Format_ARGB32_Premultiplied);
                buffer.setDevicePixelRatio(ratio * factor);
                buffer.fill(Qt::transparent);
                {
                    QPainter offscreen(&buffer);
                    // Supersampling relies on the device transform for its
                    // quality: the marks are drawn antialiased at Nx.
                    offscreen.setRenderHint(QPainter::Antialiasing, true);
                    offscreen.setRenderHint(QPainter::SmoothPixmapTransform, true);
                    offscreen.translate(-plot.topLeft());
                    drawMarks(offscreen);
                }
                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter.drawImage(plot.topLeft(), buffer);
                break;
            }

            case ChartAntialiasing::Jittered4x:
            case ChartAntialiasing::Jittered8x: {
                const QSize pixelSize = pixelSizeFor(1);
                if (!withinRasterLimit(pixelSize)) {
                    drawGreyFallback();
                    break;
                }

                const int samples =
                    antialiasing_ == ChartAntialiasing::Jittered8x ? 8 : 4;
                const QImage buffer =
                    renderJittered(pixelSize, ratio, plot, samples, drawMarks);
                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter.drawImage(plot.topLeft(), buffer);
                break;
            }

            case ChartAntialiasing::Fxaa:
            case ChartAntialiasing::Fmaa:
            case ChartAntialiasing::Mlaa:
            case ChartAntialiasing::Smaa:
            case ChartAntialiasing::Cmaa: {
                const QSize pixelSize = pixelSizeFor(1);
                if (!withinRasterLimit(pixelSize)) {
                    drawGreyFallback();
                    break;
                }

                QImage buffer(pixelSize, QImage::Format_ARGB32_Premultiplied);
                buffer.setDevicePixelRatio(ratio);
                buffer.fill(Qt::transparent);
                {
                    QPainter offscreen(&buffer);
                    // The post-process filters want hard edges to work on, so
                    // they render aliased and filter the finished image.
                    offscreen.setRenderHint(QPainter::Antialiasing, false);
                    offscreen.setRenderHint(QPainter::SmoothPixmapTransform, true);
                    offscreen.translate(-plot.topLeft());
                    drawMarks(offscreen);
                }

                if (antialiasing_ == ChartAntialiasing::Fxaa)
                    filterFxaa(buffer);
                else if (antialiasing_ == ChartAntialiasing::Fmaa)
                    filterFmaa(buffer);
                else if (antialiasing_ == ChartAntialiasing::Mlaa)
                    filterMlaa(buffer);
                else if (antialiasing_ == ChartAntialiasing::Smaa)
                    filterSmaa(buffer);
                else
                    filterCmaa(buffer);

                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter.drawImage(plot.topLeft(), buffer);
                break;
            }
            }
        }
    } catch (const std::exception& error) {
        qWarning("md::ChartBase::paintContent failed: %s", error.what());
    } catch (...) {
        qWarning("md::ChartBase::paintContent failed with an unknown exception");
    }
    painter.restore();

    // The animation callback runs outside View's guarded entry points, so a
    // failure there is stored instead of thrown. The next paint — after it has
    // painted the settled frame above — hands the diagnostic to the guard
    // around View::paintEvent, which records it in lastError().
    reportPendingAnimationFailure();
}

void ChartBase::startRevealIfNeeded()
{
    if (revealStarted_)
        return;

    // A chart that is not visible (an offscreen render or grab before it is
    // shown) paints its settled frame; the reveal belongs to the first paint the
    // user can actually see.
    if (!isVisible())
        return;

    revealStarted_ = true;

    if (!animated_) {
        animation_.finishRun();
        return;
    }

    animation_.play(ChartAnimation::PlayMode::Reveal);
}

void ChartBase::startChartAnimation(ChartAnimation& animation, ChartAnimation::PlayMode mode)
{
    Q_UNUSED(animation);

    // One chart, one run. A retarget starts from 0 again, which is exactly the
    // update contract: the old data is re-revealed, not interpolated.
    animator_.stop();

    const MotionSpec spec = animation_.specFor(mode);
    const bool reduced = !animated_ || animation_.isReducedMotion()
        || Theme::forWidget(this).motion().reducedMotion;

    if (reduced || spec.duration.count() <= 0) {
        // No animator is ever created for a snap. Settling synchronously means
        // the frame painted right after this call is already complete.
        animation_.finishRun();
        requestUpdate();
        return;
    }

    animation_.beginRun(mode);
    animator_.start(spec, [this](qreal value) { onAnimatorValue(value); });
    requestUpdate();
}

void ChartBase::stopChartAnimation(ChartAnimation& animation)
{
    Q_UNUSED(animation);

    animator_.stop();
    requestUpdate();
}

void ChartBase::onAnimatorValue(qreal value)
{
    try {
        animation_.setRunProgress(value);
        onAnimationProgress(animation_.progress());

        // The final frame is never dropped; the intermediate ones are capped at
        // roughly 60 Hz so a burst of values cannot outrun the paint path.
        const bool finished = animation_.progress() >= 1.0;
        if (finished || !animationRepaintClock_.isValid()
            || animationRepaintClock_.elapsed() >= kMinimumAnimationFrameMs) {
            animationRepaintClock_.restart();
            requestUpdate();
        }
    } catch (const std::exception& error) {
        reportAnimationFailure(QString::fromUtf8(error.what()));
    } catch (...) {
        reportAnimationFailure(QStringLiteral("unknown exception"));
    }
}

void ChartBase::reportAnimationFailure(const QString& message)
{
    // The run is over: stop the animator and settle on the final frame so the
    // chart is never left half-revealed, then let the next paint report the
    // failure through the guarded entry point.
    animator_.stop();
    animation_.finishRun();

    if (pendingAnimationError_.isEmpty()) {
        pendingAnimationError_ =
            QStringLiteral("md::ChartBase: animation callback failed: ") + message;
    }

    requestUpdate();
}

void ChartBase::reportPendingAnimationFailure()
{
    if (pendingAnimationError_.isEmpty())
        return;

    const QString message = pendingAnimationError_;
    pendingAnimationError_.clear();

    // Deliberately thrown from outside this class's own catch blocks: the guard
    // around View::paintEvent catches it and stores it, which is the mechanism
    // lastError() exposes. The frame above has already been painted.
    throw std::runtime_error(message.toStdString());
}

void ChartBase::onStoreChanged(Store* store, const QString& key, const QVariant& value)
{
    View::onStoreChanged(store, key, value);

    // The empty-state label is the only localized string the base paints; a
    // language switch must drop the cached copy.
    if (key == QStringLiteral("md.language"))
        emptyStateTextValid_ = false;
}

void ChartBase::onThemeChanged()
{
    View::onThemeChanged();

    // One revision covers the fonts, the palette, the resolved series colours,
    // the layout and the static layer: each cache compares it instead of
    // comparing whole Theme values every frame.
    ++themeRevision_;
    fontsRevision_ = ~quint64(0);
    layoutValid_ = false;
    paletteValid_ = false;
    seriesColoursValid_ = false;
    staticLayerValid_ = false;
    chartCacheValid_ = false;
    emptyStateTextValid_ = false;

    if (animator_.isRunning()) {
        // A theme can turn reduced motion on while a reveal is in flight. The
        // chart must not keep animating against the new preference: settle it.
        if (Theme::forWidget(this).motion().reducedMotion) {
            animator_.stop();
            animation_.finishRun();
        }
    }

    requestUpdate();
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
            const QPointF sample = points.at(pointAt);
            if (!std::isfinite(sample.x()) || !std::isfinite(sample.y()))
                continue;

            const QPointF pixel = math_->toPixel(sample);
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
