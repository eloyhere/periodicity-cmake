// Periodicity — MathematicsCoordination implementation (framework.md §5.9, §7).
//
// The transform is deliberately plain arithmetic rather than a QTransform:
// every value a chart feeds to toPixel() is a data point, not a path, and the
// axis mapping is easier to audit as two independent affine maps. The only
// piece of state that changes with time is visibleBounds_, which the fit
// animation interpolates; toPixel() always reads the current value, so a chart
// that paints during a fit simply follows it.
//
// The frame painter is shared by the standalone widget and by ChartBase: it
// reads plotArea(), which is either the host's rectangle or the local
// contentRect()-derived one, and draws into the painter it is handed. No second
// QPainter is created, so the widget never needs to be visible or parented to
// participate in a chart's paint pass.

#include <md/components/math_coordination.h>

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
#include <utility>

namespace md {

namespace {

constexpr qreal kEpsilon = 1.0e-9;

constexpr qreal kTickLengthDp = 4.0;
constexpr qreal kLabelGapDp = 4.0;
constexpr qreal kHairlineDp = 1.0;
constexpr qreal kPlotTopPadDp = 4.0;
constexpr qreal kPlotRightPadDp = 4.0;
constexpr qreal kPreferredWidthDp = 320.0;
constexpr qreal kPreferredHeightDp = 220.0;

constexpr qreal kMinimumZoomFactor = 1.0e-3;
constexpr qreal kMaximumZoomFactor = 1.0e3;

constexpr int kMinimumTickCount = 2;
constexpr int kMaximumTickCount = 12;
constexpr int kMinimumFitTicks = 4;
constexpr int kMaximumFitTicks = 8;

// Validates one axis and widens a zero span to a unit span. The function is
// used by every entry point that accepts a range, so the "NaN is rejected,
// empty is widened" rule lives in one place.
bool normaliseBounds(qreal& minimum, qreal& maximum)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum))
        return false;

    if (maximum < minimum)
        std::swap(minimum, maximum);

    if (maximum == minimum) {
        const qreal centre = minimum;
        qreal lower = centre - 0.5;
        qreal upper = centre + 0.5;

        // At very large magnitudes ±0.5 rounds back to the same value; step to
        // the neighbouring representable values instead so the span is still
        // positive. If even that is not finite the range is rejected.
        if (!(upper > lower)) {
            lower = std::nextafter(centre, -std::numeric_limits<qreal>::infinity());
            upper = std::nextafter(centre, std::numeric_limits<qreal>::infinity());
        }

        if (!std::isfinite(lower) || !std::isfinite(upper) || !(upper > lower))
            return false;

        minimum = lower;
        maximum = upper;
    }

    // A finite pair can still produce an infinite span (for example
    // -1e308..1e308); such a range has no usable reciprocal scale.
    if (!std::isfinite(maximum - minimum))
        return false;

    return std::isfinite(minimum) && std::isfinite(maximum) && maximum > minimum;
}

} // namespace

MathematicsCoordination::MathematicsCoordination(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

MathematicsCoordination::~MathematicsCoordination() = default;

void MathematicsCoordination::setDataBounds(QPointF minimum, QPointF maximum)
{
    qreal xMinimum = minimum.x();
    qreal xMaximum = maximum.x();
    qreal yMinimum = minimum.y();
    qreal yMaximum = maximum.y();

    if (!normaliseBounds(xMinimum, xMaximum) || !normaliseBounds(yMinimum, yMaximum)) {
        qWarning("md::MathematicsCoordination::setDataBounds: bounds must be finite");
        return;
    }

    const QRectF bounds(QPointF(xMinimum, yMinimum), QPointF(xMaximum, yMaximum));
    if (bounds == dataBounds_)
        return;

    dataBounds_ = bounds;

    if (autoFit_) {
        applyFit(true);
    } else {
        // Manual mode: dataBounds() is metadata for the next fit and for hit
        // testing; the viewport the user navigated to stays where it is.
        plotAreaDirty_ = true;
        requestUpdate();
        emit transformChanged();
    }
}

QRectF MathematicsCoordination::dataBounds() const noexcept
{
    return dataBounds_;
}

void MathematicsCoordination::setVisibleBounds(QPointF minimum, QPointF maximum)
{
    qreal xMinimum = minimum.x();
    qreal xMaximum = maximum.x();
    qreal yMinimum = minimum.y();
    qreal yMaximum = maximum.y();

    if (!normaliseBounds(xMinimum, xMaximum) || !normaliseBounds(yMinimum, yMaximum)) {
        qWarning("md::MathematicsCoordination::setVisibleBounds: bounds must be finite");
        return;
    }

    const QRectF bounds(QPointF(xMinimum, yMinimum), QPointF(xMaximum, yMaximum));

    autoFit_ = false;
    animator_.stop();
    visibleBounds_ = bounds;
    fittedBounds_ = bounds;
    plotAreaDirty_ = true;
    requestUpdate();
    emit transformChanged();
}

QRectF MathematicsCoordination::visibleBounds() const noexcept
{
    return visibleBounds_;
}

QRectF MathematicsCoordination::fittedBounds() const noexcept
{
    return fittedBounds_;
}

void MathematicsCoordination::setViewportPadding(qreal dp)
{
    if (!std::isfinite(dp) || dp < 0.0) {
        qWarning("md::MathematicsCoordination::setViewportPadding: padding must be finite and non-negative");
        return;
    }

    if (viewportPadding_ == dp)
        return;

    viewportPadding_ = dp;
    plotAreaDirty_ = true;
    requestUpdate();
}

qreal MathematicsCoordination::viewportPadding() const noexcept
{
    return viewportPadding_;
}

QPointF MathematicsCoordination::toPixel(QPointF data) const
{
    const QRectF plot = plotArea();
    const QPointF centre = plot.center();

    if (!std::isfinite(data.x()) || !std::isfinite(data.y()))
        return centre;

    const qreal xSpan = visibleBounds_.width();
    const qreal ySpan = visibleBounds_.height();

    qreal x = centre.x();
    qreal y = centre.y();

    if (plot.width() > 0.0 && xSpan > 0.0)
        x = plot.left() + (data.x() - visibleBounds_.left()) / xSpan * plot.width();

    if (plot.height() > 0.0 && ySpan > 0.0)
        y = plot.bottom() - (data.y() - visibleBounds_.top()) / ySpan * plot.height();

    return QPointF(x, y);
}

QPointF MathematicsCoordination::toData(QPointF pixel) const
{
    const QRectF plot = plotArea();
    const qreal xSpan = visibleBounds_.width();
    const qreal ySpan = visibleBounds_.height();

    if (!std::isfinite(pixel.x()) || !std::isfinite(pixel.y()))
        return visibleBounds_.center();

    qreal x = visibleBounds_.left() + xSpan * 0.5;
    qreal y = visibleBounds_.top() + ySpan * 0.5;

    if (plot.width() > 0.0)
        x = visibleBounds_.left() + (pixel.x() - plot.left()) / plot.width() * xSpan;

    if (plot.height() > 0.0)
        y = visibleBounds_.top() + (plot.bottom() - pixel.y()) / plot.height() * ySpan;

    return QPointF(x, y);
}

void MathematicsCoordination::setXAxisLabel(const QString& label)
{
    if (xAxisLabel_ == label)
        return;

    xAxisLabel_ = label;
    plotAreaDirty_ = true;
    requestUpdate();
}

QString MathematicsCoordination::xAxisLabel() const
{
    return xAxisLabel_;
}

void MathematicsCoordination::setYAxisLabel(const QString& label)
{
    if (yAxisLabel_ == label)
        return;

    yAxisLabel_ = label;
    plotAreaDirty_ = true;
    requestUpdate();
}

QString MathematicsCoordination::yAxisLabel() const
{
    return yAxisLabel_;
}

void MathematicsCoordination::setTickCount(int count)
{
    const int clamped = std::clamp(count, kMinimumTickCount, kMaximumTickCount);
    if (tickCount_ == clamped)
        return;

    tickCount_ = clamped;
    plotAreaDirty_ = true;

    if (autoFit_) {
        // The nice range depends on the requested tick count, so re-fit.
        applyFit(true);
    } else {
        requestUpdate();
    }
}

int MathematicsCoordination::tickCount() const noexcept
{
    return tickCount_;
}

void MathematicsCoordination::setGridVisible(bool visible)
{
    if (gridVisible_ == visible)
        return;

    gridVisible_ = visible;
    requestUpdate();
}

bool MathematicsCoordination::isGridVisible() const noexcept
{
    return gridVisible_;
}

void MathematicsCoordination::setAutoFit(bool enabled)
{
    if (autoFit_ == enabled)
        return;

    autoFit_ = enabled;

    if (enabled) {
        applyFit(true);
        return;
    }

    // Manual mode starts at the range auto-fit was heading for, not in the
    // middle of the animation, so the first manual frame is predictable.
    animator_.stop();
    visibleBounds_ = fittedBounds_;
    plotAreaDirty_ = true;
    requestUpdate();
    emit transformChanged();
}

bool MathematicsCoordination::isAutoFit() const noexcept
{
    return autoFit_;
}

void MathematicsCoordination::zoomAt(QPointF pixelAnchor, qreal factor)
{
    if (!std::isfinite(pixelAnchor.x()) || !std::isfinite(pixelAnchor.y())) {
        qWarning("md::MathematicsCoordination::zoomAt: anchor must be finite");
        return;
    }

    if (!std::isfinite(factor) || factor <= 0.0) {
        qWarning("md::MathematicsCoordination::zoomAt: factor must be finite and positive");
        return;
    }

    const qreal clamped = std::clamp(factor, kMinimumZoomFactor, kMaximumZoomFactor);
    if (std::abs(clamped - 1.0) <= 1.0e-12)
        return;

    const QRectF plot = plotArea();
    if (plot.width() <= 0.0 || plot.height() <= 0.0)
        return;

    const QRectF before = visibleBounds_;
    const qreal xSpan = before.width();
    const qreal ySpan = before.height();
    if (!(xSpan > 0.0) || !(ySpan > 0.0))
        return;

    const QPointF anchorData = toData(pixelAnchor);
    const qreal fractionX = (anchorData.x() - before.left()) / xSpan;
    const qreal fractionY = (anchorData.y() - before.top()) / ySpan;

    const qreal newXSpan = xSpan / clamped;
    const qreal newYSpan = ySpan / clamped;
    const qreal newMinimumX = anchorData.x() - fractionX * newXSpan;
    const qreal newMinimumY = anchorData.y() - fractionY * newYSpan;

    if (!std::isfinite(newMinimumX) || !std::isfinite(newMinimumY)
        || !std::isfinite(newXSpan) || !std::isfinite(newYSpan)
        || !(newXSpan > 0.0) || !(newYSpan > 0.0)) {
        qWarning("md::MathematicsCoordination::zoomAt: zoom range is not representable");
        return;
    }

    autoFit_ = false;
    animator_.stop();
    visibleBounds_ = QRectF(newMinimumX, newMinimumY, newXSpan, newYSpan);
    fittedBounds_ = visibleBounds_;

    // Deliberately no plot-area relayout here: the gesture was computed with
    // the current plot rectangle and re-measuring the label gutters against
    // the new range would move the anchor by a few pixels. The plot area is
    // remeasured on resize, theme, padding, tick-count and explicit-range
    // changes, which are the moments a gutter can legitimately change.
    requestUpdate();
    emit transformChanged();
}

void MathematicsCoordination::pan(QPointF deltaPixels)
{
    if (!std::isfinite(deltaPixels.x()) || !std::isfinite(deltaPixels.y())) {
        qWarning("md::MathematicsCoordination::pan: delta must be finite");
        return;
    }

    if (deltaPixels.isNull())
        return;

    const QRectF plot = plotArea();
    if (plot.width() <= 0.0 || plot.height() <= 0.0)
        return;

    const qreal xSpan = visibleBounds_.width();
    const qreal ySpan = visibleBounds_.height();
    if (!(xSpan > 0.0) || !(ySpan > 0.0))
        return;

    // toPixel_new(p) = toPixel_old(p) + delta, with the y axis flipped. The
    // span is unchanged; only the data-space origin moves.
    const qreal dataDeltaX = -deltaPixels.x() / plot.width() * xSpan;
    const qreal dataDeltaY = deltaPixels.y() / plot.height() * ySpan;
    const qreal newMinimumX = visibleBounds_.left() + dataDeltaX;
    const qreal newMinimumY = visibleBounds_.top() + dataDeltaY;

    if (!std::isfinite(newMinimumX) || !std::isfinite(newMinimumY)) {
        qWarning("md::MathematicsCoordination::pan: panned range is not representable");
        return;
    }

    autoFit_ = false;
    animator_.stop();
    visibleBounds_ = QRectF(newMinimumX, newMinimumY, xSpan, ySpan);
    fittedBounds_ = visibleBounds_;

    // As in zoomAt(): keep the plot rectangle stable during the gesture so the
    // requested pixel delta is exact for every point.
    requestUpdate();
    emit transformChanged();
}

QVector<qreal> MathematicsCoordination::niceTicks(qreal minimum, qreal maximum, int count)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || count <= 0)
        return {};

    if (maximum < minimum)
        std::swap(minimum, maximum);

    if (maximum == minimum)
        return {minimum};

    if (count == 1)
        return {minimum};

    const int intervals = std::clamp(count, 2, kMaximumTickCount) - 1;
    const qreal step = niceStep((maximum - minimum) / static_cast<qreal>(intervals));

    if (!(step > 0.0) || !std::isfinite(step))
        return {minimum, maximum};

    const qreal firstIndex = std::ceil(minimum / step - kEpsilon);
    const qreal lastIndex = std::floor(maximum / step + kEpsilon);

    QVector<qreal> ticks;
    if (firstIndex <= lastIndex && (lastIndex - firstIndex) <= 1.0e6) {
        const int tickTotal = static_cast<int>(lastIndex - firstIndex) + 1;
        ticks.reserve(tickTotal);

        for (int index = 0; index < tickTotal; ++index) {
            qreal value = (firstIndex + static_cast<qreal>(index)) * step;

            // Snap the tick that lands on zero: -1e-17 * step reads badly and
            // compares badly in a test.
            if (std::abs(value) < step * 1.0e-9)
                value = 0.0;

            ticks.append(value);
        }
    }

    if (ticks.isEmpty())
        return {minimum, maximum};

    return ticks;
}

QString MathematicsCoordination::tickLabel(qreal value, qreal step)
{
    if (!std::isfinite(value))
        return QString();

    const qreal magnitude = std::abs(value);
    if (magnitude >= 1.0e6 || (magnitude > 0.0 && magnitude < 1.0e-4))
        return QString::number(value, 'g', 6);

    if (!(step > 0.0) || !std::isfinite(step))
        step = 1.0;

    if (std::abs(value) < step * 1.0e-9)
        value = 0.0;

    const int decimals = std::clamp(
        static_cast<int>(std::ceil(-std::log10(step))), 0, 6);

    return QString::number(value, 'f', decimals);
}

QVector<qreal> MathematicsCoordination::xTicks() const
{
    return niceTicks(visibleBounds_.left(), visibleBounds_.right(), tickCount_);
}

QVector<qreal> MathematicsCoordination::yTicks() const
{
    return niceTicks(visibleBounds_.top(), visibleBounds_.bottom(), tickCount_);
}

QRectF MathematicsCoordination::plotArea() const
{
    if (plotAreaExplicit_)
        return plotArea_;

    const QRectF content = contentRect();
    if (plotAreaDirty_ || cachedContentRect_ != content)
        updatePlotArea();

    return plotArea_;
}

void MathematicsCoordination::setPlotArea(const QRectF& area)
{
    plotAreaExplicit_ = true;
    plotArea_ = area.isValid() ? area : QRectF();
    plotAreaDirty_ = false;
    cachedContentRect_ = contentRect();
}

void MathematicsCoordination::clearPlotArea()
{
    plotAreaExplicit_ = false;
    plotAreaDirty_ = true;
    cachedContentRect_ = QRectF();
    plotArea_ = QRectF();
}

void MathematicsCoordination::updatePlotArea() const
{
    const QRectF content = contentRect();
    cachedContentRect_ = content;
    plotAreaDirty_ = false;

    if (plotAreaExplicit_)
        return;

    if (!content.isValid() || content.width() <= 0.0 || content.height() <= 0.0) {
        plotArea_ = QRectF();
        return;
    }

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    const qreal padding = std::max<qreal>(0.0, density.dp(viewportPadding_));
    const QRectF inner = content.adjusted(padding, padding, -padding, -padding);
    if (!inner.isValid() || inner.width() <= 0.0 || inner.height() <= 0.0) {
        plotArea_ = QRectF();
        return;
    }

    const QFont tickFont = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF tickMetrics(tickFont);
    const qreal tickLength = density.dp(kTickLengthDp);
    const qreal labelGap = density.dp(kLabelGapDp);

    // The automatic gutter is measured from the fitted range, which stays
    // fixed while a fit animation interpolates visibleBounds(). Using the
    // animated range instead would move the plot rectangle every frame and make
    // the data marks slide sideways while they zoom.
    const QRectF measured = fittedBounds_;
    const QVector<qreal> yValues = niceTicks(measured.top(), measured.bottom(), tickCount_);
    const qreal yStep = tickSpan(yValues, measured.height());

    qreal widestYLabel = 0.0;
    for (qreal value : yValues)
        widestYLabel = std::max(widestYLabel, tickMetrics.horizontalAdvance(tickLabel(value, yStep)));

    qreal leftGutter = widestYLabel + tickLength + labelGap;
    qreal bottomGutter = tickMetrics.height() + tickLength + labelGap;

    if (!yAxisLabel_.isEmpty()) {
        const QFontMetricsF axisMetrics(Font::resolve(this, theme.type().labelMedium, density));
        leftGutter += axisMetrics.height() + labelGap;
    }

    if (!xAxisLabel_.isEmpty()) {
        const QFontMetricsF axisMetrics(Font::resolve(this, theme.type().labelMedium, density));
        bottomGutter += axisMetrics.height() + labelGap;
    }

    const QRectF area = inner.adjusted(leftGutter,
                                       density.dp(kPlotTopPadDp),
                                       -density.dp(kPlotRightPadDp),
                                       -bottomGutter);

    plotArea_ = area.isValid() && area.width() > 0.0 && area.height() > 0.0
        ? area
        : QRectF();
}

bool MathematicsCoordination::isAnimating() const noexcept
{
    return animator_.isRunning();
}

void MathematicsCoordination::measureOverride(MeasureSpec width, MeasureSpec height)
{
    try {
        const Density& density = Theme::forWidget(this).density();
        const qreal preferredWidth = density.dp(kPreferredWidthDp);
        const qreal preferredHeight = density.dp(kPreferredHeightDp);

        setMeasuredSize(QSizeF(width.resolve(preferredWidth), height.resolve(preferredHeight)));
    } catch (const std::exception& error) {
        qWarning("md::MathematicsCoordination::measureOverride failed: %s", error.what());
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    } catch (...) {
        qWarning("md::MathematicsCoordination::measureOverride failed with an unknown exception");
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    }
}

void MathematicsCoordination::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    try {
        paintFrame(painter);
    } catch (const std::exception& error) {
        qWarning("md::MathematicsCoordination::paintContent failed: %s", error.what());
    } catch (...) {
        qWarning("md::MathematicsCoordination::paintContent failed with an unknown exception");
    }
}

void MathematicsCoordination::resizeEvent(QResizeEvent* event)
{
    try {
        plotAreaDirty_ = true;
        cachedContentRect_ = QRectF();
        View::resizeEvent(event);
    } catch (const std::exception& error) {
        qWarning("md::MathematicsCoordination::resizeEvent failed: %s", error.what());
    } catch (...) {
        qWarning("md::MathematicsCoordination::resizeEvent failed with an unknown exception");
    }
}

void MathematicsCoordination::onThemeChanged()
{
    try {
        View::onThemeChanged();

        plotAreaDirty_ = true;

        // A theme switch can turn reduced motion on while a fit is in flight.
        // Finish at the destination instead of leaving the transform between
        // two ranges, which is also what the accessibility contract asks for.
        if (animator_.isRunning() && Theme::forWidget(this).motion().reducedMotion) {
            animator_.stop();
            visibleBounds_ = fittedBounds_;
            emit transformChanged();
        }

        requestUpdate();
    } catch (const std::exception& error) {
        qWarning("md::MathematicsCoordination::onThemeChanged failed: %s", error.what());
    } catch (...) {
        qWarning("md::MathematicsCoordination::onThemeChanged failed with an unknown exception");
    }
}

bool MathematicsCoordination::focusable() const
{
    try {
        return false;
    } catch (const std::exception& error) {
        qWarning("md::MathematicsCoordination::focusable failed: %s", error.what());
        return false;
    } catch (...) {
        qWarning("md::MathematicsCoordination::focusable failed with an unknown exception");
        return false;
    }
}

void MathematicsCoordination::paintFrame(QPainter& painter)
{
    QRectF plot;
    try {
        plot = plotArea();
    } catch (const std::exception& error) {
        qWarning("md::MathematicsCoordination::paintFrame failed: %s", error.what());
        return;
    } catch (...) {
        qWarning("md::MathematicsCoordination::paintFrame failed with an unknown exception");
        return;
    }

    if (!plot.isValid() || plot.width() <= 0.0 || plot.height() <= 0.0)
        return;

    const qreal xSpan = visibleBounds_.width();
    const qreal ySpan = visibleBounds_.height();
    if (!(xSpan > 0.0) || !(ySpan > 0.0))
        return;

    painter.save();
    try {
        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();

        const QVector<qreal> xValues = xTicks();
        const QVector<qreal> yValues = yTicks();
        const qreal xStep = tickSpan(xValues, xSpan);
        const qreal yStep = tickSpan(yValues, ySpan);

        const qreal lineWidth = std::max<qreal>(0.0, density.dp(kHairlineDp));
        const qreal tickLength = density.dp(kTickLengthDp);
        const qreal labelGap = density.dp(kLabelGapDp);

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        // 1. Grid, clipped to the plot rectangle. Ticks on the boundary are
        //    drawn too; they coincide with the axis and are invisible there.
        if (gridVisible_) {
            painter.save();
            painter.setClipRect(plot, Qt::IntersectClip);

            QPen gridPen(theme.colours().outlineVariant);
            gridPen.setWidthF(lineWidth);
            painter.setPen(gridPen);

            for (qreal value : xValues) {
                const qreal x = plot.left() + (value - visibleBounds_.left()) / xSpan * plot.width();
                painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            }

            for (qreal value : yValues) {
                const qreal y = plot.bottom() - (value - visibleBounds_.top()) / ySpan * plot.height();
                painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            }

            painter.restore();
        }

        // 2. Axes: left and bottom only. A full frame adds visual weight the
        //    plot content already provides.
        QPen axisPen(theme.colours().outline);
        axisPen.setWidthF(lineWidth);
        painter.setPen(axisPen);

        painter.drawLine(QPointF(plot.left(), plot.top()), QPointF(plot.left(), plot.bottom()));
        painter.drawLine(QPointF(plot.left(), plot.bottom()), QPointF(plot.right(), plot.bottom()));

        // 3. Tick marks, outside the plot area.
        for (qreal value : xValues) {
            const qreal x = plot.left() + (value - visibleBounds_.left()) / xSpan * plot.width();
            painter.drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + tickLength));
        }

        for (qreal value : yValues) {
            const qreal y = plot.bottom() - (value - visibleBounds_.top()) / ySpan * plot.height();
            painter.drawLine(QPointF(plot.left() - tickLength, y), QPointF(plot.left(), y));
        }

        // 4. Tick labels.
        const QFont tickFont = Font::resolve(this, theme.type().labelSmall, density);
        const QFontMetricsF tickMetrics(tickFont);
        const qreal labelHeight = tickMetrics.height();

        painter.setFont(tickFont);
        painter.setPen(theme.colours().onSurfaceVariant);

        qreal widestYLabel = 0.0;
        for (qreal value : yValues)
            widestYLabel = std::max(widestYLabel,
                                    tickMetrics.horizontalAdvance(tickLabel(value, yStep)));

        const qreal yLabelRight = plot.left() - tickLength - labelGap;
        for (qreal value : yValues) {
            const qreal y = plot.bottom() - (value - visibleBounds_.top()) / ySpan * plot.height();
            const QString text = tickLabel(value, yStep);
            const qreal textWidth = tickMetrics.horizontalAdvance(text);
            const QRectF labelRect(yLabelRight - textWidth, y - labelHeight * 0.5,
                                   textWidth, labelHeight);
            painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, text);
        }

        const qreal xLabelTop = plot.bottom() + tickLength + labelGap;
        for (qreal value : xValues) {
            const qreal x = plot.left() + (value - visibleBounds_.left()) / xSpan * plot.width();
            const QString text = tickLabel(value, xStep);
            const qreal textWidth = std::max<qreal>(
                tickMetrics.horizontalAdvance(text), density.dp(8.0));
            const QRectF labelRect(x - textWidth * 0.5, xLabelTop, textWidth, labelHeight);
            painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, text);
        }

        // 5. Axis titles, when the caller supplied one.
        if (!xAxisLabel_.isEmpty() || !yAxisLabel_.isEmpty()) {
            const QFont axisFont = Font::resolve(this, theme.type().labelMedium, density);
            const QFontMetricsF axisMetrics(axisFont);

            painter.setFont(axisFont);

            if (!xAxisLabel_.isEmpty()) {
                const QRectF titleRect(plot.left(),
                                       xLabelTop + labelHeight + labelGap,
                                       plot.width(),
                                       axisMetrics.height());
                painter.drawText(titleRect, Qt::AlignHCenter | Qt::AlignTop, xAxisLabel_);
            }

            if (!yAxisLabel_.isEmpty()) {
                const qreal axisX = plot.left() - tickLength - labelGap - widestYLabel - labelGap;

                painter.save();
                painter.translate(axisX, plot.center().y());
                painter.rotate(-90.0);

                const QRectF titleRect(-plot.height() * 0.5,
                                       -axisMetrics.height() * 0.5,
                                       plot.height(),
                                       axisMetrics.height());
                painter.drawText(titleRect, Qt::AlignCenter, yAxisLabel_);
                painter.restore();
            }
        }
    } catch (const std::exception& error) {
        qWarning("md::MathematicsCoordination::paintFrame failed: %s", error.what());
    } catch (...) {
        qWarning("md::MathematicsCoordination::paintFrame failed with an unknown exception");
    }
    painter.restore();
}

void MathematicsCoordination::applyFit(bool animate)
{
    const NiceRange xRange = chooseNiceRange(dataBounds_.left(), dataBounds_.right(), tickCount_);
    const NiceRange yRange = chooseNiceRange(dataBounds_.top(), dataBounds_.bottom(), tickCount_);

    const QRectF target = xRange.valid && yRange.valid
        ? QRectF(QPointF(xRange.minimum, yRange.minimum),
                 QPointF(xRange.maximum, yRange.maximum))
        : dataBounds_;

    // A data update that stays inside the same nice range does not need a new
    // animation; restarting one would make a streaming chart pulse.
    if (target == fittedBounds_ && animator_.isRunning()) {
        emit transformChanged();
        return;
    }

    fittedBounds_ = target;

    if (!animate) {
        animator_.stop();
        visibleBounds_ = target;
        plotAreaDirty_ = true;
        requestUpdate();
        emit transformChanged();
        return;
    }

    const Theme& theme = Theme::forWidget(this);
    const MotionScheme& motion = theme.motion();
    const MotionSpec spec = motion.medium2();

    if (motion.reducedMotion || spec.duration.count() <= 0) {
        animator_.stop();
        visibleBounds_ = target;
        plotAreaDirty_ = true;
        requestUpdate();
        emit transformChanged();
        return;
    }

    if (target == visibleBounds_) {
        // Nothing to animate: the current range already is the fit.
        animator_.stop();
        plotAreaDirty_ = true;
        requestUpdate();
        emit transformChanged();
        return;
    }

    animator_.stop();
    // The automatic plot gutter is measured from fittedBounds_, so the target
    // range has to be laid out once before the animation starts. Later frames
    // deliberately do not dirty it again.
    plotAreaDirty_ = true;
    animationFrom_ = visibleBounds_;

    animator_.start(spec, [this](qreal progress) {
        const qreal t = std::clamp(progress, 0.0, 1.0);
        const qreal left = animationFrom_.left()
            + (fittedBounds_.left() - animationFrom_.left()) * t;
        const qreal top = animationFrom_.top()
            + (fittedBounds_.top() - animationFrom_.top()) * t;
        const qreal width = animationFrom_.width()
            + (fittedBounds_.width() - animationFrom_.width()) * t;
        const qreal height = animationFrom_.height()
            + (fittedBounds_.height() - animationFrom_.height()) * t;

        visibleBounds_ = QRectF(left, top, width, height);
        requestUpdate();
        emit transformChanged();
    });

    emit transformChanged();
}

MathematicsCoordination::NiceRange MathematicsCoordination::chooseNiceRange(
    qreal minimum, qreal maximum, int count)
{
    NiceRange result;

    if (!std::isfinite(minimum) || !std::isfinite(maximum))
        return result;

    if (maximum < minimum)
        std::swap(minimum, maximum);

    const qreal span = maximum - minimum;
    if (!(span > 0.0) || !std::isfinite(span))
        return result;

    const int target = std::clamp(count, kMinimumFitTicks, kMaximumFitTicks);
    const qreal middleStep = span / static_cast<qreal>(target - 1);
    const qreal magnitude = std::pow(10.0, std::floor(std::log10(middleStep)));

    if (!(magnitude > 0.0) || !std::isfinite(magnitude))
        return result;

    const qreal multipliers[] = {1.0, 2.0, 5.0, 10.0};
    qreal bestScore = std::numeric_limits<qreal>::max();

    // The step is a 1/2/5 mantissa at one of the three decades around the
    // estimate. That window always contains a step whose expanded range has
    // between four and eight ticks for any representable positive span.
    for (int decade = -1; decade <= 1; ++decade) {
        const qreal scale = magnitude * std::pow(10.0, decade);
        if (!(scale > 0.0) || !std::isfinite(scale))
            continue;

        for (qreal multiplier : multipliers) {
            const qreal step = multiplier * scale;
            if (!(step > 0.0) || !std::isfinite(step))
                continue;

            const qreal niceMinimum = std::floor(minimum / step + kEpsilon) * step;
            const qreal niceMaximum = std::ceil(maximum / step - kEpsilon) * step;

            if (!std::isfinite(niceMinimum) || !std::isfinite(niceMaximum)
                || !(niceMaximum > niceMinimum)) {
                continue;
            }

            const qreal intervals = (niceMaximum - niceMinimum) / step;
            if (!(intervals >= 1.0) || intervals > 1.0e6)
                continue;

            const int ticks = static_cast<int>(std::llround(intervals)) + 1;
            if (ticks < 2 || ticks > 1000000)
                continue;

            const int excess = ticks < kMinimumFitTicks
                ? (kMinimumFitTicks - ticks)
                : (ticks > kMaximumFitTicks ? (ticks - kMaximumFitTicks) : 0);
            const qreal expansion = (niceMaximum - niceMinimum - span) / span;
            const qreal score = static_cast<qreal>(excess) * 100.0
                + std::abs(static_cast<qreal>(ticks - target))
                + expansion * 0.01;

            if (!result.valid || score < bestScore) {
                bestScore = score;
                result.minimum = niceMinimum;
                result.maximum = niceMaximum;
                result.step = step;
                result.ticks = ticks;
                result.valid = true;
            }
        }
    }

    return result;
}

qreal MathematicsCoordination::niceStep(qreal rawStep)
{
    if (!(rawStep > 0.0) || !std::isfinite(rawStep))
        return 1.0;

    const qreal exponent = std::floor(std::log10(rawStep));
    const qreal magnitude = std::pow(10.0, exponent);

    if (!(magnitude > 0.0) || !std::isfinite(magnitude))
        return rawStep;

    const qreal scaled = rawStep / magnitude;

    qreal multiplier = 10.0;
    if (scaled <= 1.0 + kEpsilon)
        multiplier = 1.0;
    else if (scaled <= 2.0 + kEpsilon)
        multiplier = 2.0;
    else if (scaled <= 5.0 + kEpsilon)
        multiplier = 5.0;

    return multiplier * magnitude;
}

qreal MathematicsCoordination::tickSpan(const QVector<qreal>& ticks, qreal fallback)
{
    if (ticks.size() >= 2) {
        const qreal step = ticks.at(1) - ticks.at(0);
        if (step > 0.0 && std::isfinite(step))
            return step;
    }

    return fallback > 0.0 && std::isfinite(fallback) ? fallback : 1.0;
}

} // namespace md
