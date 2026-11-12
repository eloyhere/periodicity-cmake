// Periodicity — VirtualTable, a table over an on-demand data source.
//
// The paint pass asks the source only for the cells inside the viewport plus
// the configured prefetch rows. A 10 000-row source therefore performs work
// proportional to one screen, not to its size; visibleRowCount() and
// visibleColumnCount() are bounded by the viewport, and the frozen-column
// region is clipped separately so a scrolling cell is never queried when it is
// hidden underneath the pinned columns.

#include <md/components/virtual_table.h>

#include <md/structure/table.h>
#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QResizeEvent>
#include <QSizeF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace md {

namespace {

// The design baseline data-table cell carries 16 dp of leading/trailing padding, so a
// frozen column, a scrolling column and the header all start their text on the
// same grid. The row stays on the 48 dp default that the header declares and
// the component suites assert.
constexpr qreal kHeaderHeightDp = 48.0;
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

} // namespace

VirtualTable::VirtualTable(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setBackgroundColour(QColor(Qt::transparent));
}

void VirtualTable::setSource(structure::VirtualTableSource* source)
{
    if (source_ == source)
        return;

    source_ = source;
    currentRow_ = -1;
    currentColumn_ = -1;
    hoveredRow_ = -1;
    hoveredColumn_ = -1;
    pressedRow_ = -1;
    pressedColumn_ = -1;
    scrollOffset_ = 0.0;
    horizontalScrollOffset_ = 0.0;

    refresh();
}

structure::VirtualTableSource* VirtualTable::source() const
{
    return source_;
}

void VirtualTable::refresh()
{
    rowCount_ = source_ != nullptr ? std::max(0, source_->rowCount()) : 0;
    columnCount_ = source_ != nullptr ? std::max(0, source_->columnCount()) : 0;
    frozenColumns_ = std::clamp(frozenColumns_, 0, columnCount_);

    bool changed = false;
    if (currentRow_ >= rowCount_ || currentColumn_ >= columnCount_) {
        currentRow_ = -1;
        currentColumn_ = -1;
        changed = true;
    }

    clampScroll();
    if (changed)
        emit currentCellChanged(-1, -1);
    requestUpdate();
}

int VirtualTable::rowCount() const
{
    return rowCount_;
}

int VirtualTable::columnCount() const
{
    return columnCount_;
}

QString VirtualTable::cellText(int row, int column) const
{
    if (source_ == nullptr || row < 0 || row >= rowCount_ || column < 0
        || column >= columnCount_) {
        return QString();
    }

    return source_->cell(row, column).toString();
}

void VirtualTable::setHeadersVisible(bool visible)
{
    if (headersVisible_ == visible)
        return;

    headersVisible_ = visible;
    clampScroll();
    requestUpdate();
}

bool VirtualTable::headersVisible() const noexcept
{
    return headersVisible_;
}

void VirtualTable::setRowHeight(qreal dp)
{
    const qreal clamped = std::max(qreal(1.0), dp);
    if (qFuzzyCompare(rowHeightDp_, clamped))
        return;

    rowHeightDp_ = clamped;
    clampScroll();
    requestUpdate();
}

qreal VirtualTable::rowHeight() const noexcept
{
    return rowHeightDp_;
}

void VirtualTable::setColumnWidth(int column, qreal widthDp)
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

qreal VirtualTable::columnWidth(int column) const
{
    const auto it = columnWidthsDp_.constFind(column);
    return it != columnWidthsDp_.constEnd() ? it.value() : defaultColumnWidthDp_;
}

void VirtualTable::setDefaultColumnWidth(qreal widthDp)
{
    const qreal clamped = std::max(qreal(1.0), widthDp);
    if (qFuzzyCompare(defaultColumnWidthDp_, clamped))
        return;

    defaultColumnWidthDp_ = clamped;
    clampScroll();
    requestUpdate();
}

qreal VirtualTable::defaultColumnWidth() const noexcept
{
    return defaultColumnWidthDp_;
}

void VirtualTable::setPrefetchRows(int rows)
{
    const int clamped = std::max(0, rows);
    if (prefetchRows_ == clamped)
        return;

    prefetchRows_ = clamped;
    requestUpdate();
}

int VirtualTable::prefetchRows() const noexcept
{
    return prefetchRows_;
}

void VirtualTable::setFrozenColumns(int columns)
{
    const int clamped = std::clamp(columns, 0, columnCount_);
    if (frozenColumns_ == clamped)
        return;

    frozenColumns_ = clamped;
    clampScroll();
    requestUpdate();
}

int VirtualTable::frozenColumns() const noexcept
{
    return frozenColumns_;
}

void VirtualTable::setSelectionMode(SelectionMode mode)
{
    if (selectionMode_ == mode)
        return;

    selectionMode_ = mode;
    requestUpdate();
}

VirtualTable::SelectionMode VirtualTable::selectionMode() const noexcept
{
    return selectionMode_;
}

int VirtualTable::currentRow() const noexcept
{
    return currentRow_;
}

int VirtualTable::currentColumn() const noexcept
{
    return currentColumn_;
}

void VirtualTable::setCurrentCell(int row, int column)
{
    if (rowCount_ <= 0 || columnCount_ <= 0 || row < 0 || column < 0) {
        clearSelection();
        return;
    }

    const int targetRow = std::clamp(row, 0, rowCount_ - 1);
    const int targetColumn = std::clamp(column, 0, columnCount_ - 1);
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

void VirtualTable::clearSelection()
{
    if (currentRow_ < 0 && currentColumn_ < 0)
        return;

    currentRow_ = -1;
    currentColumn_ = -1;
    emit currentCellChanged(-1, -1);
    requestUpdate();
}

qreal VirtualTable::scrollOffset() const noexcept
{
    return scrollOffset_;
}

void VirtualTable::setScrollOffset(qreal offset)
{
    const qreal clamped = std::clamp(offset, qreal(0.0), maximumScroll());
    if (qFuzzyCompare(scrollOffset_ + 1.0, clamped + 1.0))
        return;

    scrollOffset_ = clamped;
    requestUpdate();
}

qreal VirtualTable::horizontalScrollOffset() const noexcept
{
    return horizontalScrollOffset_;
}

void VirtualTable::setHorizontalScrollOffset(qreal offset)
{
    const qreal clamped = std::clamp(offset, qreal(0.0), maximumHorizontalScroll());
    if (qFuzzyCompare(horizontalScrollOffset_ + 1.0, clamped + 1.0))
        return;

    horizontalScrollOffset_ = clamped;
    requestUpdate();
}

void VirtualTable::scrollToRow(int row)
{
    if (row < 0 || row >= rowCount_)
        return;

    ensureCellVisible(row, currentColumn_ >= 0 ? currentColumn_ : 0);
    requestUpdate();
}

void VirtualTable::scrollToCell(int row, int column)
{
    if (row < 0 || row >= rowCount_ || column < 0 || column >= columnCount_)
        return;

    ensureCellVisible(row, column);
    requestUpdate();
}

int VirtualTable::firstVisibleRow() const
{
    const qreal height = rowHeightPx();
    if (height <= 0.0 || rowCount_ == 0)
        return 0;

    return std::clamp(static_cast<int>(std::floor(scrollOffset_ / height)), 0,
                      rowCount_ - 1);
}

int VirtualTable::visibleRowCount() const
{
    const qreal height = rowHeightPx();
    const qreal viewport = viewportHeight();
    if (height <= 0.0 || viewport <= 0.0 || rowCount_ == 0)
        return 0;

    const int visible = static_cast<int>(std::ceil(viewport / height)) + 1;
    return std::min(visible, rowCount_);
}

int VirtualTable::firstVisibleColumn() const
{
    if (columnCount_ == 0)
        return 0;

    if (frozenColumns_ > 0)
        return 0;

    qreal left = 0.0;
    for (int column = frozenColumns_; column < columnCount_; ++column) {
        const qreal width = columnWidthPx(column);
        if (left + width > horizontalScrollOffset_)
            return column;
        left += width;
    }

    return columnCount_ - 1;
}

int VirtualTable::visibleColumnCount() const
{
    if (columnCount_ == 0 || viewportWidth() <= 0.0)
        return 0;

    int visible = std::min(frozenColumns_, columnCount_);
    const qreal leftEdge = contentRect().left() + frozenWidthPx();

    qreal x = contentRect().left() + frozenWidthPx() - horizontalScrollOffset_;
    const qreal rightEdge = contentRect().right();
    for (int column = frozenColumns_; column < columnCount_; ++column) {
        const qreal width = columnWidthPx(column);
        if (x + width > leftEdge && x < rightEdge)
            ++visible;
        x += width;
    }

    return std::min(visible, columnCount_);
}

QRectF VirtualTable::rowRect(int row) const
{
    const QRectF body = bodyRect();
    return QRectF(body.left(),
                  body.top() + static_cast<qreal>(row) * rowHeightPx() - scrollOffset_,
                  body.width(), rowHeightPx());
}

QRectF VirtualTable::cellRect(int row, int column) const
{
    if (row < 0 || column < 0 || row >= rowCount_ || column >= columnCount_)
        return QRectF();

    const QRectF rowGeometry = rowRect(row);
    const QRectF columnGeometry = columnRect(column);
    return QRectF(columnGeometry.left(), rowGeometry.top(), columnGeometry.width(),
                  rowGeometry.height());
}

QRectF VirtualTable::columnRect(int column) const
{
    if (column < 0 || column >= columnCount_)
        return QRectF();

    const QRectF body = bodyRect();
    return QRectF(body.left() + columnX(column), body.top(), columnWidthPx(column),
                  body.height());
}

QRectF VirtualTable::headerRect() const
{
    const QRectF content = contentRect();
    return QRectF(content.left(), content.top(), content.width(), headerHeightPx());
}

QRectF VirtualTable::bodyRect() const
{
    const QRectF content = contentRect();
    const qreal header = headerHeightPx();

    return QRectF(content.left(), content.top() + header, content.width(),
                  std::max(qreal(0.0), content.height() - header));
}

qreal VirtualTable::rowHeightPx() const
{
    return Theme::forWidget(this).density().dp(rowHeightDp_);
}

qreal VirtualTable::headerHeightPx() const
{
    return headersVisible_ ? Theme::forWidget(this).density().dp(kHeaderHeightDp) : 0.0;
}

qreal VirtualTable::columnWidthPx(int column) const
{
    return Theme::forWidget(this).density().dp(columnWidth(column));
}

qreal VirtualTable::frozenWidthPx() const
{
    qreal width = 0.0;
    for (int column = 0; column < frozenColumns_; ++column)
        width += columnWidthPx(column);

    return width;
}

qreal VirtualTable::columnX(int column) const
{
    qreal x = 0.0;
    if (column < frozenColumns_) {
        for (int index = 0; index < column; ++index)
            x += columnWidthPx(index);
        return x;
    }

    for (int index = 0; index < frozenColumns_; ++index)
        x += columnWidthPx(index);
    for (int index = frozenColumns_; index < column; ++index)
        x += columnWidthPx(index);

    return x - horizontalScrollOffset_;
}

qreal VirtualTable::totalColumnWidth() const
{
    qreal total = 0.0;
    for (int column = 0; column < columnCount_; ++column)
        total += columnWidthPx(column);

    return total;
}

qreal VirtualTable::viewportWidth() const
{
    return std::max(qreal(0.0), contentRect().width());
}

qreal VirtualTable::viewportHeight() const
{
    return std::max(qreal(0.0), bodyRect().height());
}

qreal VirtualTable::maximumScroll() const
{
    const qreal total = static_cast<qreal>(rowCount_) * rowHeightPx();
    return std::max(qreal(0.0), total - viewportHeight());
}

qreal VirtualTable::maximumHorizontalScroll() const
{
    const qreal frozen = frozenWidthPx();
    const qreal scrolling = std::max(qreal(0.0), totalColumnWidth() - frozen);
    const qreal window = std::max(qreal(0.0), viewportWidth() - frozen);
    return std::max(qreal(0.0), scrolling - window);
}

qreal VirtualTable::scrollbarWidthPx() const
{
    return Theme::forWidget(this).density().dp(kScrollbarWidthDp);
}

QRectF VirtualTable::verticalThumbRect() const
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

    const qreal total = static_cast<qreal>(rowCount_) * rowHeightPx();
    const qreal thumb = std::min(
        track, std::max(Theme::forWidget(this).density().dp(kMinimumThumbDp),
                        track * viewportHeight() / std::max(total, qreal(1.0))));
    const qreal travel = track - thumb;
    const qreal progress = std::clamp(scrollOffset_ / maximum, qreal(0.0), qreal(1.0));

    return QRectF(content.right() - bar + Theme::forWidget(this).density().dp(1.0),
                  top + travel * progress, bar - Theme::forWidget(this).density().dp(2.0),
                  thumb);
}

QRectF VirtualTable::horizontalThumbRect() const
{
    const qreal maximum = maximumHorizontalScroll();
    const QRectF content = contentRect();
    const qreal bar = scrollbarWidthPx();
    const qreal left = content.left() + frozenWidthPx()
        + Theme::forWidget(this).density().dp(2.0);
    const qreal track = std::max(
        qreal(0.0),
        viewportWidth() - frozenWidthPx() - Theme::forWidget(this).density().dp(4.0));
    if (maximum <= 0.0 || track <= 0.0 || content.height() <= bar)
        return QRectF();

    const qreal total = std::max(totalColumnWidth() - frozenWidthPx(), qreal(1.0));
    const qreal window = std::max(viewportWidth() - frozenWidthPx(), qreal(1.0));
    const qreal thumb = std::min(
        track, std::max(Theme::forWidget(this).density().dp(kMinimumThumbDp),
                        track * window / total));
    const qreal travel = track - thumb;
    const qreal progress = std::clamp(horizontalScrollOffset_ / maximum, qreal(0.0), qreal(1.0));

    return QRectF(left + travel * progress,
                  content.bottom() - bar + Theme::forWidget(this).density().dp(1.0), thumb,
                  bar - Theme::forWidget(this).density().dp(2.0));
}

bool VirtualTable::onVerticalScrollbar(const QPointF& position) const
{
    const QRectF content = contentRect();
    return maximumScroll() > 0.0
        && position.x() >= content.right() - scrollbarWidthPx() * 1.5
        && position.y() >= content.top() + headerHeightPx()
        && position.y() <= content.bottom();
}

bool VirtualTable::onHorizontalScrollbar(const QPointF& position) const
{
    const QRectF content = contentRect();
    return maximumHorizontalScroll() > 0.0
        && position.y() >= content.bottom() - scrollbarWidthPx() * 1.5
        && position.x() >= content.left() && position.x() <= content.right();
}

void VirtualTable::updateVerticalScroll(qreal pointerY)
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

void VirtualTable::updateHorizontalScroll(qreal pointerX)
{
    const QRectF thumb = horizontalThumbRect();
    const qreal left = contentRect().left() + frozenWidthPx()
        + Theme::forWidget(this).density().dp(2.0);
    const qreal track = std::max(
        qreal(0.0),
        viewportWidth() - frozenWidthPx() - Theme::forWidget(this).density().dp(4.0));
    const qreal travel = std::max(qreal(0.0), track - thumb.width());
    if (travel <= 0.0)
        return;

    const qreal offset = std::clamp(pointerX - dragGrab_ - left, qreal(0.0), travel);
    setHorizontalScrollOffset((offset / travel) * maximumHorizontalScroll());
}

void VirtualTable::clampScroll()
{
    scrollOffset_ = std::clamp(scrollOffset_, qreal(0.0), maximumScroll());
    horizontalScrollOffset_ =
        std::clamp(horizontalScrollOffset_, qreal(0.0), maximumHorizontalScroll());
}

void VirtualTable::ensureCellVisible(int row, int column)
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

    if (column >= frozenColumns_) {
        qreal left = 0.0;
        for (int index = frozenColumns_; index < column; ++index)
            left += columnWidthPx(index);
        const qreal right = left + columnWidthPx(column);
        const qreal window = std::max(qreal(0.0), viewportWidth() - frozenWidthPx());
        if (window > 0.0) {
            if (left < horizontalScrollOffset_)
                horizontalScrollOffset_ = left;
            else if (right > horizontalScrollOffset_ + window)
                horizontalScrollOffset_ = right - window;
        }
    }

    clampScroll();
}

int VirtualTable::rowAtPosition(const QPointF& position) const
{
    const QRectF content = contentRect();
    const qreal height = rowHeightPx();
    const qreal top = content.top() + headerHeightPx();
    if (height <= 0.0 || !content.contains(position) || position.y() < top)
        return -1;

    const int row = static_cast<int>(
        std::floor((position.y() - top + scrollOffset_) / height));
    if (row < 0 || row >= rowCount_)
        return -1;

    return row;
}

int VirtualTable::columnAtPosition(const QPointF& position) const
{
    const QRectF content = contentRect();
    if (!content.contains(position) || position.x() < content.left())
        return -1;

    const qreal x = position.x() - content.left();
    const qreal frozen = frozenWidthPx();
    if (x < frozen) {
        qreal left = 0.0;
        for (int column = 0; column < frozenColumns_; ++column) {
            const qreal width = columnWidthPx(column);
            if (x >= left && x < left + width)
                return column;
            left += width;
        }
        return -1;
    }

    const qreal scrolled = x - frozen + horizontalScrollOffset_;
    qreal left = 0.0;
    for (int column = frozenColumns_; column < columnCount_; ++column) {
        const qreal width = columnWidthPx(column);
        if (scrolled >= left && scrolled < left + width)
            return column;
        left += width;
    }

    return -1;
}

void VirtualTable::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    const qreal totalWidth = totalColumnWidth();
    const qreal naturalWidth = std::clamp(
        totalWidth > 0.0 ? totalWidth : density.dp(320.0), density.dp(240.0),
        density.dp(480.0));
    const qreal naturalHeight = std::clamp(
        headerHeightPx() + static_cast<qreal>(rowCount_) * rowHeightPx(), density.dp(96.0),
        density.dp(320.0));

    setMeasuredSize(
        QSizeF(width.resolve(naturalWidth), height.resolve(naturalHeight)));
}

void VirtualTable::arrangeOverride(const QRectF& content)
{
    Q_UNUSED(content);
    clampScroll();
    requestUpdate();
}

void VirtualTable::paintContent(QPainter& painter, const QRectF& content)
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

    // Everything a row paints — background, text and separator — is clipped to
    // bodyRect(). The header is painted first, so without this clip a partially
    // scrolled first row would draw its selection band and its text into the
    // header band. The pinned region uses the same body, which is what keeps
    // the frozen column aligned with the rows that scroll under it.
    const QRectF body = bodyRect();
    if (body.height() > 0.0)
        painter.setClipRect(body, Qt::IntersectClip);

    if (rowCount_ == 0) {
        if (body.height() > 0.0) {
            painter.setFont(fontFor(theme.type().bodyMedium, density, this));
            painter.setPen(colours.onSurfaceVariant);
            painter.drawText(body, static_cast<int>(Qt::AlignCenter),
                             localized(QStringLiteral("table.empty"),
                                       QStringLiteral("No data")));
        }
    } else {
        const int first = std::max(0, firstVisibleRow() - prefetchRows_);
        const int last = std::min(
            rowCount_ - 1,
            firstVisibleRow() + visibleRowCount() + prefetchRows_);
        const bool hasSelection = selectionMode_ != SelectionMode::NoSelection
            && currentRow_ >= 0 && currentColumn_ >= 0;

        const qreal frozen = std::min(frozenWidthPx(), body.width());
        const QRectF scrolling(body.left() + frozen, body.top(),
                               std::max(qreal(0.0), body.width() - frozen),
                               body.height());
        const QRectF pinned(body.left(), body.top(), frozen, body.height());

        for (int row = first; row <= last; ++row) {
            const QRectF geometry = rowRect(row);
            if (geometry.bottom() < body.top())
                continue;
            if (geometry.top() > body.bottom())
                break;

            if (scrolling.width() > 0.0) {
                painter.save();
                painter.setClipRect(scrolling, Qt::IntersectClip);
                qreal x = content.left() + frozen - horizontalScrollOffset_;
                for (int column = frozenColumns_; column < columnCount_; ++column) {
                    const qreal width = columnWidthPx(column);
                    if (x + width <= scrolling.left()) {
                        x += width;
                        continue;
                    }
                    if (x >= scrolling.right())
                        break;

                    const bool selected = hasSelection && row == currentRow_
                        && (selectionMode_ == SelectionMode::SingleRow
                            || column == currentColumn_);
                    const bool hovered = hoveredRow_ == row && hoveredColumn_ == column
                        && !selected;
                    paintCell(painter, row, column,
                              QRectF(x, geometry.top(), width, geometry.height()), selected,
                              hovered);
                    x += width;
                }
                painter.restore();
            }

            if (pinned.width() > 0.0) {
                painter.save();
                painter.setClipRect(pinned, Qt::IntersectClip);
                qreal x = content.left();
                for (int column = 0; column < frozenColumns_; ++column) {
                    const qreal width = columnWidthPx(column);
                    if (x >= pinned.right())
                        break;

                    const bool selected = hasSelection && row == currentRow_
                        && (selectionMode_ == SelectionMode::SingleRow
                            || column == currentColumn_);
                    const bool hovered = hoveredRow_ == row && hoveredColumn_ == column
                        && !selected;
                    paintCell(painter, row, column,
                              QRectF(x, geometry.top(), width, geometry.height()), selected,
                              hovered);
                    x += width;
                }
                painter.restore();
            }

            painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
            painter.drawLine(QPointF(content.left(), geometry.bottom() - density.dp(0.5)),
                             QPointF(content.right(), geometry.bottom() - density.dp(0.5)));
        }

        // The frozen band and the scrolling band are separate paint passes with
        // separate clips, so a scrolling cell disappears under the pinned column
        // with no visual break and the two texts read as one run. A single 1 dp
        // outline-variant edge on the frozen boundary is the seam.
        if (frozen > 0.0 && frozen < body.width()) {
            const qreal edge = body.left() + frozen - density.dp(0.5);
            painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
            painter.drawLine(QPointF(edge, body.top()), QPointF(edge, body.bottom()));
        }
    }

    // The scrollbars live on the frame, not in the row band.
    painter.setClipRect(content);
    paintScrollbars(painter, content);
    painter.restore();
}

void VirtualTable::paintHeader(QPainter& painter, const QRectF& content)
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();
    const qreal height = headerHeightPx();
    if (height <= 0.0 || source_ == nullptr)
        return;

    const QRectF header(content.left(), content.top(), content.width(), height);
    painter.save();
    painter.fillRect(header, colours.surfaceContainer);
    painter.setClipRect(header);

    const QFont font = fontFor(theme.type().titleSmall, density, this);
    const QFontMetricsF metrics(font);
    const qreal padding = density.dp(kCellPaddingDp);
    const qreal frozen = std::min(frozenWidthPx(), content.width());

    const QRectF scrolling(content.left() + frozen, header.top(),
                           std::max(qreal(0.0), content.width() - frozen), height);
    if (scrolling.width() > 0.0) {
        painter.save();
        painter.setClipRect(scrolling);
        qreal x = content.left() + frozen - horizontalScrollOffset_;
        for (int column = frozenColumns_; column < columnCount_; ++column) {
            const qreal width = columnWidthPx(column);
            if (x + width <= scrolling.left()) {
                x += width;
                continue;
            }
            if (x >= scrolling.right())
                break;

            paintHeaderCell(painter, column, QRectF(x, header.top(), width, height), font,
                            metrics, padding, colours);
            x += width;
        }
        painter.restore();
    }

    const QRectF pinned(content.left(), header.top(), frozen, height);
    if (pinned.width() > 0.0) {
        painter.save();
        painter.setClipRect(pinned);
        qreal x = content.left();
        for (int column = 0; column < frozenColumns_; ++column) {
            const qreal width = columnWidthPx(column);
            if (x >= pinned.right())
                break;
            paintHeaderCell(painter, column, QRectF(x, header.top(), width, height), font,
                            metrics, padding, colours);
            x += width;
        }
        painter.restore();
    }

    // The same seam on the header band, so the pinned column's title does not
    // read as the first word of the scrolling column's title.
    if (frozen > 0.0 && frozen < content.width()) {
        const qreal edge = header.left() + frozen - density.dp(0.5);
        painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
        painter.drawLine(QPointF(edge, header.top()), QPointF(edge, header.bottom()));
    }

    painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
    painter.drawLine(QPointF(header.left(), header.bottom() - density.dp(0.5)),
                     QPointF(header.right(), header.bottom() - density.dp(0.5)));
    painter.restore();
}

void VirtualTable::paintHeaderCell(QPainter& painter, int column, const QRectF& rect,
                                   const QFont& font, const QFontMetricsF& metrics,
                                   qreal padding, const ColorScheme& colours)
{
    if (source_ == nullptr)
        return;

    const QString title = source_->header(column);
    if (title.isEmpty())
        return;

    const qreal available = std::max(rect.width() - padding * 2.0, qreal(0.0));
    if (available <= 0.0)
        return;

    const QString elided = metrics.elidedText(title, Qt::ElideRight, available);
    painter.save();
    painter.setFont(font);
    painter.setPen(colours.onSurfaceVariant);
    painter.drawText(rect.adjusted(padding, 0.0, -padding, 0.0),
                     static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), elided);
    painter.restore();
}

void VirtualTable::paintCell(QPainter& painter, int row, int column, const QRectF& rect,
                             bool selected, bool hovered)
{
    if (rect.isEmpty() || source_ == nullptr)
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

    const QString text = source_->cell(row, column).toString();
    if (!text.isEmpty()) {
        const QFont font = fontFor(theme.type().bodyMedium, density, this);
        const QFontMetricsF metrics(font);
        const qreal padding = density.dp(kCellPaddingDp);
        const qreal available = std::max(rect.width() - padding * 2.0, qreal(0.0));
        if (available > 0.0) {
            const QString elided = metrics.elidedText(text, Qt::ElideRight, available);
            painter.setFont(font);
            painter.setPen(selected ? colours.onSecondaryContainer : colours.onSurface);
            painter.drawText(rect.adjusted(padding, 0.0, -padding, 0.0),
                             static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), elided);
        }
    }

    painter.restore();
}

void VirtualTable::paintScrollbars(QPainter& painter, const QRectF& content)
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

void VirtualTable::mousePressEvent(QMouseEvent* event)
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

void VirtualTable::mouseMoveEvent(QMouseEvent* event)
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

void VirtualTable::mouseReleaseEvent(QMouseEvent* event)
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

void VirtualTable::mouseDoubleClickEvent(QMouseEvent* event)
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

void VirtualTable::wheelEvent(QWheelEvent* event)
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

void VirtualTable::keyPressEvent(QKeyEvent* event)
{
    if (rowCount_ == 0 || columnCount_ == 0 || event->key() == Qt::Key_Tab
        || event->key() == Qt::Key_Backtab) {
        View::keyPressEvent(event);
        return;
    }

    const int activeRow = currentRow_ >= 0
        ? currentRow_
        : (event->key() == Qt::Key_Up || event->key() == Qt::Key_PageUp ? rowCount_ - 1 : 0);
    const int activeColumn = currentColumn_ >= 0 ? currentColumn_ : 0;
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);

    int row = activeRow;
    int column = activeColumn;
    bool handled = true;

    switch (event->key()) {
    case Qt::Key_Down:
        row = std::min(rowCount_ - 1, activeRow + 1);
        break;
    case Qt::Key_Up:
        row = std::max(0, activeRow - 1);
        break;
    case Qt::Key_Left:
        column = std::max(0, activeColumn - 1);
        break;
    case Qt::Key_Right:
        column = std::min(columnCount_ - 1, activeColumn + 1);
        break;
    case Qt::Key_Home:
        column = 0;
        if (control)
            row = 0;
        break;
    case Qt::Key_End:
        column = columnCount_ - 1;
        if (control)
            row = rowCount_ - 1;
        break;
    case Qt::Key_PageDown: {
        const int stride = std::max(1, visibleRowCount() - static_cast<int>(kPageOverlap));
        row = std::min(rowCount_ - 1, activeRow + stride);
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

void VirtualTable::resizeEvent(QResizeEvent* event)
{
    View::resizeEvent(event);
    clampScroll();
    requestUpdate();
}

void VirtualTable::leaveEvent(QEvent* event)
{
    View::leaveEvent(event);

    if (hoveredRow_ < 0 && hoveredColumn_ < 0)
        return;

    hoveredRow_ = -1;
    hoveredColumn_ = -1;
    requestUpdate();
}

} // namespace md
