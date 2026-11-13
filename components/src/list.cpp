// Periodicity — List, a flat list view backed by any QAbstractItemModel.
//
// Rows are painted, never instantiated: the paint pass visits only the rows
// that intersect the content rectangle, which is what keeps a long model
// cheap. Selection is a single persistent index, so a model reset clears it
// instead of leaving a dangling row number behind.

#include <md/components/list.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QAbstractItemModel>
#include <QFont>
#include <QFontMetricsF>
#include <QIcon>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QRectF>
#include <QResizeEvent>
#include <QSizeF>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QVariant>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace md {

namespace {

QFont fontFor(const TextStyle& style, const Density& density, const QWidget* context)
{
    return Font::resolve(context, style, density);
}

QIcon iconFromVariant(const QVariant& value)
{
    if (!value.isValid())
        return QIcon();

    if (value.canConvert<QIcon>())
        return value.value<QIcon>();

    if (value.canConvert<QPixmap>())
        return QIcon(value.value<QPixmap>());

    return QIcon();
}

} // namespace

List::List(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setBackgroundColour(QColor(Qt::transparent));
}

void List::setModel(QAbstractItemModel* model)
{
    QAbstractItemModel* previous = model_.data();
    if (previous == model)
        return;

    if (previous != nullptr)
        QObject::disconnect(previous, nullptr, this, nullptr);

    model_ = model;
    current_ = QPersistentModelIndex();
    hasSelection_ = false;
    hoveredRow_ = -1;
    pressedRow_ = -1;
    removedCurrentRow_ = -1;
    scrollOffset_ = 0.0;

    connectModel();
    requestUpdate();
}

QAbstractItemModel* List::model() const
{
    return model_.data();
}

void List::setItemDelegate(QStyledItemDelegate* delegate)
{
    if (delegate_ == delegate)
        return;

    delegate_ = delegate;
    requestUpdate();
}

QStyledItemDelegate* List::itemDelegate() const
{
    return delegate_.data();
}

void List::setRowHeight(qreal dp)
{
    const qreal clamped = dp > 0.0 ? dp : 1.0;
    if (qFuzzyCompare(rowHeightDp_, clamped))
        return;

    rowHeightDp_ = clamped;
    clampScroll();
    requestUpdate();
}

qreal List::rowHeight() const noexcept
{
    return rowHeightDp_;
}

qreal List::rowHeightFor(RowLines lines)
{
    switch (lines) {
    case RowLines::TwoLine:
        return 72.0;
    case RowLines::ThreeLine:
        return 88.0;
    case RowLines::OneLine:
        break;
    }

    return 56.0;
}

void List::setRowLines(RowLines lines)
{
    setRowHeight(rowHeightFor(lines));
}

qreal List::rowHeightPx() const
{
    return Theme::forWidget(this).density().dp(rowHeightDp_);
}

int List::rowCount() const
{
    return model_ != nullptr ? model_->rowCount() : 0;
}

QModelIndex List::indexAt(int row) const
{
    if (model_ == nullptr || row < 0 || row >= rowCount())
        return QModelIndex();

    return model_->index(row, 0);
}

QModelIndex List::currentIndex() const
{
    return QModelIndex(current_);
}

int List::currentRow() const
{
    return current_.isValid() ? current_.row() : -1;
}

void List::setCurrentIndex(const QModelIndex& index)
{
    const QPersistentModelIndex next(index);
    if (current_ == next)
        return;

    current_ = next;
    hasSelection_ = next.isValid();
    if (next.isValid())
        ensureVisible(next.row());

    // Emit last: a currentIndexChanged handler is free to delete the List.
    requestUpdate();
    emit currentIndexChanged(currentIndex());
}

void List::clearSelection()
{
    hasSelection_ = false;

    if (!current_.isValid())
        return;

    current_ = QPersistentModelIndex();
    requestUpdate();
    emit currentIndexChanged(QModelIndex());
}

qreal List::scrollOffset() const noexcept
{
    return scrollOffset_;
}

void List::setScrollOffset(qreal offset)
{
    const qreal clamped = std::clamp(offset, qreal(0.0), maximumScroll());
    if (qFuzzyCompare(scrollOffset_ + 1.0, clamped + 1.0))
        return;

    scrollOffset_ = clamped;
    requestUpdate();
}

void List::scrollToRow(int row)
{
    if (row < 0 || row >= rowCount())
        return;

    ensureVisible(row);
    requestUpdate();
}

int List::firstVisibleRow() const
{
    const int count = rowCount();
    const qreal rh = rowHeightPx();
    if (rh <= 0.0 || count == 0 || contentRect().height() <= 0.0)
        return 0;

    // A collapsed viewport lets ensureVisible() scroll to `count * rowHeight`,
    // whose row number is `count`; an index always names an existing row.
    return std::clamp(static_cast<int>(std::floor(scrollOffset_ / rh)), 0, count - 1);
}

int List::visibleRowCount() const
{
    const qreal rh = rowHeightPx();
    const qreal height = contentRect().height();
    if (rh <= 0.0 || height <= 0.0 || rowCount() == 0)
        return 0;

    const int visible = static_cast<int>(std::ceil(height / rh)) + 1;
    return std::min(visible, rowCount());
}

QRectF List::rowRect(int row) const
{
    const QRectF content = contentRect();
    return QRectF(content.left(), content.top() + static_cast<qreal>(row) * rowHeightPx()
                                       - scrollOffset_,
                  content.width(), rowHeightPx());
}

qreal List::maximumScroll() const
{
    const qreal total = static_cast<qreal>(rowCount()) * rowHeightPx();
    return std::max(qreal(0.0), total - contentRect().height());
}

void List::clampScroll()
{
    scrollOffset_ = std::clamp(scrollOffset_, qreal(0.0), maximumScroll());
}

void List::ensureVisible(int row)
{
    const qreal rh = rowHeightPx();
    if (rh <= 0.0)
        return;

    const qreal top = static_cast<qreal>(row) * rh;
    const qreal bottom = top + rh;
    const qreal viewport = contentRect().height();

    if (top < scrollOffset_)
        scrollOffset_ = top;
    else if (bottom > scrollOffset_ + viewport)
        scrollOffset_ = bottom - viewport;

    clampScroll();
}

int List::rowAtPosition(const QPointF& position) const
{
    const QRectF content = contentRect();
    const qreal rh = rowHeightPx();
    if (rh <= 0.0 || !content.contains(position))
        return -1;

    const int row = static_cast<int>(
        std::floor((position.y() - content.top() + scrollOffset_) / rh));
    if (row < 0 || row >= rowCount())
        return -1;

    return row;
}

void List::connectModel()
{
    if (model_ == nullptr)
        return;

    connect(model_, &QAbstractItemModel::dataChanged, this, [this] { requestUpdate(); });

    connect(model_, &QAbstractItemModel::rowsInserted, this, [this] {
        clampScroll();
        requestUpdate();
    });

    // QPersistentModelIndex invalidates itself inside beginRemoveRows(), before
    // rowsRemoved() runs, so the selected row is cached while it still exists.
    connect(model_, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex& parent, int first, int last) {
                removedCurrentRow_ = !parent.isValid() && current_.isValid()
                        && current_.row() >= first && current_.row() <= last
                    ? current_.row()
                    : -1;
            });

    connect(model_, &QAbstractItemModel::rowsRemoved, this, [this] {
        try {
            const bool selectionRemoved = removedCurrentRow_ >= 0;
            removedCurrentRow_ = -1;

            if (selectionRemoved) {
                current_ = QPersistentModelIndex();
                hasSelection_ = false;
            }

            clampScroll();
            requestUpdate();

            // Emit last: a currentIndexChanged handler may delete the List.
            if (selectionRemoved)
                emit currentIndexChanged(QModelIndex());
        } catch (...) {
            // A model that throws while the view repairs itself must not break
            // the widget.
        }
    });

    connect(model_, &QAbstractItemModel::modelReset, this, [this] {
        try {
            const bool hadCurrent = hasSelection_ || current_.isValid();
            current_ = QPersistentModelIndex();
            hasSelection_ = false;
            removedCurrentRow_ = -1;
            hoveredRow_ = -1;
            pressedRow_ = -1;
            scrollOffset_ = 0.0;

            requestUpdate();
            // Emit last: a currentIndexChanged handler may delete the List.
            if (hadCurrent)
                emit currentIndexChanged(QModelIndex());
        } catch (...) {
            // A model that throws while the view repairs itself must not break
            // the widget.
        }
    });

    connect(model_, &QAbstractItemModel::layoutChanged, this, [this] {
        clampScroll();
        requestUpdate();
    });
}

void List::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    const qreal total = static_cast<qreal>(rowCount()) * rowHeightPx();
    const qreal naturalHeight =
        std::max(density.dp(48.0), std::min(total, density.dp(320.0)));

    setMeasuredSize(QSizeF(width.resolve(density.dp(240.0)),
                           height.resolve(naturalHeight)));
}

void List::arrangeOverride(const QRectF& content)
{
    Q_UNUSED(content);
    clampScroll();
    requestUpdate();
}

void List::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    painter.save();
    painter.fillRect(content, theme.colours().surface);
    painter.setClipRect(content);

    const int count = rowCount();
    const int first = firstVisibleRow();
    const int last = std::min(count - 1, first + visibleRowCount());
    const int row = currentRow();

    for (int index = first; index <= last; ++index) {
        const QRectF rect = rowRect(index);
        if (rect.bottom() < content.top())
            continue;
        if (rect.top() > content.bottom())
            break;

        paintRow(painter, index, rect, index == row, index == hoveredRow_);
    }

    painter.restore();
}

void List::paintRow(QPainter& painter, int row, const QRectF& rect, bool selected,
                    bool hovered)
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();
    const QModelIndex index = model_->index(row, 0);

    // The delegate is shared and not owned. If its owner destroyed it while
    // it was installed, the QPointer is null and the row falls back to the
    // default painting instead of calling a dead object.
    QStyledItemDelegate* delegate = delegate_.data();

    painter.save();
    painter.setClipRect(rect);

    if (selected && delegate == nullptr)
        painter.fillRect(rect, colours.secondaryContainer);
    else if (hovered)
        painter.fillRect(rect, colours.stateLayer(colours.onSurface, ViewState::Hovered));

    const qreal padding = density.dp(16.0);
    const QRectF itemRect = rect.adjusted(padding, 0.0, -padding, 0.0);
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;

    if (delegate != nullptr) {
        QStyleOptionViewItem option;
        option.rect = itemRect.toRect();
        option.widget = this;
        option.direction = layoutDirection();
        option.font = fontFor(theme.type().bodyLarge, density, this);
        option.displayAlignment = (rightToLeft ? Qt::AlignRight : Qt::AlignLeft)
            | Qt::AlignVCenter;
        option.decorationAlignment = option.displayAlignment;
        option.textElideMode = rightToLeft ? Qt::ElideLeft : Qt::ElideRight;
        option.decorationPosition = rightToLeft ? QStyleOptionViewItem::Right
                                                : QStyleOptionViewItem::Left;
        option.decorationSize = QSize(qRound(density.dp(24.0)), qRound(density.dp(24.0)));
        option.features = QStyleOptionViewItem::HasDisplay;
        option.index = index;

        QPalette palette = theme.toPalette();
        palette.setColor(QPalette::Highlight, colours.secondaryContainer);
        palette.setColor(QPalette::HighlightedText, colours.onSecondaryContainer);
        palette.setColor(QPalette::Text, colours.onSurface);
        palette.setColor(QPalette::WindowText, colours.onSurface);
        option.palette = palette;

        option.state = QStyle::State_Enabled;
        if (selected)
            option.state |= QStyle::State_Selected;
        if (hovered)
            option.state |= QStyle::State_MouseOver;

        delegate->paint(&painter, option, index);
    } else {
        paintDefaultRow(painter, index, itemRect, selected);
    }

    // The divider is theme-derived and sits exactly on the row's bottom edge.
    // Design baseline list dividers are full-width: the 16 dp inset that used to be applied
    // here made the rule look like a text underline instead of a separator.
    painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
    painter.drawLine(QPointF(rect.left(), rect.bottom() - density.dp(0.5)),
                     QPointF(rect.right(), rect.bottom() - density.dp(0.5)));

    painter.restore();
}

void List::paintDefaultRow(QPainter& painter, const QModelIndex& index,
                           const QRectF& rect, bool selected)
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();
    const QFont font = fontFor(theme.type().bodyLarge, density, this);
    const QFontMetricsF metrics(font);
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;

    const qreal side = density.dp(24.0);
    qreal textLeft = rect.left();
    qreal textRight = rect.right();

    const QIcon icon = iconFromVariant(index.data(Qt::DecorationRole));
    if (!icon.isNull()) {
        // The decoration sits on the leading edge, which is the right one in a
        // right-to-left list; the text run shrinks from that same side.
        const QRectF iconRect =
            rightToLeft ? QRectF(rect.right() - side, rect.center().y() - side / 2.0, side, side)
                        : QRectF(rect.left(), rect.center().y() - side / 2.0, side, side);
        icon.paint(&painter, iconRect.toRect(), Qt::AlignCenter,
                   selected ? QIcon::Selected : QIcon::Normal);

        if (rightToLeft)
            textRight -= side + density.dp(16.0);
        else
            textLeft += side + density.dp(16.0);
    }

    const QString text = index.data(Qt::DisplayRole).toString();
    if (text.isEmpty())
        return;

    const qreal available = std::max(textRight - textLeft, qreal(0.0));
    const QString elided =
        metrics.elidedText(text, rightToLeft ? Qt::ElideLeft : Qt::ElideRight, available);

    painter.save();
    painter.setFont(font);
    painter.setPen(selected ? colours.onSecondaryContainer : colours.onSurface);
    painter.drawText(QRectF(textLeft, rect.top(), available, rect.height()),
                     static_cast<int>((rightToLeft ? Qt::AlignRight : Qt::AlignLeft)
                                      | Qt::AlignVCenter),
                     elided);
    painter.restore();
}

void List::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    // setFocus() can run application event filters that delete the List.
    QPointer<List> self(this);
    setFocus(Qt::MouseFocusReason);
    if (self.isNull())
        return;

    pressedRow_ = rowAtPosition(event->position());
    if (pressedRow_ >= 0)
        setCurrentIndex(indexAt(pressedRow_));
    if (self.isNull())
        return;

    requestUpdate();
}

void List::mouseReleaseEvent(QMouseEvent* event)
{
    View::mouseReleaseEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    const int released = rowAtPosition(event->position());
    const int pressed = pressedRow_;
    pressedRow_ = -1;

    // Emit last: a clicked handler is free to delete the List.
    requestUpdate();
    if (pressed >= 0 && pressed == released)
        emit clicked(indexAt(released));
}

void List::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    const int row = rowAtPosition(event->position());
    if (row >= 0)
        emit doubleClicked(indexAt(row));

    event->accept();
}

void List::mouseMoveEvent(QMouseEvent* event)
{
    View::mouseMoveEvent(event);

    const int row = rowAtPosition(event->position());
    if (row == hoveredRow_)
        return;

    hoveredRow_ = row;
    requestUpdate();
}

void List::leaveEvent(QEvent* event)
{
    View::leaveEvent(event);

    if (hoveredRow_ < 0)
        return;

    hoveredRow_ = -1;
    requestUpdate();
}

void List::wheelEvent(QWheelEvent* event)
{
    const qreal steps = static_cast<qreal>(event->angleDelta().y()) / 120.0;
    if (qFuzzyIsNull(steps)) {
        event->ignore();
        return;
    }

    setScrollOffset(scrollOffset_ - steps * rowHeightPx() * 3.0);
    event->accept();
}

void List::keyPressEvent(QKeyEvent* event)
{
    const int count = rowCount();
    if (count == 0 || event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        View::keyPressEvent(event);
        return;
    }

    const int last = count - 1;
    const int row = currentRow();
    int target = row;
    bool handled = true;

    switch (event->key()) {
    case Qt::Key_Down:
        target = row < 0 ? 0 : std::min(row + 1, last);
        break;
    case Qt::Key_Up:
        target = row < 0 ? last : std::max(row - 1, 0);
        break;
    case Qt::Key_Home:
        target = 0;
        break;
    case Qt::Key_End:
        target = last;
        break;
    case Qt::Key_PageDown: {
        const int stride = std::max(1, visibleRowCount() - 1);
        target = std::min(last, (row < 0 ? 0 : row) + stride);
        break;
    }
    case Qt::Key_PageUp: {
        const int stride = std::max(1, visibleRowCount() - 1);
        target = std::max(0, (row < 0 ? last : row) - stride);
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

    setCurrentIndex(indexAt(target));
    event->accept();
}

void List::resizeEvent(QResizeEvent* event)
{
    View::resizeEvent(event);
    clampScroll();
    requestUpdate();
}

} // namespace md
