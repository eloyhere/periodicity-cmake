// Periodicity — the LineChart component.
//
// The paint path is deliberately small: ChartBase has already painted the
// title, legend, grid and axes and installed plotRect() on the transform, so
// this file only maps the finite samples and strokes the series. Colours come
// from ChartBase::seriesColour() (an explicit series colour or the themed
// categorical palette) and every length is converted through Density::dp().
//
// The fill pass runs before the stroke pass so a translucent area from one
// series cannot wash over another series' line. The painter is clipped to the
// plot rectangle (plus room for edge markers), because ChartBase leaves
// paintPlot() unclipped by design and a Catmull-Rom curve can overshoot the
// data range.
//
// A throwing paintPlot() is caught locally, even though ChartBase also guards
// the call: keeping the failure here preserves the painter state and makes the
// component's contract independent of its host.

#include <md/components/line_chart.h>

#include <md/theme/theme.h>

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace md {
namespace {

constexpr qreal kFillAlpha = 0.16;
constexpr qreal kMarkerRadiusDp = 2.5;
constexpr qreal kMinimumPenWidth = 0.5;
constexpr qreal kMinimumMarkerRadius = 1.0;

// Restores the painter on every exit path, including one taken by an
// exception. A manual save()/restore() pair around a try block would leak the
// clip and pen state into the next paint stage when a helper throws.
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

// One series reduced to pixel space. A non-finite sample ends the current run,
// so the polyline breaks at a gap instead of bridging it, as ChartSeries'
// contract requires.
struct MappedSeries
{
    QVector<QVector<QPointF>> runs;
    QColor colour;
    bool filled = false;
};

// Catmull-Rom through the samples, as cubic Béziers. The endpoints are clamped,
// so the curve interpolates the first and last points exactly and there is no
// phantom segment outside the data range.
[[nodiscard]] QPainterPath buildSeriesPath(const QVector<QPointF>& points, bool smooth)
{
    QPainterPath path;
    if (points.isEmpty())
        return path;

    path.moveTo(points.first());
    if (!smooth || points.size() < 3) {
        for (qsizetype i = 1; i < points.size(); ++i)
            path.lineTo(points.at(i));
        return path;
    }

    for (qsizetype i = 0; i + 1 < points.size(); ++i) {
        const QPointF previous = points.at(std::max<qsizetype>(i - 1, 0));
        const QPointF start = points.at(i);
        const QPointF end = points.at(i + 1);
        const QPointF next = points.at(std::min<qsizetype>(i + 2, points.size() - 1));

        const QPointF control1 = start + (end - previous) / 6.0;
        const QPointF control2 = end - (next - start) / 6.0;
        path.cubicTo(control1, control2, end);
    }

    return path;
}

} // namespace

LineChart::LineChart(QWidget* parent)
    : ChartBase(parent)
{
    // ChartBase already disables focus; the line chart adds no interaction of
    // its own.

    // Read-only data surface: a whole-surface hover tint would misrepresent the
    // painted colours; per-chart hover is painted by the chart itself.
    setInteractionFeedbackEnabled(false);
}

void LineChart::setSmooth(bool smooth)
{
    if (smooth_ == smooth)
        return;

    smooth_ = smooth;
    requestUpdate();
}

bool LineChart::isSmooth() const noexcept
{
    return smooth_;
}

void LineChart::setShowMarkers(bool showMarkers)
{
    if (showMarkers_ == showMarkers)
        return;

    showMarkers_ = showMarkers;
    requestUpdate();
}

bool LineChart::showsMarkers() const noexcept
{
    return showMarkers_;
}

void LineChart::setLineWidth(qreal dp)
{
    const qreal width = dp > 0.0 ? dp : 0.0;
    if (lineWidthDp_ == width)
        return;

    lineWidthDp_ = width;
    requestUpdate();
}

qreal LineChart::lineWidth() const noexcept
{
    return lineWidthDp_;
}

void LineChart::measureOverride(MeasureSpec width, MeasureSpec height)
{
    try {
        // The base owns the preferred chart size (and the title/legend-aware
        // layout that consumes it), so the subclass only guard-rails the call.
        ChartBase::measureOverride(width, height);
    } catch (const std::exception& error) {
        qWarning("md::LineChart::measureOverride failed: %s", error.what());
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    } catch (...) {
        qWarning("md::LineChart::measureOverride failed with an unknown exception");
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    }
}

void LineChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    if (plot.isEmpty())
        return;

    [[maybe_unused]] const PainterStateGuard guard(painter);
    try {
        const Theme theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const QVector<ChartSeries>& entries = series();

        const qreal penWidth = std::max(kMinimumPenWidth, density.dp(lineWidthDp_));
        const qreal markerRadius = std::max(kMinimumMarkerRadius, density.dp(kMarkerRadiusDp));

        // One mapping pass, shared by the fill and the stroke pass.
        QVector<MappedSeries> mapped;
        mapped.reserve(entries.size());

        for (qsizetype i = 0; i < entries.size(); ++i) {
            const ChartSeries& entry = entries.at(i);
            MappedSeries mappedSeries;
            mappedSeries.colour = seriesColour(static_cast<int>(i));
            mappedSeries.filled = entry.filled;

            QVector<QPointF> run;
            run.reserve(entry.points.size());
            for (const QPointF& point : entry.points) {
                if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
                    if (!run.isEmpty()) {
                        mappedSeries.runs.append(std::move(run));
                        run.clear();
                    }
                    continue;
                }
                run.append(math()->toPixel(point));
            }
            if (!run.isEmpty())
                mappedSeries.runs.append(std::move(run));

            mapped.append(std::move(mappedSeries));
        }

        [[maybe_unused]] const PainterStateGuard clipGuard(painter);
        const qreal clipMargin = std::max(penWidth, markerRadius) + 2.0;
        painter.setClipRect(plot.adjusted(-clipMargin, -clipMargin, clipMargin, clipMargin),
                            Qt::IntersectClip);

        // 1. Area fills, behind every stroke.
        painter.setPen(Qt::NoPen);
        for (const MappedSeries& item : mapped) {
            if (!item.filled || !item.colour.isValid())
                continue;

            for (const QVector<QPointF>& run : item.runs) {
                if (run.size() < 2)
                    continue;

                const QPainterPath line = buildSeriesPath(run, smooth_);
                QPainterPath area = line;
                area.lineTo(run.last().x(), plot.bottom());
                area.lineTo(run.first().x(), plot.bottom());
                area.closeSubpath();

                QColor fill = item.colour;
                fill.setAlphaF(kFillAlpha);
                painter.fillPath(area, fill);
            }
        }

        // 2. Strokes and optional markers, in series order.
        for (const MappedSeries& item : mapped) {
            if (!item.colour.isValid())
                continue;

            for (const QVector<QPointF>& run : item.runs) {
                if (run.isEmpty())
                    continue;

                QPen pen(item.colour, penWidth);
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(buildSeriesPath(run, smooth_));

                if (showMarkers_) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(item.colour);
                    for (const QPointF& pixel : run)
                        painter.drawEllipse(pixel, markerRadius, markerRadius);
                }
            }
        }
    } catch (const std::exception& error) {
        qWarning("md::LineChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::LineChart::paintPlot failed with an unknown exception");
    }
}

} // namespace md
