// Periodicity — MenuGroup, a titled group of menu rows with one selection.
//
// The group owns three things and derives the rest: its rows, the selection
// index and the keyboard cursor. Row rectangles are integer geometry recorded
// by arrangeChildren(), because a child's rectangle is what View::arrange()
// applies to a QWidget — deriving them again from a fractional measure would
// drift by a pixel per row on a long menu.

#include <md/components/menu_group.h>

#include <md/components/menu_item.h>
#include <md/components/sub_menu.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <utility>

namespace md {

namespace {

constexpr qreal kTitleHeightDp = 40.0;
constexpr qreal kTitleInsetDp = 12.0;
constexpr qreal kSpacingDp = 2.0;
constexpr qreal kTitleChevronDp = 16.0;
constexpr qreal kTitleChevronStrokeDp = 2.0;

QFont titleFont(const Theme& theme)
{
    const TextStyle& style = theme.type().labelSmall;
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

} // namespace

MenuSelectionModel::MenuSelectionModel(QObject* parent)
    : QObject(parent)
{
}

void MenuSelectionModel::setSelectedIndex(int index)
{
    if (selectedIndex_ == index)
        return;

    selectedIndex_ = index;
    emit selectedIndexChanged(selectedIndex_);
}

MenuGroup::MenuGroup(QWidget* parent)
    : ViewGroup(parent)
{
    // The group is a container, not a control: keyboard focus lives on its rows
    // so that a highlight and a focus ring are always about one row.
    setFocusPolicy(Qt::NoFocus);
    setBackgroundColour(QColor(Qt::transparent));

    model_ = &internalModel_;
    QObject::connect(model_, &MenuSelectionModel::selectedIndexChanged, this,
                     [this](int index) {
                         applySelection();
                         emit selectionChanged(index);
                     });
}

MenuGroup::~MenuGroup()
{
    // ViewGroup disconnects its own destroyed() connections in its destructor,
    // which runs after this class's members are gone; the row list must not be
    // touched by a notification that arrives later.
    for (MenuItem* item : items_) {
        if (item != nullptr)
            QObject::disconnect(item, nullptr, this, nullptr);
    }
    items_.clear();
}

// ---------------------------------------------------------------------------
// Title and expansion
// ---------------------------------------------------------------------------

void MenuGroup::setTitle(const QString& title)
{
    if (title_ == title)
        return;

    title_ = title;
    relayout();
}

void MenuGroup::setTitleVisible(bool visible)
{
    if (titleVisible_ == visible)
        return;

    titleVisible_ = visible;
    relayout();
}

void MenuGroup::setCollapsible(bool collapsible)
{
    if (collapsible_ == collapsible)
        return;

    collapsible_ = collapsible;
    if (!collapsible_)
        expanded_ = true;
    relayout();
}

void MenuGroup::setExpanded(bool expanded)
{
    if (expanded_ == expanded)
        return;

    expanded_ = expanded;
    updateRowVisibility();
    relayout();
    emit expandedChanged(expanded_);
}

void MenuGroup::toggleExpanded()
{
    if (!collapsible_)
        return;

    setExpanded(!expanded_);
}

qreal MenuGroup::titleHeight() const
{
    if (!titleVisible_ || title_.isEmpty())
        return 0.0;

    return Theme::forWidget(this).density().dp(kTitleHeightDp);
}

qreal MenuGroup::spacingPx() const
{
    return Theme::forWidget(this).density().dp(kSpacingDp);
}

bool MenuGroup::rowsVisible() const
{
    return !collapsible_ || expanded_;
}

QRectF MenuGroup::titleRect() const
{
    const qreal height = titleHeight();
    if (height <= 0.0)
        return {};

    return QRectF(0.0, 0.0, static_cast<qreal>(width()), height);
}

// ---------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------

int MenuGroup::addItem(MenuItem* item)
{
    return insertItem(count(), item);
}

int MenuGroup::insertItem(int index, MenuItem* item)
{
    if (item == nullptr)
        return -1;

    // Moving a row inside the group keeps one entry: detach first, then insert
    // at the requested position (the paint order follows the row order).
    const int existing = indexOf(item);
    if (existing >= 0) {
        items_.removeAt(existing);
        removeView(item);
        if (existing < index)
            --index;
    } else if (auto* oldGroup = qobject_cast<MenuGroup*>(item->parentWidget())) {
        oldGroup->removeItem(item);
    }

    const int position = std::clamp(index, 0, count());
    addView(item);
    items_.insert(position, item);
    attachItem(item);

    updateRowVisibility();
    relayout();
    return position;
}

void MenuGroup::removeItem(MenuItem* item)
{
    const int index = indexOf(item);
    if (index < 0)
        return;

    detachItem(item);
    items_.removeAt(index);
    itemRects_.clear();
    removeView(item);

    relayout();
}

void MenuGroup::clearItems()
{
    for (MenuItem* item : std::as_const(items_))
        detachItem(item);

    items_.clear();
    itemRects_.clear();

    for (View* view : views())
        removeView(view);

    internalModel_.setSelectedIndex(-1);
    currentRow_ = -1;
    relayout();
}

int MenuGroup::count() const
{
    return static_cast<int>(items_.size());
}

MenuItem* MenuGroup::itemAt(int index) const
{
    if (index < 0 || index >= count())
        return nullptr;

    return items_.at(index);
}

int MenuGroup::indexOf(const MenuItem* item) const
{
    for (int index = 0; index < count(); ++index) {
        if (items_.at(index) == item)
            return index;
    }
    return -1;
}

QList<MenuItem*> MenuGroup::items() const
{
    return items_;
}

SubMenu* MenuGroup::subMenuAt(int index) const
{
    return qobject_cast<SubMenu*>(itemAt(index));
}

void MenuGroup::attachItem(MenuItem* item)
{
    QObject::connect(item, &MenuItem::activated, this, [this, item] { handleActivated(item); });
    QObject::connect(item, &QObject::destroyed, this, [this, item] {
        items_.removeAll(item);
        itemRects_.clear();
        relayout();
    });

    if (auto* submenu = qobject_cast<SubMenu*>(item)) {
        QObject::connect(submenu, &SubMenu::expandedChanged, this, [this] { relayout(); });
        QObject::connect(submenu, &SubMenu::animationFinished, this, [this] { relayout(); });
        QObject::connect(submenu, &SubMenu::childActivated, this,
                         [this, submenu](int childIndex) {
                             MenuItem* child = submenu->childAt(childIndex);
                             if (child != nullptr) {
                                 emit childItemActivated(items_.indexOf(submenu), childIndex,
                                                         child->label());
                             }
                         });
    }
}

void MenuGroup::detachItem(MenuItem* item)
{
    QObject::disconnect(item, nullptr, this, nullptr);
}

void MenuGroup::handleActivated(MenuItem* item)
{
    const int index = indexOf(item);
    if (index < 0)
        return;

    const int navIndex = static_cast<int>(navigationRows().indexOf(item));
    if (navIndex >= 0)
        currentRow_ = navIndex;

    // An expandable row opens instead of becoming the selection: the drawer
    // highlights the destination, not the section.
    if (item->isExpandable())
        return;

    setSelectedIndex(index);
    emit itemActivated(index, item->label());
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

void MenuGroup::setSelectionModel(MenuSelectionModel* model)
{
    if (model == nullptr)
        model = &internalModel_;
    if (model_ == model)
        return;

    QObject::disconnect(model_, &MenuSelectionModel::selectedIndexChanged, this, nullptr);
    model_ = model;
    QObject::connect(model_, &MenuSelectionModel::selectedIndexChanged, this,
                     [this](int index) {
                         applySelection();
                         emit selectionChanged(index);
                     });

    if (model_->selectedIndex() >= count())
        model_->setSelectedIndex(-1);

    applySelection();
    emit selectionChanged(selectedIndex());
}

MenuSelectionModel* MenuGroup::selectionModel() const
{
    return model_;
}

void MenuGroup::setSelectedIndex(int index)
{
    if (index < -1 || index >= count())
        return;

    model_->setSelectedIndex(index);
}

int MenuGroup::selectedIndex() const
{
    const int index = model_ != nullptr ? model_->selectedIndex() : -1;
    if (index < 0 || index >= count())
        return -1;

    return index;
}

void MenuGroup::clearSelection()
{
    setSelectedIndex(-1);
}

void MenuGroup::applySelection()
{
    const int selected = selectedIndex();
    for (int index = 0; index < count(); ++index)
        items_.at(index)->setSelected(index == selected);
}

// ---------------------------------------------------------------------------
// Geometry and keyboard
// ---------------------------------------------------------------------------

qreal MenuGroup::naturalWidth() const
{
    const Theme& theme = Theme::forWidget(this);
    qreal natural = 0.0;

    if (titleVisible_ && !title_.isEmpty()) {
        const QFontMetricsF metrics(titleFont(theme));
        natural = 2.0 * theme.density().dp(kTitleInsetDp)
            + metrics.horizontalAdvance(title_)
            + (collapsible_ ? theme.density().dp(kTitleChevronDp) + theme.density().dp(8.0)
                            : 0.0);
    }

    for (MenuItem* item : items_) {
        item->measure(MeasureSpec::unspecified(), MeasureSpec::unspecified());
        natural = std::max(natural, item->measuredSize().width());
    }

    return natural;
}

qreal MenuGroup::rowsHeight() const
{
    if (!rowsVisible())
        return 0.0;

    qreal total = 0.0;
    for (int index = 0; index < count(); ++index) {
        if (index > 0)
            total += spacingPx();
        total += items_.at(index)->preferredHeight();
    }
    return total;
}

void MenuGroup::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const qreal widthPx = width.resolve(naturalWidth());
    const qreal titleHeightPx = titleHeight();

    qreal total = titleHeightPx;
    if (rowsVisible()) {
        for (int index = 0; index < count(); ++index) {
            if (index > 0)
                total += spacingPx();
            items_.at(index)->measure(MeasureSpec::exactly(widthPx), MeasureSpec::unspecified());
            total += std::max(items_.at(index)->measuredSize().height(),
                              items_.at(index)->preferredHeight());
        }
    }

    setMeasuredSize(QSizeF(widthPx, height.resolve(total)));
}

void MenuGroup::arrangeChildren(const QRectF& content)
{
    itemRects_.clear();

    if (!rowsVisible())
        return;

    const qreal widthPx = std::max(qreal(0.0), content.width());
    int y = static_cast<int>(qRound(content.top() + titleHeight()));

    for (int index = 0; index < count(); ++index) {
        MenuItem* item = items_.at(index);
        // An expanded SubMenu is taller than its last measured height once its
        // animation has run; preferredHeight() is always the current truth.
        const qreal naturalHeight = std::max(item->measuredSize().height(),
                                             item->preferredHeight());
        const int rowHeight = std::max(1, static_cast<int>(qRound(naturalHeight)));

        item->arrange(QRect(static_cast<int>(qRound(content.left())), y,
                            std::max(0, static_cast<int>(qRound(widthPx))), rowHeight));
        itemRects_.append(QRectF(item->geometry()));

        y += rowHeight + static_cast<int>(qRound(spacingPx()));
    }
}

QRectF MenuGroup::itemRect(int index) const
{
    if (index < 0 || index >= count() || !rowsVisible())
        return {};

    if (itemRects_.size() == count())
        return itemRects_.at(index);

    qreal y = titleHeight();
    for (int row = 0; row < index; ++row) {
        y += items_.at(row)->preferredHeight() + spacingPx();
    }
    return QRectF(0.0, y, static_cast<qreal>(width()), items_.at(index)->preferredHeight());
}

MenuItem* MenuGroup::itemAtPosition(const QPointF& position) const
{
    for (int index = 0; index < count(); ++index) {
        if (itemRect(index).contains(position))
            return items_.at(index);
    }
    return nullptr;
}

QList<MenuItem*> MenuGroup::navigationRows() const
{
    QList<MenuItem*> rows;
    if (!rowsVisible())
        return rows;

    for (MenuItem* item : items_) {
        // isHidden() is the explicit bit; isVisible() would also be false for
        // every row of a group that has not been shown yet.
        if (item->isHidden())
            continue;

        rows.append(item);

        auto* submenu = qobject_cast<SubMenu*>(item);
        if (submenu == nullptr)
            continue;
        if (!submenu->isExpanded() && submenu->expansion() <= 0.0)
            continue;

        for (MenuItem* child : submenu->childItems()) {
            if (!child->isHidden())
                rows.append(child);
        }
    }

    return rows;
}

int MenuGroup::currentRow() const
{
    const int focused = focusedRow();
    return focused >= 0 ? focused : currentRow_;
}

MenuItem* MenuGroup::currentItem() const
{
    const int index = currentRow();
    return index >= 0 ? navigationRowAt(index) : nullptr;
}

MenuItem* MenuGroup::navigationRowAt(int index) const
{
    const QList<MenuItem*> rows = navigationRows();
    if (index < 0 || index >= rows.size())
        return nullptr;

    return rows.at(index);
}

int MenuGroup::focusedRow() const
{
    QWidget* focused = QApplication::focusWidget();
    if (focused == nullptr)
        return -1;

    const QList<MenuItem*> rows = navigationRows();
    for (int index = 0; index < rows.size(); ++index) {
        if (rows.at(index) == focused)
            return index;
    }
    return -1;
}

void MenuGroup::moveCurrent(int delta)
{
    const QList<MenuItem*> rows = navigationRows();
    if (rows.isEmpty())
        return;

    // The keyboard cursor follows focus when a row has it; otherwise the last
    // cursor position is the fallback, which keeps navigation working while
    // the group itself owns the focus.
    int index = focusedRow();
    if (index < 0)
        index = currentRow_;
    if (index < 0)
        index = delta >= 0 ? -1 : static_cast<int>(rows.size());

    index = std::clamp(index + delta, 0, static_cast<int>(rows.size()) - 1);
    currentRow_ = index;

    MenuItem* target = rows.at(index);
    if (!target->hasFocus())
        target->setFocus(Qt::OtherFocusReason);
    requestUpdate();
}

void MenuGroup::activateCurrent()
{
    const QList<MenuItem*> rows = navigationRows();
    if (rows.isEmpty())
        return;

    int index = currentRow();
    if (index < 0 || index >= rows.size())
        index = 0;

    rows.at(index)->activate();
}

void MenuGroup::updateRowVisibility()
{
    // isVisible() is false for every child of a hidden group, so it cannot tell
    // an explicitly hidden row from one that is merely not shown yet. isHidden()
    // carries exactly the explicit bit this method sets.
    const bool visible = rowsVisible();
    for (MenuItem* item : items_) {
        if (visible && item->isHidden())
            item->show();
        else if (!visible && !item->isHidden())
            item->hide();
    }
}

void MenuGroup::relayout()
{
    updateRowVisibility();
    updateGeometry();
    requestUpdate();

    QList<QRectF> previous = itemRects_;
    arrangeChildren(contentRect());
    if (previous != itemRects_)
        requestUpdate();
}

// ---------------------------------------------------------------------------
// Painting and input
// ---------------------------------------------------------------------------

void MenuGroup::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);
    paintTitle(painter);
}

void MenuGroup::paintChildren(QPainter& painter)
{
    // No manual child rendering. The rows are ordinary child widgets, so Qt
    // composites them after this paint event; rendering them here as well (what
    // ViewGroup::paintChildren does) painted every row twice and made the label
    // look bolder. ViewGroup renders its children so that its foreground stages
    // can paint above them, but a section has no foreground of its own: the
    // state overlay and the focus ring are suppressed in onEvent().
    Q_UNUSED(painter);
}

void MenuGroup::paintTitle(QPainter& painter)
{
    const QRectF rect = titleRect();
    if (rect.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
    const qreal inset = theme.density().dp(kTitleInsetDp);
    const qreal chevron = theme.density().dp(kTitleChevronDp);
    const qreal gap = theme.density().dp(8.0);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(titleFont(theme));
    painter.setPen(isEnabled() ? colours.onSurfaceVariant : colours.onSurface);

    const qreal textWidth = std::max(
        qreal(0.0), rect.width() - 2.0 * inset - (collapsible_ ? chevron + gap : 0.0));
    const QRectF textRect(rightToLeft ? rect.right() - inset - textWidth : rect.left() + inset,
                          rect.top(), textWidth, rect.height());

    const QFontMetricsF metrics(painter.font());
    const Qt::TextElideMode mode = rightToLeft ? Qt::ElideLeft : Qt::ElideRight;
    painter.drawText(textRect,
                     static_cast<int>((rightToLeft ? Qt::AlignRight : Qt::AlignLeft)
                                      | Qt::AlignVCenter),
                     metrics.elidedText(title_, mode, textWidth));

    if (collapsible_) {
        const QPointF centre(rightToLeft ? rect.left() + inset + chevron / 2.0
                                         : rect.right() - inset - chevron / 2.0,
                             rect.center().y());
        const qreal half = theme.density().dp(3.0);
        // The base path points down (the expanded state); a collapsed section
        // turns it towards the trailing edge, mirrored in a right-to-left
        // layout.
        const qreal angle = expanded_ ? 0.0 : (rightToLeft ? 90.0 : -90.0);

        QPainterPath chevronPath;
        chevronPath.moveTo(-half, -half);
        chevronPath.lineTo(0.0, half);
        chevronPath.lineTo(half, -half);

        QPen pen(colours.onSurfaceVariant,
                 std::max(qreal(1.0), theme.density().dp(kTitleChevronStrokeDp)));
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);

        painter.save();
        painter.translate(centre);
        painter.rotate(angle);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(chevronPath);
        painter.restore();
    }

    painter.restore();
}

void MenuGroup::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        ViewGroup::keyPressEvent(event);
        return;
    }

    const int span = static_cast<int>(navigationRows().size());

    switch (event->key()) {
    case Qt::Key_Up:
        moveCurrent(-1);
        event->accept();
        return;
    case Qt::Key_Down:
        moveCurrent(1);
        event->accept();
        return;
    case Qt::Key_Home:
        moveCurrent(-(span + 1));
        event->accept();
        return;
    case Qt::Key_End:
        moveCurrent(span + 1);
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (rowsVisible() && span > 0) {
            activateCurrent();
            event->accept();
            return;
        }
        if (collapsible_) {
            toggleExpanded();
            event->accept();
            return;
        }
        break;
    default:
        break;
    }

    ViewGroup::keyPressEvent(event);
}

void MenuGroup::mousePressEvent(QMouseEvent* event)
{
    ViewGroup::mousePressEvent(event);

    if (!isEnabled() || event->button() != Qt::LeftButton)
        return;

    // Rows keep their own presses; the group only owns the title strip.
    if (collapsible_ && titleRect().contains(event->position()))
        toggleExpanded();
}

void MenuGroup::changeEvent(QEvent* event)
{
    ViewGroup::changeEvent(event);

    if (event->type() == QEvent::LayoutDirectionChange)
        requestUpdate();
}

void MenuGroup::onThemeChanged()
{
    relayout();
}

bool MenuGroup::focusable() const
{
    // A section paints no focus indicator of its own: the kernel's foreground
    // state layer would cover the whole group, title and all.
    return false;
}

bool MenuGroup::onEvent(Event& event)
{
    // The group owns the arrangement, not the interaction: pointer state is the
    // rows' business, so hovering the title strip must not tint the section.
    switch (event.type()) {
    case EventType::PointerEnter:
    case EventType::PointerLeave:
    case EventType::PointerDown:
    case EventType::PointerUp:
    case EventType::PointerMove:
        return false;
    default:
        break;
    }

    return ViewGroup::onEvent(event);
}

} // namespace md
