// Periodicity — VirtualTreeSelect, a tree menu over a VirtualTreeSource.
//
// The field and the menu are painted here; the menu is a child of the window,
// like TreeSelect's. What changes is the data path: the source is asked for its
// roots, and then for the children of a branch only when that branch is
// expanded. Nothing below a collapsed branch is ever read, so a lazily built
// hierarchy stays cheap no matter how large it is.
//
// Expansion state is a set of node values (the key the source itself chose),
// which is why a rebuilt row list keeps the same open branches: positions would
// not survive an insert, a value does. The label of every visible row is read
// once per rebuild — the rows below a collapsed branch are never labelled.

#include <md/components/dropdown.h>
#include <md/components/virtual_tree_select.h>

#include <md/core/geometry.h>
#include <md/theme/theme.h>

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

namespace md {

namespace {

constexpr qreal kFieldHeightDp = 56.0;
constexpr qreal kHorizontalPaddingDp = 16.0;
constexpr qreal kFieldRadiusDp = 4.0;
// The design baseline menu container is the extra-small shape (4 dp); the popup used 8 dp,
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

QPainterPath fieldPath(const QRectF& bounds, qreal radius)
{
    return CornerRadii{radius, radius, 0.0, 0.0}.toPath(bounds);
}

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
// VirtualTreeContent — the rows of the menu. The surface (placement, flipping,
// clamping, the layer, dismissal) belongs to md::Dropdown; the rows, the scroll
// window, the expansion rule and the selection rule stay in the control.
// ---------------------------------------------------------------------------
class VirtualTreeContent final : public View
{
public:
    explicit VirtualTreeContent(VirtualTreeSelect* owner)
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

    VirtualTreeSelect* owner_ = nullptr;
    int firstRow_ = 0;
};

// ---------------------------------------------------------------------------
// VirtualTreeSelect
// ---------------------------------------------------------------------------

VirtualTreeSelect::VirtualTreeSelect(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));
    setShape(Theme::forWidget(this).shapes().extraSmall);
    requestUpdate();
}

VirtualTreeSelect::~VirtualTreeSelect()
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

void VirtualTreeSelect::setSource(structure::VirtualTreeSource* source)
{
    if (source_ == source)
        return;

    hidePopup();
    source_ = source;
    expandedKeys_.clear();
    clearSelection();
    highlighted_ = -1;

    rebuildRows();
    requestUpdate();
    updateGeometry();
}

structure::VirtualTreeSource* VirtualTreeSelect::source() const noexcept
{
    return source_;
}

void VirtualTreeSelect::setPlaceholderText(const QString& text)
{
    if (placeholder_ == text)
        return;

    placeholder_ = text;
    requestUpdate();
}

QString VirtualTreeSelect::placeholderText() const
{
    return placeholder_;
}

void VirtualTreeSelect::setMaxVisibleRows(int rows)
{
    const int clamped = std::max(1, rows);
    if (maxVisibleRows_ == clamped)
        return;

    maxVisibleRows_ = clamped;
    syncPopupLayout();
    ensureHighlightVisible();
    requestUpdate();
}

int VirtualTreeSelect::maxVisibleRows() const noexcept
{
    return maxVisibleRows_;
}

bool VirtualTreeSelect::isExpanded(NodeId node) const
{
    if (source_ == nullptr)
        return false;

    return expandedKeys_.contains(keyOf(node));
}

void VirtualTreeSelect::setExpanded(NodeId node, bool expanded)
{
    if (source_ == nullptr)
        return;

    if (expanded && source_->childCount(node) <= 0)
        return;

    const QVariant key = keyOf(node);
    if (expandedKeys_.contains(key) == expanded)
        return;

    if (expanded)
        expandedKeys_.append(key);
    else
        expandedKeys_.removeOne(key);

    rebuildRows();
    clampHighlight();
    syncPopupLayout();
    requestUpdate();

    emit expandedChanged(node, expanded);
}

int VirtualTreeSelect::visibleRowCount() const
{
    return static_cast<int>(rows_.size());
}

int VirtualTreeSelect::paintedRowCount() const
{
    return std::min(maxVisibleRows_, static_cast<int>(rows_.size()));
}

VirtualTreeSelect::NodeId VirtualTreeSelect::rowNode(int row) const
{
    if (row < 0 || row >= rows_.size())
        return 0;

    return rows_.at(row).node;
}

int VirtualTreeSelect::rowDepth(int row) const
{
    if (row < 0 || row >= rows_.size())
        return 0;

    return rows_.at(row).depth;
}

QString VirtualTreeSelect::rowLabel(int row) const
{
    if (row < 0 || row >= rows_.size())
        return {};

    return rows_.at(row).label;
}

bool VirtualTreeSelect::rowHasChildren(int row) const
{
    if (row < 0 || row >= rows_.size())
        return false;

    return rows_.at(row).branch;
}

bool VirtualTreeSelect::rowIsExpanded(int row) const
{
    if (row < 0 || row >= rows_.size())
        return false;

    return rows_.at(row).expanded;
}

bool VirtualTreeSelect::rowIsSelectable(int row) const
{
    return row >= 0 && row < rows_.size() && !rows_.at(row).branch;
}

bool VirtualTreeSelect::rowIsSelected(int row) const
{
    if (row < 0 || row >= rows_.size() || !hasSelection_)
        return false;

    return rows_.at(row).node == selectedNode_;
}

int VirtualTreeSelect::rowOfNode(NodeId node) const
{
    return rowOfNodeInternal(node);
}

bool VirtualTreeSelect::hasSelection() const
{
    return hasSelection_;
}

QVariant VirtualTreeSelect::selectedValue() const
{
    return selectedValue_;
}

QString VirtualTreeSelect::currentText() const
{
    return hasSelection_ ? selectedLabel_ : placeholder_;
}

QStringList VirtualTreeSelect::selectedPath() const
{
    return selectedPath_;
}

int VirtualTreeSelect::highlightedRow() const noexcept
{
    return highlighted_;
}

void VirtualTreeSelect::setHighlightedRow(int row)
{
    const int clamped = rows_.isEmpty() ? -1 : std::min(row, static_cast<int>(rows_.size()) - 1);
    if (clamped == highlighted_)
        return;

    highlighted_ = clamped;
    ensureHighlightVisible();
    requestUpdate();
}

void VirtualTreeSelect::activateRow(int row)
{
    if (row < 0 || row >= rows_.size())
        return;

    const Row info = rows_.at(row);
    setHighlightedRow(row);

    if (info.branch) {
        setExpanded(info.node, !info.expanded);
        return;
    }

    hasSelection_ = true;
    selectedNode_ = info.node;
    selectedValue_ = source_ != nullptr ? source_->value(info.node) : QVariant();
    selectedLabel_ = info.label;
    selectedPath_ = pathForRow(row);

    requestUpdate();
    emit selected(selectedValue_);
    hidePopup();
}

int VirtualTreeSelect::rowAt(const QPointF& popupPoint) const
{
    if (!popupOpen_ || popup_ == nullptr)
        return -1;

    return static_cast<VirtualTreeContent*>(popup_)->rowAtPoint(popupPoint);
}

QRectF VirtualTreeSelect::rowRect(int row) const
{
    if (!popupOpen_ || popup_ == nullptr)
        return {};

    return static_cast<VirtualTreeContent*>(popup_)->rowRectFor(row);
}

QRectF VirtualTreeSelect::fieldRect() const
{
    const Density& density = Theme::forWidget(this).density();
    const qreal fieldHeight = std::min(qreal(height()), density.dp(kFieldHeightDp));
    return QRectF(0.0, 0.0, qreal(width()), fieldHeight);
}

void VirtualTreeSelect::showPopup()
{
    if (!isEnabled() || rows_.isEmpty())
        return;

    if (dropdown_ == nullptr) {
        auto* content = new VirtualTreeContent(this);
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
        const int selectedRow = hasSelection_ ? rowOfNodeInternal(selectedNode_) : -1;
        highlighted_ = selectedRow >= 0 ? selectedRow : -1;
    }

    popupOpen_ = true;

    auto* content = static_cast<VirtualTreeContent*>(popup_);
    content->ensureRowVisible(highlighted_);

    // The Dropdown owns the placement: it measures from the anchor's global
    // rectangle, keeps the panel as wide as the field and flips it above when
    // the screen has no room below.
    dropdown_->open();
    content->setFocus(Qt::PopupFocusReason);
    requestUpdate();
}

void VirtualTreeSelect::hidePopup()
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

bool VirtualTreeSelect::isPopupVisible() const
{
    return popupOpen_ && dropdown_ != nullptr && dropdown_->isOpen();
}

QWidget* VirtualTreeSelect::popupWidget() const noexcept
{
    return popup_;
}

Dropdown* VirtualTreeSelect::dropdown() const noexcept
{
    return dropdown_;
}

void VirtualTreeSelect::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(240.0)),
                           height.resolve(density.dp(kFieldHeightDp))));
}

void VirtualTreeSelect::paintContent(QPainter& painter, const QRectF& content)
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
    const bool showingPlaceholder = !hasSelection_;
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

    const QString label = currentText();
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

void VirtualTreeSelect::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressed_ = fieldRect().contains(event->position());

    View::mousePressEvent(event);
}

void VirtualTreeSelect::mouseReleaseEvent(QMouseEvent* event)
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

void VirtualTreeSelect::keyPressEvent(QKeyEvent* event)
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
                    setExpanded(info.node, true);
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
                setExpanded(info.node, false);
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

void VirtualTreeSelect::hideEvent(QHideEvent* event)
{
    View::hideEvent(event);
    hidePopup();
}

void VirtualTreeSelect::onThemeChanged()
{
    // The panel's placement is density-dependent: re-run it after a theme
    // switch instead of leaving the old side and offsets behind.
    if (popupOpen_ && dropdown_ != nullptr)
        dropdown_->reposition();

    requestUpdate();
}

bool VirtualTreeSelect::focusable() const
{
    return isEnabled();
}

// ---------------------------------------------------------------------------
// The lazily built row list
// ---------------------------------------------------------------------------

void VirtualTreeSelect::rebuildRows()
{
    rows_.clear();
    if (source_ == nullptr)
        return;

    const int roots = source_->rootCount();
    for (int index = 0; index < roots; ++index)
        appendNode(source_->rootAt(index), 0);
}

void VirtualTreeSelect::appendNode(NodeId node, int depth)
{
    // Only what is needed: the branch flag of this node, its label, and — when
    // it is expanded — its children. A collapsed subtree is never read.
    const bool branch = source_->hasChildren(node);
    const bool expanded = branch && expandedKeys_.contains(keyOf(node));

    rows_.append(Row{node, depth, branch, expanded, source_->label(node)});

    if (!expanded)
        return;

    const int children = source_->childCount(node);
    for (int index = 0; index < children; ++index)
        appendNode(source_->childAt(node, index), depth + 1);
}

QVariant VirtualTreeSelect::keyOf(NodeId node) const
{
    if (source_ == nullptr)
        return {};

    const QVariant value = source_->value(node);
    if (value.isValid())
        return value;

    // A source that reports no value for a node still needs a stable key; fall
    // back to the node id itself.
    return QVariant::fromValue<qulonglong>(static_cast<qulonglong>(node));
}

QStringList VirtualTreeSelect::pathForRow(int row) const
{
    if (row < 0 || row >= rows_.size())
        return {};

    QStringList path;
    int depth = rows_.at(row).depth;
    path.prepend(rows_.at(row).label);

    for (int cursor = row - 1; cursor >= 0 && depth > 0; --cursor) {
        if (rows_.at(cursor).depth == depth - 1) {
            path.prepend(rows_.at(cursor).label);
            --depth;
        }
    }

    return path;
}

int VirtualTreeSelect::rowOfNodeInternal(NodeId node) const
{
    for (int row = 0; row < rows_.size(); ++row) {
        if (rows_.at(row).node == node)
            return row;
    }
    return -1;
}

void VirtualTreeSelect::clampHighlight()
{
    if (rows_.isEmpty()) {
        highlighted_ = -1;
        return;
    }

    if (highlighted_ >= rows_.size())
        highlighted_ = static_cast<int>(rows_.size()) - 1;
}

void VirtualTreeSelect::ensureHighlightVisible()
{
    if (!popupOpen_ || popup_ == nullptr || highlighted_ < 0)
        return;

    static_cast<VirtualTreeContent*>(popup_)->ensureRowVisible(highlighted_);
}

void VirtualTreeSelect::restoreFocus()
{
    if (isVisible() && isEnabled())
        setFocus(Qt::PopupFocusReason);
}

void VirtualTreeSelect::clearSelection()
{
    hasSelection_ = false;
    selectedNode_ = 0;
    selectedValue_ = QVariant();
    selectedLabel_.clear();
    selectedPath_.clear();
}

void VirtualTreeSelect::syncPopupLayout()
{
    if (!popupOpen_ || dropdown_ == nullptr)
        return;

    // The row window (and with it the panel height) may have changed; the
    // Dropdown re-measures the content and re-applies the placement.
    dropdown_->reposition();
}

void VirtualTreeSelect::handlePopupClosed()
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
