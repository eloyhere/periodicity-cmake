// Periodicity — TreeSelect, a model-driven tree menu behind a filled field.
//
// The control and its menu are both painted by this file. The menu is a plain
// child widget of the window the control lives in, exactly like the menu behind
// DropDown is painted by input.cpp: it is not a Qt::Popup, so it inherits the
// window's theme scope, belongs to the same top-level and closes on an outside
// click instead of on a window deactivation.
//
// The interesting part is the row model. The source is a tree, the menu is a
// list of rows; rebuildRows() walks the borrowed model and appends a row for
// every node that is visible with the current expansion set, descending only
// into the branches that are expanded. Everything the menu paints — depth,
// branch flag, expansion, label — is read from that flattened list, which is
// what keeps paint and hit-testing in agreement.

#include <md/components/dropdown.h>
#include <md/components/tree_select.h>

#include <md/core/geometry.h>
#include <md/theme/theme.h>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QWidget>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace md {

namespace {

// M3 measurements, in dp; the density converts them to pixels.
constexpr qreal kFieldHeightDp = 56.0;
constexpr qreal kHorizontalPaddingDp = 16.0;
constexpr qreal kFieldRadiusDp = 4.0;
// The M3 menu container is the extra-small shape (4 dp); the popup used 8 dp,
// which read as a card rather than a menu surface.
constexpr qreal kPopupRadiusDp = 4.0;
constexpr qreal kPopupPaddingDp = 4.0;
constexpr qreal kPopupWidthDp = 200.0;
constexpr qreal kRowHeightDp = 40.0;
constexpr qreal kIndentDp = 20.0;
constexpr qreal kExpanderDp = 24.0;

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, qreal(0.0), qreal(1.0))));
    return result;
}

QFont themeFont(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

QString elided(const QFont& font, const QString& text, qreal width)
{
    if (text.isEmpty() || width <= 0.0)
        return {};

    return QFontMetricsF(font).elidedText(text, Qt::ElideRight, width);
}

// The filled text-field surface: rounded on the top corners only.
QPainterPath fieldPath(const QRectF& bounds, qreal radius)
{
    return CornerRadii{radius, radius, 0.0, 0.0}.toPath(bounds);
}

// The expander chevron of a branch: pointing right when collapsed, down when
// expanded.
QPainterPath expanderPath(const QPointF& centre, qreal size, bool expanded)
{
    QPainterPath path;
    if (expanded) {
        path.moveTo(centre.x() - size, centre.y() - size * 0.5);
        path.lineTo(centre.x(), centre.y() + size * 0.5);
        path.lineTo(centre.x() + size, centre.y() - size * 0.5);
    } else {
        path.moveTo(centre.x() - size * 0.5, centre.y() - size);
        path.lineTo(centre.x() + size * 0.5, centre.y());
        path.lineTo(centre.x() - size * 0.5, centre.y() + size);
    }
    return path;
}

} // namespace

// ---------------------------------------------------------------------------
// TreeContent — the rows of the tree menu.
//
// The surface belongs to md::Dropdown, which owns the placement (below the
// field, flipped above when the screen has no room, clamped to the screen), the
// layer and the dismissal. This widget is only the content inside that surface:
// the scroll window, the painted rows and the hit testing that agrees with
// them. It asks the control for every row it paints, which keeps the selection
// rule (only leaves) in one place — the control.
// ---------------------------------------------------------------------------
class TreeContent final : public View
{
public:
    explicit TreeContent(TreeSelect* owner)
        : View(nullptr)
        , owner_(owner)
    {
        setFocusPolicy(Qt::StrongFocus);
        setBackgroundColour(QColor(Qt::transparent));
        setMouseTracking(true);
    }

    [[nodiscard]] int firstRow() const noexcept { return firstRow_; }

    // The Dropdown sizes — and re-sizes — its panel from this hint, so it has to
    // describe the current row window. View::sizeHint() answers with the last
    // measurement, which stops being true as soon as a branch is expanded.
    [[nodiscard]] QSize sizeHint() const override { return naturalSize().toSize(); }

    void setFirstRow(int row)
    {
        const int clamped = std::clamp(row, 0, maxFirstRow());
        if (clamped == firstRow_)
            return;

        firstRow_ = clamped;
        update();
    }

    void ensureRowVisible(int row)
    {
        if (row < 0)
            return;

        const int page = std::max(1, owner_->paintedRowCount());
        if (row < firstRow_)
            firstRow_ = row;
        else if (row >= firstRow_ + page)
            firstRow_ = row - page + 1;

        setFirstRow(firstRow_);
    }

    [[nodiscard]] int contentRows() const
    {
        const int total = owner_->visibleRowCount();
        return std::max(0, std::min(owner_->paintedRowCount(), total - firstRow_));
    }

    [[nodiscard]] QRectF rowRectFor(int row) const
    {
        const int index = row - firstRow_;
        if (index < 0 || index >= contentRows())
            return {};

        const Density& density = Theme::forWidget(owner_).density();
        const qreal padding = density.dp(kPopupPaddingDp);
        const qreal rowHeight = density.dp(kRowHeightDp);
        const qreal width = std::max(qreal(0.0), qreal(this->width()) - padding * 2.0);
        return QRectF(padding, padding + qreal(index) * rowHeight, width, rowHeight);
    }

    [[nodiscard]] int rowAtPoint(const QPointF& point) const
    {
        const Density& density = Theme::forWidget(owner_).density();
        const qreal padding = density.dp(kPopupPaddingDp);
        const qreal rowHeight = density.dp(kRowHeightDp);
        if (rowHeight <= 0.0)
            return -1;

        const qreal local = point.y() - padding;
        if (local < 0.0)
            return -1;

        const int index = static_cast<int>(std::floor(local / rowHeight));
        if (index < 0 || index >= contentRows())
            return -1;

        return firstRow_ + index;
    }

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override
    {
        const QSizeF natural = naturalSize();
        setMeasuredSize(QSizeF(width.resolve(natural.width()), height.resolve(natural.height())));
    }

    // The panel, its shadow and its outline belong to the Dropdown; this paints
    // only the rows, inside the rectangle the Dropdown arranges.
    void paintContent(QPainter& painter, const QRectF& content) override
    {
        const Theme& theme = Theme::forWidget(owner_);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();
        const bool enabled = owner_->isEnabled();
        const qreal unit = density.dp(1.0);
        const qreal radius = density.dp(kPopupRadiusDp);
        const qreal padding = density.dp(kPopupPaddingDp);
        const qreal rowHeight = density.dp(kRowHeightDp);

        const QRectF bounds = content;
        if (bounds.isEmpty())
            return;

        painter.save();
        painter.setClipRect(bounds);

        const QFont itemFont = themeFont(theme, theme.type().bodyLarge);
        painter.setFont(itemFont);

        const int rows = contentRows();
        for (int index = 0; index < rows; ++index) {
            const int row = firstRow_ + index;
            const QRectF rowBounds(bounds.left() + padding,
                                   bounds.top() + padding + qreal(index) * rowHeight,
                                   std::max(qreal(0.0), bounds.width() - padding * 2.0),
                                   rowHeight);

            if (row == owner_->highlightedRow()) {
                QPainterPath pill;
                pill.addRoundedRect(rowBounds.adjusted(0.0, unit, 0.0, -unit),
                                    radius * 0.65, radius * 0.65);
                painter.fillPath(pill, withAlpha(colours.onSurface, 0.08));
            }

            const qreal depthOffset = qreal(owner_->rowDepth(row)) * density.dp(kIndentDp);
            const qreal expanderLeft = rowBounds.left() + density.dp(8.0) + depthOffset;
            const qreal expanderWidth = density.dp(kExpanderDp);
            const QPointF expanderCentre(expanderLeft + expanderWidth * 0.5, rowBounds.center().y());

            if (owner_->rowHasChildren(row)) {
                const QColor arrowColour = enabled ? colours.onSurfaceVariant
                                                   : withAlpha(colours.onSurfaceVariant, 0.38);
                painter.setPen(QPen(arrowColour, density.dp(1.6), Qt::SolidLine,
                                    Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(expanderPath(expanderCentre, density.dp(4.5),
                                              owner_->rowIsExpanded(row)));
            }

            const qreal textLeft = expanderLeft + expanderWidth;
            const qreal textWidth = std::max(qreal(0.0), rowBounds.right() - textLeft - density.dp(28.0));
            const bool selected = owner_->rowIsSelected(row);

            QColor textColour = colours.onSurface;
            if (!enabled)
                textColour = withAlpha(colours.onSurface, 0.38);
            else if (selected)
                textColour = colours.primary;

            painter.setPen(textColour);
            painter.drawText(QRectF(textLeft, rowBounds.top(), textWidth, rowBounds.height()),
                             static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                             elided(itemFont, owner_->rowLabel(row), textWidth));

            if (selected) {
                const QPointF centre(rowBounds.right() - density.dp(14.0), rowBounds.center().y());
                QPainterPath check;
                check.moveTo(centre.x() - density.dp(4.0), centre.y());
                check.lineTo(centre.x() - density.dp(1.0), centre.y() + density.dp(3.0));
                check.lineTo(centre.x() + density.dp(4.0), centre.y() - density.dp(3.5));
                painter.setPen(QPen(colours.primary, density.dp(2.0), Qt::SolidLine,
                                    Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(check);
            }
        }

        painter.restore();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            View::mousePressEvent(event);
            return;
        }

        const int row = rowAtPoint(event->position());
        if (row >= 0) {
            owner_->setHighlightedRow(row);
            owner_->activateRow(row);
            event->accept();
            return;
        }

        View::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const int row = rowAtPoint(event->position());
        if (row >= 0 && row != owner_->highlightedRow())
            owner_->setHighlightedRow(row);

        View::mouseMoveEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        // The control owns the navigation rules; the menu only has the focus.
        QKeyEvent forwarded(event->type(), event->key(), event->modifiers(),
                            event->text(), event->isAutoRepeat());
        QCoreApplication::sendEvent(owner_, &forwarded);
        event->accept();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        const int delta = event->angleDelta().y();
        if (delta == 0 || maxFirstRow() == 0) {
            event->ignore();
            return;
        }

        setFirstRow(firstRow_ + (delta < 0 ? 3 : -3));
        event->accept();
    }

private:
    [[nodiscard]] int maxFirstRow() const
    {
        const int page = std::max(1, owner_->paintedRowCount());
        return std::max(0, owner_->visibleRowCount() - page);
    }

    // The natural size of the row window: the padded rows plus the menu's
    // minimum width. The Dropdown turns it into the panel height and, because
    // it matches the anchor width, keeps the panel as wide as the field.
    [[nodiscard]] QSizeF naturalSize() const
    {
        const Density& density = Theme::forWidget(owner_).density();
        const qreal padding = density.dp(kPopupPaddingDp);
        const qreal rowHeight = density.dp(kRowHeightDp);
        const qreal rows = qreal(contentRows());
        return QSizeF(density.dp(kPopupWidthDp), padding * 2.0 + rowHeight * rows);
    }

    TreeSelect* owner_ = nullptr;
    int firstRow_ = 0;
};

// ---------------------------------------------------------------------------
// TreeSelect
// ---------------------------------------------------------------------------

TreeSelect::TreeSelect(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));
    setShape(Theme::forWidget(this).shapes().extraSmall);

    adapter_ = new Adapter(this);

    connect(adapter_, &QAbstractItemModel::modelReset, this, [this] {
        expandedPaths_.clear();
        handleModelReset();
    });
    connect(adapter_, &QAbstractItemModel::rowsInserted, this, [this] { handleStructureChanged(); });
    connect(adapter_, &QAbstractItemModel::rowsRemoved, this, [this] { handleStructureChanged(); });
    connect(adapter_, &QAbstractItemModel::rowsMoved, this, [this] { handleStructureChanged(); });
    connect(adapter_, &QAbstractItemModel::layoutChanged, this, [this] { handleStructureChanged(); });
    connect(adapter_, &QAbstractItemModel::dataChanged, this, [this] {
        requestUpdate();
        if (popupOpen_ && popup_ != nullptr)
            popup_->update();
    });

    requestUpdate();
}

TreeSelect::~TreeSelect()
{
    if (dropdown_ != nullptr) {
        Dropdown* dropdown = dropdown_;
        dropdown_ = nullptr;
        // Clear the flag first: the closed() handler must not run against a
        // half-destroyed control.
        popupOpen_ = false;
        dropdown->close();
        // The content is the dropdown's child until it is detached; deleting it
        // through the surface would otherwise free it twice.
        dropdown->setContent(nullptr);
        delete popup_;
        popup_ = nullptr;
        delete dropdown;
    }

    if (popup_ != nullptr) {
        popup_->hide();
        delete popup_;
        popup_ = nullptr;
    }
}

void TreeSelect::setModel(QAbstractItemModel* model)
{
    if (adapter_->sourceModel() == model)
        return;

    hidePopup();
    expandedPaths_.clear();
    selected_ = QPersistentModelIndex();
    highlighted_ = -1;

    adapter_->setSourceModel(model);

    rebuildRows();
    requestUpdate();
    updateGeometry();
}

QAbstractItemModel* TreeSelect::sourceModel() const noexcept
{
    return adapter_->sourceModel();
}

Adapter* TreeSelect::adapter() const noexcept
{
    return adapter_;
}

void TreeSelect::setFormatter(Adapter::Formatter formatter)
{
    formatterSet_ = static_cast<bool>(formatter);
    adapter_->setLabelFor(std::move(formatter));
    requestUpdate();
    if (popupOpen_ && popup_ != nullptr)
        popup_->update();
}

void TreeSelect::setPlaceholderText(const QString& text)
{
    if (placeholder_ == text)
        return;

    placeholder_ = text;
    requestUpdate();
}

QString TreeSelect::placeholderText() const
{
    return placeholder_;
}

void TreeSelect::setMaxVisibleRows(int rows)
{
    const int clamped = std::max(1, rows);
    if (maxVisibleRows_ == clamped)
        return;

    maxVisibleRows_ = clamped;
    syncPopupLayout();
    ensureHighlightVisible();
    requestUpdate();
}

int TreeSelect::maxVisibleRows() const noexcept
{
    return maxVisibleRows_;
}

QModelIndex TreeSelect::selectedIndex() const
{
    return selected_;
}

QVariant TreeSelect::selectedValue() const
{
    return valueForIndex(selectedIndex());
}

QString TreeSelect::currentText() const
{
    if (!hasSelection())
        return placeholder_;

    return labelForIndex(selectedIndex());
}

QStringList TreeSelect::selectedPath() const
{
    QStringList path;
    QModelIndex index = selectedIndex();
    while (index.isValid()) {
        path.prepend(labelForIndex(index));
        index = index.parent();
    }
    return path;
}

bool TreeSelect::hasSelection() const
{
    return selected_.isValid();
}

bool TreeSelect::isExpanded(const QModelIndex& index) const
{
    const QModelIndex proxy = proxyIndexFor(index);
    return proxy.isValid() && expandedPaths_.contains(pathOf(proxy));
}

void TreeSelect::setExpanded(const QModelIndex& index, bool expanded)
{
    const QModelIndex proxy = proxyIndexFor(index);
    if (!proxy.isValid())
        return;

    if (expanded && adapter_->rowCount(proxy) <= 0)
        return;

    const QString path = pathOf(proxy);
    if (expandedPaths_.contains(path) == expanded)
        return;

    if (expanded)
        expandedPaths_.insert(path);
    else
        expandedPaths_.remove(path);

    rebuildRows();
    clampHighlight();
    syncPopupLayout();
    requestUpdate();

    emit expandedChanged(proxy, expanded);
}

int TreeSelect::visibleRowCount() const
{
    return static_cast<int>(rows_.size());
}

int TreeSelect::paintedRowCount() const
{
    return std::min(maxVisibleRows_, static_cast<int>(rows_.size()));
}

int TreeSelect::rowDepth(int row) const
{
    if (row < 0 || row >= rows_.size())
        return 0;

    return rows_.at(row).depth;
}

QString TreeSelect::rowLabel(int row) const
{
    if (row < 0 || row >= rows_.size())
        return {};

    return labelForIndex(rows_.at(row).index);
}

bool TreeSelect::rowHasChildren(int row) const
{
    if (row < 0 || row >= rows_.size())
        return false;

    return rows_.at(row).branch;
}

bool TreeSelect::rowIsExpanded(int row) const
{
    if (row < 0 || row >= rows_.size())
        return false;

    return rows_.at(row).expanded;
}

bool TreeSelect::rowIsSelectable(int row) const
{
    return row >= 0 && row < rows_.size() && !rows_.at(row).branch;
}

bool TreeSelect::rowIsSelected(int row) const
{
    if (row < 0 || row >= rows_.size() || !selected_.isValid())
        return false;

    return rows_.at(row).index == selectedIndex();
}

QModelIndex TreeSelect::rowIndex(int row) const
{
    if (row < 0 || row >= rows_.size())
        return {};

    return rows_.at(row).index;
}

int TreeSelect::highlightedRow() const noexcept
{
    return highlighted_;
}

void TreeSelect::setHighlightedRow(int row)
{
    const int clamped = rows_.isEmpty() ? -1 : std::min(row, static_cast<int>(rows_.size()) - 1);
    if (clamped == highlighted_)
        return;

    highlighted_ = clamped;
    ensureHighlightVisible();
    requestUpdate();
}

void TreeSelect::activateRow(int row)
{
    if (row < 0 || row >= rows_.size())
        return;

    // The row is copied: expanding rebuilds the flattened list underneath us.
    const Row info = rows_.at(row);
    setHighlightedRow(row);

    if (info.branch) {
        setExpanded(info.index, !info.expanded);
        return;
    }

    applySelection(info.index);
    hidePopup();
}

int TreeSelect::rowAt(const QPointF& popupPoint) const
{
    if (!popupOpen_ || popup_ == nullptr)
        return -1;

    return static_cast<TreeContent*>(popup_)->rowAtPoint(popupPoint);
}

QRectF TreeSelect::rowRect(int row) const
{
    if (!popupOpen_ || popup_ == nullptr)
        return {};

    return static_cast<TreeContent*>(popup_)->rowRectFor(row);
}

QRectF TreeSelect::fieldRect() const
{
    const Density& density = Theme::forWidget(this).density();
    const qreal fieldHeight = std::min(qreal(height()), density.dp(kFieldHeightDp));
    return QRectF(0.0, 0.0, qreal(width()), fieldHeight);
}

void TreeSelect::showPopup()
{
    if (!isEnabled() || rows_.isEmpty())
        return;

    if (dropdown_ == nullptr) {
        auto* content = new TreeContent(this);
        auto* dropdown = new Dropdown(nullptr);
        dropdown->setAnchor(this);
        dropdown->setPlacement(Dropdown::Placement::Auto);
        dropdown->setMatchAnchorWidth(true);
        dropdown->setContent(content);
        connect(dropdown, &Dropdown::closed, this, [this] { handlePopupClosed(); });
        // The surface lives in the window, not in this widget: if the window
        // takes it down first, the control must forget it rather than touch a
        // destroyed pointer.
        connect(dropdown, &QObject::destroyed, this, [this] {
            dropdown_ = nullptr;
            popup_ = nullptr;
            popupOpen_ = false;
        });

        popup_ = content;
        dropdown_ = dropdown;
    }

    if (popupOpen_) {
        dropdown_->reposition();
        return;
    }

    if (highlighted_ < 0) {
        const int selectedRow = rowOfIndex(selectedIndex());
        // Opening the menu with nothing selected leaves the focus row empty: the
        // first Down (or a hover) gives the tree its focus row.
        highlighted_ = selectedRow >= 0 ? selectedRow : -1;
    }

    popupOpen_ = true;

    auto* content = static_cast<TreeContent*>(popup_);
    content->ensureRowVisible(highlighted_);

    // The Dropdown owns the placement: it measures from the anchor's global
    // rectangle, keeps the panel as wide as the field and flips it above when
    // the screen has no room below.
    dropdown_->open();
    content->setFocus(Qt::PopupFocusReason);
    requestUpdate();
}

void TreeSelect::hidePopup()
{
    const bool wasOpen = popupOpen_;
    popupOpen_ = false;

    if (dropdown_ != nullptr && dropdown_->isOpen())
        dropdown_->close();

    if (!wasOpen)
        return;

    requestUpdate();
    restoreFocus();
}

bool TreeSelect::isPopupVisible() const
{
    return popupOpen_ && dropdown_ != nullptr && dropdown_->isOpen();
}

QWidget* TreeSelect::popupWidget() const noexcept
{
    return popup_;
}

Dropdown* TreeSelect::dropdown() const noexcept
{
    return dropdown_;
}

void TreeSelect::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(240.0)),
                           height.resolve(density.dp(kFieldHeightDp))));
}

void TreeSelect::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const bool enabled = isEnabled();
    const bool focused = hasFocus();

    const QRectF field = fieldRect();
    if (field.isEmpty())
        return;

    const qreal padding = density.dp(kHorizontalPaddingDp);
    const qreal radius = density.dp(kFieldRadiusDp);

    const QColor container = enabled ? colours.surfaceContainerHighest
                                     : withAlpha(colours.onSurface, 0.04);
    const QColor underline = !enabled
        ? withAlpha(colours.onSurface, 0.12)
        : focused ? colours.primary : colours.outline;
    const bool showingPlaceholder = !hasSelection();
    const QColor textColour = showingPlaceholder
        ? (enabled ? colours.onSurfaceVariant : withAlpha(colours.onSurfaceVariant, 0.38))
        : (enabled ? colours.onSurface : withAlpha(colours.onSurface, 0.38));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(container);
    painter.drawPath(fieldPath(field, radius));

    const qreal underlineWidth = density.dp(focused ? 2.0 : 1.0);
    const qreal underlineY = field.bottom() - underlineWidth / 2.0;
    painter.setPen(QPen(underline, underlineWidth));
    painter.drawLine(QPointF(field.left(), underlineY), QPointF(field.right(), underlineY));

    const QFont valueFont = themeFont(theme, theme.type().bodyLarge);
    painter.setFont(valueFont);
    painter.setPen(textColour);

    const QString label = showingPlaceholder ? placeholder_ : currentText();
    const qreal textWidth = std::max(qreal(0.0), field.width() - padding * 2.0 - density.dp(32.0));
    painter.drawText(QRectF(field.left() + padding, field.top(), textWidth, field.height()),
                     static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                     elided(valueFont, label, textWidth));

    const QPainterPath chevron = expanderPath(
        QPointF(field.right() - density.dp(24.0), field.center().y()), density.dp(4.5), popupOpen_);
    painter.setPen(QPen(enabled ? colours.onSurfaceVariant
                                : withAlpha(colours.onSurfaceVariant, 0.38),
                        density.dp(2.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(chevron);

    painter.restore();
}

void TreeSelect::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressed_ = fieldRect().contains(event->position());

    View::mousePressEvent(event);
}

void TreeSelect::mouseReleaseEvent(QMouseEvent* event)
{
    const bool inside = fieldRect().contains(event->position());
    const bool activate = pressed_ && inside && event->button() == Qt::LeftButton;
    pressed_ = false;

    View::mouseReleaseEvent(event);

    if (!activate || !isEnabled())
        return;

    if (popupOpen_)
        hidePopup();
    else
        showPopup();
}

void TreeSelect::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        View::keyPressEvent(event);
        return;
    }

    const int rows = static_cast<int>(rows_.size());

    switch (event->key()) {
    case Qt::Key_Down:
        if (!popupOpen_) {
            showPopup();
            if (popupOpen_ && !rows_.isEmpty() && highlighted_ < 0)
                setHighlightedRow(0);
        } else if (rows > 0) {
            setHighlightedRow(highlighted_ < 0 ? 0 : std::min(highlighted_ + 1, rows - 1));
        }
        event->accept();
        return;
    case Qt::Key_Up:
        if (!popupOpen_) {
            showPopup();
            if (popupOpen_ && !rows_.isEmpty())
                setHighlightedRow(static_cast<int>(rows_.size()) - 1);
        } else if (rows > 0) {
            setHighlightedRow(highlighted_ <= 0 ? 0 : highlighted_ - 1);
        }
        event->accept();
        return;
    case Qt::Key_Home:
        if (popupOpen_ && rows > 0)
            setHighlightedRow(0);
        event->accept();
        return;
    case Qt::Key_End:
        if (popupOpen_ && rows > 0)
            setHighlightedRow(rows - 1);
        event->accept();
        return;
    case Qt::Key_Right:
        if (popupOpen_ && highlighted_ >= 0 && highlighted_ < rows) {
            const Row info = rows_.at(highlighted_);
            // A branch expands, or — already expanded — steps into it. A leaf
            // has nothing to open.
            if (info.branch) {
                if (!info.expanded)
                    setExpanded(info.index, true);
                else if (highlighted_ + 1 < rows)
                    setHighlightedRow(highlighted_ + 1);
            }
        }
        event->accept();
        return;
    case Qt::Key_Left:
        if (popupOpen_ && highlighted_ >= 0 && highlighted_ < rows) {
            const Row info = rows_.at(highlighted_);
            if (info.branch && info.expanded) {
                setExpanded(info.index, false);
            } else {
                for (int row = highlighted_ - 1; row >= 0; --row) {
                    if (rows_.at(row).depth == info.depth - 1) {
                        setHighlightedRow(row);
                        break;
                    }
                }
            }
        }
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (!popupOpen_)
            showPopup();
        else if (highlighted_ >= 0)
            activateRow(highlighted_);
        event->accept();
        return;
    case Qt::Key_Escape:
        if (popupOpen_) {
            hidePopup();
            event->accept();
            return;
        }
        break;
    default:
        break;
    }

    View::keyPressEvent(event);
}

void TreeSelect::hideEvent(QHideEvent* event)
{
    View::hideEvent(event);
    hidePopup();
}

void TreeSelect::onThemeChanged()
{
    // The panel's placement is density-dependent: re-run it after a theme
    // switch instead of leaving the old side and offsets behind.
    if (popupOpen_ && dropdown_ != nullptr)
        dropdown_->reposition();

    requestUpdate();
}

bool TreeSelect::focusable() const
{
    return isEnabled();
}

// ---------------------------------------------------------------------------
// Row model
// ---------------------------------------------------------------------------

void TreeSelect::rebuildRows()
{
    rows_.clear();
    appendRows(QModelIndex(), 0, QString());
}

void TreeSelect::appendRows(const QModelIndex& parent, int depth, const QString& parentPath)
{
    const int count = adapter_->rowCount(parent);
    for (int row = 0; row < count; ++row) {
        const QModelIndex index = adapter_->index(row, 0, parent);
        if (!index.isValid())
            continue;

        const QString path = parentPath.isEmpty()
            ? QString::number(row)
            : parentPath + QLatin1Char('/') + QString::number(row);
        const bool branch = adapter_->rowCount(index) > 0;
        const bool expanded = branch && expandedPaths_.contains(path);

        rows_.append(Row{index, depth, branch, expanded});

        if (expanded)
            appendRows(index, depth + 1, path);
    }
}

void TreeSelect::handleStructureChanged()
{
    rebuildRows();
    clampHighlight();

    if (rows_.isEmpty())
        hidePopup();
    else
        syncPopupLayout();

    requestUpdate();
}

void TreeSelect::handleModelReset()
{
    selected_ = QPersistentModelIndex();
    highlighted_ = -1;
    rebuildRows();

    if (rows_.isEmpty())
        hidePopup();
    else
        syncPopupLayout();

    requestUpdate();
}

QString TreeSelect::pathOf(const QModelIndex& index) const
{
    QStringList parts;
    QModelIndex cursor = index;
    while (cursor.isValid()) {
        parts.prepend(QString::number(cursor.row()));
        cursor = cursor.parent();
    }
    return parts.join(QLatin1Char('/'));
}

QModelIndex TreeSelect::proxyIndexFor(const QModelIndex& index) const
{
    if (!index.isValid())
        return {};

    if (index.model() == adapter_)
        return index;

    // A source-model index is accepted as well: the Adapter is an identity
    // proxy, so the row chain is the same. It has to be rebuilt in the proxy's
    // coordinate space, because QIdentityProxyModel asserts on a foreign index.
    if (adapter_->sourceModel() == nullptr || index.model() != adapter_->sourceModel())
        return {};

    return adapter_->index(index.row(), index.column(), proxyIndexFor(index.parent()));
}

QString TreeSelect::labelForIndex(const QModelIndex& index) const
{
    if (!index.isValid())
        return {};

    const QVariant stored = index.data(adapter_->valueRole());

    // Adapter::data() formats a row's value by row number, which is correct for
    // the flat controls but not for a tree (a child row shares its number with
    // a top-level row). The formatter is applied here through the Adapter's own
    // labelFor(), so a formatter still owns the presentation and is always
    // applied to this node's value.
    if (formatterSet_) {
        if (stored.isValid()) {
            const QString formatted = adapter_->labelFor(stored);
            if (!formatted.isEmpty())
                return formatted;
        }
        return sourceTextFor(index);
    }

    const QString text = sourceTextFor(index);
    return text.isEmpty() ? adapter_->labelFor(stored) : text;
}

QString TreeSelect::sourceTextFor(const QModelIndex& index) const
{
    QAbstractItemModel* source = adapter_->sourceModel();
    if (source == nullptr || !index.isValid())
        return {};

    const QModelIndex sourceIndex = adapter_->mapToSource(index);
    if (!sourceIndex.isValid())
        return {};

    return source->data(sourceIndex, Qt::DisplayRole).toString();
}

QVariant TreeSelect::valueForIndex(const QModelIndex& index) const
{
    if (!index.isValid())
        return {};

    const QVariant stored = index.data(adapter_->valueRole());
    if (stored.isValid())
        return stored;

    return sourceTextFor(index);
}

int TreeSelect::rowOfIndex(const QModelIndex& index) const
{
    if (!index.isValid())
        return -1;

    for (int row = 0; row < rows_.size(); ++row) {
        if (rows_.at(row).index == index)
            return row;
    }
    return -1;
}

void TreeSelect::clampHighlight()
{
    if (rows_.isEmpty()) {
        highlighted_ = -1;
        return;
    }

    if (highlighted_ >= rows_.size())
        highlighted_ = static_cast<int>(rows_.size()) - 1;
}

void TreeSelect::ensureHighlightVisible()
{
    if (!popupOpen_ || popup_ == nullptr || highlighted_ < 0)
        return;

    static_cast<TreeContent*>(popup_)->ensureRowVisible(highlighted_);
}

void TreeSelect::restoreFocus()
{
    if (isVisible() && isEnabled())
        setFocus(Qt::PopupFocusReason);
}

void TreeSelect::applySelection(const QModelIndex& index)
{
    if (!index.isValid() || adapter_->rowCount(index) > 0)
        return;

    selected_ = QPersistentModelIndex(index);
    requestUpdate();
    emit selected(selectedValue());
}

void TreeSelect::syncPopupLayout()
{
    if (!popupOpen_ || dropdown_ == nullptr)
        return;

    // The row window (and with it the panel height) may have changed; the
    // Dropdown re-measures the content and re-applies the placement.
    dropdown_->reposition();
}

void TreeSelect::handlePopupClosed()
{
    // The Dropdown dismissed itself (an outside press, Escape, the anchor
    // hiding): mirror that in the control's own state exactly once.
    if (!popupOpen_)
        return;

    popupOpen_ = false;
    requestUpdate();
    restoreFocus();
}

} // namespace md
