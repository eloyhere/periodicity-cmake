// Periodicity — the PieChart component.
//
// The paint pass resolves every colour and dimension from the theme snapshot
// taken at the start of the frame: slice colours come from ChartBase's
// seriesColour(), the percentage labels use the surface/on-surface pair chosen
// by contrast, and every length is converted through Density::dp(). No colour is
// written literally and no pixel size assumes a particular display scale.
//
// The base paints the title, the legend and the empty state; this file paints
// only the disc, its labels and the selection ring, and it turns the Cartesian
// frame off because a pie has no axes.
//
// Geometry is derived once per call from the settled angular layout
// (buildSlices()): a slice's raw offset and share define where it starts and
// how wide it is, and the gap only shrinks the painted span. sliceAt() walks
// the same offsets, so a click can never disagree with the pixels, and the
// reveal budget only limits how much of a span is painted — it never moves an
// edge that hit-testing sees.

#include <md/components/pie_chart.h>

#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>
#include <QSizeF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace md {
namespace {

constexpr qreal kPi = 3.14159265358979323846;
constexpr qreal kDegreesPerRadian = 180.0 / kPi;

constexpr qreal kPreferredSizeDp = 260.0;

// Room between the plot rectangle and the disc, plus the distance a selected
// slice travels outwards. Both are reserved, so selecting a slice never pushes
// it outside the plot rectangle.
constexpr qreal kPieInsetDp = 2.0;
constexpr qreal kExplodeDp = 4.0;

// Where a percentage label sits, as a fraction of the outer radius.
constexpr qreal kLabelRadiusFactor = 0.62;
constexpr qreal kLabelPaddingDp = 4.0;

// Below this share a slice is too narrow to label; the threshold is part of the
// documented contract and is reported by sliceLabel().
constexpr qreal kMinimumLabelShare = 5.0;

// Slices are drawn this much wider than their settled span so adjacent fills
// overlap and no hairline of background shows through an antialiased seam.
// Only the paint path uses it; sliceSpan() stays exact.
constexpr qreal kSeamOverlapDegrees = 0.15;

constexpr qreal kSelectedPenDp = 1.5;

[[nodiscard]] QFont fontFor(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(QFont::Weight(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

void paintNoDataMessage(QPainter& painter, const QRectF& plot, const Theme& theme,
                        const QString& text)
{
    if (plot.isEmpty())
        return;

    painter.setFont(fontFor(theme, theme.type().bodyMedium));
    painter.setPen(theme.colours().onSurfaceVariant);
    painter.drawText(plot, Qt::AlignCenter, text);
}

// Picks whichever of the surface/on-surface pair contrasts more with `fill`.
// The pair is always a contrast pair in both brightnesses, so the rule works for
// a light and a dark theme without hard-coding white or black.
[[nodiscard]] QColor readableOn(const QColor& fill, const ColorScheme& colours)
{
    const qreal fillLightness = fill.lightnessF();
    const qreal againstSurface = std::abs(fillLightness - colours.surface.lightnessF());
    const qreal againstOnSurface = std::abs(fillLightness - colours.onSurface.lightnessF());
    return againstSurface >= againstOnSurface ? colours.surface : colours.onSurface;
}

// The square the disc is inscribed in: the smaller plot side, less the reserved
// margin, centred in the plot.
[[nodiscard]] QRectF pieArea(const QRectF& plot, const Density& density)
{
    if (plot.width() <= 0.0 || plot.height() <= 0.0)
        return {};

    const qreal inset = density.dp(kPieInsetDp) + density.dp(kExplodeDp);
    const qreal radius = std::min(plot.width(), plot.height()) / 2.0 - inset;
    if (!(radius > 0.0))
        return {};

    const QPointF centre = plot.center();
    return QRectF(centre.x() - radius, centre.y() - radius, radius * 2.0, radius * 2.0);
}

// A point on the circle at `degrees` clockwise from twelve o'clock.
[[nodiscard]] QPointF polar(const QPointF& centre, qreal degrees, qreal radius)
{
    const qreal radians = degrees / kDegreesPerRadian;
    return QPointF(centre.x() + std::sin(radians) * radius,
                   centre.y() - std::cos(radians) * radius);
}

[[nodiscard]] qreal normaliseDegrees(qreal degrees)
{
    qreal value = std::fmod(degrees, 360.0);
    if (value < 0.0)
        value += 360.0;
    return value;
}

} // namespace

PieChart::PieChart(QWidget* parent)
    : ChartBase(parent)
{
    // A pie has no Cartesian frame: without this the base would paint a grid,
    // axes and numeric tick labels behind the disc and reserve their gutters.
    setAxesVisible(false);

    // The chart is a data surface, not a control: it takes no keyboard focus and
    // the shared state/ripple layers would wash over the slices. Clicks are
    // still delivered and handled by the overrides below.
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(false);
    setInteractionFeedbackEnabled(false);
}

void PieChart::setSeries(QVector<ChartSeries> series)
{
    ChartBase::setSeries(std::move(series));
    // The stored index described a slice that may not exist any more.
    selectedSlice_ = -1;
    pressedSlice_ = -1;
    requestUpdate();
}

void PieChart::setDonut(bool donut)
{
    if (donut_ == donut)
        return;

    donut_ = donut;
    requestUpdate();
}

bool PieChart::isDonut() const noexcept
{
    return donut_;
}

void PieChart::setDonutRatio(qreal ratio)
{
    const qreal clamped = std::clamp(ratio, 0.0, 0.95);
    if (!std::isfinite(clamped) || donutRatio_ == clamped)
        return;

    donutRatio_ = clamped;
    requestUpdate();
}

qreal PieChart::donutRatio() const noexcept
{
    return donutRatio_;
}

void PieChart::setShowPercentages(bool show)
{
    if (showPercentages_ == show)
        return;

    showPercentages_ = show;
    requestUpdate();
}

bool PieChart::showsPercentages() const noexcept
{
    return showPercentages_;
}

void PieChart::setStartAngle(qreal degrees)
{
    if (!std::isfinite(degrees))
        return;

    const qreal normalised = normaliseDegrees(degrees);
    if (startAngle_ == normalised)
        return;

    startAngle_ = normalised;
    requestUpdate();
}

qreal PieChart::startAngle() const noexcept
{
    return startAngle_;
}

void PieChart::setSliceGap(qreal degrees)
{
    if (!std::isfinite(degrees))
        return;

    const qreal clamped = std::clamp(degrees, 0.0, 120.0);
    if (sliceGap_ == clamped)
        return;

    sliceGap_ = clamped;
    requestUpdate();
}

qreal PieChart::sliceGap() const noexcept
{
    return sliceGap_;
}

void PieChart::setSelectedSlice(int index)
{
    const int selected = index >= 0 ? index : -1;
    if (selectedSlice_ == selected)
        return;

    selectedSlice_ = selected;
    requestUpdate();
}

int PieChart::selectedSlice() const noexcept
{
    return selectedSlice_;
}

int PieChart::sliceCount() const
{
    return static_cast<int>(buildSlices().size());
}

qreal PieChart::sliceSpan(int index) const
{
    const QVector<Slice> slices = buildSlices();
    for (const Slice& slice : slices) {
        if (slice.index == index)
            return slice.span;
    }
    return 0.0;
}

QString PieChart::sliceLabel(int index) const
{
    if (!showPercentages_)
        return {};

    const QVector<Slice> slices = buildSlices();
    const Slice* match = nullptr;
    for (const Slice& slice : slices) {
        if (slice.index == index) {
            match = &slice;
            break;
        }
    }
    if (match == nullptr)
        return {};

    const qreal total = totalValue();
    if (!(total > 0.0))
        return {};

    const qreal share = match->value / total * 100.0;
    if (share < kMinimumLabelShare)
        return {};

    return QStringLiteral("%1%").arg(qRound(share));
}

qreal PieChart::revealProgress() const noexcept
{
    // One animation, one owner: the sweep is the base chart's run.
    return animation().progress();
}

bool PieChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

bool PieChart::rightToLeft() const
{
    return layoutDirection() == Qt::RightToLeft;
}

qreal PieChart::totalValue() const
{
    qreal total = 0.0;
    for (const ChartSeries& item : series()) {
        if (item.points.isEmpty())
            continue;

        const qreal value = item.points.first().y();
        if (std::isfinite(value) && value > 0.0)
            total += value;
    }
    return total;
}

QVector<PieChart::Slice> PieChart::buildSlices() const
{
    QVector<Slice> slices;

    const QVector<ChartSeries>& items = series();
    const qreal total = totalValue();
    if (!(total > 0.0))
        return slices;

    // Clamped once here as well as in the setter, so a value injected by a
    // future member can never produce a negative span.
    const qreal gap = std::clamp(sliceGap_, 0.0, 120.0);

    qreal offset = 0.0;
    for (qsizetype i = 0; i < items.size(); ++i) {
        const ChartSeries& item = items.at(i);
        if (item.points.isEmpty())
            continue;

        const qreal value = item.points.first().y();
        if (!std::isfinite(value) || !(value > 0.0))
            continue;

        const qreal share = value / total * 360.0;

        Slice slice;
        slice.index = static_cast<int>(i);
        slice.offset = offset;
        slice.share = share;
        slice.span = std::max<qreal>(0.0, share - gap);
        slice.value = value;
        slices.append(slice);

        offset += share;
    }

    return slices;
}

QRectF PieChart::pieRect() const
{
    QRectF plot = plotRect();
    if (!plot.isValid())
        plot = contentRect();

    return pieArea(plot, Theme::forWidget(this).density());
}

int PieChart::sliceAt(QPointF pixel) const
{
    const QVector<Slice> slices = buildSlices();
    if (slices.isEmpty())
        return -1;

    const QRectF area = pieRect();
    if (!area.isValid())
        return -1;

    const QPointF centre = area.center();
    const qreal outer = area.width() / 2.0;
    const qreal inner = donut_ ? outer * std::clamp(donutRatio_, 0.0, 0.95) : 0.0;

    const qreal dx = pixel.x() - centre.x();
    const qreal dy = pixel.y() - centre.y();
    const qreal distance = std::hypot(dx, dy);
    if (!(distance <= outer) || distance < inner)
        return -1;

    // atan2(dx, -dy) is zero at twelve o'clock and grows clockwise.
    const qreal angle = normaliseDegrees(std::atan2(dx, -dy) * kDegreesPerRadian);
    const qreal relative = rightToLeft() ? normaliseDegrees(startAngle_ - angle)
                                         : normaliseDegrees(angle - startAngle_);

    // The settled raw spans, not the painted ones: a gap is not a dead zone.
    for (const Slice& slice : slices) {
        if (relative >= slice.offset && relative < slice.offset + slice.share)
            return slice.index;
    }
    return -1;
}

void PieChart::onShown()
{
    // ChartBase starts its reveal from the first paint so that an offscreen
    // render does not animate. The pie is the one chart whose sweep must already
    // be running when show() returns (its revealProgress() is observed right
    // after the show), so it starts the same single run here instead. The call is
    // idempotent: the paint path sees revealStarted_ and leaves it alone.
    startRevealIfNeeded();
}

void PieChart::measureOverride(MeasureSpec width, MeasureSpec height)
{
    try {
        const Density& density = Theme::forWidget(this).density();
        setMeasuredSize(QSizeF(width.resolve(density.dp(kPreferredSizeDp)),
                               height.resolve(density.dp(kPreferredSizeDp))));
    } catch (const std::exception& error) {
        qWarning("md::PieChart::measureOverride failed: %s", error.what());
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    } catch (...) {
        qWarning("md::PieChart::measureOverride failed with an unknown exception");
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    }
}

void PieChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    if (plot.isEmpty())
        return;

    painter.save();
    try {
        const Theme theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const QVector<Slice> slices = buildSlices();
        const QRectF area = pieArea(plot, density);

        if (slices.isEmpty() || !area.isValid()) {
            // hasData() can be true while every value is not positive (a pie of
            // zero and negative values); the base then calls paintPlot() rather
            // than paintEmptyState(), so the message is painted here.
            paintNoDataMessage(painter, plot, theme,
                               localized(QStringLiteral("chart.noData"), QStringLiteral("No data")));
        } else {
            painter.setRenderHint(QPainter::Antialiasing, true);

            const QPointF centre = area.center();
            const qreal outer = area.width() / 2.0;
            const qreal inner = donut_ ? outer * std::clamp(donutRatio_, 0.0, 0.95) : 0.0;
            const bool rtl = rightToLeft();
            const qreal direction = rtl ? -1.0 : 1.0;
            // The sweep is the base chart's single animation run; the pie never
            // owns a second one. Before the first show the run is settled at 1,
            // so a chart that is only rendered paints finished.
            const qreal budget = 360.0 * std::clamp(animation().progress(), 0.0, 1.0);
            const qreal gap = std::clamp(sliceGap_, 0.0, 120.0);
            const qreal explode = density.dp(kExplodeDp);

            const QRectF outerRect(centre.x() - outer, centre.y() - outer, outer * 2.0,
                                   outer * 2.0);
            const QRectF innerRect(centre.x() - inner, centre.y() - inner, inner * 2.0,
                                   inner * 2.0);

            const QFont labelFont = fontFor(theme, theme.type().labelSmall);
            const QFontMetricsF labelMetrics(labelFont);

            // Labels are collected first and drawn after every slice, so a label
            // can never end up underneath the next slice's fill.
            struct Label
            {
                QRectF box;
                QColor colour;
                QString text;
            };
            QVector<Label> labels;

            for (const Slice& slice : slices) {
                const QColor colour = seriesColour(slice.index);
                const qreal pad = std::min(gap / 2.0, slice.share / 2.0);
                const qreal start = startAngle_ + direction * (slice.offset + pad);
                const bool selected = slice.index == selectedSlice_;
                const qreal mid = start + direction * (slice.span / 2.0);
                const QPointF shift = selected ? polar(QPointF(0.0, 0.0), mid, explode)
                                               : QPointF(0.0, 0.0);

                // How much of this slice the sweep has uncovered. The budget is
                // measured from the start angle in the sweep direction.
                const qreal visible = std::clamp(budget - slice.offset - pad, 0.0, slice.span);
                if (visible > 0.0) {
                    // Qt measures arc angles counter-clockwise from three
                    // o'clock, so clockwise-from-twelve maps to 90 - angle. A
                    // sweep that advances our angle therefore runs *against*
                    // Qt's positive direction: LTR (direction +1) needs a
                    // negative sweep, and RTL mirrors it. The seam overlap
                    // makes adjacent fills meet instead of leaving a hairline.
                    const qreal qtStart = 90.0 - start;
                    const qreal sweep = -direction * (visible + kSeamOverlapDegrees);

                    QPainterPath path;
                    if (inner > 0.0) {
                        path.arcMoveTo(outerRect.translated(shift), qtStart);
                        path.arcTo(outerRect.translated(shift), qtStart, sweep);
                        path.arcTo(innerRect.translated(shift), qtStart + sweep, -sweep);
                        path.closeSubpath();
                    } else {
                        path.moveTo(centre + shift);
                        path.arcTo(outerRect.translated(shift), qtStart, sweep);
                        path.closeSubpath();
                    }

                    painter.setPen(Qt::NoPen);
                    painter.setBrush(colour);
                    painter.drawPath(path);

                    if (selected) {
                        const qreal penWidth = std::max<qreal>(1.0, density.dp(kSelectedPenDp));
                        painter.setBrush(Qt::NoBrush);
                        painter.setPen(QPen(theme.colours().outline, penWidth));
                        painter.drawPath(path);
                    }
                }

                // A label waits until its slice is fully revealed and only
                // appears when the arc at the label radius can hold the text.
                const QString text = sliceLabel(slice.index);
                if (text.isEmpty() || visible < slice.span - 1e-6)
                    continue;

                const qreal radius = inner > 0.0 ? (inner + outer) / 2.0
                                                 : outer * kLabelRadiusFactor;
                // The room the slice offers the label is the length of the arc
                // at the label radius, not the chord between the slice edges:
                // past 180 degrees the chord shrinks again while the arc keeps
                // growing, so a chord test would drop the label of the widest
                // slice exactly when there is the most room for it.
                const qreal arc = radius * slice.span / kDegreesPerRadian;
                const qreal textWidth = labelMetrics.horizontalAdvance(text);
                const qreal textHeight = labelMetrics.height();
                const qreal padding = density.dp(kLabelPaddingDp);

                bool fits = arc >= textWidth + padding;
                if (inner > 0.0)
                    fits = fits && (outer - inner) >= textHeight + padding / 2.0;
                if (!fits)
                    continue;

                const QPointF labelCentre = polar(centre + shift, mid, radius);
                Label label;
                label.box = QRectF(labelCentre.x() - (textWidth + padding) / 2.0,
                                   labelCentre.y() - textHeight / 2.0,
                                   textWidth + padding, textHeight);
                label.colour = readableOn(colour, theme.colours());
                label.text = text;
                labels.append(label);
            }

            painter.setFont(labelFont);
            for (const Label& label : labels) {
                painter.setPen(label.colour);
                painter.drawText(label.box, Qt::AlignCenter, label.text);
            }
        }
    } catch (const std::exception& error) {
        qWarning("md::PieChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::PieChart::paintPlot failed with an unknown exception");
    }
    painter.restore();
}

void PieChart::mousePressEvent(QMouseEvent* event)
{
    try {
        View::mousePressEvent(event);
        pressedSlice_ = sliceAt(event->position());
    } catch (const std::exception& error) {
        qWarning("md::PieChart::mousePressEvent failed: %s", error.what());
        pressedSlice_ = -1;
    } catch (...) {
        qWarning("md::PieChart::mousePressEvent failed with an unknown exception");
        pressedSlice_ = -1;
    }
}

void PieChart::mouseReleaseEvent(QMouseEvent* event)
{
    try {
        View::mouseReleaseEvent(event);

        const int released = sliceAt(event->position());
        const bool activated = released >= 0 && released == pressedSlice_;
        pressedSlice_ = -1;

        if (activated) {
            setSelectedSlice(released);
            emit sliceActivated(released);
        }
    } catch (const std::exception& error) {
        qWarning("md::PieChart::mouseReleaseEvent failed: %s", error.what());
        pressedSlice_ = -1;
    } catch (...) {
        qWarning("md::PieChart::mouseReleaseEvent failed with an unknown exception");
        pressedSlice_ = -1;
    }
}

} // namespace md
