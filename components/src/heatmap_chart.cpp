// Periodicity — the HeatmapChart component.
//
// The paint path resolves every colour and length from one theme snapshot at
// the start of the pass: the scale runs from surfaceContainerHighest to
// primary, the grid and the empty cell hatch use outlineVariant, the labels use
// onSurfaceVariant and the value text picks onSurface or onPrimary by contrast.
// No colour is written literally and every length goes through Density::dp().
//
// The component leaves the title, legend and empty state to ChartBase. The
// base's frame is switched off in the constructor with setAxesVisible(false),
// because a categorical matrix has no numeric axes; plotRect() is then the full
// inner rectangle that the cells and their labels divide. ChartBase selects
// paintPlot()/paintEmptyState() from hasData(), which reads ChartBase::series(),
// so every matrix mutation mirrors "has a finite cell" onto one unnamed
// presence point (see syncPresenceSeries()). The presence series is never
// painted.
//
// The cell scale is normalised over the finite cells only. A degenerate range
// (one value) maps to the maximum colour, a NaN cell is never scaled, and the
// row/column order mirrors under Qt::RightToLeft while the logical matrix
// stays in its original order.

#include <md/components/heatmap_chart.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QBrush>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace md {
namespace {

constexpr qreal kLabelGapDp = 4.0;
constexpr qreal kGridWidthDp = 1.0;
constexpr qreal kGridAlpha = 0.8;
constexpr qreal kHatchAlpha = 0.55;
constexpr qreal kHoverOutlineDp = 2.0;
constexpr qreal kHoverOverlayAlpha = 0.10;
constexpr qreal kMaximumRowLabelGutterFraction = 0.36;
constexpr qreal kMinimumValueCellWidthDp = 22.0;
constexpr qreal kMinimumValueCellHeightDp = 12.0;
constexpr qreal kValueInsetDp = 3.0;

[[nodiscard]] qreal emptyValue() noexcept
{
    return std::numeric_limits<qreal>::quiet_NaN();
}

// Restores the painter on every exit path, including one taken by an
// exception; a manual restore after the try block would be skipped by it.
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

// sRGB interpolation between two theme colours. The result is always opaque:
// a cell is a surface, not an overlay.
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

// Both candidates are theme content colours; the one with the higher contrast
// against the cell wins, so the scale is readable in light and dark schemes.
[[nodiscard]] QColor readableTextColour(const QColor& background, const ColorScheme& colours)
{
    const qreal luminance = relativeLuminance(background);
    const qreal onSurface = contrastRatio(luminance, relativeLuminance(colours.onSurface));
    const qreal onPrimary = contrastRatio(luminance, relativeLuminance(colours.onPrimary));
    return onSurface >= onPrimary ? colours.onSurface : colours.onPrimary;
}

// A compact numeric label: scientific notation only outside a readable
// magnitude, otherwise 0..2 decimals with trailing zeroes trimmed.
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

    return text;
}

} // namespace

HeatmapChart::HeatmapChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAxesVisible(false);

    // A heatmap cell is a colour reading, not a control surface: the shared
    // interaction layer would paint onSurface at 8% over the whole plot after
    // paintPlot(), so every cell would stop matching the role colour its value
    // maps to while the pointer is over the chart. The chart paints its own
    // per-cell hover instead, so the base layers are switched off as they are
    // for BarChart and PieChart.
    setInteractionFeedbackEnabled(false);
}

void HeatmapChart::setRows(int rowCount)
{
    const int count = std::max(0, rowCount);
    if (rows_ == count && values_.size() == count)
        return;

    rows_ = count;
    values_.resize(rows_);
    for (QVector<qreal>& row : values_)
        row.resize(columns_, emptyValue());

    syncPresenceSeries();
}

int HeatmapChart::rows() const noexcept
{
    return rows_;
}

void HeatmapChart::setColumns(int columnCount)
{
    const int count = std::max(0, columnCount);
    if (columns_ == count)
        return;

    columns_ = count;
    for (QVector<qreal>& row : values_)
        row.resize(columns_, emptyValue());

    syncPresenceSeries();
}

int HeatmapChart::columns() const noexcept
{
    return columns_;
}

void HeatmapChart::setValue(int row, int column, qreal cellValue)
{
    if (row < 0 || row >= rows_ || column < 0 || column >= columns_)
        return;
    if (row >= values_.size() || column >= values_.at(row).size())
        return;

    values_[row][column] = cellValue;
    syncPresenceSeries();
}

qreal HeatmapChart::value(int row, int column) const
{
    if (row < 0 || row >= rows_ || column < 0 || column >= columns_)
        return emptyValue();
    if (row >= values_.size() || column >= values_.at(row).size())
        return emptyValue();

    return values_.at(row).at(column);
}

void HeatmapChart::setValues(QVector<QVector<qreal>> matrix)
{
    rows_ = static_cast<int>(matrix.size());
    columns_ = 0;
    for (const QVector<qreal>& row : matrix)
        columns_ = std::max(columns_, static_cast<int>(row.size()));

    values_ = std::move(matrix);
    for (QVector<qreal>& row : values_)
        row.resize(columns_, emptyValue());

    syncPresenceSeries();
}

const QVector<QVector<qreal>>& HeatmapChart::values() const noexcept
{
    return values_;
}

void HeatmapChart::setRowLabels(QStringList labels)
{
    if (rowLabels_ == labels)
        return;

    rowLabels_ = std::move(labels);
    requestUpdate();
}

QStringList HeatmapChart::rowLabels() const
{
    return rowLabels_;
}

void HeatmapChart::setColumnLabels(QStringList labels)
{
    if (columnLabels_ == labels)
        return;

    columnLabels_ = std::move(labels);
    requestUpdate();
}

QStringList HeatmapChart::columnLabels() const
{
    return columnLabels_;
}

void HeatmapChart::setShowValues(bool show)
{
    if (showValues_ == show)
        return;

    showValues_ = show;
    requestUpdate();
}

bool HeatmapChart::showsValues() const noexcept
{
    return showValues_;
}

void HeatmapChart::setShowGrid(bool show)
{
    if (showGrid_ == show)
        return;

    showGrid_ = show;
    requestUpdate();
}

bool HeatmapChart::showsGrid() const noexcept
{
    return showGrid_;
}

bool HeatmapChart::hasHeatValues() const noexcept
{
    for (const QVector<qreal>& row : values_) {
        for (qreal cell : row) {
            if (std::isfinite(cell))
                return true;
        }
    }

    return false;
}

void HeatmapChart::syncPresenceSeries()
{
    // ChartBase selects paintPlot() over paintEmptyState() from hasData(), and
    // that predicate reads the series list. The matrix is not a series, so the
    // gate is mirrored with one unnamed point (an empty list when no cell is
    // finite). paintPlot() never looks at it, and the empty name keeps it out
    // of the legend.
    QVector<ChartSeries> presence;

    if (hasHeatValues()) {
        ChartSeries marker;
        marker.points.append(QPointF(0.0, 0.0));
        presence.append(marker);
    }

    ChartBase::setSeries(std::move(presence));
}

qreal HeatmapChart::rowLabelGutter(qreal availableWidth) const
{
    if (rowLabels_.isEmpty() || !(availableWidth > 0.0))
        return 0.0;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFontMetricsF metrics(Font::resolve(this, theme.type().labelSmall, density));

    qreal widest = 0.0;
    for (const QString& label : rowLabels_)
        widest = std::max(widest, metrics.horizontalAdvance(label));

    const qreal gap = density.dp(kLabelGapDp);
    return std::min(availableWidth * kMaximumRowLabelGutterFraction, widest + gap);
}

QRectF HeatmapChart::cellAreaFor(const QRectF& plot) const
{
    if (plot.isEmpty() || rows_ <= 0 || columns_ <= 0)
        return QRectF();

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal gap = density.dp(kLabelGapDp);

    QRectF area = plot;

    if (!columnLabels_.isEmpty()) {
        const QFontMetricsF metrics(Font::resolve(this, theme.type().labelSmall, density));
        area.setTop(area.top() + metrics.height() + gap);
    }

    const qreal gutter = rowLabelGutter(plot.width());
    if (gutter > 0.0) {
        if (layoutDirection() == Qt::RightToLeft)
            area.setRight(area.right() - gutter);
        else
            area.setLeft(area.left() + gutter);
    }

    if (area.width() <= 0.0 || area.height() <= 0.0)
        return QRectF();

    return area;
}

QRectF HeatmapChart::cellRect(int row, int column) const
{
    if (row < 0 || row >= rows_ || column < 0 || column >= columns_)
        return QRectF();

    const QRectF area = cellAreaFor(plotRect());
    if (area.isEmpty())
        return QRectF();

    const qreal cellWidth = area.width() / columns_;
    const qreal cellHeight = area.height() / rows_;
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
    const qreal x = rightToLeft ? area.right() - (column + 1) * cellWidth
                                : area.left() + column * cellWidth;

    return QRectF(x, area.top() + row * cellHeight, cellWidth, cellHeight);
}

QPoint HeatmapChart::cellAt(QPointF position) const
{
    const QRectF area = cellAreaFor(plotRect());
    if (area.isEmpty() || !std::isfinite(position.x()) || !std::isfinite(position.y()))
        return QPoint(-1, -1);

    const qreal cellWidth = area.width() / columns_;
    const qreal cellHeight = area.height() / rows_;
    if (!(cellWidth > 0.0) || !(cellHeight > 0.0))
        return QPoint(-1, -1);

    if (position.x() < area.left() || position.x() > area.right()
        || position.y() < area.top() || position.y() > area.bottom()) {
        return QPoint(-1, -1);
    }

    const int row = std::clamp(static_cast<int>((position.y() - area.top()) / cellHeight),
                               0, rows_ - 1);
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
    const int column = rightToLeft
        ? std::clamp(static_cast<int>((area.right() - position.x()) / cellWidth), 0, columns_ - 1)
        : std::clamp(static_cast<int>((position.x() - area.left()) / cellWidth), 0, columns_ - 1);

    return QPoint(column, row);
}

QPoint HeatmapChart::hoveredCell() const noexcept
{
    if (hoveredRow_ < 0 || hoveredColumn_ < 0)
        return QPoint(-1, -1);

    return QPoint(hoveredColumn_, hoveredRow_);
}

void HeatmapChart::setHoveredCell(int row, int column)
{
    if (hoveredRow_ == row && hoveredColumn_ == column)
        return;

    hoveredRow_ = row;
    hoveredColumn_ = column;
    requestUpdate();
}

void HeatmapChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    if (plot.isEmpty() || rows_ <= 0 || columns_ <= 0)
        return;

    const PainterStateGuard guard(painter);

    try {
        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();

        const QRectF area = cellAreaFor(plot);
        if (area.isEmpty())
            return;

        // The finite range drives the scale; a NaN cell never stretches it.
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
            return;

        const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
        const qreal cellWidth = area.width() / columns_;
        const qreal cellHeight = area.height() / rows_;

        const auto cellFor = [&](int row, int column) {
            const qreal x = rightToLeft ? area.right() - (column + 1) * cellWidth
                                        : area.left() + column * cellWidth;
            return QRectF(x, area.top() + row * cellHeight, cellWidth, cellHeight);
        };

        const QColor low = colours.surfaceContainerHighest;
        const QColor high = colours.primary;
        const qreal span = maximum - minimum;

        const auto colourFor = [&](qreal cellValue) {
            const qreal fraction = span > 0.0 ? (cellValue - minimum) / span : 1.0;
            return interpolateColour(low, high, fraction);
        };

        QColor hatch = colours.outlineVariant;
        hatch.setAlphaF(static_cast<float>(kHatchAlpha));

        // 1. Cell fills, with the diagonal hatch standing in for an empty cell.
        for (int row = 0; row < rows_; ++row) {
            const QVector<qreal>& cells = values_.at(row);

            for (int column = 0; column < columns_; ++column) {
                const QRectF cell = cellFor(row, column);
                const qreal cellValue = column < cells.size() ? cells.at(column) : emptyValue();

                if (std::isfinite(cellValue))
                    painter.fillRect(cell, colourFor(cellValue));
                else
                    painter.fillRect(cell, QBrush(hatch, Qt::BDiagPattern));
            }
        }

        // 2. Row and column labels, in the gutters cellAreaFor() reserved.
        const QFont labelFont = Font::resolve(this, theme.type().labelSmall, density);
        const QFontMetricsF labelMetrics(labelFont);
        const qreal gap = density.dp(kLabelGapDp);
        const qreal gutter = rowLabelGutter(plot.width());

        painter.setFont(labelFont);
        painter.setPen(colours.onSurfaceVariant);

        if (gutter > 0.0 && !rowLabels_.isEmpty()) {
            const qsizetype count = std::min<qsizetype>(rowLabels_.size(), rows_);

            for (qsizetype row = 0; row < count; ++row) {
                const QRectF cell = cellFor(static_cast<int>(row), 0);
                const QRectF labelRect = rightToLeft
                    ? QRectF(area.right() + gap, cell.top(),
                             std::max<qreal>(0.0, gutter - gap), cell.height())
                    : QRectF(area.left() - gutter, cell.top(),
                             std::max<qreal>(0.0, gutter - gap), cell.height());

                const QString text = labelMetrics.elidedText(
                    rowLabels_.at(row), Qt::ElideRight, labelRect.width());
                painter.drawText(labelRect,
                                 (rightToLeft ? Qt::AlignLeft : Qt::AlignRight)
                                     | Qt::AlignVCenter,
                                 text);
            }
        }

        if (!columnLabels_.isEmpty()) {
            const qsizetype count = std::min<qsizetype>(columnLabels_.size(), columns_);
            const qreal labelTop = area.top() - gap - labelMetrics.height();

            for (qsizetype column = 0; column < count; ++column) {
                const QRectF cell = cellFor(0, static_cast<int>(column));
                const QRectF labelRect(cell.left(), labelTop, cell.width(),
                                       labelMetrics.height());
                const QString text = labelMetrics.elidedText(
                    columnLabels_.at(column), Qt::ElideRight, cell.width());
                painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignBottom, text);
            }
        }

        // 3. The grid, over the fills but under the value text.
        if (showGrid_) {
            QColor grid = colours.outlineVariant;
            grid.setAlphaF(static_cast<float>(kGridAlpha));

            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(grid, std::max<qreal>(1.0, density.dp(kGridWidthDp))));

            for (int column = 0; column <= columns_; ++column) {
                const qreal x = rightToLeft ? area.right() - column * cellWidth
                                            : area.left() + column * cellWidth;
                painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
            }

            for (int row = 0; row <= rows_; ++row) {
                const qreal y = area.top() + row * cellHeight;
                painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            }
        }

        // 4. Numeric values, only when the cell can hold them.
        if (showValues_ && cellWidth >= density.dp(kMinimumValueCellWidthDp)
            && cellHeight >= density.dp(kMinimumValueCellHeightDp)) {
            const QFont valueFont = Font::resolve(this, theme.type().labelSmall, density);
            const QFontMetricsF valueMetrics(valueFont);
            const qreal inset = density.dp(kValueInsetDp);

            painter.setFont(valueFont);

            for (int row = 0; row < rows_; ++row) {
                const QVector<qreal>& cells = values_.at(row);

                for (int column = 0; column < columns_; ++column) {
                    if (column >= cells.size())
                        continue;

                    const qreal cellValue = cells.at(column);
                    if (!std::isfinite(cellValue))
                        continue;

                    const QRectF textRect = cellFor(row, column)
                                                .adjusted(inset, inset, -inset, -inset);
                    if (textRect.width() <= 0.0 || textRect.height() <= 0.0)
                        continue;

                    painter.setPen(readableTextColour(colourFor(cellValue), colours));
                    painter.drawText(textRect, Qt::AlignCenter,
                                     valueMetrics.elidedText(formatValue(cellValue),
                                                             Qt::ElideRight, textRect.width()));
                }
            }
        }

        // 5. Hover highlight, on top of the cells and their text.
        if (hoveredRow_ >= 0 && hoveredRow_ < rows_
            && hoveredColumn_ >= 0 && hoveredColumn_ < columns_) {
            const QRectF cell = cellFor(hoveredRow_, hoveredColumn_);

            QColor overlay = colours.onSurface;
            overlay.setAlphaF(static_cast<float>(kHoverOverlayAlpha));
            painter.fillRect(cell, overlay);

            QColor outline = colours.onSurface;
            outline.setAlphaF(static_cast<float>(0.9));

            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(outline, std::max<qreal>(1.0, density.dp(kHoverOutlineDp))));
            painter.drawRect(cell.adjusted(0.0, 0.0, -1.0, -1.0));
        }
    } catch (const std::exception& error) {
        qWarning("md::HeatmapChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::HeatmapChart::paintPlot failed with an unknown exception");
    }
}

void HeatmapChart::mouseMoveEvent(QMouseEvent* event)
{
    try {
        View::mouseMoveEvent(event);
        const QPoint cell = cellAt(event->position());
        setHoveredCell(cell.y(), cell.x());
    } catch (const std::exception& error) {
        qWarning("md::HeatmapChart::mouseMoveEvent failed: %s", error.what());
        setHoveredCell(-1, -1);
    } catch (...) {
        qWarning("md::HeatmapChart::mouseMoveEvent failed with an unknown exception");
        setHoveredCell(-1, -1);
    }
}

void HeatmapChart::leaveEvent(QEvent* event)
{
    try {
        View::leaveEvent(event);
        setHoveredCell(-1, -1);
    } catch (const std::exception& error) {
        qWarning("md::HeatmapChart::leaveEvent failed: %s", error.what());
        setHoveredCell(-1, -1);
    } catch (...) {
        qWarning("md::HeatmapChart::leaveEvent failed with an unknown exception");
        setHoveredCell(-1, -1);
    }
}

} // namespace md
