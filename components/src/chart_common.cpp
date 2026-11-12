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
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
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

            const int factor = antialiasing_ == ChartAntialiasing::Supersample2x ? 2
                             : antialiasing_ == ChartAntialiasing::Supersample4x ? 4
                                                                                 : 0;

            if (factor == 0) {
                painter.setRenderHint(QPainter::Antialiasing,
                                      antialiasing_ != ChartAntialiasing::None);
                drawMarks(painter);
            } else {
                const qreal ratio = devicePixelRatioF();
                const QSize pixelSize = (plot.size() * ratio * factor).toSize();
                if (pixelSize.isEmpty() || pixelSize.width() > 16384
                    || pixelSize.height() > 16384) {
                    // Beyond the raster limit supersampling would allocate an
                    // image the engine cannot hold; fall back to the grey raster.
                    painter.setRenderHint(QPainter::Antialiasing, true);
                    drawMarks(painter);
                } else {
                    QImage buffer(pixelSize, QImage::Format_ARGB32_Premultiplied);
                    buffer.setDevicePixelRatio(ratio * factor);
                    buffer.fill(Qt::transparent);
                    {
                        QPainter offscreen(&buffer);
                        offscreen.setRenderHint(QPainter::Antialiasing, true);
                        offscreen.setRenderHint(QPainter::SmoothPixmapTransform, true);
                        offscreen.translate(-plot.topLeft());
                        drawMarks(offscreen);
                    }
                    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                    painter.drawImage(plot.topLeft(), buffer);
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
