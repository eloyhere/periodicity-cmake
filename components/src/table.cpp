// Periodicity — Table, a two-dimensional view backed by any QAbstractItemModel.
//
// Cells are painted, never instantiated: every paint pass computes the visible
// row range and walks only the columns that intersect the viewport. The current
// cell is a pair of integers that is shifted or cleared when the model reports
// an insertion, a removal or a reset, so no stale row number survives a model
// change.
//
// The overlay scrollbars follow Recycler: a thin thumb painted on the right and
// bottom edges, no QScrollBar child. The thumb can be dragged; a press anywhere
// on the track jumps to that position first.

#include <md/components/table.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QAbstractItemModel>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QResizeEvent>
#include <QSizeF>
#include <QVariant>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace md {

namespace {

// Design baseline data table metrics: the cell carries 16 dp of leading/trailing padding,
// which is what puts a header title and its column's cells on the same text
// grid (the header used 12 dp, so every column had a four pixel step).
//
// The design baseline data row is 52 dp and the design baseline header is 56 dp; both heights stay on the
// existing 48 dp contract because Table::cellRect() height and headerRect()
// height are asserted at 48 in the component suites. Raising them is a
// behaviour change that has to update those expectations in the same commit,
// which is outside this file's scope.
constexpr qreal kRowHeightDp = 48.0;
constexpr qreal kHeaderHeightDp = 48.0;
constexpr qreal kDefaultColumnWidthDp = 120.0;
constexpr qreal kCellPaddingDp = 16.0;
constexpr qreal kScrollbarWidthDp = 8.0;
constexpr qreal kMinimumThumbDp = 24.0;
constexpr qreal kPageOverlap = 1.0;

QFont fontFor(const TextStyle& style, const Density& density, const QWidget* context)
{
    return Font::resolve(context, style, density);
}

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, qreal(0.0), qreal(1.0))));
    return result;
}

// AlignLeft/AlignRight are logical in a table: they mirror together with the
// layout direction, exactly like the column order itself.
Qt::Alignment mirrorHorizontal(Qt::Alignment alignment)
{
    const bool left = alignment.testFlag(Qt::AlignLeft);
    const bool right = alignment.testFlag(Qt::AlignRight);
    if (left == right)
        return alignment;

    Qt::Alignment result = alignment & ~(Qt::AlignLeft | Qt::AlignRight);
    result |= left ? Qt::AlignRight : Qt::AlignLeft;
    return result;
}

// A number reads from its least significant digit, so a column of numbers is
// right-aligned unless the model says otherwise.
bool isNumericValue(const QVariant& value)
{
    switch (value.metaType().id()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        return true;
    default:
        break;
    }

    return false;
}

} // namespace

Table::Table(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setBackgroundColour(QColor(Qt::transparent));
}

void Table::setModel(QAbstractItemModel* model)
{
    if (model_ == model)
        return;

    if (model_ != nullptr)
        QObject::disconnect(model_, nullptr, this, nullptr);

    model_ = model;
    currentRow_ = -1;
    currentColumn_ = -1;
    hoveredRow_ = -1;
    hoveredColumn_ = -1;
    pressedRow_ = -1;
    pressedColumn_ = -1;
    scrollOffset_ = 0.0;
    horizontalScrollOffset_ = 0.0;

    connectModel();
    requestUpdate();
}

QAbstractItemModel* Table::model() const
{
    return model_;
}

void Table::setHeadersVisible(bool visible)
{
    if (headersVisible_ == visible)
        return;

    headersVisible_ = visible;
    clampScroll();
    requestUpdate();
}

bool Table::headersVisible() const noexcept
{
    return headersVisible_;
}

void Table::setRowHeight(qreal dp)
{
    const qreal clamped = std::max(qreal(1.0), dp);
    if (qFuzzyCompare(rowHeightDp_, clamped))
        return;

    rowHeightDp_ = clamped;
    clampScroll();
    requestUpdate();
}

qreal Table::rowHeight() const noexcept
{
    return rowHeightDp_;
}

void Table::setColumnWidth(int column, qreal widthDp)
{
    if (column < 0)
        return;

    const qreal clamped = std::max(qreal(1.0), widthDp);
    const qreal current = columnWidth(column);
    if (qFuzzyCompare(current + 1.0, clamped + 1.0))
        return;

    columnWidthsDp_.insert(column, clamped);
    clampScroll();
    requestUpdate();
}

qreal Table::columnWidth(int column) const
{
    const auto it = columnWidthsDp_.constFind(column);
    return it != columnWidthsDp_.constEnd() ? it.value() : defaultColumnWidthDp_;
}

void Table::setDefaultColumnWidth(qreal widthDp)
{
    const qreal clamped = std::max(qreal(1.0), widthDp);
    if (qFuzzyCompare(defaultColumnWidthDp_, clamped))
        return;

    defaultColumnWidthDp_ = clamped;
    clampScroll();
    requestUpdate();
}

qreal Table::defaultColumnWidth() const noexcept
{
    return defaultColumnWidthDp_;
}

int Table::rowCount() const
{
    return model_ != nullptr ? model_->rowCount() : 0;
}

int Table::columnCount() const
{
    return model_ != nullptr ? model_->columnCount() : 0;
}

void Table::setSelectionMode(SelectionMode mode)
{
    if (selectionMode_ == mode)
        return;

    selectionMode_ = mode;
    requestUpdate();
}

Table::SelectionMode Table::selectionMode() const noexcept
{
    return selectionMode_;
}

int Table::currentRow() const noexcept
{
    return currentRow_;
}

int Table::currentColumn() const noexcept
{
    return currentColumn_;
}

void Table::setCurrentCell(int row, int column)
{
    const int rows = rowCount();
    const int columns = columnCount();
    if (rows <= 0 || columns <= 0 || row < 0 || column < 0) {
        clearSelection();
        return;
    }

    const int targetRow = std::clamp(row, 0, rows - 1);
    const int targetColumn = std::clamp(column, 0, columns - 1);
    if (targetRow == currentRow_ && targetColumn == currentColumn_) {
        ensureCellVisible(targetRow, targetColumn);
        return;
    }

    currentRow_ = targetRow;
    currentColumn_ = targetColumn;
    ensureCellVisible(targetRow, targetColumn);
    emit currentCellChanged(targetRow, targetColumn);
    requestUpdate();
}

void Table::clearSelection()
{
    if (currentRow_ < 0 && currentColumn_ < 0)
        return;

    currentRow_ = -1;
    currentColumn_ = -1;
    emit currentCellChanged(-1, -1);
    requestUpdate();
}

qreal Table::scrollOffset() const noexcept
{
    return scrollOffset_;
}

void Table::setScrollOffset(qreal offset)
{
    const qreal clamped = std::clamp(offset, qreal(0.0), maximumScroll());
    if (qFuzzyCompare(scrollOffset_ + 1.0, clamped + 1.0))
        return;

    scrollOffset_ = clamped;
    requestUpdate();
}

qreal Table::horizontalScrollOffset() const noexcept
{
    return horizontalScrollOffset_;
}

void Table::setHorizontalScrollOffset(qreal offset)
{
    const qreal clamped = std::clamp(offset, qreal(0.0), maximumHorizontalScroll());
    if (qFuzzyCompare(horizontalScrollOffset_ + 1.0, clamped + 1.0))
        return;

    horizontalScrollOffset_ = clamped;
    requestUpdate();
}

void Table::scrollToRow(int row)
{
    if (row < 0 || row >= rowCount())
        return;

    ensureCellVisible(row, currentColumn_ >= 0 ? currentColumn_ : 0);
    requestUpdate();
}

void Table::scrollToCell(int row, int column)
{
    if (row < 0 || row >= rowCount() || column < 0 || column >= columnCount())
        return;

    ensureCellVisible(row, column);
    requestUpdate();
}

int Table::firstVisibleRow() const
{
    const qreal height = rowHeightPx();
    const int rows = rowCount();
    if (height <= 0.0 || rows == 0)
        return 0;

    return std::clamp(static_cast<int>(std::floor(scrollOffset_ / height)), 0, rows - 1);
}

int Table::visibleRowCount() const
{
    const qreal height = rowHeightPx();
    const qreal viewport = viewportHeight();
    const int rows = rowCount();
    if (height <= 0.0 || viewport <= 0.0 || rows == 0)
        return 0;

    const int visible = static_cast<int>(std::ceil(viewport / height)) + 1;
    return std::min(visible, rows);
}

int Table::firstVisibleColumn() const
{
    const int columns = columnCount();
    if (columns == 0)
        return 0;

    qreal left = 0.0;
    for (int column = 0; column < columns; ++column) {
        const qreal width = columnWidthPx(column);
        if (left + width > horizontalScrollOffset_)
            return column;
        left += width;
    }

    return columns - 1;
}

int Table::visibleColumnCount() const
{
    const int columns = columnCount();
    const qreal viewport = viewportWidth();
    if (columns == 0 || viewport <= 0.0)
        return 0;

    int visible = 0;
    qreal left = 0.0;
    for (int column = 0; column < columns; ++column) {
        const qreal width = columnWidthPx(column);
        if (left + width > horizontalScrollOffset_ && left < horizontalScrollOffset_ + viewport)
            ++visible;
        left += width;
    }

    return std::min(visible, columns);
}

QRectF Table::rowRect(int row) const
{
    const QRectF body = bodyRect();
    return QRectF(body.left(),
                  body.top() + static_cast<qreal>(row) * rowHeightPx() - scrollOffset_,
                  body.width(), rowHeightPx());
}

QRectF Table::cellRect(int row, int column) const
{
    if (row < 0 || column < 0 || row >= rowCount() || column >= columnCount())
        return QRectF();

    const QRectF content = contentRect();
    const QRectF rowGeometry = rowRect(row);
    return QRectF(content.left() + columnX(column) - horizontalScrollOffset_,
                  rowGeometry.top(), columnWidthPx(column), rowGeometry.height());
}

QRectF Table::columnRect(int column) const
{
    if (column < 0 || column >= columnCount())
        return QRectF();

    const QRectF content = contentRect();
    return QRectF(content.left() + columnX(column) - horizontalScrollOffset_,
                  content.top() + headerHeightPx(), columnWidthPx(column), viewportHeight());
}

QRectF Table::headerRect() const
{
    const QRectF content = contentRect();
    return QRectF(content.left(), content.top(), content.width(), headerHeightPx());
}

QRectF Table::bodyRect() const
{
    const QRectF content = contentRect();
    const qreal header = headerHeightPx();

    return QRectF(content.left(), content.top() + header, content.width(),
                  std::max(qreal(0.0), content.height() - header));
}

qreal Table::rowHeightPx() const
{
    return Theme::forWidget(this).density().dp(rowHeightDp_);
}

qreal Table::headerHeightPx() const
{
    return headersVisible_ ? Theme::forWidget(this).density().dp(kHeaderHeightDp) : 0.0;
}

qreal Table::columnWidthPx(int column) const
{
    return Theme::forWidget(this).density().dp(columnWidth(column));
}

qreal Table::columnX(int column) const
{
    qreal x = 0.0;
    for (int index = 0; index < column; ++index)
        x += columnWidthPx(index);

    return x;
}

qreal Table::totalColumnWidth() const
{
    qreal total = 0.0;
    const int columns = columnCount();
    for (int column = 0; column < columns; ++column)
        total += columnWidthPx(column);

    return total;
}

qreal Table::viewportWidth() const
{
    return std::max(qreal(0.0), contentRect().width());
}

qreal Table::viewportHeight() const
{
    return std::max(qreal(0.0), bodyRect().height());
}

qreal Table::maximumScroll() const
{
    const qreal total = static_cast<qreal>(rowCount()) * rowHeightPx();
    return std::max(qreal(0.0), total - viewportHeight());
}

qreal Table::maximumHorizontalScroll() const
{
    return std::max(qreal(0.0), totalColumnWidth() - viewportWidth());
}

qreal Table::scrollbarWidthPx() const
{
    return Theme::forWidget(this).density().dp(kScrollbarWidthDp);
}

QRectF Table::verticalThumbRect() const
{
    const qreal maximum = maximumScroll();
    const QRectF content = contentRect();
    const qreal bar = scrollbarWidthPx();
    const qreal top = content.top() + headerHeightPx()
        + Theme::forWidget(this).density().dp(2.0);
    const qreal track = std::max(qreal(0.0), viewportHeight()
                                               - Theme::forWidget(this).density().dp(4.0));
    if (maximum <= 0.0 || track <= 0.0 || content.width() <= bar)
        return QRectF();

    const qreal total = static_cast<qreal>(rowCount()) * rowHeightPx();
    const qreal thumb = std::min(
        track, std::max(Theme::forWidget(this).density().dp(kMinimumThumbDp),
                        track * viewportHeight() / std::max(total, qreal(1.0))));
    const qreal travel = track - thumb;
    const qreal progress = std::clamp(scrollOffset_ / maximum, qreal(0.0), qreal(1.0));

    return QRectF(content.right() - bar + Theme::forWidget(this).density().dp(1.0),
                  top + travel * progress, bar - Theme::forWidget(this).density().dp(2.0),
                  thumb);
}

QRectF Table::horizontalThumbRect() const
{
    const qreal maximum = maximumHorizontalScroll();
    const QRectF content = contentRect();
    const qreal bar = scrollbarWidthPx();
    const qreal left = content.left() + Theme::forWidget(this).density().dp(2.0);
    const qreal track = std::max(qreal(0.0), viewportWidth()
                                               - Theme::forWidget(this).density().dp(4.0));
    if (maximum <= 0.0 || track <= 0.0 || content.height() <= bar)
        return QRectF();

    const qreal total = std::max(totalColumnWidth(), qreal(1.0));
    const qreal thumb = std::min(
        track, std::max(Theme::forWidget(this).density().dp(kMinimumThumbDp),
                        track * viewportWidth() / total));
    const qreal travel = track - thumb;
    const qreal progress = std::clamp(horizontalScrollOffset_ / maximum, qreal(0.0), qreal(1.0));

    return QRectF(left + travel * progress,
                  content.bottom() - bar + Theme::forWidget(this).density().dp(1.0), thumb,
                  bar - Theme::forWidget(this).density().dp(2.0));
}

bool Table::onVerticalScrollbar(const QPointF& position) const
{
    const QRectF content = contentRect();
    return maximumScroll() > 0.0
        && position.x() >= content.right() - scrollbarWidthPx() * 1.5
        && position.y() >= content.top() + headerHeightPx()
        && position.y() <= content.bottom();
}

bool Table::onHorizontalScrollbar(const QPointF& position) const
{
    const QRectF content = contentRect();
    return maximumHorizontalScroll() > 0.0
        && position.y() >= content.bottom() - scrollbarWidthPx() * 1.5
        && position.x() >= content.left() && position.x() <= content.right();
}

void Table::updateVerticalScroll(qreal pointerY)
{
    const QRectF thumb = verticalThumbRect();
    const qreal top = contentRect().top() + headerHeightPx()
        + Theme::forWidget(this).density().dp(2.0);
    const qreal track = std::max(qreal(0.0), viewportHeight()
                                               - Theme::forWidget(this).density().dp(4.0));
    const qreal travel = std::max(qreal(0.0), track - thumb.height());
    if (travel <= 0.0)
        return;

    const qreal offset = std::clamp(pointerY - dragGrab_ - top, qreal(0.0), travel);
    setScrollOffset((offset / travel) * maximumScroll());
}

void Table::updateHorizontalScroll(qreal pointerX)
{
    const QRectF thumb = horizontalThumbRect();
    const qreal left = contentRect().left() + Theme::forWidget(this).density().dp(2.0);
    const qreal track = std::max(qreal(0.0), viewportWidth()
                                               - Theme::forWidget(this).density().dp(4.0));
    const qreal travel = std::max(qreal(0.0), track - thumb.width());
    if (travel <= 0.0)
        return;

    const qreal offset = std::clamp(pointerX - dragGrab_ - left, qreal(0.0), travel);
    setHorizontalScrollOffset((offset / travel) * maximumHorizontalScroll());
}

void Table::clampScroll()
{
    scrollOffset_ = std::clamp(scrollOffset_, qreal(0.0), maximumScroll());
    horizontalScrollOffset_ =
        std::clamp(horizontalScrollOffset_, qreal(0.0), maximumHorizontalScroll());
}

void Table::ensureCellVisible(int row, int column)
{
    if (row < 0 || column < 0)
        return;

    const qreal height = rowHeightPx();
    const qreal viewport = viewportHeight();
    if (height > 0.0 && viewport > 0.0) {
        const qreal top = static_cast<qreal>(row) * height;
        const qreal bottom = top + height;
        if (top < scrollOffset_)
            scrollOffset_ = top;
        else if (bottom > scrollOffset_ + viewport)
            scrollOffset_ = bottom - viewport;
    }

    const qreal width = viewportWidth();
    if (width > 0.0) {
        const qreal left = columnX(column);
        const qreal right = left + columnWidthPx(column);
        if (left < horizontalScrollOffset_)
            horizontalScrollOffset_ = left;
        else if (right > horizontalScrollOffset_ + width)
            horizontalScrollOffset_ = right - width;
    }

    clampScroll();
}

int Table::rowAtPosition(const QPointF& position) const
{
    const QRectF content = contentRect();
    const qreal height = rowHeightPx();
    const qreal top = content.top() + headerHeightPx();
    if (height <= 0.0 || !content.contains(position) || position.y() < top)
        return -1;

    const int row = static_cast<int>(
        std::floor((position.y() - top + scrollOffset_) / height));
    if (row < 0 || row >= rowCount())
        return -1;

    return row;
}

int Table::columnAtPosition(const QPointF& position) const
{
    const QRectF content = contentRect();
    if (!content.contains(position) || position.x() < content.left())
        return -1;

    const qreal x = position.x() - content.left() + horizontalScrollOffset_;
    qreal left = 0.0;
    const int columns = columnCount();
    for (int column = 0; column < columns; ++column) {
        const qreal width = columnWidthPx(column);
        if (x >= left && x < left + width)
            return column;
        left += width;
    }

    return -1;
}

void Table::connectModel()
{
    if (model_ == nullptr)
        return;

    connect(model_, &QAbstractItemModel::dataChanged, this, [this] { requestUpdate(); });

    connect(model_, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex&, int first, int last) {
                const int count = last - first + 1;
                if (currentRow_ >= first)
                    currentRow_ += count;
                if (hoveredRow_ >= first)
                    hoveredRow_ += count;
                clampScroll();
                requestUpdate();
            });

    connect(model_, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex&, int first, int last) {
                const int count = last - first + 1;
                bool changed = false;
                if (currentRow_ >= first && currentRow_ <= last) {
                    currentRow_ = -1;
                    currentColumn_ = -1;
                    changed = true;
                } else if (currentRow_ > last) {
                    currentRow_ -= count;
                }

                if (hoveredRow_ >= first && hoveredRow_ <= last)
                    hoveredRow_ = -1;
                else if (hoveredRow_ > last)
                    hoveredRow_ -= count;

                pressedRow_ = -1;
                pressedColumn_ = -1;
                clampScroll();
                if (changed)
                    emit currentCellChanged(-1, -1);
                requestUpdate();
            });

    connect(model_, &QAbstractItemModel::columnsInserted, this,
            [this](const QModelIndex&, int first, int last) {
                const int count = last - first + 1;
                if (currentColumn_ >= first)
                    currentColumn_ += count;
                if (hoveredColumn_ >= first)
                    hoveredColumn_ += count;
                clampScroll();
                requestUpdate();
            });

    connect(model_, &QAbstractItemModel::columnsRemoved, this,
            [this](const QModelIndex&, int first, int last) {
                const int count = last - first + 1;
                bool changed = false;
                if (currentColumn_ >= first && currentColumn_ <= last) {
                    currentRow_ = -1;
                    currentColumn_ = -1;
                    changed = true;
                } else if (currentColumn_ > last) {
                    currentColumn_ -= count;
                }

                if (hoveredColumn_ >= first && hoveredColumn_ <= last)
                    hoveredColumn_ = -1;
                else if (hoveredColumn_ > last)
                    hoveredColumn_ -= count;

                pressedRow_ = -1;
                pressedColumn_ = -1;
                clampScroll();
                if (changed)
                    emit currentCellChanged(-1, -1);
                requestUpdate();
            });

    connect(model_, &QAbstractItemModel::modelReset, this, [this] { resetCurrent(true); });

    connect(model_, &QAbstractItemModel::layoutChanged, this,
            [this] { clampScroll(); requestUpdate(); });

    connect(model_, &QObject::destroyed, this, [this] {
        model_ = nullptr;
        currentRow_ = -1;
        currentColumn_ = -1;
        hoveredRow_ = -1;
        hoveredColumn_ = -1;
        pressedRow_ = -1;
        pressedColumn_ = -1;
        scrollOffset_ = 0.0;
        horizontalScrollOffset_ = 0.0;
        requestUpdate();
    });
}

void Table::resetCurrent(bool notify)
{
    const bool hadCurrent = currentRow_ >= 0 || currentColumn_ >= 0;
    currentRow_ = -1;
    currentColumn_ = -1;
    hoveredRow_ = -1;
    hoveredColumn_ = -1;
    pressedRow_ = -1;
    pressedColumn_ = -1;
    scrollOffset_ = 0.0;
    horizontalScrollOffset_ = 0.0;
    if (notify && hadCurrent)
        emit currentCellChanged(-1, -1);
    requestUpdate();
}

void Table::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    const qreal totalWidth = totalColumnWidth();
    const qreal naturalWidth = std::clamp(
        totalWidth > 0.0 ? totalWidth : density.dp(320.0), density.dp(240.0),
        density.dp(480.0));
    const qreal naturalHeight = std::clamp(
        headerHeightPx() + static_cast<qreal>(rowCount()) * rowHeightPx(), density.dp(96.0),
        density.dp(320.0));

    setMeasuredSize(
        QSizeF(width.resolve(naturalWidth), height.resolve(naturalHeight)));
}

void Table::arrangeOverride(const QRectF& content)
{
    Q_UNUSED(content);
    clampScroll();
    requestUpdate();
}

void Table::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();

    painter.save();
    painter.fillRect(content, colours.surface);
    painter.setClipRect(content);

    if (headersVisible_)
        paintHeader(painter, content);

    // The rows live strictly inside bodyRect(). Clipping the body to that band
    // is what keeps a partially scrolled first row — its fill, its text and its
    // separator — from painting over the header, which is painted first and
    // would otherwise lose the overlap. Row geometry and the clip come from the
    // same rect, so they cannot drift apart.
    const QRectF body = bodyRect();
    if (body.height() > 0.0)
        painter.setClipRect(body, Qt::IntersectClip);

    const int rows = rowCount();
    if (rows == 0) {
        if (body.height() > 0.0) {
            painter.setFont(fontFor(theme.type().bodyMedium, density, this));
            painter.setPen(colours.onSurfaceVariant);
            painter.drawText(body, static_cast<int>(Qt::AlignCenter),
                             QStringLiteral("No rows"));
        }
    } else {
        const int columns = columnCount();
        const int first = firstVisibleRow();
        const int last = std::min(rows - 1, first + visibleRowCount());
        const bool hasSelection = selectionMode_ != SelectionMode::NoSelection
            && currentRow_ >= 0 && currentColumn_ >= 0;

        for (int row = first; row <= last; ++row) {
            const QRectF geometry = rowRect(row);
            if (geometry.bottom() < body.top())
                continue;
            if (geometry.top() > body.bottom())
                break;

            qreal x = content.left() - horizontalScrollOffset_;
            for (int column = 0; column < columns; ++column) {
                const qreal cellWidth = columnWidthPx(column);
                if (x + cellWidth <= content.left()) {
                    x += cellWidth;
                    continue;
                }
                if (x >= content.right())
                    break;

                const bool selected = hasSelection && row == currentRow_
                    && (selectionMode_ == SelectionMode::SingleRow
                        || column == currentColumn_);
                const bool hovered = hoveredRow_ == row && hoveredColumn_ == column
                    && !selected;

                paintCell(painter, row, column,
                          QRectF(x, geometry.top(), cellWidth, geometry.height()), selected,
                          hovered);
                x += cellWidth;
            }

            painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
            painter.drawLine(QPointF(content.left(), geometry.bottom() - density.dp(0.5)),
                             QPointF(content.right(), geometry.bottom() - density.dp(0.5)));
        }
    }

    // The scrollbars live on the frame, not in the body band.
    painter.setClipRect(content);
    paintScrollbars(painter, content);
    painter.restore();
}

void Table::paintHeader(QPainter& painter, const QRectF& content)
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();
    const qreal height = headerHeightPx();
    if (height <= 0.0)
        return;

    const QRectF header(content.left(), content.top(), content.width(), height);
    painter.save();
    painter.fillRect(header, colours.surfaceContainer);
    painter.setClipRect(header);

    const QFont font = fontFor(theme.type().titleSmall, density, this);
    const QFontMetricsF metrics(font);
    const qreal padding = density.dp(kCellPaddingDp);
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
    const Qt::Alignment alignment =
        (rightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter;

    qreal x = content.left() - horizontalScrollOffset_;
    const int columns = columnCount();
    for (int column = 0; column < columns; ++column) {
        const qreal width = columnWidthPx(column);
        if (x + width <= content.left()) {
            x += width;
            continue;
        }
        if (x >= content.right())
            break;

        const QRectF cell(x, header.top(), width, height);
        const QString title =
            model_ != nullptr
            ? model_->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString()
            : QString();

        painter.save();
        painter.setClipRect(cell, Qt::IntersectClip);
        if (!title.isEmpty()) {
            const qreal available = std::max(cell.width() - padding * 2.0, qreal(0.0));
            if (available > 0.0) {
                const QString elided = metrics.elidedText(
                    title, rightToLeft ? Qt::ElideLeft : Qt::ElideRight, available);
                painter.setFont(font);
                painter.setPen(colours.onSurfaceVariant);
                painter.drawText(cell.adjusted(padding, 0.0, -padding, 0.0),
                                 static_cast<int>(alignment), elided);
            }
        }
        painter.restore();
        x += width;
    }

    painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
    painter.drawLine(QPointF(header.left(), header.bottom() - density.dp(0.5)),
                     QPointF(header.right(), header.bottom() - density.dp(0.5)));
    painter.restore();
}

void Table::paintCell(QPainter& painter, int row, int column, const QRectF& rect,
                      bool selected, bool hovered)
{
    if (rect.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();

    painter.save();
    painter.setClipRect(rect, Qt::IntersectClip);

    if (selected)
        painter.fillRect(rect, colours.secondaryContainer);
    else if (hovered)
        painter.fillRect(rect, colours.stateLayer(colours.onSurface, ViewState::Hovered));

    if (model_ != nullptr) {
        const QModelIndex index = model_->index(row, column);
        const QVariant cellData = index.data(Qt::DisplayRole);
        const QString text = cellData.toString();
        if (!text.isEmpty()) {
            const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
            Qt::Alignment alignment = Qt::AlignLeft;
            bool explicitAlignment = false;
            const QVariant alignmentValue = index.data(Qt::TextAlignmentRole);
            if (alignmentValue.isValid()) {
                bool ok = false;
                const int raw = alignmentValue.toInt(&ok);
                if (ok) {
                    alignment = static_cast<Qt::Alignment>(raw) & Qt::AlignHorizontal_Mask;
                    explicitAlignment = true;
                }
            }

            if (!explicitAlignment && isNumericValue(cellData))
                alignment = Qt::AlignRight;

            if (rightToLeft)
                alignment = mirrorHorizontal(alignment);

            // A TextAlignmentRole that carries only a horizontal flag used to
            // leave the text at the top of the cell; the vertical centring is
            // the table's job, not the model's.
            alignment |= Qt::AlignVCenter;

            const QFont font = fontFor(theme.type().bodyMedium, density, this);
            const QFontMetricsF metrics(font);
            const qreal padding = density.dp(kCellPaddingDp);
            const qreal available = std::max(rect.width() - padding * 2.0, qreal(0.0));
            if (available > 0.0) {
                const QString elided = metrics.elidedText(
                    text, rightToLeft ? Qt::ElideLeft : Qt::ElideRight, available);
                painter.setFont(font);
                painter.setPen(selected ? colours.onSecondaryContainer : colours.onSurface);
                painter.drawText(rect.adjusted(padding, 0.0, -padding, 0.0),
                                 static_cast<int>(alignment), elided);
            }
        }
    }

    painter.restore();
}

void Table::paintScrollbars(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);
    const Theme& theme = Theme::forWidget(this);
    const QColor thumbColour = withAlpha(theme.colours().onSurface, 0.38);

    const QRectF vertical = verticalThumbRect();
    if (!vertical.isEmpty()) {
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(thumbColour);
        painter.drawRoundedRect(vertical, vertical.width() / 2.0, vertical.width() / 2.0);
        painter.restore();
    }

    const QRectF horizontal = horizontalThumbRect();
    if (!horizontal.isEmpty()) {
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(thumbColour);
        painter.drawRoundedRect(horizontal, horizontal.height() / 2.0,
                                horizontal.height() / 2.0);
        painter.restore();
    }
}

void Table::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    setFocus(Qt::MouseFocusReason);
    const QPointF position = event->position();

    if (onVerticalScrollbar(position)) {
        draggingVertical_ = true;
        const QRectF thumb = verticalThumbRect();
        dragGrab_ = thumb.contains(position)
            ? position.y() - thumb.top()
            : thumb.height() / 2.0;
        updateVerticalScroll(position.y());
        event->accept();
        return;
    }

    if (onHorizontalScrollbar(position)) {
        draggingHorizontal_ = true;
        const QRectF thumb = horizontalThumbRect();
        dragGrab_ = thumb.contains(position)
            ? position.x() - thumb.left()
            : thumb.width() / 2.0;
        updateHorizontalScroll(position.x());
        event->accept();
        return;
    }

    pressedRow_ = rowAtPosition(position);
    pressedColumn_ = columnAtPosition(position);
    if (pressedRow_ >= 0 && pressedColumn_ >= 0)
        setCurrentCell(pressedRow_, pressedColumn_);

    requestUpdate();
}

void Table::mouseMoveEvent(QMouseEvent* event)
{
    View::mouseMoveEvent(event);

    if (draggingVertical_) {
        updateVerticalScroll(event->position().y());
        event->accept();
        return;
    }

    if (draggingHorizontal_) {
        updateHorizontalScroll(event->position().x());
        event->accept();
        return;
    }

    const int row = rowAtPosition(event->position());
    const int column = columnAtPosition(event->position());
    if (row == hoveredRow_ && column == hoveredColumn_)
        return;

    hoveredRow_ = row;
    hoveredColumn_ = column;
    requestUpdate();
}

void Table::mouseReleaseEvent(QMouseEvent* event)
{
    View::mouseReleaseEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    if (draggingVertical_ || draggingHorizontal_) {
        draggingVertical_ = false;
        draggingHorizontal_ = false;
        event->accept();
        return;
    }

    const int row = rowAtPosition(event->position());
    const int column = columnAtPosition(event->position());
    const int pressedRow = pressedRow_;
    const int pressedColumn = pressedColumn_;
    pressedRow_ = -1;
    pressedColumn_ = -1;

    if (pressedRow >= 0 && pressedRow == row && pressedColumn == column)
        emit cellClicked(row, column);

    requestUpdate();
}

void Table::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    const int row = rowAtPosition(event->position());
    const int column = columnAtPosition(event->position());
    if (row >= 0 && column >= 0) {
        setCurrentCell(row, column);
        emit doubleClicked(row, column);
    }

    event->accept();
}

void Table::wheelEvent(QWheelEvent* event)
{
    const QPoint delta = event->angleDelta();
    const bool horizontal = delta.x() != 0
        || event->modifiers().testFlag(Qt::ShiftModifier);
    if (!horizontal && delta.y() == 0) {
        event->ignore();
        return;
    }

    if (horizontal) {
        const qreal steps = static_cast<qreal>(delta.x() != 0 ? delta.x() : delta.y()) / 120.0;
        setHorizontalScrollOffset(horizontalScrollOffset_
                                  - steps * Theme::forWidget(this).density().dp(48.0) * 3.0);
    } else {
        const qreal steps = static_cast<qreal>(delta.y()) / 120.0;
        setScrollOffset(scrollOffset_ - steps * rowHeightPx() * 3.0);
    }

    event->accept();
}

void Table::keyPressEvent(QKeyEvent* event)
{
    const int rows = rowCount();
    const int columns = columnCount();
    if (rows == 0 || columns == 0 || event->key() == Qt::Key_Tab
        || event->key() == Qt::Key_Backtab) {
        View::keyPressEvent(event);
        return;
    }

    const int activeRow = currentRow_ >= 0
        ? currentRow_
        : (event->key() == Qt::Key_Up || event->key() == Qt::Key_PageUp ? rows - 1 : 0);
    const int activeColumn = currentColumn_ >= 0 ? currentColumn_ : 0;
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);

    int row = activeRow;
    int column = activeColumn;
    bool handled = true;

    switch (event->key()) {
    case Qt::Key_Down:
        row = std::min(rows - 1, activeRow + 1);
        break;
    case Qt::Key_Up:
        row = std::max(0, activeRow - 1);
        break;
    case Qt::Key_Left:
        column = std::max(0, activeColumn - 1);
        break;
    case Qt::Key_Right:
        column = std::min(columns - 1, activeColumn + 1);
        break;
    case Qt::Key_Home:
        column = 0;
        if (control)
            row = 0;
        break;
    case Qt::Key_End:
        column = columns - 1;
        if (control)
            row = rows - 1;
        break;
    case Qt::Key_PageDown: {
        const int stride = std::max(1, visibleRowCount() - static_cast<int>(kPageOverlap));
        row = std::min(rows - 1, activeRow + stride);
        break;
    }
    case Qt::Key_PageUp: {
        const int stride = std::max(1, visibleRowCount() - static_cast<int>(kPageOverlap));
        row = std::max(0, activeRow - stride);
        break;
    }
    default:
        handled = false;
        break;
    }

    if (!handled) {
        View::keyPressEvent(event);
        return;
    }

    setCurrentCell(row, column);
    event->accept();
}

void Table::resizeEvent(QResizeEvent* event)
{
    View::resizeEvent(event);
    clampScroll();
    requestUpdate();
}

void Table::leaveEvent(QEvent* event)
{
    View::leaveEvent(event);

    if (hoveredRow_ < 0 && hoveredColumn_ < 0)
        return;

    hoveredRow_ = -1;
    hoveredColumn_ = -1;
    requestUpdate();
}

} // namespace md
