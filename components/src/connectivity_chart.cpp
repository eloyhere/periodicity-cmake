// Periodicity — the ConnectivityChart component.
//
// The paint path takes one theme snapshot at the start of the pass. The
// diverging scale is neutral at zero: negative values move from the neutral
// surface-container role towards primary, positive values towards error. Both
// halves are interpolated independently, so an asymmetric range keeps zero at
// its true position on the colour bar and never tints an empty edge.
//
// Layout is derived, not cached: geometryFor() measures the status row, the
// column-label row, the row-label gutter and the colour-bar gutter every time
// it is asked. cellRect() and cellAt() call it too, which keeps hit-testing and
// painting on exactly the same geometry before and after the first paint.
//
// The matrix has no ChartSeries, so the data gate mirrors "at least one finite
// cell" onto an unnamed presence series, as HeatmapChart does. The presence
// point is never painted.
//
// Every entry point catches locally: a broken theme cannot unwind into Qt's
// paint or event dispatch.

#include <md/components/connectivity_chart.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QBrush>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace md {
namespace {

constexpr qreal kLabelGapDp = 4.0;
constexpr qreal kBarWidthDp = 9.0;
constexpr qreal kBarLabelGapDp = 3.0;
constexpr qreal kGridWidthDp = 0.75;
constexpr qreal kGridAlpha = 0.65;
constexpr qreal kHoverOutlineDp = 2.0;
constexpr qreal kReadoutInsetDp = 4.0;
constexpr qreal kReadoutGapDp = 7.0;
constexpr qreal kMaximumLabelFraction = 0.34;
constexpr qreal kMinimumCellDp = 2.0;

[[nodiscard]] qreal emptyValue() noexcept
{
    return std::numeric_limits<qreal>::quiet_NaN();
}

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

[[nodiscard]] QColor interpolateColour(const QColor& from, const QColor& to, qreal fraction)
{
    const qreal t = std::clamp(fraction, 0.0, 1.0);
    const auto channel = [t](int start, int end) {
        return static_cast<int>(qRound(start + (end - start) * t));
    };

    return QColor(channel(from.red(), to.red()),
                  channel(from.green(), to.green()),
                  channel(from.blue(), to.blue()));
}

// A compact numeric label, matching the other matrix charts: scientific
// notation outside a readable magnitude, otherwise 0..2 decimals with trailing
// zeroes trimmed.
[[nodiscard]] QString formatValue(qreal value)
{
    if (!std::isfinite(value))
        return QString();

    const qreal magnitude = std::abs(value);
    if (magnitude > 0.0 && (magnitude < 0.01 || magnitude >= 100000.0))
        return QString::number(value, 'g', 3);

    int decimals = 2;
    if (magnitude >= 100.0)
        decimals = 0;
    else if (magnitude >= 10.0)
        decimals = 1;

    QString text = QString::number(value, 'f', decimals);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(QLatin1Char('.')))
            text.chop(1);
    }

    if (text == QStringLiteral("-0"))
        text = QStringLiteral("0");

    return text;
}

} // namespace

ConnectivityChart::ConnectivityChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAxesVisible(false);

    // A cell is a colour reading, not a control surface. The shared whole-
    // surface state layer would tint every cell and corrupt the scale, so the
    // chart supplies its own cell-local hover feedback instead.
    setInteractionFeedbackEnabled(false);

    // The data-topology reveal is staged by the chart, so it runs on the
    // medium reveal rung rather than the base's default.
    animation().setDuration(MotionLevel::Level2);
}

void ConnectivityChart::setMatrix(QStringList names, QVector<QVector<qreal>> values)
{
    int dimension = static_cast<int>(names.size());
    dimension = std::max<int>(dimension, static_cast<int>(values.size()));
    for (const QVector<qreal>& row : values)
        dimension = std::max(dimension, static_cast<int>(row.size()));
    dimension = std::max(0, dimension);

    names_ = std::move(names);
    while (names_.size() < dimension)
        names_.append(QString::number(names_.size() + 1));
    if (names_.size() > dimension)
        names_ = names_.mid(0, dimension);

    values_.clear();
    values_.reserve(dimension);

    for (int row = 0; row < dimension; ++row) {
        QVector<qreal> cells(dimension, emptyValue());
        if (row < values.size()) {
            const QVector<qreal>& source = values.at(row);
            const qsizetype count = std::min<qsizetype>(source.size(), dimension);
            for (qsizetype column = 0; column < count; ++column) {
                const qreal cell = source.at(column);
                if (std::isfinite(cell))
                    cells[column] = cell;
            }
        }
        values_.append(std::move(cells));
    }

    size_ = dimension;
    hoveredRow_ = -1;
    hoveredColumn_ = -1;

    syncPresenceSeries();
    requestUpdate();
}

int ConnectivityChart::size() const noexcept
{
    return size_;
}

QStringList ConnectivityChart::names() const
{
    return names_;
}

const QVector<QVector<qreal>>& ConnectivityChart::matrix() const noexcept
{
    return values_;
}

qreal ConnectivityChart::value(int row, int column) const
{
    if (row < 0 || row >= size_ || column < 0 || column >= size_)
        return emptyValue();
    if (row >= values_.size() || column >= values_.at(row).size())
        return emptyValue();

    return values_.at(row).at(column);
}

bool ConnectivityChart::isSquare() const noexcept
{
    return size_ > 0;
}

bool ConnectivityChart::isSymmetric(qreal tolerance) const
{
    if (size_ <= 0)
        return true;

    if (!std::isfinite(tolerance) || tolerance < 0.0)
        tolerance = 0.0;

    for (int row = 0; row < size_; ++row) {
        for (int column = 0; column < size_; ++column) {
            const qreal first = value(row, column);
            const qreal second = value(column, row);
            const bool firstFinite = std::isfinite(first);
            const bool secondFinite = std::isfinite(second);

            if (firstFinite != secondFinite)
                return false;
            if (!firstFinite)
                continue;

            const qreal scale = std::max({qreal(1.0), std::abs(first), std::abs(second)});
            if (std::abs(first - second) > tolerance * scale)
                return false;
        }
    }

    return true;
}

void ConnectivityChart::setRange(qreal minimum, qreal maximum)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum))
        return;

    if (minimum > maximum)
        std::swap(minimum, maximum);

    if (maximum - minimum <= 0.0) {
        const qreal pad = std::max<qreal>(1.0, std::abs(minimum) * 0.5);
        minimum -= pad;
        maximum += pad;
    }

    hasRange_ = true;
    rangeMinimum_ = minimum;
    rangeMaximum_ = maximum;
    requestUpdate();
}

void ConnectivityChart::clearRange()
{
    if (!hasRange_)
        return;

    hasRange_ = false;
    requestUpdate();
}

bool ConnectivityChart::hasExplicitRange() const noexcept
{
    return hasRange_;
}

std::pair<qreal, qreal> ConnectivityChart::automaticRange() const
{
    bool found = false;
    qreal minimum = 0.0;
    qreal maximum = 0.0;

    for (const QVector<qreal>& row : values_) {
        for (qreal cell : row) {
            if (!std::isfinite(cell))
                continue;

            if (!found) {
                minimum = cell;
                maximum = cell;
                found = true;
            } else {
                minimum = std::min(minimum, cell);
                maximum = std::max(maximum, cell);
            }
        }
    }

    if (!found)
        return {0.0, 1.0};

    // A diverging scale always contains its neutral midpoint.
    minimum = std::min(minimum, 0.0);
    maximum = std::max(maximum, 0.0);

    if (maximum - minimum <= 0.0)
        return {-1.0, 1.0};

    return {minimum, maximum};
}

qreal ConnectivityChart::rangeMinimum() const
{
    return hasRange_ ? rangeMinimum_ : automaticRange().first;
}

qreal ConnectivityChart::rangeMaximum() const
{
    return hasRange_ ? rangeMaximum_ : automaticRange().second;
}

void ConnectivityChart::setUnit(const QString& unit)
{
    if (unit_ == unit)
        return;

    unit_ = unit;
    requestUpdate();
}

QString ConnectivityChart::unit() const
{
    return unit_;
}

QColor ConnectivityChart::colourFor(qreal value, qreal minimum, qreal maximum) const
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();

    const QColor neutral = colours.surfaceContainerHighest;
    const QColor negative = colours.primary;
    const QColor positive = colours.error;

    if (!std::isfinite(value))
        return neutral;

    const qreal magnitude = std::max(std::abs(minimum), std::abs(maximum));
    if (!(magnitude > 0.0))
        return neutral;

    if (value < 0.0)
        return interpolateColour(neutral, negative, std::abs(value) / magnitude);

    if (value > 0.0)
        return interpolateColour(neutral, positive, value / magnitude);

    return neutral;
}

ConnectivityChart::Geometry ConnectivityChart::geometryFor(const QRectF& plot) const
{
    Geometry geometry;

    if (plot.isEmpty() || size_ <= 0)
        return geometry;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFontMetricsF metrics(Font::resolve(this, theme.type().labelSmall, density));

    const qreal gap = density.dp(kLabelGapDp);
    const qreal barWidth = density.dp(kBarWidthDp);
    const qreal barLabelGap = density.dp(kBarLabelGapDp);

    geometry.labelGap = gap;
    geometry.labelHeight = metrics.height();

    const auto range = hasRange_ ? std::pair<qreal, qreal>(rangeMinimum_, rangeMaximum_)
                                 : automaticRange();

    qreal labelWidth = std::max(metrics.horizontalAdvance(formatValue(range.first)),
                                metrics.horizontalAdvance(formatValue(range.second)));
    if (range.first < 0.0 && range.second > 0.0)
        labelWidth = std::max(labelWidth, metrics.horizontalAdvance(QStringLiteral("0")));
    labelWidth = std::max<qreal>(labelWidth, density.dp(10.0));
    geometry.barLabelWidth = labelWidth;

    const qreal barReserve = barWidth + barLabelGap + labelWidth + gap;
    const qreal statusHeight = metrics.height();

    QRectF area = plot;
    area.setTop(area.top() + statusHeight + gap + metrics.height() + gap);
    area.setRight(area.right() - barReserve);

    qreal widestName = 0.0;
    for (const QString& name : names_)
        widestName = std::max(widestName, metrics.horizontalAdvance(name));

    const qreal gutter = std::min(area.width() * kMaximumLabelFraction, widestName + gap);
    if (gutter > 0.0)
        area.setLeft(area.left() + gutter);

    if (area.width() <= 0.0 || area.height() <= 0.0)
        return geometry;

    const qreal cell = std::floor(std::min(area.width(), area.height()) / size_);
    if (cell < density.dp(kMinimumCellDp))
        return geometry;

    const qreal span = cell * size_;
    geometry.cell = cell;
    geometry.matrix = QRectF(area.left() + (area.width() - span) / 2.0,
                             area.top() + (area.height() - span) / 2.0,
                             span,
                             span);

    qreal barLeft = geometry.matrix.right() + gap;
    const qreal latestBarLeft = plot.right() - labelWidth - barLabelGap - barWidth;
    barLeft = std::min(barLeft, latestBarLeft);
    geometry.bar = QRectF(barLeft, geometry.matrix.top(), barWidth,
                          geometry.matrix.height());

    return geometry;
}

QRectF ConnectivityChart::cellRectFor(const Geometry& geometry, int row, int column) const
{
    if (row < 0 || row >= size_ || column < 0 || column >= size_ || !(geometry.cell > 0.0))
        return QRectF();

    return QRectF(geometry.matrix.left() + column * geometry.cell,
                  geometry.matrix.top() + row * geometry.cell,
                  geometry.cell,
                  geometry.cell);
}

QRectF ConnectivityChart::cellRect(int row, int column) const
{
    return cellRectFor(geometryFor(plotRect()), row, column);
}

QPoint ConnectivityChart::cellAt(QPointF position) const
{
    const Geometry geometry = geometryFor(plotRect());
    if (geometry.matrix.isEmpty() || !(geometry.cell > 0.0))
        return QPoint(-1, -1);

    if (!std::isfinite(position.x()) || !std::isfinite(position.y()))
        return QPoint(-1, -1);
    if (!geometry.matrix.contains(position))
        return QPoint(-1, -1);

    const int column = std::clamp(
        static_cast<int>((position.x() - geometry.matrix.left()) / geometry.cell),
        0, size_ - 1);
    const int row = std::clamp(
        static_cast<int>((position.y() - geometry.matrix.top()) / geometry.cell),
        0, size_ - 1);

    return QPoint(column, row);
}

QPoint ConnectivityChart::hoveredCell() const noexcept
{
    if (hoveredRow_ < 0 || hoveredColumn_ < 0)
        return QPoint(-1, -1);

    return QPoint(hoveredColumn_, hoveredRow_);
}

qreal ConnectivityChart::hoveredValue() const
{
    if (hoveredRow_ < 0 || hoveredColumn_ < 0)
        return emptyValue();

    return value(hoveredRow_, hoveredColumn_);
}

QString ConnectivityChart::hoverLabel() const
{
    const qreal current = hoveredValue();
    if (!std::isfinite(current))
        return QString();

    const QString rowName = names_.value(hoveredRow_);
    const QString columnName = names_.value(hoveredColumn_);
    const QString measured = unit_.isEmpty()
        ? formatValue(current)
        : QStringLiteral("%1 %2").arg(formatValue(current), unit_);

    return QStringLiteral("%1 × %2  (%3, %4)  %5")
        .arg(rowName.isEmpty() ? QStringLiteral("?") : rowName,
             columnName.isEmpty() ? QStringLiteral("?") : columnName)
        .arg(hoveredRow_)
        .arg(hoveredColumn_)
        .arg(measured);
}

void ConnectivityChart::setHoveredCell(int row, int column)
{
    if (hoveredRow_ == row && hoveredColumn_ == column)
        return;

    hoveredRow_ = row;
    hoveredColumn_ = column;
    requestUpdate();
}

bool ConnectivityChart::hasFiniteCells() const noexcept
{
    for (const QVector<qreal>& row : values_) {
        for (qreal cell : row) {
            if (std::isfinite(cell))
                return true;
        }
    }

    return false;
}

void ConnectivityChart::syncPresenceSeries()
{
    QVector<ChartSeries> presence;

    if (hasFiniteCells()) {
        ChartSeries marker;
        marker.points.append(QPointF(0.0, 0.0));
        presence.append(marker);
    }

    ChartBase::setSeries(std::move(presence));
}

qreal ConnectivityChart::revealProgress() const noexcept
{
    return animation().progress();
}

bool ConnectivityChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

void ConnectivityChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    paintPlot(painter, plot, 1.0);
}

void ConnectivityChart::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    if (plot.isEmpty() || size_ <= 0)
        return;

    const PainterStateGuard guard(painter);

    try {
        const qreal stage = std::clamp(progress, 0.0, 1.0);
        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();

        const Geometry geometry = geometryFor(plot);
        if (geometry.matrix.isEmpty())
            return;

        const auto range = hasRange_ ? std::pair<qreal, qreal>(rangeMinimum_, rangeMaximum_)
                                     : automaticRange();
        const QFont labelFont = Font::resolve(this, theme.type().labelSmall, density);
        const QFontMetricsF labelMetrics(labelFont);
        const qreal gap = geometry.labelGap;

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setFont(labelFont);

        // 1. Status line: dimensions and symmetry. This is chart chrome and is
        //    visible for the whole reveal, so the reading is never ambiguous.
        const QString status = QStringLiteral("%1 × %2  ·  %3")
            .arg(size_)
            .arg(size_)
            .arg(isSymmetric() ? QStringLiteral("symmetric")
                               : QStringLiteral("asymmetric"));
        painter.setPen(colours.onSurfaceVariant);
        painter.drawText(QRectF(plot.left(), plot.top(), plot.width(),
                                geometry.labelHeight),
                         Qt::AlignRight | Qt::AlignVCenter,
                         status);

        // 2. The matrix, clipped to the revealed row band. The row labels stay
        //    in step because they share the clip.
        const qreal visibleHeight = geometry.matrix.height() * stage;

        painter.save();
        QRectF clip = geometry.matrix;
        clip.setHeight(visibleHeight);
        painter.setClipRect(clip);

        QColor hatch = colours.outlineVariant;
        hatch.setAlphaF(0.55f);

        for (int row = 0; row < size_; ++row) {
            for (int column = 0; column < size_; ++column) {
                const QRectF cell = cellRectFor(geometry, row, column);
                const qreal cellValue = value(row, column);

                if (std::isfinite(cellValue))
                    painter.fillRect(cell, colourFor(cellValue, range.first, range.second));
                else
                    painter.fillRect(cell, QBrush(hatch, Qt::BDiagPattern));
            }
        }

        // 3. Cell grid and outer border, over the fills.
        QColor grid = colours.outlineVariant;
        grid.setAlphaF(static_cast<float>(kGridAlpha));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(grid, std::max<qreal>(1.0, density.dp(kGridWidthDp))));

        for (int index = 0; index <= size_; ++index) {
            const qreal x = geometry.matrix.left() + index * geometry.cell;
            painter.drawLine(QPointF(x, geometry.matrix.top()),
                             QPointF(x, geometry.matrix.bottom()));
            const qreal y = geometry.matrix.top() + index * geometry.cell;
            painter.drawLine(QPointF(geometry.matrix.left(), y),
                             QPointF(geometry.matrix.right(), y));
        }

        // 4. Row labels, right-aligned against the cell area.
        const qreal rowLabelWidth = std::max<qreal>(0.0, geometry.matrix.left() - plot.left() - gap);
        if (rowLabelWidth > 0.0) {
            painter.setPen(colours.onSurfaceVariant);
            for (int row = 0; row < size_; ++row) {
                const QRectF cell = cellRectFor(geometry, row, 0);
                const QRectF labelRect(plot.left(), cell.top(), rowLabelWidth, cell.height());
                const QString text = labelMetrics.elidedText(
                    names_.value(row), Qt::ElideRight, rowLabelWidth);
                painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, text);
            }
        }
        painter.restore();

        // 5. Column labels, above the matrix.
        const qreal columnLabelTop = geometry.matrix.top() - gap - geometry.labelHeight;
        painter.setPen(colours.onSurfaceVariant);
        for (int column = 0; column < size_; ++column) {
            const QRectF cell = cellRectFor(geometry, 0, column);
            const QRectF labelRect(cell.left(), columnLabelTop, cell.width(),
                                   geometry.labelHeight);
            const QString text = labelMetrics.elidedText(
                names_.value(column), Qt::ElideRight, cell.width());
            painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignBottom, text);
        }

        // 6. Colour bar with a tick at the neutral midpoint.
        const qreal span = range.second - range.first;
        if (geometry.bar.width() > 0.0 && geometry.bar.height() > 0.0 && span > 0.0) {
            QLinearGradient gradient(geometry.bar.topLeft(), geometry.bar.bottomLeft());

            QVector<std::pair<qreal, QColor>> stops;
            stops.append({0.0, colourFor(range.second, range.first, range.second)});
            stops.append({1.0, colourFor(range.first, range.first, range.second)});

            qreal zeroPosition = -1.0;
            if (range.first < 0.0 && range.second > 0.0) {
                zeroPosition = (range.second - 0.0) / span;
                stops.append({zeroPosition, colours.surfaceContainerHighest});
            }

            std::sort(stops.begin(), stops.end(),
                      [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
            for (const auto& stop : stops)
                gradient.setColorAt(std::clamp(stop.first, 0.0, 1.0), stop.second);

            painter.setPen(QPen(colours.outlineVariant, 1.0));
            painter.setBrush(gradient);
            painter.drawRect(geometry.bar);

            painter.setFont(labelFont);
            painter.setPen(colours.onSurfaceVariant);

            const qreal labelLeft = geometry.bar.right() + density.dp(kBarLabelGapDp);
            const qreal labelWidth = std::max<qreal>(0.0, plot.right() - labelLeft);
            if (labelWidth > 0.0) {
                painter.drawText(QRectF(labelLeft, geometry.bar.top(), labelWidth,
                                        geometry.labelHeight),
                                 Qt::AlignLeft | Qt::AlignTop,
                                 formatValue(range.second));
                painter.drawText(QRectF(labelLeft, geometry.bar.bottom() - geometry.labelHeight,
                                        labelWidth, geometry.labelHeight),
                                 Qt::AlignLeft | Qt::AlignBottom,
                                 formatValue(range.first));

                if (zeroPosition > 0.0 && zeroPosition < 1.0) {
                    const qreal zeroY = geometry.bar.top() + zeroPosition * geometry.bar.height();
                    if (zeroY > geometry.bar.top() + geometry.labelHeight * 1.5
                        && zeroY < geometry.bar.bottom() - geometry.labelHeight * 1.5) {
                        painter.drawText(QRectF(labelLeft, zeroY - geometry.labelHeight / 2.0,
                                                labelWidth, geometry.labelHeight),
                                         Qt::AlignLeft | Qt::AlignVCenter,
                                         QStringLiteral("0"));
                    }
                }
            }

            const QString caption = unit_.isEmpty()
                ? localized(QStringLiteral("chart.value"), QStringLiteral("Value"))
                : unit_;
            const qreal captionWidth = std::max<qreal>(0.0, plot.right() - geometry.bar.left());
            painter.drawText(QRectF(geometry.bar.left(),
                                    geometry.bar.top() - gap - geometry.labelHeight,
                                    captionWidth, geometry.labelHeight),
                             Qt::AlignLeft | Qt::AlignBottom,
                             labelMetrics.elidedText(caption, Qt::ElideRight, captionWidth));
        }

        // 7. Hover highlight and readout. The readout is kept inside the plot
        //    so it cannot cover the colour bar or leave the widget.
        if (hoveredRow_ >= 0 && hoveredRow_ < size_
            && hoveredColumn_ >= 0 && hoveredColumn_ < size_) {
            const QRectF cell = cellRectFor(geometry, hoveredRow_, hoveredColumn_);

            QColor outline = colours.onSurface;
            outline.setAlphaF(0.9f);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(outline, std::max<qreal>(1.0, density.dp(kHoverOutlineDp))));
            painter.drawRect(cell.adjusted(0.0, 0.0, -1.0, -1.0));

            const QString text = hoverLabel();
            if (!text.isEmpty()) {
                const qreal inset = density.dp(kReadoutInsetDp);
                const qreal readoutGap = density.dp(kReadoutGapDp);
                const qreal maximumWidth = std::max<qreal>(0.0, plot.width() - 2.0 * readoutGap);
                const QString shown = labelMetrics.elidedText(text, Qt::ElideRight, maximumWidth);
                const qreal boxWidth = std::min(maximumWidth,
                                                labelMetrics.horizontalAdvance(shown) + 2.0 * inset);
                const qreal boxHeight = geometry.labelHeight + 2.0 * inset;

                qreal boxLeft = cell.right() + readoutGap;
                if (boxLeft + boxWidth > plot.right())
                    boxLeft = cell.left() - readoutGap - boxWidth;
                boxLeft = std::clamp(boxLeft, plot.left(), plot.right() - boxWidth);

                qreal boxTop = cell.top();
                if (boxTop + boxHeight > plot.bottom())
                    boxTop = plot.bottom() - boxHeight;
                boxTop = std::max(boxTop, plot.top());

                const QRectF box(boxLeft, boxTop, boxWidth, boxHeight);
                painter.setPen(QPen(colours.outlineVariant, 1.0));
                painter.setBrush(colours.surfaceContainerHigh);
                painter.drawRoundedRect(box, inset, inset);

                painter.setPen(colours.onSurface);
                painter.drawText(box.adjusted(inset, 0.0, -inset, 0.0),
                                 Qt::AlignLeft | Qt::AlignVCenter, shown);
            }
        }
    } catch (const std::exception& error) {
        qWarning("md::ConnectivityChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::ConnectivityChart::paintPlot failed with an unknown exception");
    }
}

void ConnectivityChart::mouseMoveEvent(QMouseEvent* event)
{
    try {
        View::mouseMoveEvent(event);
        hoverPosition_ = event->position();
        const QPoint cell = cellAt(hoverPosition_);
        setHoveredCell(cell.y(), cell.x());
        requestUpdate();
    } catch (const std::exception& error) {
        qWarning("md::ConnectivityChart::mouseMoveEvent failed: %s", error.what());
        setHoveredCell(-1, -1);
    } catch (...) {
        qWarning("md::ConnectivityChart::mouseMoveEvent failed with an unknown exception");
        setHoveredCell(-1, -1);
    }
}

void ConnectivityChart::leaveEvent(QEvent* event)
{
    try {
        View::leaveEvent(event);
        setHoveredCell(-1, -1);
    } catch (const std::exception& error) {
        qWarning("md::ConnectivityChart::leaveEvent failed: %s", error.what());
        setHoveredCell(-1, -1);
    } catch (...) {
        qWarning("md::ConnectivityChart::leaveEvent failed with an unknown exception");
        setHoveredCell(-1, -1);
    }
}

} // namespace md
