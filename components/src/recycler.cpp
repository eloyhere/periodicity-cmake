// Periodicity — Recycler, a widget-recycling virtual list.
//
// The realisation window is computed from the scroll offset and the viewport
// height. Items outside it go back to the pool; rows inside it are bound to a
// pooled item of the same view type or, when the pool is empty, to a newly
// created one. A wheel notch therefore moves the window by a few rows and
// reuses the widgets of the rows that left it.

#include <md/components/recycler.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QAbstractItemModel>
#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QRectF>
#include <QResizeEvent>
#include <QSizeF>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QVariant>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace md {

// The realised row. It is a plain QWidget: no md::View pipeline, because a
// recycled row is a data-bound cell, not a component. Its painting still comes
// from the Recycler, which owns the theme and the delegate.
struct Recycler::Item : public QWidget
{
    Item(Recycler* owner, int viewType)
        : QWidget(owner)
        , owner_(owner)
        , type(viewType)
    {
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setMouseTracking(true);
        setFocusPolicy(Qt::NoFocus);
    }

    [[nodiscard]] bool hovered() const noexcept { return hovered_; }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        owner_->paintItem(painter, *this);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            pressed_ = true;
            event->accept();
            return;
        }

        event->ignore();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        const bool activate = pressed_ && event->button() == Qt::LeftButton
            && rect().contains(event->position().toPoint());
        pressed_ = false;

        if (activate)
            owner_->handleItemClick(this);

        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            event->ignore();
            return;
        }

        owner_->handleItemDoubleClick(this);
        event->accept();
    }

    void enterEvent(QEnterEvent* event) override
    {
        hovered_ = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        hovered_ = false;
        update();
        QWidget::leaveEvent(event);
    }

    QPersistentModelIndex index;
    int row = -1;
    QWidget* content = nullptr;
    const int type;

private:
    Recycler* owner_ = nullptr;
    bool hovered_ = false;
    bool pressed_ = false;
};

namespace {

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

Recycler::Recycler(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));
}

void Recycler::setModel(QAbstractItemModel* model)
{
    QAbstractItemModel* previous = model_.data();
    if (previous == model)
        return;

    if (previous != nullptr)
        QObject::disconnect(previous, nullptr, this, nullptr);

    model_ = model;
    current_ = QPersistentModelIndex();
    hasSelection_ = false;
    removedCurrentRow_ = -1;
    scrollOffset_ = 0.0;

    connectModel();
    rebuild();
}

QAbstractItemModel* Recycler::model() const
{
    return model_.data();
}

void Recycler::setItemDelegate(QStyledItemDelegate* delegate)
{
    if (delegate_ == delegate)
        return;

    delegate_ = delegate;
    updateItems();
    requestUpdate();
}

QStyledItemDelegate* Recycler::itemDelegate() const
{
    return delegate_;
}

void Recycler::setItemFactory(ItemFactory factory)
{
    factory_ = std::move(factory);

    // Pooled hosts may carry a widget built by the previous factory, so they
    // are discarded rather than rebound to a shape they were not built for.
    for (auto it = pool_.cbegin(); it != pool_.cend(); ++it) {
        for (Item* item : it.value()) {
            item->hide();
            delete item;
        }
    }
    pool_.clear();
}

void Recycler::setItemBinder(ItemBinder binder)
{
    binder_ = std::move(binder);
    updateItems();
    requestUpdate();
}

void Recycler::setRowHeight(qreal dp)
{
    const qreal clamped = dp > 0.0 ? dp : 1.0;
    if (qFuzzyCompare(rowHeightDp_, clamped))
        return;

    rowHeightDp_ = clamped;
    clampScroll();
    relayout();
    requestUpdate();
}

qreal Recycler::rowHeight() const noexcept
{
    return rowHeightDp_;
}

void Recycler::setPrefetchRows(int rows)
{
    const int clamped = std::max(0, rows);
    if (prefetchRows_ == clamped)
        return;

    prefetchRows_ = clamped;
    relayout();
}

int Recycler::prefetchRows() const noexcept
{
    return prefetchRows_;
}

void Recycler::setViewTypeRole(int role)
{
    if (viewTypeRole_ == role)
        return;

    viewTypeRole_ = role;
    rebuild();
}

int Recycler::viewTypeRole() const noexcept
{
    return viewTypeRole_;
}

QModelIndex Recycler::currentIndex() const
{
    return QModelIndex(current_);
}

int Recycler::currentRow() const
{
    return current_.isValid() ? current_.row() : -1;
}

void Recycler::setCurrentIndex(const QModelIndex& index)
{
    const QPersistentModelIndex next(index);
    if (current_ == next)
        return;

    current_ = next;
    hasSelection_ = next.isValid();
    if (next.isValid())
        ensureVisible(next.row());

    relayout();
    updateItems();
    emit currentIndexChanged(currentIndex());
    requestUpdate();
}

int Recycler::rowCount() const
{
    return model_ != nullptr ? model_->rowCount() : 0;
}

QModelIndex Recycler::indexAtRow(int row) const
{
    if (model_ == nullptr || row < 0 || row >= rowCount())
        return QModelIndex();

    return model_->index(row, 0);
}

QRectF Recycler::itemRect(int row) const
{
    const qreal height = rowHeightPx();
    const qreal itemWidth =
        std::max(qreal(1.0), static_cast<qreal>(width()) - scrollbarWidthPx());
    return QRectF(0.0, static_cast<qreal>(row) * height - scrollOffset_, itemWidth, height);
}

qreal Recycler::scrollOffset() const noexcept
{
    return scrollOffset_;
}

qreal Recycler::maximumScrollOffset() const
{
    const qreal total = static_cast<qreal>(rowCount()) * rowHeightPx();
    return std::max(qreal(0.0), total - static_cast<qreal>(height()));
}

void Recycler::setScrollOffset(qreal offset)
{
    const qreal clamped = std::clamp(offset, qreal(0.0), maximumScrollOffset());
    if (qFuzzyCompare(scrollOffset_ + 1.0, clamped + 1.0))
        return;

    scrollOffset_ = clamped;
    relayout();
    requestUpdate();
}

void Recycler::scrollToRow(int row)
{
    if (row < 0 || row >= rowCount())
        return;

    ensureVisible(row);
    relayout();
    requestUpdate();
}

void Recycler::scrollTo(const QModelIndex& index)
{
    if (!index.isValid())
        return;

    scrollToRow(index.row());
}

int Recycler::instantiatedItemCount() const
{
    return static_cast<int>(active_.size());
}

int Recycler::pooledItemCount() const
{
    int total = 0;
    for (auto it = pool_.cbegin(); it != pool_.cend(); ++it)
        total += static_cast<int>(it.value().size());

    return total;
}

int Recycler::createdItemCount() const noexcept
{
    return created_;
}

int Recycler::reusedItemCount() const noexcept
{
    return reused_;
}

qreal Recycler::rowHeightPx() const
{
    return Theme::forWidget(this).density().dp(rowHeightDp_);
}

qreal Recycler::scrollbarWidthPx() const
{
    return Theme::forWidget(this).density().dp(8.0);
}

int Recycler::viewTypeFor(const QModelIndex& index) const
{
    const QVariant value = index.data(viewTypeRole_);
    if (value.isValid()) {
        bool ok = false;
        const int type = value.toInt(&ok);
        if (ok)
            return type;
    }

    return 0;
}

int Recycler::rowAtPosition(const QPointF& position) const
{
    const qreal rh = rowHeightPx();
    if (rh <= 0.0 || position.y() < 0.0 || position.y() >= static_cast<qreal>(height()))
        return -1;

    const int row = static_cast<int>(std::floor((position.y() + scrollOffset_) / rh));
    if (row < 0 || row >= rowCount())
        return -1;

    return row;
}

void Recycler::clampScroll()
{
    scrollOffset_ = std::clamp(scrollOffset_, qreal(0.0), maximumScrollOffset());
}

void Recycler::ensureVisible(int row)
{
    const qreal rh = rowHeightPx();
    if (rh <= 0.0)
        return;

    const qreal viewport = static_cast<qreal>(height());
    const qreal top = static_cast<qreal>(row) * rh;
    const qreal bottom = top + rh;

    if (top < scrollOffset_)
        scrollOffset_ = top;
    else if (bottom > scrollOffset_ + viewport)
        scrollOffset_ = bottom - viewport;

    clampScroll();
}

void Recycler::connectModel()
{
    if (model_ == nullptr)
        return;

    connect(model_, &QAbstractItemModel::dataChanged, this, [this] {
        updateItems();
        requestUpdate();
    });

    connect(model_, &QAbstractItemModel::rowsInserted, this, [this] { rebuild(); });

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
                emit currentIndexChanged(QModelIndex());
            }

            rebuild();
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
            scrollOffset_ = 0.0;

            if (hadCurrent)
                emit currentIndexChanged(QModelIndex());

            rebuild();
        } catch (...) {
            // A model that throws while the view repairs itself must not break
            // the widget.
        }
    });

    connect(model_, &QAbstractItemModel::layoutChanged, this, [this] { rebuild(); });
}

void Recycler::rebuild()
{
    clampScroll();
    relayout();
    requestUpdate();
}

void Recycler::updateItems()
{
    for (Item* item : active_)
        item->update();
}

void Recycler::relayout()
{
    const qreal rh = rowHeightPx();
    const int count = rowCount();

    if (count == 0 || rh <= 0.0 || height() <= 0) {
        for (Item* item : active_)
            releaseItem(item);
        active_.clear();
        requestUpdate();
        return;
    }

    const int first =
        std::max(0, static_cast<int>(std::floor(scrollOffset_ / rh)) - prefetchRows_);
    const int last = std::min(
        count - 1,
        static_cast<int>(std::ceil((scrollOffset_ + static_cast<qreal>(height())) / rh))
            + prefetchRows_);

    // Release the rows that left the realisation window.
    for (int i = static_cast<int>(active_.size()) - 1; i >= 0; --i) {
        Item* item = active_.at(i);
        if (item->row < first || item->row > last) {
            releaseItem(item);
            active_.removeAt(i);
        }
    }

    QHash<int, Item*> realised;
    realised.reserve(static_cast<int>(active_.size()));
    for (Item* item : active_)
        realised.insert(item->row, item);

    for (int row = first; row <= last; ++row) {
        Item* item = realised.value(row, nullptr);
        if (item == nullptr) {
            const QModelIndex index = model_->index(row, 0);
            item = acquireItem(viewTypeFor(index));
            item->row = row;
            active_.append(item);
            bindItem(item, index);
        }

        applyItemGeometry(item);
        item->show();
    }

    requestUpdate();
}

Recycler::Item* Recycler::acquireItem(int viewType)
{
    QVector<Item*>& pool = pool_[viewType];
    if (!pool.isEmpty()) {
        Item* item = pool.takeLast();
        ++reused_;
        item->show();
        return item;
    }

    ++created_;

    auto* item = new Item(this, viewType);
    if (factory_ != nullptr) {
        QWidget* content = factory_(viewType, item);
        if (content != nullptr) {
            item->content = content;
            content->setGeometry(item->rect());
            content->show();
        }
    }

    item->show();
    return item;
}

void Recycler::releaseItem(Item* item)
{
    if (item == nullptr)
        return;

    item->hide();
    item->row = -1;
    item->index = QPersistentModelIndex();
    pool_[item->type].append(item);
}

void Recycler::applyItemGeometry(Item* item)
{
    const qreal rh = rowHeightPx();
    const int bar = static_cast<int>(std::ceil(scrollbarWidthPx()));
    const int itemWidth = std::max(1, width() - bar);
    const int itemHeight = std::max(1, static_cast<int>(std::ceil(rh)));
    const int top = qRound(static_cast<qreal>(item->row) * rh - scrollOffset_);

    item->setGeometry(0, top, itemWidth, itemHeight);
    if (item->content != nullptr)
        item->content->setGeometry(item->rect());
}

void Recycler::bindItem(Item* item, const QModelIndex& index)
{
    item->index = QPersistentModelIndex(index);
    item->row = index.row();

    if (binder_ != nullptr)
        binder_(item->content != nullptr ? item->content : item, index);
    else if (item->content == nullptr)
        item->setToolTip(index.data(Qt::ToolTipRole).toString());

    item->update();
}

void Recycler::handleItemClick(Item* item)
{
    const QModelIndex index = QModelIndex(item->index);
    if (!index.isValid())
        return;

    setCurrentIndex(index);
    emit itemClicked(index);
}

void Recycler::handleItemDoubleClick(Item* item)
{
    const QModelIndex index = QModelIndex(item->index);
    if (!index.isValid())
        return;

    setCurrentIndex(index);
    emit itemDoubleClicked(index);
}

void Recycler::paintItem(QPainter& painter, Item& item)
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();
    const QRectF bounds(0.0, 0.0, static_cast<qreal>(item.width()),
                        static_cast<qreal>(item.height()));

    const bool selected = current_.isValid() && item.index.isValid()
        && QModelIndex(current_) == QModelIndex(item.index);

    painter.fillRect(bounds, colours.surface);

    if (selected)
        painter.fillRect(bounds, colours.secondaryContainer);
    else if (item.hovered())
        painter.fillRect(bounds, colours.stateLayer(colours.onSurface, ViewState::Hovered));

    // A factory-provided widget draws its own content; the Recycler only owns
    // the surface and the selection layer beneath it.
    if (item.content != nullptr)
        return;

    const qreal padding = density.dp(16.0);
    const QRectF content = bounds.adjusted(padding, 0.0, -padding, 0.0);

    if (delegate_ != nullptr) {
        QStyleOptionViewItem option;
        option.rect = content.toRect();
        option.widget = this;
        option.font = fontFor(theme.type().bodyLarge, density, this);
        option.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
        option.decorationAlignment = Qt::AlignLeft | Qt::AlignVCenter;
        option.textElideMode = Qt::ElideRight;
        option.decorationPosition = QStyleOptionViewItem::Left;
        option.decorationSize = QSize(qRound(density.dp(24.0)), qRound(density.dp(24.0)));
        option.features = QStyleOptionViewItem::HasDisplay;
        option.index = QModelIndex(item.index);

        QPalette palette = theme.toPalette();
        palette.setColor(QPalette::Highlight, colours.secondaryContainer);
        palette.setColor(QPalette::HighlightedText, colours.onSecondaryContainer);
        palette.setColor(QPalette::Text, colours.onSurface);
        palette.setColor(QPalette::WindowText, colours.onSurface);
        option.palette = palette;

        option.state = QStyle::State_Enabled;
        if (selected)
            option.state |= QStyle::State_Selected;
        if (item.hovered())
            option.state |= QStyle::State_MouseOver;

        delegate_->paint(&painter, option, QModelIndex(item.index));
        return;
    }

    const QModelIndex index = QModelIndex(item.index);
    const QString text = index.data(Qt::DisplayRole).toString();
    if (text.isEmpty())
        return;

    const QFont font = fontFor(theme.type().bodyLarge, density, this);
    const QFontMetricsF metrics(font);
    const QString elided = metrics.elidedText(text, Qt::ElideRight, content.width());

    painter.save();
    painter.setFont(font);
    painter.setPen(selected ? colours.onSecondaryContainer : colours.onSurface);
    painter.drawText(content, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), elided);
    painter.restore();
}

void Recycler::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    const qreal total = static_cast<qreal>(rowCount()) * rowHeightPx();
    const qreal naturalHeight =
        std::max(density.dp(48.0), std::min(total, density.dp(320.0)));

    setMeasuredSize(QSizeF(width.resolve(density.dp(240.0)),
                           height.resolve(naturalHeight)));
}

void Recycler::paintContent(QPainter& painter, const QRectF& content)
{
    const qreal maximum = maximumScrollOffset();
    if (maximum <= 0.0 || content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal barWidth = scrollbarWidthPx();
    const qreal trackTop = content.top() + density.dp(2.0);
    const qreal trackHeight = std::max(content.height() - density.dp(4.0), qreal(1.0));
    const qreal contentHeight = static_cast<qreal>(rowCount()) * rowHeightPx();
    if (contentHeight <= 0.0)
        return;

    const qreal thumbHeight =
        std::min(trackHeight, std::max(density.dp(24.0), trackHeight * content.height() / contentHeight));
    const qreal travel = trackHeight - thumbHeight;
    const qreal progress = std::clamp(scrollOffset_ / maximum, qreal(0.0), qreal(1.0));
    const qreal thumbTop = trackTop + travel * progress;

    const QRectF thumb(content.right() - barWidth + density.dp(1.0), thumbTop,
                       barWidth - density.dp(2.0), thumbHeight);

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(withAlpha(theme.colours().onSurface, 0.38));
    painter.drawRoundedRect(thumb, thumb.width() / 2.0, thumb.height() / 2.0);
    painter.restore();
}

void Recycler::wheelEvent(QWheelEvent* event)
{
    const qreal steps = static_cast<qreal>(event->angleDelta().y()) / 120.0;
    if (qFuzzyIsNull(steps)) {
        event->ignore();
        return;
    }

    setScrollOffset(scrollOffset_ - steps * rowHeightPx() * 2.0);
    event->accept();
}

void Recycler::keyPressEvent(QKeyEvent* event)
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
    case Qt::Key_PageDown:
        target = std::min(last, (row < 0 ? 0 : row) + 5);
        break;
    case Qt::Key_PageUp:
        target = std::max(0, (row < 0 ? last : row) - 5);
        break;
    default:
        handled = false;
        break;
    }

    if (!handled) {
        View::keyPressEvent(event);
        return;
    }

    setCurrentIndex(indexAtRow(target));
    event->accept();
}

void Recycler::arrangeOverride(const QRectF& content)
{
    Q_UNUSED(content);
    clampScroll();
    relayout();
}

void Recycler::resizeEvent(QResizeEvent* event)
{
    View::resizeEvent(event);
    clampScroll();
    relayout();
    requestUpdate();
}

} // namespace md
