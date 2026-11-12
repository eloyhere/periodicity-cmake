// Periodicity —Menu.
//
// The panel is painted by the menu itself rather than by View's background
// layer, because the widget rectangle also carries the shadow margin that the
// surface needs to look elevated. The panel geometry is therefore
// panelRect(); everything the caller positions against uses that rectangle.
//
// Submenus are separate Menu widgets parented to the same window. They are
// created when a parent row is opened and destroyed when they close, which
// keeps the item model the single source of truth for their contents.

#include <md/components/menu.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QScreen>
#include <QSizeF>
#include <QWidget>
#include <QtMath>

#include <algorithm>

namespace md {

namespace {

constexpr qreal kShadowMarginDp = 4.0;
constexpr qreal kScreenMarginDp = 4.0;
constexpr qreal kAnchorGapDp = 2.0;
constexpr qreal kSubmenuGapDp = 2.0;
constexpr qreal kPanelPaddingDp = 8.0;
constexpr qreal kItemHeightDp = 48.0;
constexpr qreal kSeparatorHeightDp = 9.0;
constexpr qreal kHorizontalPaddingDp = 12.0;
constexpr qreal kIconSizeDp = 18.0;
constexpr qreal kCheckSizeDp = 18.0;
constexpr qreal kSlotGapDp = 8.0;
constexpr qreal kArrowSizeDp = 18.0;
constexpr qreal kShortcutGapDp = 16.0;
constexpr qreal kMinWidthDp = 112.0;
constexpr qreal kMaxWidthDp = 320.0;
constexpr qreal kEmptyHeightDp = 48.0;

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

QColor disabledColour(const QColor& colour)
{
    return withAlpha(colour, 0.38);
}

} // namespace

Menu::Menu(QWidget* parent)
    : ViewGroup(parent)
{
    setBackgroundColour(QColor(Qt::transparent));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    syncAppearance();

    // A menu is a popup panel: it exists before it is shown, but it must not
    // paint until open() has placed it.
    hide();
}

Menu::~Menu()
{
    shutdown();
}

bool Menu::isRoot() const
{
    return parentMenu_ == nullptr;
}

Menu* Menu::rootMenu()
{
    Menu* menu = this;
    while (menu->parentMenu_ != nullptr)
        menu = menu->parentMenu_;
    return menu;
}

const Menu* Menu::rootMenu() const
{
    const Menu* menu = this;
    while (menu->parentMenu_ != nullptr)
        menu = menu->parentMenu_;
    return menu;
}

void Menu::syncAppearance()
{
    // Design baseline menu container shape is extra small (4 dp corner radius).
    setShape(Theme::forWidget(this).shapes().extraSmall);
    setBackgroundColour(QColor(Qt::transparent));
    requestUpdate();
}

void Menu::shutdown()
{
    open_ = false;
    removeFilters();

    if (submenu_ != nullptr) {
        Menu* submenu = submenu_;
        submenu_ = nullptr;
        submenuIndex_ = -1;
        submenu->parentMenu_ = nullptr;
        submenu->shutdown();
        submenu->hide();
        delete submenu;
    }
}

int Menu::addItem(const MenuItem& item)
{
    if (open_)
        closeSubmenu();

    items_.push_back(item);
    const int index = static_cast<int>(items_.size()) - 1;

    if (open_) {
        measure(MeasureSpec::atMost(static_cast<qreal>(width())),
                MeasureSpec::atMost(static_cast<qreal>(height())));
        positionPanel();
    } else if (highlighted_ < 0) {
        highlighted_ = firstEnabledIndex();
    }

    requestUpdate();
    return index;
}

void Menu::addSeparator()
{
    MenuItem item;
    item.separator = true;
    addItem(item);
}

void Menu::clear()
{
    if (open_)
        closeSubmenu();

    items_.clear();
    highlighted_ = -1;
    pressedIndex_ = -1;

    if (open_)
        positionPanel();

    requestUpdate();
}

int Menu::count() const
{
    return static_cast<int>(items_.size());
}

Menu::MenuItem Menu::itemAt(int index) const
{
    if (index < 0 || index >= count())
        return MenuItem();
    return items_[static_cast<std::size_t>(index)];
}

void Menu::setItemEnabled(int index, bool enabled)
{
    if (index < 0 || index >= count())
        return;
    if (items_[static_cast<std::size_t>(index)].enabled == enabled)
        return;

    items_[static_cast<std::size_t>(index)].enabled = enabled;
    if (open_)
        positionPanel();
    requestUpdate();
}

bool Menu::isItemEnabled(int index) const
{
    if (index < 0 || index >= count())
        return false;
    return items_[static_cast<std::size_t>(index)].enabled;
}

void Menu::setItemChecked(int index, bool checked)
{
    if (index < 0 || index >= count())
        return;

    MenuItem& item = items_[static_cast<std::size_t>(index)];
    if (!item.checkable || item.checked == checked)
        return;

    item.checked = checked;
    requestUpdate();
}

bool Menu::isItemChecked(int index) const
{
    if (index < 0 || index >= count())
        return false;
    return items_[static_cast<std::size_t>(index)].checked;
}

void Menu::setAnchor(QWidget* anchor)
{
    if (anchor_.data() == anchor)
        return;

    if (open_ && anchor_ != nullptr)
        anchor_->removeEventFilter(this);

    anchor_ = anchor;
    explicitPosition_ = false;

    if (anchor != nullptr) {
        QObject::connect(anchor, &QObject::destroyed, this, [this](QObject* object) {
            if (anchor_.isNull() || static_cast<QObject*>(anchor_.data()) == object)
                close();
        });
    }

    if (open_) {
        installFilters();
        positionPanel();
    }
}

QWidget* Menu::anchor() const
{
    return anchor_.data();
}

void Menu::open()
{
    if (open_)
        return;

    if (parentWidget() == nullptr && anchor_ != nullptr)
        setParent(anchor_->window());

    if (parentWidget() == nullptr)
        return;

    open_ = true;

    if (highlighted_ < 0 || highlighted_ >= count() || isSeparator(highlighted_))
        highlighted_ = firstEnabledIndex();

    syncAppearance();
    positionPanel();
    show();
    raise();
    setFocus(Qt::PopupFocusReason);
    installFilters();

    emit opened();
}

void Menu::openAt(const QPoint& globalPanelTopLeft)
{
    explicitPosition_ = true;
    explicitTopLeft_ = globalPanelTopLeft;

    if (open_)
        positionPanel();
    else
        open();
}

void Menu::close()
{
    if (!open_) {
        hide();
        return;
    }

    open_ = false;
    closeSubmenu();
    removeFilters();
    hide();

    emit closed();
}

bool Menu::isOpen() const noexcept
{
    return open_;
}

Menu::Placement Menu::effectivePlacement() const noexcept
{
    return effective_;
}

void Menu::installFilters()
{
    if (!isRoot())
        return;

    if (auto* app = QCoreApplication::instance())
        app->installEventFilter(this);

    if (QWidget* parent = parentWidget())
        parent->installEventFilter(this);

    if (anchor_ != nullptr)
        anchor_->installEventFilter(this);
}

void Menu::removeFilters()
{
    if (auto* app = QCoreApplication::instance())
        app->removeEventFilter(this);

    if (QWidget* parent = parentWidget())
        parent->removeEventFilter(this);

    if (anchor_ != nullptr)
        anchor_->removeEventFilter(this);
}

bool Menu::containsGlobal(const QPoint& globalPoint) const
{
    if (!open_)
        return false;

    if (panelGlobalRect().contains(QPointF(globalPoint)))
        return true;

    // A press on the anchor belongs to the anchor: leaving it to the anchor is
    // what lets a toggle-style host (the Language chip, for example) close the
    // panel itself instead of having the press close it and then reopen it.
    if (anchor_ != nullptr) {
        const QPoint local = anchor_->mapFromGlobal(globalPoint);
        if (anchor_->rect().contains(local))
            return true;
    }

    if (submenu_ != nullptr && submenu_->containsGlobal(globalPoint))
        return true;

    return false;
}

void Menu::buildSubmenu(int index)
{
    if (index < 0 || index >= count())
        return;

    const MenuItem& parentItem = items_[static_cast<std::size_t>(index)];
    if (parentItem.submenu.empty())
        return;

    auto* submenu = new Menu(parentWidget() != nullptr ? parentWidget() : window());
    submenu->parentMenu_ = this;
    submenu->items_ = parentItem.submenu;
    submenu->highlighted_ = submenu->firstEnabledIndex();
    submenu->measure(MeasureSpec::unspecified(), MeasureSpec::unspecified());

    QObject::connect(submenu, &Menu::itemTriggered, this,
                     [this](int itemIndex, const QString& text) {
                         emit itemTriggered(itemIndex, text);
                     });
    QObject::connect(submenu, &Menu::closed, this, [this, submenu] {
        if (submenu_ == submenu) {
            submenu_ = nullptr;
            submenuIndex_ = -1;
            submenu->deleteLater();
        }
    });
    QObject::connect(submenu, &QObject::destroyed, this, [this, submenu] {
        if (submenu_ == submenu) {
            submenu_ = nullptr;
            submenuIndex_ = -1;
        }
    });

    submenu_ = submenu;
    submenuIndex_ = index;
}

void Menu::openSubmenu(int index)
{
    if (index < 0 || index >= count())
        return;

    const MenuItem& item = items_[static_cast<std::size_t>(index)];
    if (!item.enabled || item.submenu.empty())
        return;

    if (submenu_ != nullptr && submenuIndex_ == index) {
        submenu_->setFocus(Qt::PopupFocusReason);
        return;
    }

    closeSubmenu();
    buildSubmenu(index);

    if (submenu_ == nullptr)
        return;

    placeSubmenu();
    submenu_->raise();
    submenu_->setFocus(Qt::PopupFocusReason);

    requestUpdate();
}

void Menu::closeSubmenu()
{
    if (submenu_ == nullptr)
        return;

    Menu* submenu = submenu_;
    submenu_ = nullptr;
    submenuIndex_ = -1;

    submenu->close();
    submenu->deleteLater();
}

void Menu::placeSubmenu()
{
    if (submenu_ == nullptr)
        return;

    const QRectF row = itemRect(submenuIndex_);
    if (row.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal margin = density.dp(kShadowMarginDp);
    const qreal inset = density.dp(kScreenMarginDp);
    const qreal gap = density.dp(kSubmenuGapDp);

    const QSizeF measured = submenu_->measuredSize();
    const qreal panelWidth = std::max(qreal(0.0), measured.width() - margin * 2.0);
    const qreal panelHeight = std::max(qreal(0.0), measured.height() - margin * 2.0);

    const QRectF bounds = availableBoundsGlobal();
    const QRectF parentPanel = panelGlobalRect();

    qreal panelLeft = parentPanel.right() - gap;
    if (panelLeft + panelWidth > bounds.right() - inset)
        panelLeft = parentPanel.left() - panelWidth + gap;

    const qreal rowTop = static_cast<qreal>(mapToGlobal(row.topLeft().toPoint()).y());
    qreal panelTop = rowTop;
    const qreal minTop = bounds.top() + inset;
    const qreal maxTop = std::max(minTop, bounds.bottom() - inset - panelHeight);
    panelTop = std::clamp(panelTop, minTop, maxTop);

    submenu_->openAt(QPoint(qRound(panelLeft), qRound(panelTop)));
}

void Menu::positionPanel()
{
    if (parentWidget() == nullptr)
        return;

    if (submenu_ != nullptr)
        closeSubmenu();

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal margin = density.dp(kShadowMarginDp);
    const qreal inset = density.dp(kScreenMarginDp);

    const QRectF bounds = availableBoundsGlobal();
    if (bounds.isEmpty())
        return;

    const qreal limitWidth =
        std::max(qreal(0.0), bounds.width() - inset * 2.0);
    const qreal limitHeight =
        std::max(qreal(0.0), bounds.height() - inset * 2.0);

    measure(MeasureSpec::atMost(limitWidth), MeasureSpec::atMost(limitHeight));

    const QSizeF size = measuredSize();
    const qreal panelWidth = std::max(qreal(0.0), size.width() - margin * 2.0);
    const qreal panelHeight = std::max(qreal(0.0), size.height() - margin * 2.0);

    qreal panelLeft = bounds.left() + inset;
    qreal panelTop = bounds.top() + inset;
    effective_ = Placement::Below;

    if (explicitPosition_) {
        panelLeft = static_cast<qreal>(explicitTopLeft_.x());
        panelTop = static_cast<qreal>(explicitTopLeft_.y());
    } else if (anchor_ != nullptr) {
        const QRectF anchorRect(QPointF(anchor_->mapToGlobal(QPoint(0, 0))),
                                QSizeF(anchor_->size()));
        const qreal gap = density.dp(kAnchorGapDp);

        const qreal belowTop = anchorRect.bottom() + gap;
        const qreal aboveTop = anchorRect.top() - gap - panelHeight;
        const bool belowFits = belowTop + panelHeight <= bounds.bottom() - inset;
        const bool aboveFits = aboveTop >= bounds.top() + inset;

        if (!belowFits && aboveFits) {
            effective_ = Placement::Above;
            panelTop = aboveTop;
        } else if (belowFits) {
            effective_ = Placement::Below;
            panelTop = belowTop;
        } else if (anchorRect.center().y() > bounds.center().y()) {
            effective_ = Placement::Above;
            panelTop = aboveTop;
        } else {
            effective_ = Placement::Below;
            panelTop = belowTop;
        }

        panelLeft = anchorRect.left();
    }

    const qreal minX = bounds.left() + inset;
    const qreal maxX = std::max(minX, bounds.right() - inset - panelWidth);
    panelLeft = std::clamp(panelLeft, minX, maxX);

    const qreal minY = bounds.top() + inset;
    const qreal maxY = std::max(minY, bounds.bottom() - inset - panelHeight);
    panelTop = std::clamp(panelTop, minY, maxY);

    const QPoint globalTopLeft(qRound(panelLeft - margin), qRound(panelTop - margin));
    const QPoint localTopLeft = parentWidget()->mapFromGlobal(globalTopLeft);

    arrange(QRect(localTopLeft, QSize(qCeil(size.width()), qCeil(size.height()))));
}

QRectF Menu::availableBoundsGlobal() const
{
    if (parentWidget() == nullptr)
        return QRectF();

    QRect bounds(parentWidget()->mapToGlobal(QPoint(0, 0)), parentWidget()->size());

    if (const QScreen* screen = QGuiApplication::screenAt(bounds.center())) {
        const QRect available = screen->availableGeometry();
        const QRect clipped = bounds.intersected(available);
        if (!clipped.isEmpty())
            bounds = clipped;
    }

    return QRectF(bounds);
}

qreal Menu::panelPadding() const
{
    return Theme::forWidget(this).density().dp(kPanelPaddingDp);
}

qreal Menu::rowHeight(int index) const
{
    if (index < 0 || index >= count())
        return 0.0;

    const Density& density = Theme::forWidget(this).density();
    return items_[static_cast<std::size_t>(index)].separator
        ? density.dp(kSeparatorHeightDp)
        : density.dp(kItemHeightDp);
}

qreal Menu::naturalPanelWidth() const
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    const QFont labelFont = fontFor(theme.type().labelLarge, density, this);
    const QFontMetricsF labelMetrics(labelFont);
    const QFont shortcutFont = fontFor(theme.type().bodySmall, density, this);
    const QFontMetricsF shortcutMetrics(shortcutFont);

    const qreal horizontalPadding = density.dp(kHorizontalPaddingDp);
    const qreal slotGap = density.dp(kSlotGapDp);
    const qreal iconSize = density.dp(kIconSizeDp);
    const qreal checkSize = density.dp(kCheckSizeDp);
    const qreal arrowSize = density.dp(kArrowSizeDp);
    const qreal shortcutGap = density.dp(kShortcutGapDp);

    qreal widest = 0.0;
    for (const MenuItem& item : items_) {
        if (item.separator)
            continue;

        qreal width = horizontalPadding * 2.0;

        if (item.checkable)
            width += checkSize + slotGap;

        if (!item.icon.isNull())
            width += iconSize + slotGap;

        width += labelMetrics.horizontalAdvance(item.text);

        if (!item.shortcut.isEmpty())
            width += shortcutGap + shortcutMetrics.horizontalAdvance(item.shortcut);

        if (!item.submenu.empty())
            width += slotGap + arrowSize;

        widest = std::max(widest, width);
    }

    return std::clamp(widest, density.dp(kMinWidthDp), density.dp(kMaxWidthDp));
}

qreal Menu::naturalPanelHeight() const
{
    const Density& density = Theme::forWidget(this).density();

    if (items_.empty())
        return density.dp(kEmptyHeightDp) + panelPadding() * 2.0;

    qreal height = panelPadding() * 2.0;
    for (int index = 0; index < count(); ++index)
        height += rowHeight(index);

    return height;
}

void Menu::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const qreal margin = Theme::forWidget(this).density().dp(kShadowMarginDp);
    const qreal chrome = margin * 2.0;

    setMeasuredSize(QSizeF(width.resolve(naturalPanelWidth() + chrome),
                           height.resolve(naturalPanelHeight() + chrome)));
}

void Menu::arrangeChildren(const QRectF& content)
{
    Q_UNUSED(content);

    if (open_ && submenu_ != nullptr)
        placeSubmenu();
}

QRectF Menu::panelRect() const
{
    const qreal margin = Theme::forWidget(this).density().dp(kShadowMarginDp);
    return QRectF(rect()).adjusted(margin, margin, -margin, -margin);
}

QRectF Menu::panelGlobalRect() const
{
    const QRectF panel = panelRect();
    return QRectF(QPointF(mapToGlobal(panel.topLeft().toPoint())), panel.size());
}

QRectF Menu::itemRect(int index) const
{
    if (index < 0 || index >= count())
        return QRectF();

    const QRectF panel = panelRect();
    qreal y = panel.top() + panelPadding();

    for (int row = 0; row < count(); ++row) {
        const qreal height = rowHeight(row);
        const QRectF rect(panel.left(), y, panel.width(), height);
        if (row == index)
            return rect;
        y += height;
    }

    return QRectF();
}

int Menu::highlightedIndex() const noexcept
{
    return highlighted_;
}

void Menu::setHighlightedIndex(int index)
{
    if (index < -1 || index >= count() || (index >= 0 && isSeparator(index)))
        return;
    if (highlighted_ == index)
        return;

    highlighted_ = index;
    requestUpdate();
}

Menu* Menu::submenuAt(int index) const
{
    if (submenu_ == nullptr || submenuIndex_ != index)
        return nullptr;
    return submenu_;
}

bool Menu::isSubmenuOpen() const noexcept
{
    return submenu_ != nullptr && submenu_->isOpen();
}

int Menu::firstEnabledIndex() const
{
    for (int index = 0; index < count(); ++index) {
        const MenuItem& item = items_[static_cast<std::size_t>(index)];
        if (!item.separator && item.enabled)
            return index;
    }

    return -1;
}

int Menu::lastEnabledIndex() const
{
    for (int index = count() - 1; index >= 0; --index) {
        const MenuItem& item = items_[static_cast<std::size_t>(index)];
        if (!item.separator && item.enabled)
            return index;
    }

    return -1;
}

bool Menu::isSeparator(int index) const
{
    if (index < 0 || index >= count())
        return false;
    return items_[static_cast<std::size_t>(index)].separator;
}

void Menu::moveHighlight(int step)
{
    if (step == 0 || items_.empty())
        return;

    const int total = count();
    int index = highlighted_;

    if (index < 0)
        index = step > 0 ? -1 : 0;

    for (int attempt = 0; attempt < total; ++attempt) {
        index = (index + step + total) % total;

        const MenuItem& item = items_[static_cast<std::size_t>(index)];
        if (!item.separator && item.enabled) {
            setHighlightedIndex(index);
            return;
        }
    }
}

int Menu::indexAt(const QPointF& point) const
{
    for (int index = 0; index < count(); ++index) {
        if (isSeparator(index))
            continue;

        if (itemRect(index).contains(point))
            return index;
    }

    return -1;
}

bool Menu::activateIndex(int index)
{
    if (index < 0 || index >= count())
        return false;

    MenuItem& item = items_[static_cast<std::size_t>(index)];
    if (item.separator || !item.enabled)
        return false;

    if (!item.submenu.empty()) {
        openSubmenu(index);
        return true;
    }

    const QString text = item.text;

    if (item.checkable) {
        item.checked = !item.checked;
        requestUpdate();
        emit itemTriggered(index, text);
        return true;
    }

    emit itemTriggered(index, text);

    Menu* root = rootMenu();
    if (root != nullptr)
        root->close();

    return true;
}

void Menu::mousePressEvent(QMouseEvent* event)
{
    ViewGroup::mousePressEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    setFocus(Qt::MouseFocusReason);

    pressedIndex_ = indexAt(event->position());
    if (pressedIndex_ >= 0)
        setHighlightedIndex(pressedIndex_);
}

void Menu::mouseReleaseEvent(QMouseEvent* event)
{
    ViewGroup::mouseReleaseEvent(event);

    if (event->button() != Qt::LeftButton) {
        pressedIndex_ = -1;
        return;
    }

    const int index = indexAt(event->position());
    const bool activate = index >= 0 && index == pressedIndex_;
    pressedIndex_ = -1;

    if (activate)
        (void)activateIndex(index);
}

void Menu::mouseMoveEvent(QMouseEvent* event)
{
    ViewGroup::mouseMoveEvent(event);

    int index = indexAt(event->position());
    if (index >= 0 && !items_[static_cast<std::size_t>(index)].enabled)
        index = -1;

    if (index != highlighted_)
        setHighlightedIndex(index);
}

void Menu::leaveEvent(QEvent* event)
{
    ViewGroup::leaveEvent(event);
    pressedIndex_ = -1;
}

void Menu::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Up:
        moveHighlight(-1);
        event->accept();
        return;

    case Qt::Key_Down:
        moveHighlight(1);
        event->accept();
        return;

    case Qt::Key_Home:
        setHighlightedIndex(firstEnabledIndex());
        event->accept();
        return;

    case Qt::Key_End:
        setHighlightedIndex(lastEnabledIndex());
        event->accept();
        return;

    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        (void)activateIndex(highlighted_);
        event->accept();
        return;

    case Qt::Key_Right:
        if (highlighted_ >= 0)
            openSubmenu(highlighted_);
        event->accept();
        return;

    case Qt::Key_Left:
        if (parentMenu_ != nullptr) {
            Menu* parent = parentMenu_;
            parent->closeSubmenu();
            parent->setFocus(Qt::PopupFocusReason);
            event->accept();
            return;
        }
        break;

    case Qt::Key_Escape:
        rootMenu()->close();
        event->accept();
        return;

    default:
        break;
    }

    ViewGroup::keyPressEvent(event);
}

void Menu::hideEvent(QHideEvent* event)
{
    ViewGroup::hideEvent(event);

    if (!open_)
        return;

    open_ = false;
    closeSubmenu();
    removeFilters();

    emit closed();
}

bool Menu::eventFilter(QObject* watched, QEvent* event)
{
    if (anchor_ != nullptr && watched == anchor_.data()) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
            positionPanel();
            break;
        case QEvent::Hide:
            close();
            break;
        default:
            break;
        }

        return ViewGroup::eventFilter(watched, event);
    }

    // The application-wide rules come before the parent filter: a press on the
    // parent window itself is an outside press and must close the panel, while
    // a press on the anchor is handled above.
    if (open_ && isRoot()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (!containsGlobal(mouse->globalPosition().toPoint()))
                close();
            break;
        }

        case QEvent::KeyPress: {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) {
                close();
                return true;
            }
            break;
        }

        default:
            break;
        }
    }

    if (parentWidget() != nullptr && watched == parentWidget()) {
        if (event->type() == QEvent::Resize)
            positionPanel();
        else if (event->type() == QEvent::Hide)
            close();

        return ViewGroup::eventFilter(watched, event);
    }

    return ViewGroup::eventFilter(watched, event);
}

bool Menu::focusable() const
{
    // The highlight is the keyboard indicator; a ring around the whole panel
    // would suggest that the panel itself is one control.
    return false;
}

void Menu::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const QRectF panel = panelRect();
    if (panel.isEmpty())
        return;

    const qreal margin = density.dp(kShadowMarginDp);
    const QPainterPath panelPath = theme.shapes().extraSmall.path(panel);

    const QFont labelFont = fontFor(theme.type().labelLarge, density, this);
    const QFont shortcutFont = fontFor(theme.type().bodySmall, density, this);
    const QFontMetricsF labelMetrics(labelFont);
    const QFontMetricsF shortcutMetrics(shortcutFont);

    const qreal horizontalPadding = density.dp(kHorizontalPaddingDp);
    const qreal slotGap = density.dp(kSlotGapDp);
    const qreal iconSize = density.dp(kIconSizeDp);
    const qreal checkSize = density.dp(kCheckSizeDp);
    const qreal arrowSize = density.dp(kArrowSizeDp);
    const qreal shortcutGap = density.dp(kShortcutGapDp);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    for (int step = 3; step >= 1; --step) {
        const qreal spread = margin * static_cast<qreal>(step) / 3.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(withAlpha(colours.shadow, 0.08));
        painter.drawPath(theme.shapes().extraSmall.path(
            panel.adjusted(-spread, -spread + density.dp(1.0), spread,
                           spread + density.dp(1.0))));
    }

    // Design baseline menu: surface container at elevation level 2, with no outline. An
    // outline plus the drop shadow read as a double border around the panel.
    painter.setPen(Qt::NoPen);
    painter.setBrush(colours.surfaceContainer);
    painter.drawPath(panelPath);

    painter.setClipPath(panelPath);

    for (int index = 0; index < count(); ++index) {
        const MenuItem& item = items_[static_cast<std::size_t>(index)];
        const QRectF row = itemRect(index);
        if (row.isEmpty())
            continue;

        if (item.separator) {
            // The divider is 1 dp tall and spans the item area minus the 8 dp
            // container padding, so it aligns with the panel inset rather than
            // with the label text.
            painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
            const qreal y = row.center().y();
            const qreal inset = panelPadding();
            painter.drawLine(QPointF(row.left() + inset, y),
                             QPointF(row.right() - inset, y));
            continue;
        }

        if (index == highlighted_ && item.enabled) {
            // Design baseline vertical menu: the highlight runs edge to edge inside
            // the container (the panel clip rounds the outer corners), instead
            // of an inset pill that leaves a 4 dp sliver on either side.
            painter.setPen(Qt::NoPen);
            painter.fillRect(row, colours.stateLayer(colours.onSurface, ViewState::Hovered));
        }

        QColor textColour = colours.onSurface;
        QColor secondaryColour = colours.onSurfaceVariant;
        if (!item.enabled) {
            textColour = disabledColour(textColour);
            secondaryColour = disabledColour(secondaryColour);
        }

        qreal left = row.left() + horizontalPadding;

        if (item.checkable) {
            if (item.checked) {
                const QRectF box(left, row.center().y() - checkSize / 2.0, checkSize,
                                 checkSize);
                QPen pen(colours.onSurface, density.dp(2.0));
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                QPainterPath check;
                check.moveTo(box.left() + box.width() * 0.18,
                             box.top() + box.height() * 0.53);
                check.lineTo(box.left() + box.width() * 0.42,
                             box.top() + box.height() * 0.78);
                check.lineTo(box.left() + box.width() * 0.84,
                             box.top() + box.height() * 0.24);
                painter.drawPath(check);
            }
            left += checkSize + slotGap;
        }

        if (!item.icon.isNull()) {
            const QRect iconRect(qRound(left),
                                 qRound(row.center().y() - iconSize / 2.0),
                                 qRound(iconSize), qRound(iconSize));
            item.icon.paint(&painter, iconRect, Qt::AlignCenter,
                            item.enabled ? QIcon::Normal : QIcon::Disabled);
            left += iconSize + slotGap;
        }

        qreal right = row.right() - horizontalPadding;

        if (!item.submenu.empty()) {
            const QRectF arrow(right - arrowSize, row.center().y() - arrowSize / 2.0,
                               arrowSize, arrowSize);
            QPen pen(secondaryColour, density.dp(1.8));
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            QPainterPath chevron;
            chevron.moveTo(arrow.left() + arrow.width() * 0.38,
                           arrow.top() + arrow.height() * 0.3);
            chevron.lineTo(arrow.left() + arrow.width() * 0.64,
                           arrow.top() + arrow.height() * 0.5);
            chevron.lineTo(arrow.left() + arrow.width() * 0.38,
                           arrow.top() + arrow.height() * 0.7);
            painter.drawPath(chevron);
            right -= arrowSize + slotGap;
        }

        if (!item.shortcut.isEmpty()) {
            const qreal shortcutWidth = shortcutMetrics.horizontalAdvance(item.shortcut);
            const QRectF shortcutRect(right - shortcutWidth, row.top(), shortcutWidth,
                                      row.height());
            painter.setFont(shortcutFont);
            painter.setPen(secondaryColour);
            painter.drawText(shortcutRect, static_cast<int>(Qt::AlignRight | Qt::AlignVCenter),
                             item.shortcut);
            right -= shortcutWidth + shortcutGap;
        }

        const qreal textWidth = std::max(right - left, qreal(0.0));
        if (textWidth > 0.0) {
            const QString label =
                labelMetrics.elidedText(item.text, Qt::ElideRight, textWidth);
            painter.setFont(labelFont);
            painter.setPen(textColour);
            painter.drawText(QRectF(left, row.top(), textWidth, row.height()),
                             static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), label);
        }
    }

    painter.restore();
}

void Menu::onThemeChanged()
{
    syncAppearance();
    requestUpdate();
}

} // namespace md
