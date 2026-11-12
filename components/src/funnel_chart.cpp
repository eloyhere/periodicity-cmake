// Periodicity — the FunnelChart component.
//
// The painter walks the stages once for the fills, then draws the hover
// highlight, then the text, so the highlight never washes a label out. All
// stages share the first series' colour (the palette entry at index 0 unless
// the series carries an explicit colour); the stage labels and percentages use
// onSurface or onPrimary, whichever has the higher contrast against that
// colour. No colour is hard-coded and every length goes through Density::dp().
//
// The geometry builder is shared by painting, stageAt() and stageRect(), so a
// hit test can never disagree with the drawn silhouette. The first value
// defines the reference width; a stage's leading half-width is its own
// fraction, its trailing half-width is the next stage's fraction, and the last
// stage tapers to the point. The gap is removed from the main-axis extent
// before the stages are divided, which keeps the overall span inside plotRect()
// for any stage count.
//
// The component leaves the title, legend and empty state to ChartBase. The
// base's frame is switched off in the constructor with setAxesVisible(false),
// because a funnel has no numeric axes; plotRect() is then the full inner
// rectangle the silhouette divides. A series with no finite value reaches
// paintEmptyState() through the base's hasData() predicate without any help
// from this class.

#include <md/components/funnel_chart.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace md {
namespace {

constexpr qreal kMaximumCrossFraction = 0.9;
constexpr qreal kHoverOutlineDp = 2.0;
constexpr qreal kHoverOverlayAlpha = 0.10;
constexpr qreal kTextInsetDp = 4.0;
constexpr qreal kLineGapDp = 2.0;
constexpr qreal kMinimumTextWidthDp = 28.0;
constexpr qreal kMinimumTextHeightDp = 18.0;

class PainterStateGuard
{
public:
    explicit PainterStateGuard(QPainter& painter) : painter_(painter) { painter_.save(); }
    ~PainterStateGuard() { painter_.restore(); }

    PainterStateGuard(const PainterStateGuard&) = delete;
    PainterStateGuard& operator=(const PainterStateGuard&) = delete;

private:
    QPainter& painter_;
};

[[nodiscard]] qreal linearChannel(qreal value)
{
    return value <= 0.04045 ? value / 12.92
                            : std::pow((value + 0.055) / 1.055, 2.4);
}

[[nodiscard]] qreal relativeLuminance(const QColor& colour)
{
    return 0.2126 * linearChannel(colour.redF())
        + 0.7152 * linearChannel(colour.greenF())
        + 0.0722 * linearChannel(colour.blueF());
}

[[nodiscard]] qreal contrastRatio(qreal first, qreal second)
{
    const qreal lighter = std::max(first, second);
    const qreal darker = std::min(first, second);
    return (lighter + 0.05) / (darker + 0.05);
}

[[nodiscard]] QColor readableTextColour(const QColor& background, const ColorScheme& colours)
{
    const qreal luminance = relativeLuminance(background);
    const qreal onSurface = contrastRatio(luminance, relativeLuminance(colours.onSurface));
    const qreal onPrimary = contrastRatio(luminance, relativeLuminance(colours.onPrimary));
    return onSurface >= onPrimary ? colours.onSurface : colours.onPrimary;
}

// "100%" for whole values, one trimmed decimal below ten, so a small stage
// still reads as a percentage instead of a bare zero.
[[nodiscard]] QString percentageLabel(qreal percentage)
{
    if (!std::isfinite(percentage))
        return QString();

    const int decimals = percentage >= 10.0 ? 0 : 1;
    QString text = QString::number(percentage, 'f', decimals);

    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(QLatin1Char('.')))
            text.chop(1);
    }

    return text + QStringLiteral("%");
}

} // namespace

FunnelChart::FunnelChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAxesVisible(false);

    // Read-only data surface: a whole-surface hover tint would misrepresent the
    // painted colours; per-chart hover is painted by the chart itself.
    setInteractionFeedbackEnabled(false);
}

void FunnelChart::setStageLabels(QStringList labels)
{
    if (stageLabels_ == labels)
        return;

    stageLabels_ = std::move(labels);
    requestUpdate();
}

QStringList FunnelChart::stageLabels() const
{
    return stageLabels_;
}

void FunnelChart::setShowPercentages(bool show)
{
    if (showPercentages_ == show)
        return;

    showPercentages_ = show;
    requestUpdate();
}

bool FunnelChart::showsPercentages() const noexcept
{
    return showPercentages_;
}

void FunnelChart::setGap(qreal dp)
{
    const qreal value = std::isfinite(dp) ? std::max<qreal>(0.0, dp) : 0.0;
    if (gapDp_ == value)
        return;

    gapDp_ = value;
    requestUpdate();
}

qreal FunnelChart::gap() const noexcept
{
    return gapDp_;
}

void FunnelChart::setHorizontal(bool horizontal)
{
    if (horizontal_ == horizontal)
        return;

    horizontal_ = horizontal;
    requestUpdate();
}

bool FunnelChart::isHorizontal() const noexcept
{
    return horizontal_;
}

int FunnelChart::stageCount() const noexcept
{
    const QVector<ChartSeries>& entries = series();
    if (entries.isEmpty())
        return 0;

    return static_cast<int>(entries.first().points.size());
}

qreal FunnelChart::stagePercentage(int stage) const
{
    if (stage < 0 || stage >= stageCount())
        return 0.0;

    const QVector<QPointF>& points = series().first().points;
    const qreal reference = points.first().y();
    if (!std::isfinite(reference) || !(reference > 0.0))
        return 0.0;

    const qreal value = points.at(stage).y();
    if (!std::isfinite(value))
        return 0.0;

    return value / reference * 100.0;
}

QVector<QPointF> FunnelChart::stagePolygon(int stage, const QRectF& plot) const
{
    const int count = stageCount();
    if (plot.isEmpty() || stage < 0 || stage >= count)
        return {};

    const QVector<QPointF>& points = series().first().points;
    const qreal reference = points.first().y();

    const auto fractionFor = [&](int index) {
        if (index < 0 || index >= count)
            return 0.0;
        const qreal value = points.at(index).y();
        if (!std::isfinite(value) || !(reference > 0.0))
            return 0.0;
        return std::clamp(value / reference, 0.0, 1.0);
    };

    const qreal leading = fractionFor(stage);
    const qreal trailing = stage + 1 < count ? fractionFor(stage + 1) : 0.0;

    const Density& density = Theme::forWidget(this).density();
    const qreal gap = density.dp(gapDp_);
    const qreal totalGap = gap * (count > 1 ? count - 1 : 0);
    const qreal extent = horizontal_ ? plot.width() : plot.height();
    const qreal stageLength = (extent - totalGap) / count;
    if (!(stageLength > 0.0))
        return {};

    const qreal offset = stage * (stageLength + gap);
    const qreal cross = horizontal_ ? plot.height() : plot.width();
    const qreal maximumWidth = cross * kMaximumCrossFraction;
    const qreal halfLeading = maximumWidth * 0.5 * leading;
    const qreal halfTrailing = maximumWidth * 0.5 * trailing;

    QVector<QPointF> polygon;
    polygon.reserve(4);

    if (horizontal_) {
        const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
        const auto mapX = [&](qreal offsetFromStart) {
            return rightToLeft ? plot.right() - offsetFromStart
                               : plot.left() + offsetFromStart;
        };
        const qreal centreY = plot.center().y();
        const qreal start = offset;
        const qreal end = offset + stageLength;

        polygon.append(QPointF(mapX(start), centreY - halfLeading));
        polygon.append(QPointF(mapX(start), centreY + halfLeading));
        polygon.append(QPointF(mapX(end), centreY + halfTrailing));
        polygon.append(QPointF(mapX(end), centreY - halfTrailing));
    } else {
        const qreal centreX = plot.center().x();
        const qreal start = plot.top() + offset;
        const qreal end = start + stageLength;

        polygon.append(QPointF(centreX - halfLeading, start));
        polygon.append(QPointF(centreX + halfLeading, start));
        polygon.append(QPointF(centreX + halfTrailing, end));
        polygon.append(QPointF(centreX - halfTrailing, end));
    }

    return polygon;
}

int FunnelChart::stageAt(QPointF position) const
{
    if (!std::isfinite(position.x()) || !std::isfinite(position.y()))
        return -1;

    const QRectF plot = plotRect();
    const int count = stageCount();
    if (plot.isEmpty() || count <= 0)
        return -1;

    for (int stage = 0; stage < count; ++stage) {
        const QVector<QPointF> vertices = stagePolygon(stage, plot);
        if (vertices.size() < 4)
            continue;

        QPolygonF polygon;
        polygon.reserve(vertices.size());
        for (const QPointF& vertex : vertices)
            polygon.append(vertex);

        if (polygon.containsPoint(position, Qt::OddEvenFill))
            return stage;
    }

    return -1;
}

QRectF FunnelChart::stageRect(int stage) const
{
    const QVector<QPointF> vertices = stagePolygon(stage, plotRect());
    if (vertices.isEmpty())
        return QRectF();

    QPolygonF polygon;
    polygon.reserve(vertices.size());
    for (const QPointF& vertex : vertices)
        polygon.append(vertex);

    return polygon.boundingRect();
}

int FunnelChart::hoveredStage() const noexcept
{
    return hoveredStage_;
}

void FunnelChart::setHoveredStage(int stage)
{
    if (hoveredStage_ == stage)
        return;

    hoveredStage_ = stage;
    requestUpdate();
}

void FunnelChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    if (plot.isEmpty())
        return;

    const PainterStateGuard guard(painter);

    try {
        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();

        const int count = stageCount();
        if (count <= 0) {
            // Another series can satisfy the base data gate while the funnel's
            // own (first) series has no stage to draw.
            ChartBase::paintEmptyState(painter, plot);
            return;
        }

        QColor fillColour = seriesColour(0);
        if (!fillColour.isValid())
            fillColour = colours.primary;

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fillColour);

        QVector<QPolygonF> shapes;
        shapes.reserve(count);

        for (int stage = 0; stage < count; ++stage) {
            const QVector<QPointF> vertices = stagePolygon(stage, plot);
            if (vertices.size() < 4) {
                shapes.append(QPolygonF());
                continue;
            }

            QPolygonF polygon;
            polygon.reserve(vertices.size());
            for (const QPointF& vertex : vertices)
                polygon.append(vertex);

            shapes.append(polygon);
            painter.drawPolygon(polygon);
        }

        // 2. Hover highlight, under the text so the label stays legible.
        if (hoveredStage_ >= 0 && hoveredStage_ < shapes.size()
            && shapes.at(hoveredStage_).size() >= 4) {
            QColor overlay = colours.onSurface;
            overlay.setAlphaF(static_cast<float>(kHoverOverlayAlpha));
            painter.setPen(Qt::NoPen);
            painter.setBrush(overlay);
            painter.drawPolygon(shapes.at(hoveredStage_));

            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(colours.onSurface,
                                std::max<qreal>(1.0, density.dp(kHoverOutlineDp))));
            painter.drawPolygon(shapes.at(hoveredStage_));
        }

        // 3. Stage label and percentage, both centred in the trapezoid.
        const QFont nameFont = Font::resolve(this, theme.type().labelMedium, density);
        const QFontMetricsF nameMetrics(nameFont);
        const QFont percentFont = Font::resolve(this, theme.type().labelSmall, density);
        const QFontMetricsF percentMetrics(percentFont);
        const QColor textColour = readableTextColour(fillColour, colours);
        const qreal inset = density.dp(kTextInsetDp);
        const qreal lineGap = density.dp(kLineGapDp);
        const qreal minimumWidth = density.dp(kMinimumTextWidthDp);
        const qreal minimumHeight = density.dp(kMinimumTextHeightDp);

        for (int stage = 0; stage < count; ++stage) {
            const QPolygonF& shape = shapes.at(stage);
            if (shape.size() < 4)
                continue;

            const QRectF bounds = shape.boundingRect();
            if (bounds.width() < minimumWidth || bounds.height() < minimumHeight)
                continue;

            const QString label = stage < stageLabels_.size() ? stageLabels_.at(stage)
                                                              : QString();
            const QString percent = showPercentages_ ? percentageLabel(stagePercentage(stage))
                                                     : QString();
            const bool drawLabel = !label.isEmpty();
            const bool drawPercent = !percent.isEmpty();
            if (!drawLabel && !drawPercent)
                continue;

            const qreal totalHeight = (drawLabel ? nameMetrics.height() : 0.0)
                + (drawPercent ? percentMetrics.height() : 0.0)
                + (drawLabel && drawPercent ? lineGap : 0.0);
            if (totalHeight > bounds.height())
                continue;

            const qreal textWidth = std::max<qreal>(0.0, bounds.width() - inset * 2.0);
            if (textWidth <= 0.0)
                continue;

            qreal top = bounds.center().y() - totalHeight * 0.5;

            if (drawLabel) {
                const QRectF nameRect(bounds.left() + inset, top, textWidth,
                                      nameMetrics.height());
                painter.setFont(nameFont);
                painter.setPen(textColour);
                painter.drawText(nameRect, Qt::AlignCenter,
                                 nameMetrics.elidedText(label, Qt::ElideRight, textWidth));
                top += nameMetrics.height() + (drawPercent ? lineGap : 0.0);
            }

            if (drawPercent) {
                const QRectF percentRect(bounds.left() + inset, top, textWidth,
                                         percentMetrics.height());
                painter.setFont(percentFont);
                painter.setPen(textColour);
                painter.drawText(percentRect, Qt::AlignCenter,
                                 percentMetrics.elidedText(percent, Qt::ElideRight, textWidth));
            }
        }
    } catch (const std::exception& error) {
        qWarning("md::FunnelChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::FunnelChart::paintPlot failed with an unknown exception");
    }
}

void FunnelChart::mouseMoveEvent(QMouseEvent* event)
{
    try {
        View::mouseMoveEvent(event);
        setHoveredStage(stageAt(event->position()));
    } catch (const std::exception& error) {
        qWarning("md::FunnelChart::mouseMoveEvent failed: %s", error.what());
        setHoveredStage(-1);
    } catch (...) {
        qWarning("md::FunnelChart::mouseMoveEvent failed with an unknown exception");
        setHoveredStage(-1);
    }
}

void FunnelChart::leaveEvent(QEvent* event)
{
    try {
        View::leaveEvent(event);
        setHoveredStage(-1);
    } catch (const std::exception& error) {
        qWarning("md::FunnelChart::leaveEvent failed: %s", error.what());
        setHoveredStage(-1);
    } catch (...) {
        qWarning("md::FunnelChart::leaveEvent failed with an unknown exception");
        setHoveredStage(-1);
    }
}

} // namespace md
