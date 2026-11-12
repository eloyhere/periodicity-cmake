// Periodicity — Transfer (media/content batch).
//
// The panes are painted rather than built from widgets: a transfer row is a
// string, so a child View per row would cost more than it buys. Both panes own
// a scroll offset, a selection set and an anchor; the hit test converts a point
// to a row exactly the way the paint path lays it out, and every mutation ends
// in one itemsChanged() emission plus a button-state refresh.
//
// The four buttons are md::Button children, arranged in the middle column and
// wired to the public move methods. Their enabled state mirrors the selection
// so the column reads like a toolbar even before it is clicked.

#include <md/components/transfer.h>

#include <md/components/button.h>
#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>
#include <QSizeF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace md {

namespace {

constexpr qreal kDefaultWidthDp = 520.0;
constexpr qreal kDefaultHeightDp = 280.0;
constexpr qreal kButtonColumnWidthDp = 64.0;
constexpr qreal kColumnGapDp = 12.0;
constexpr qreal kButtonWidthDp = 56.0;
constexpr qreal kButtonHeightDp = 48.0;
constexpr qreal kButtonGapDp = 8.0;
constexpr qreal kPaneRadiusDp = 12.0;
constexpr qreal kHeaderHeightDp = 40.0;
constexpr qreal kRowHeightDp = 36.0;
constexpr qreal kPaneHorizontalInsetDp = 8.0;
constexpr qreal kPaneBottomInsetDp = 8.0;
constexpr qreal kRowTextInsetDp = 12.0;

QFont fontFor(const TextStyle& style, const Density& density, const QWidget* context)
{
    return Font::resolve(context, style, density);
}

QColor withAlpha(QColor colour, qreal alpha)
{
    colour.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return colour;
}

} // namespace

Transfer::Transfer(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));

    const auto makeButton = [this](const QString& text, const QString& tooltip) {
        auto* button = new Button(text);
        button->setVariant(Button::ButtonVariant::Text);
        button->setToolTip(tooltip);
        addView(button);
        return button;
    };

    addButton_ = makeButton(QStringLiteral(">"), QStringLiteral("Add selected"));
    removeButton_ = makeButton(QStringLiteral("<"), QStringLiteral("Remove selected"));
    addAllButton_ = makeButton(QStringLiteral(">>"), QStringLiteral("Add all"));
    removeAllButton_ = makeButton(QStringLiteral("<<"), QStringLiteral("Remove all"));

    connect(addButton_, &Button::clicked, this, &Transfer::moveSelectedRight);
    connect(removeButton_, &Button::clicked, this, &Transfer::moveSelectedLeft);
    connect(addAllButton_, &Button::clicked, this, &Transfer::moveAllRight);
    connect(removeAllButton_, &Button::clicked, this, &Transfer::moveAllLeft);

    syncButtons();
}

void Transfer::setItems(const QStringList& items)
{
    available_ = items;
    selected_.clear();
    availableSelection_.clear();
    selectedSelection_.clear();
    availableAnchor_ = -1;
    selectedAnchor_ = -1;
    availableScroll_ = 0.0;
    selectedScroll_ = 0.0;

    syncButtons();
    emitItemsChanged();
    requestUpdate();
}

void Transfer::addItems(const QStringList& items)
{
    if (items.isEmpty())
        return;

    available_ += items;
    syncButtons();
    emitItemsChanged();
    requestUpdate();
}

void Transfer::addItem(const QString& item)
{
    available_.append(item);
    syncButtons();
    emitItemsChanged();
    requestUpdate();
}

QStringList Transfer::availableItems() const
{
    return available_;
}

QStringList Transfer::selectedItems() const
{
    return selected_;
}

int Transfer::availableCount() const
{
    return static_cast<int>(available_.size());
}

int Transfer::selectedCount() const
{
    return static_cast<int>(selected_.size());
}

void Transfer::setAvailableLabel(const QString& label)
{
    if (availableLabel_ == label)
        return;

    availableLabel_ = label;
    requestUpdate();
}

QString Transfer::availableLabel() const
{
    return availableLabel_;
}

void Transfer::setSelectedLabel(const QString& label)
{
    if (selectedLabel_ == label)
        return;

    selectedLabel_ = label;
    requestUpdate();
}

QString Transfer::selectedLabel() const
{
    return selectedLabel_;
}

void Transfer::selectAvailable(int row, bool extend)
{
    applySelection(Pane::Available, row, extend);
}

void Transfer::selectSelected(int row, bool extend)
{
    applySelection(Pane::Selected, row, extend);
}

QList<int> Transfer::availableSelection() const
{
    QList<int> result = availableSelection_.values();
    std::sort(result.begin(), result.end());
    return result;
}

QList<int> Transfer::selectedSelection() const
{
    QList<int> result = selectedSelection_.values();
    std::sort(result.begin(), result.end());
    return result;
}

void Transfer::clearAvailableSelection()
{
    if (availableSelection_.isEmpty() && availableAnchor_ < 0)
        return;

    availableSelection_.clear();
    availableAnchor_ = -1;
    syncButtons();
    requestUpdate();
}

void Transfer::clearSelectedSelection()
{
    if (selectedSelection_.isEmpty() && selectedAnchor_ < 0)
        return;

    selectedSelection_.clear();
    selectedAnchor_ = -1;
    syncButtons();
    requestUpdate();
}

void Transfer::moveSelectedRight()
{
    moveSelected(Pane::Available);
}

void Transfer::moveSelectedLeft()
{
    moveSelected(Pane::Selected);
}

void Transfer::moveAllRight()
{
    moveAll(Pane::Available);
}

void Transfer::moveAllLeft()
{
    moveAll(Pane::Selected);
}

qreal Transfer::availableScrollOffset() const
{
    return availableScroll_;
}

void Transfer::setAvailableScrollOffset(qreal offset)
{
    const qreal clamped =
        std::clamp(offset, qreal(0.0), std::max(qreal(0.0), maximumScroll(Pane::Available)));
    if (qFuzzyCompare(availableScroll_ + 1.0, clamped + 1.0))
        return;

    availableScroll_ = clamped;
    requestUpdate();
}

qreal Transfer::selectedScrollOffset() const
{
    return selectedScroll_;
}

void Transfer::setSelectedScrollOffset(qreal offset)
{
    const qreal clamped =
        std::clamp(offset, qreal(0.0), std::max(qreal(0.0), maximumScroll(Pane::Selected)));
    if (qFuzzyCompare(selectedScroll_ + 1.0, clamped + 1.0))
        return;

    selectedScroll_ = clamped;
    requestUpdate();
}

void Transfer::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(
        QSizeF(width.resolve(density.dp(kDefaultWidthDp)),
               height.resolve(density.dp(kDefaultHeightDp))));
}

void Transfer::arrangeChildren(const QRectF& content)
{
    const Density& density = Theme::forWidget(this).density();
    const QRectF column = buttonColumnRect();

    const qreal buttonWidth = std::min(density.dp(kButtonWidthDp), column.width());
    const qreal buttonHeight = density.dp(kButtonHeightDp);
    const qreal gap = density.dp(kButtonGapDp);

    Button* buttons[4] = {addButton_, removeButton_, addAllButton_, removeAllButton_};
    const qreal totalHeight = buttonHeight * 4.0 + gap * 3.0;
    qreal cursor = content.top() + std::max(qreal(0.0), (content.height() - totalHeight) / 2.0);

    for (Button* button : buttons) {
        if (button == nullptr)
            continue;

        const qreal x = column.left() + (column.width() - buttonWidth) / 2.0;
        button->measure(MeasureSpec::exactly(buttonWidth), MeasureSpec::exactly(buttonHeight));
        button->arrange(QRectF(x, cursor, buttonWidth, buttonHeight).toAlignedRect());
        cursor += buttonHeight + gap;
    }

    clampScroll();
    requestUpdate();
}

void Transfer::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    paintPane(painter, Pane::Available, paneRect(Pane::Available));
    paintPane(painter, Pane::Selected, paneRect(Pane::Selected));
}

void Transfer::mousePressEvent(QMouseEvent* event)
{
    ViewGroup::mousePressEvent(event);

    if (event->button() != Qt::LeftButton || !isEnabled())
        return;

    setFocus(Qt::MouseFocusReason);

    const QPointF position = event->position();
    if (paneRect(Pane::Available).contains(position)) {
        focusPane(Pane::Available);
        handlePaneClick(Pane::Available, position, event->modifiers());
    } else if (paneRect(Pane::Selected).contains(position)) {
        focusPane(Pane::Selected);
        handlePaneClick(Pane::Selected, position, event->modifiers());
    }
}

void Transfer::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !isEnabled()) {
        event->ignore();
        return;
    }

    const QPointF position = event->position();
    if (const int row = rowAt(Pane::Available, position); row >= 0) {
        selectAvailable(row, false);
        moveSelectedRight();
        event->accept();
        return;
    }

    if (const int row = rowAt(Pane::Selected, position); row >= 0) {
        selectSelected(row, false);
        moveSelectedLeft();
        event->accept();
        return;
    }

    event->ignore();
}

void Transfer::wheelEvent(QWheelEvent* event)
{
    const qreal steps = static_cast<qreal>(event->angleDelta().y()) / 120.0;
    if (qFuzzyIsNull(steps)) {
        event->ignore();
        return;
    }

    const QPointF position = event->position();
    if (paneRect(Pane::Available).contains(position))
        scrollPane(Pane::Available, -steps * rowHeightPx() * 3.0);
    else if (paneRect(Pane::Selected).contains(position))
        scrollPane(Pane::Selected, -steps * rowHeightPx() * 3.0);
    else {
        event->ignore();
        return;
    }

    event->accept();
}

void Transfer::keyPressEvent(QKeyEvent* event)
{
    const int count = static_cast<int>(itemsFor(focusedPane_).size());

    if (event->modifiers().testFlag(Qt::ControlModifier) && event->key() == Qt::Key_A) {
        QSet<int>& selection = selectionFor(focusedPane_);
        selection.clear();
        for (int row = 0; row < count; ++row)
            selection.insert(row);
        anchorFor(focusedPane_) = count > 0 ? 0 : -1;
        syncButtons();
        requestUpdate();
        event->accept();
        return;
    }

    switch (event->key()) {
    case Qt::Key_Right:
        moveSelectedRight();
        event->accept();
        return;
    case Qt::Key_Left:
        moveSelectedLeft();
        event->accept();
        return;
    default:
        break;
    }

    if (count == 0) {
        ViewGroup::keyPressEvent(event);
        return;
    }

    const QSet<int>& selection = selectionFor(focusedPane_);
    int current = anchorFor(focusedPane_);
    if (current < 0 && !selection.isEmpty())
        current = *selection.begin();

    int target = current;
    switch (event->key()) {
    case Qt::Key_Up:
        target = current <= 0 ? 0 : current - 1;
        break;
    case Qt::Key_Down:
        target = current < 0 ? 0 : std::min(count - 1, current + 1);
        break;
    case Qt::Key_Home:
        target = 0;
        break;
    case Qt::Key_End:
        target = count - 1;
        break;
    default:
        ViewGroup::keyPressEvent(event);
        return;
    }

    applySelection(focusedPane_, target, false);
    event->accept();
}

bool Transfer::focusable() const
{
    return isEnabled();
}

QRectF Transfer::paneRect(Pane pane) const
{
    const QRectF content = contentRect();
    const Density& density = Theme::forWidget(this).density();
    const qreal column = density.dp(kButtonColumnWidthDp);
    const qreal gap = density.dp(kColumnGapDp);
    const qreal usable = std::max(qreal(0.0), content.width() - column - gap * 2.0);
    const qreal paneWidth = usable / 2.0;

    if (pane == Pane::Available)
        return QRectF(content.left(), content.top(), paneWidth, content.height());

    return QRectF(content.right() - paneWidth, content.top(), paneWidth, content.height());
}

QRectF Transfer::buttonColumnRect() const
{
    const QRectF content = contentRect();
    const Density& density = Theme::forWidget(this).density();
    const qreal column = density.dp(kButtonColumnWidthDp);
    const qreal gap = density.dp(kColumnGapDp);
    const QRectF availablePane = paneRect(Pane::Available);

    return QRectF(availablePane.right() + gap, content.top(), column, content.height());
}

qreal Transfer::rowHeightPx() const
{
    return Theme::forWidget(this).density().dp(kRowHeightDp);
}

qreal Transfer::headerHeightPx() const
{
    return Theme::forWidget(this).density().dp(kHeaderHeightDp);
}

qreal Transfer::paneViewportHeight(const QRectF& pane) const
{
    const qreal inset = Theme::forWidget(this).density().dp(kPaneBottomInsetDp);
    return std::max(qreal(0.0), pane.height() - headerHeightPx() - inset);
}

int Transfer::rowAt(Pane pane, const QPointF& position) const
{
    const QRectF paneGeometry = paneRect(pane);
    if (paneGeometry.width() <= 0.0 || paneGeometry.height() <= 0.0)
        return -1;

    const Density& density = Theme::forWidget(this).density();
    const qreal inset = density.dp(kPaneHorizontalInsetDp);
    const qreal rowsTop = paneGeometry.top() + headerHeightPx();
    const qreal rowsBottom = paneGeometry.bottom() - density.dp(kPaneBottomInsetDp);

    if (position.x() < paneGeometry.left() + inset
        || position.x() > paneGeometry.right() - inset) {
        return -1;
    }

    if (position.y() < rowsTop || position.y() > rowsBottom)
        return -1;

    const qreal rowHeight = rowHeightPx();
    if (rowHeight <= 0.0)
        return -1;

    const qreal scroll = pane == Pane::Available ? availableScroll_ : selectedScroll_;
    const int row = static_cast<int>(std::floor((position.y() - rowsTop + scroll) / rowHeight));
    const int count = static_cast<int>(itemsFor(pane).size());

    if (row < 0 || row >= count)
        return -1;

    return row;
}

qreal Transfer::maximumScroll(Pane pane) const
{
    const qreal total = static_cast<qreal>(itemsFor(pane).size()) * rowHeightPx();
    return std::max(qreal(0.0), total - paneViewportHeight(paneRect(pane)));
}

const QStringList& Transfer::itemsFor(Pane pane) const
{
    return pane == Pane::Available ? available_ : selected_;
}

QStringList& Transfer::itemsFor(Pane pane)
{
    return pane == Pane::Available ? available_ : selected_;
}

const QSet<int>& Transfer::selectionFor(Pane pane) const
{
    return pane == Pane::Available ? availableSelection_ : selectedSelection_;
}

QSet<int>& Transfer::selectionFor(Pane pane)
{
    return pane == Pane::Available ? availableSelection_ : selectedSelection_;
}

int& Transfer::anchorFor(Pane pane)
{
    return pane == Pane::Available ? availableAnchor_ : selectedAnchor_;
}

void Transfer::applySelection(Pane pane, int row, bool extend)
{
    const int count = static_cast<int>(itemsFor(pane).size());
    if (row < 0 || row >= count)
        return;

    QSet<int>& selection = selectionFor(pane);
    int& anchor = anchorFor(pane);

    selection.clear();
    if (extend && anchor >= 0 && anchor < count) {
        const int first = std::min(anchor, row);
        const int last = std::max(anchor, row);
        for (int index = first; index <= last; ++index)
            selection.insert(index);
    } else {
        selection.insert(row);
        anchor = row;
    }

    syncButtons();
    requestUpdate();
}

void Transfer::handlePaneClick(Pane pane, const QPointF& position,
                               Qt::KeyboardModifiers modifiers)
{
    const int row = rowAt(pane, position);
    if (row < 0) {
        QSet<int>& selection = selectionFor(pane);
        if (!selection.isEmpty() || anchorFor(pane) >= 0) {
            selection.clear();
            anchorFor(pane) = -1;
            syncButtons();
            requestUpdate();
        }
        return;
    }

    const int count = static_cast<int>(itemsFor(pane).size());
    QSet<int>& selection = selectionFor(pane);
    int& anchor = anchorFor(pane);

    if (modifiers.testFlag(Qt::ShiftModifier) && anchor >= 0 && anchor < count) {
        selection.clear();
        const int first = std::min(anchor, row);
        const int last = std::max(anchor, row);
        for (int index = first; index <= last; ++index)
            selection.insert(index);
    } else if (modifiers.testFlag(Qt::ControlModifier)) {
        if (selection.contains(row))
            selection.remove(row);
        else
            selection.insert(row);
        anchor = row;
    } else {
        selection.clear();
        selection.insert(row);
        anchor = row;
    }

    syncButtons();
    requestUpdate();
}

void Transfer::scrollPane(Pane pane, qreal delta)
{
    const qreal maximum = std::max(qreal(0.0), maximumScroll(pane));
    qreal& offset = pane == Pane::Available ? availableScroll_ : selectedScroll_;
    const qreal clamped = std::clamp(offset + delta, qreal(0.0), maximum);

    if (qFuzzyCompare(offset + 1.0, clamped + 1.0))
        return;

    offset = clamped;
    requestUpdate();
}

void Transfer::clampScroll()
{
    availableScroll_ = std::clamp(availableScroll_, qreal(0.0),
                                  std::max(qreal(0.0), maximumScroll(Pane::Available)));
    selectedScroll_ = std::clamp(selectedScroll_, qreal(0.0),
                                 std::max(qreal(0.0), maximumScroll(Pane::Selected)));
}

void Transfer::syncButtons()
{
    if (addButton_ == nullptr)
        return;

    addButton_->setEnabled(!availableSelection_.isEmpty());
    removeButton_->setEnabled(!selectedSelection_.isEmpty());
    addAllButton_->setEnabled(!available_.isEmpty());
    removeAllButton_->setEnabled(!selected_.isEmpty());
}

void Transfer::emitItemsChanged()
{
    emit itemsChanged(available_, selected_);
    syncButtons();
}

void Transfer::moveSelected(Pane from)
{
    const Pane targetPane = from == Pane::Available ? Pane::Selected : Pane::Available;
    const QList<int> rows = from == Pane::Available ? availableSelection() : selectedSelection();
    if (rows.isEmpty())
        return;

    QStringList& source = itemsFor(from);
    QStringList& target = itemsFor(targetPane);

    for (const int row : rows) {
        if (row >= 0 && row < static_cast<int>(source.size()))
            target.append(source.at(row));
    }

    for (int index = static_cast<int>(rows.size()) - 1; index >= 0; --index) {
        const int row = rows.at(index);
        if (row >= 0 && row < static_cast<int>(source.size()))
            source.removeAt(row);
    }

    selectionFor(from).clear();
    anchorFor(from) = -1;

    clampScroll();
    emitItemsChanged();
    requestUpdate();
}

void Transfer::moveAll(Pane from)
{
    const Pane targetPane = from == Pane::Available ? Pane::Selected : Pane::Available;
    QStringList& source = itemsFor(from);

    if (source.isEmpty())
        return;

    QStringList& target = itemsFor(targetPane);
    target += source;
    source.clear();

    selectionFor(from).clear();
    anchorFor(from) = -1;

    clampScroll();
    emitItemsChanged();
    requestUpdate();
}

void Transfer::paintPane(QPainter& painter, Pane pane, const QRectF& paneGeometry)
{
    if (paneGeometry.width() <= 0.0 || paneGeometry.height() <= 0.0)
        return;

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();
    const QStringList& items = itemsFor(pane);
    const QSet<int>& selection = selectionFor(pane);
    const qreal scroll = pane == Pane::Available ? availableScroll_ : selectedScroll_;
    const bool focused = hasFocus() && focusedPane_ == pane;

    const QPainterPath outline =
        Shape::rounded(CornerRadii::uniform(density.dp(kPaneRadiusDp))).path(paneGeometry);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    painter.setPen(Qt::NoPen);
    painter.setBrush(colours.surfaceContainerLow);
    painter.drawPath(outline);

    // M3 outlined containers carry a 1 dp rest outline and a 2 dp focus outline
    // in the primary role. The focus stroke used to be 1.6 dp, an off-scale
    // width that rendered as a fuzzy 2 px edge at fractional densities; the
    // path is inset by half the stroke so the whole stroke lands inside the
    // pane instead of being clipped by the widget bounds.
    const qreal stroke = density.dp(focused ? 2.0 : 1.0);
    const qreal inset = stroke / 2.0;
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(focused ? colours.primary : colours.outlineVariant, stroke,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(
        Shape::rounded(CornerRadii::uniform(density.dp(kPaneRadiusDp)))
            .path(paneGeometry.adjusted(inset, inset, -inset, -inset)));

    // Header: label plus the live count.
    const qreal header = headerHeightPx();
    const QString label = (pane == Pane::Available ? availableLabel_ : selectedLabel_)
                          + QStringLiteral(" (") + QString::number(items.size())
                          + QLatin1Char(')');
    const qreal headerInset = density.dp(kRowTextInsetDp);
    const QRectF headerRect(paneGeometry.left() + headerInset, paneGeometry.top(),
                            std::max(qreal(0.0), paneGeometry.width() - headerInset * 2.0),
                            header);

    painter.setFont(fontFor(theme.type().labelLarge, density, this));
    painter.setPen(colours.onSurfaceVariant);
    const QString elidedHeader =
        QFontMetricsF(fontFor(theme.type().labelLarge, density, this))
            .elidedText(label, Qt::ElideRight, headerRect.width());
    painter.drawText(headerRect, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                     elidedHeader);

    const qreal separatorY = paneGeometry.top() + header;
    painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
    painter.drawLine(QPointF(paneGeometry.left() + headerInset, separatorY),
                     QPointF(paneGeometry.right() - headerInset, separatorY));

    // Rows, clipped to the viewport above the bottom inset.
    const qreal bottomInset = density.dp(kPaneBottomInsetDp);
    const QRectF viewport(paneGeometry.left(), separatorY, paneGeometry.width(),
                          std::max(qreal(0.0), paneGeometry.height() - header - bottomInset));
    const qreal rowInset = density.dp(kPaneHorizontalInsetDp);
    const qreal rowHeight = rowHeightPx();

    painter.save();
    painter.setClipRect(viewport);

    const QFont rowFont = fontFor(theme.type().bodyMedium, density, this);
    const QFontMetricsF rowMetrics(rowFont);
    painter.setFont(rowFont);

    for (int row = 0; row < static_cast<int>(items.size()); ++row) {
        const QRectF rowRect(paneGeometry.left() + rowInset,
                             separatorY + static_cast<qreal>(row) * rowHeight - scroll,
                             std::max(qreal(0.0), paneGeometry.width() - rowInset * 2.0),
                             rowHeight);

        if (rowRect.bottom() < viewport.top())
            continue;
        if (rowRect.top() > viewport.bottom())
            break;

        const bool isSelected = selection.contains(row);
        if (isSelected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.secondaryContainer);
            painter.drawRoundedRect(rowRect.adjusted(0.0, density.dp(2.0), 0.0,
                                                     -density.dp(2.0)),
                                    density.dp(8.0), density.dp(8.0));
        }

        const QRectF textRect = rowRect.adjusted(density.dp(kRowTextInsetDp), 0.0,
                                                 -density.dp(kRowTextInsetDp), 0.0);
        const QString text = rowMetrics.elidedText(items.at(row), Qt::ElideRight,
                                                   std::max(qreal(0.0), textRect.width()));
        painter.setPen(isSelected ? colours.onSecondaryContainer : colours.onSurface);
        painter.drawText(textRect, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), text);
    }

    if (items.isEmpty()) {
        painter.setFont(fontFor(theme.type().bodyMedium, density, this));
        painter.setPen(withAlpha(colours.onSurfaceVariant, 0.6));
        painter.drawText(viewport, static_cast<int>(Qt::AlignCenter),
                         localized(QStringLiteral("transfer.empty"),
                                   QStringLiteral("No items")));
    }

    painter.restore();

    // A slim scroll indicator when the content overflows.
    const qreal maximum = std::max(qreal(0.0), maximumScroll(pane));
    if (maximum > 0.0 && viewport.height() > 0.0) {
        const qreal thumbHeight =
            std::max(density.dp(24.0),
                     viewport.height() * viewport.height() / (viewport.height() + maximum));
        const qreal travel = std::max(qreal(0.0), viewport.height() - thumbHeight);
        const qreal position = travel * std::clamp(scroll / maximum, 0.0, 1.0);

        painter.setPen(Qt::NoPen);
        painter.setBrush(withAlpha(colours.outlineVariant, 0.8));
        painter.drawRoundedRect(
            QRectF(paneGeometry.right() - density.dp(6.0), viewport.top() + position,
                   density.dp(3.0), thumbHeight),
            density.dp(1.5), density.dp(1.5));
    }

    painter.restore();
}

void Transfer::focusPane(Pane pane)
{
    if (focusedPane_ == pane)
        return;

    focusedPane_ = pane;
    requestUpdate();
}

} // namespace md
