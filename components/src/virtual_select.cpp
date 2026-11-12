// Periodicity — VirtualSelect, a dropdown over a VirtualSource.
//
// The field and the menu are painted here; the menu is a child of the window
// (like TreeSelect's), not a Qt::Popup.
//
// The data path is the point of this control. The menu never asks a
// VirtualSource for "all items": it asks for the labels of the rows inside the
// painted window plus a small prefetch margin, and it keeps those labels in a
// cache that lasts for one menu session. A source with a million rows is
// painted by the same number of calls as one with ten. Filtering is the single
// exception — it has to read every label, because a virtual source offers no
// other way to match — and it is off unless setFilterable(true) is used.
//
// Scrolling belongs to the menu: the wheel, the draggable scrollbar and
// PageUp/PageDown move the painted window, while up/down/Home/End move the
// highlight and keep it visible. Enter commits the highlight.

#include <md/components/dropdown.h>
#include <md/components/virtual_select.h>

#include <md/core/geometry.h>
#include <md/theme/theme.h>

#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
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
// The M3 menu container is the extra-small shape (4 dp); the popup used 8 dp,
// which read as a card rather than a menu surface.
constexpr qreal kPopupRadiusDp = 4.0;
constexpr qreal kPopupPaddingDp = 4.0;
constexpr qreal kPopupWidthDp = 200.0;
constexpr qreal kRowHeightDp = 44.0;
constexpr qreal kScrollBarWidthDp = 6.0;
constexpr int kPrefetchRows = 2;

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

} // namespace

// ---------------------------------------------------------------------------
// ListContent — the rows of the menu, the scroll window and the scrollbar.
//
// The surface (placement, flipping, clamping, the layer, dismissal) belongs to
// md::Dropdown; this widget is the content inside it. It holds no labels: it
// asks the control for the label of a position, which is what fills (and
// bounds) the label cache.
// ---------------------------------------------------------------------------
class ListContent final : public View
{
public:
    explicit ListContent(VirtualSelect* owner)
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
    // measurement, which stops being true as rows scroll or a filter narrows the
    // list.
    [[nodiscard]] QSize sizeHint() const override { return naturalSize().toSize(); }

    [[nodiscard]] int pageRows() const
    {
        return std::max(0, std::min(owner_->maxVisibleRows(), owner_->filteredCount()));
    }

    [[nodiscard]] int maxFirstRow() const
    {
        return std::max(0, owner_->filteredCount() - pageRows());
    }

    [[nodiscard]] bool scrollable() const
    {
        return owner_->filteredCount() > pageRows();
    }

    void setFirstRow(int row)
    {
        const int clamped = std::clamp(row, 0, maxFirstRow());
        if (clamped == firstRow_)
            return;

        firstRow_ = clamped;
        update();
    }

    void ensurePositionVisible(int position)
    {
        if (position < 0)
            return;

        const int page = std::max(1, pageRows());
        if (position < firstRow_)
            firstRow_ = position;
        else if (position >= firstRow_ + page)
            firstRow_ = position - page + 1;

        setFirstRow(firstRow_);
    }

    [[nodiscard]] QRectF rowRectFor(int visibleRow) const
    {
        if (visibleRow < 0 || visibleRow >= pageRows())
            return {};

        const Density& density = Theme::forWidget(owner_).density();
        const qreal padding = density.dp(kPopupPaddingDp);
        const qreal rowHeight = density.dp(kRowHeightDp);
        const qreal rightInset = scrollable() ? density.dp(kScrollBarWidthDp) + padding : 0.0;
        const qreal width = std::max(qreal(0.0), qreal(this->width()) - padding * 2.0 - rightInset);
        return QRectF(padding, padding + qreal(visibleRow) * rowHeight, width, rowHeight);
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

        const int visibleRow = static_cast<int>(std::floor(local / rowHeight));
        if (visibleRow < 0 || visibleRow >= pageRows())
            return -1;

        return visibleRow;
    }

    [[nodiscard]] QRectF scrollBarRect() const
    {
        const Density& density = Theme::forWidget(owner_).density();
        const qreal padding = density.dp(kPopupPaddingDp);
        const qreal track = density.dp(kScrollBarWidthDp);
        const qreal height = std::max(qreal(0.0), qreal(this->height()) - padding * 2.0);
        return QRectF(qreal(this->width()) - padding - track, padding, track, height);
    }

    [[nodiscard]] QRectF scrollBarThumbRect() const
    {
        const QRectF bar = scrollBarRect();
        const int total = owner_->filteredCount();
        const int page = pageRows();
        if (total <= page || bar.height() <= 0.0)
            return {};

        const qreal fraction = qreal(page) / qreal(total);
        const qreal density24 = Theme::forWidget(owner_).density().dp(24.0);
        const qreal thumbHeight = std::min(bar.height(), std::max(density24, bar.height() * fraction));
        const int maxFirst = maxFirstRow();
        const qreal travel = bar.height() - thumbHeight;
        const qreal offset = maxFirst > 0 ? qreal(firstRow_) / qreal(maxFirst) : 0.0;

        return QRectF(bar.left(), bar.top() + travel * offset, bar.width(), thumbHeight);
    }

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override
    {
        const QSizeF natural = naturalSize();
        setMeasuredSize(QSizeF(width.resolve(natural.width()), height.resolve(natural.height())));
    }

    // The panel, its shadow and its outline belong to the Dropdown; this paints
    // only the rows and the scrollbar, inside the rectangle the Dropdown
    // arranges.
    void paintContent(QPainter& painter, const QRectF& content) override
    {
        const Theme& theme = Theme::forWidget(owner_);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();
        const bool enabled = owner_->isEnabled();
        const qreal unit = density.dp(1.0);
        const qreal radius = density.dp(kPopupRadiusDp);

        const QRectF bounds = content;
        if (bounds.isEmpty())
            return;

        painter.save();
        painter.setClipRect(bounds);

        const QFont itemFont = themeFont(theme, theme.type().bodyLarge);
        painter.setFont(itemFont);

        const int rows = pageRows();
        for (int index = 0; index < rows; ++index) {
            const QRectF rowBounds = rowRectFor(index);
            const int position = firstRow_ + index;
            const bool isCurrent = position == owner_->currentPosition();

            if (isCurrent) {
                QPainterPath pill;
                pill.addRoundedRect(rowBounds.adjusted(0.0, unit, 0.0, -unit),
                                    radius * 0.65, radius * 0.65);
                painter.fillPath(pill, colours.secondaryContainer);
            }

            const qreal textLeft = rowBounds.left() + density.dp(12.0);
            const qreal textWidth = std::max(qreal(0.0), rowBounds.right() - textLeft - density.dp(28.0));

            QColor textColour = colours.onSurface;
            if (!enabled) {
                textColour = withAlpha(colours.onSurface, 0.38);
            } else if (position == owner_->highlightedPosition()) {
                textColour = colours.onSecondaryContainer;
            }

            painter.setPen(textColour);
            painter.drawText(QRectF(textLeft, rowBounds.top(), textWidth, rowBounds.height()),
                             static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                             elided(itemFont, owner_->labelAt(position), textWidth));

            if (isCurrent) {
                const QPointF centre(rowBounds.right() - density.dp(12.0), rowBounds.center().y());
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

        if (scrollable()) {
            const QRectF track = scrollBarRect();
            const QRectF thumb = scrollBarThumbRect();
            if (!track.isEmpty() && !thumb.isEmpty()) {
                QPainterPath trackPath;
                trackPath.addRoundedRect(track, track.width() * 0.5, track.width() * 0.5);
                painter.fillPath(trackPath, withAlpha(colours.onSurface, 0.10));

                QPainterPath thumbPath;
                thumbPath.addRoundedRect(thumb, thumb.width() * 0.5, thumb.width() * 0.5);
                painter.fillPath(thumbPath, withAlpha(colours.onSurface, 0.38));
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

        if (scrollable() && scrollBarRect().contains(event->position())) {
            dragging_ = true;
            scrollToPointer(event->position().y());
            event->accept();
            return;
        }

        const int visibleRow = rowAtPoint(event->position());
        if (visibleRow >= 0) {
            owner_->activatePosition(firstRow_ + visibleRow);
            event->accept();
            return;
        }

        View::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (dragging_) {
            scrollToPointer(event->position().y());
            event->accept();
            return;
        }

        View::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (dragging_ && event->button() == Qt::LeftButton) {
            dragging_ = false;
            event->accept();
            return;
        }

        View::mouseReleaseEvent(event);
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
        QCoreApplication::sendEvent(owner_, event);
        if (!event->isAccepted())
            event->ignore();
    }

private:
    void scrollToPointer(qreal y)
    {
        const QRectF bar = scrollBarRect();
        if (bar.height() <= 0.0)
            return;

        const qreal fraction = std::clamp((y - bar.top()) / bar.height(), qreal(0.0), qreal(1.0));
        setFirstRow(static_cast<int>(std::lround(fraction * qreal(maxFirstRow()))));
    }

    // The natural size of the row window: the padded rows plus the menu's
    // minimum width. The Dropdown turns it into the panel height and, because
    // it matches the anchor width, keeps the panel as wide as the field.
    [[nodiscard]] QSizeF naturalSize() const
    {
        const Density& density = Theme::forWidget(owner_).density();
        const qreal padding = density.dp(kPopupPaddingDp);
        const qreal rowHeight = density.dp(kRowHeightDp);
        const qreal rows = qreal(std::max(1, pageRows()));
        return QSizeF(density.dp(kPopupWidthDp), padding * 2.0 + rowHeight * rows);
    }

    VirtualSelect* owner_ = nullptr;
    int firstRow_ = 0;
    bool dragging_ = false;
};

// ---------------------------------------------------------------------------
// VirtualSelect
// ---------------------------------------------------------------------------

VirtualSelect::VirtualSelect(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));
    setShape(Theme::forWidget(this).shapes().extraSmall);

    editor_ = new QLineEdit(this);
    editor_->setFrame(false);
    editor_->setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: 0; padding: 0px; }"));
    editor_->setFont(themeFont(Theme::forWidget(this), Theme::forWidget(this).type().bodyLarge));
    editor_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    editor_->installEventFilter(this);
    editor_->hide();

    connect(editor_, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (!popupOpen_)
            return;

        if (filterText_ == text)
            return;

        filterText_ = text;
        applyFilter();
    });

    requestUpdate();
}

VirtualSelect::~VirtualSelect()
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

void VirtualSelect::setSource(structure::VirtualSource* source)
{
    if (source_ == source)
        return;

    hidePopup();
    source_ = source;
    current_ = -1;
    highlighted_ = -1;
    filterText_.clear();
    filtered_.clear();
    labelCache_.clear();
    syncEditor();
    requestUpdate();
    updateGeometry();
}

structure::VirtualSource* VirtualSelect::source() const noexcept
{
    return source_;
}

void VirtualSelect::setFilterable(bool filterable)
{
    if (filterable_ == filterable)
        return;

    filterable_ = filterable;

    if (editor_ != nullptr)
        editor_->setVisible(filterable_);

    if (!filterable_) {
        filterText_.clear();
        filtered_.clear();
        if (popupOpen_)
            syncPopupLayout();
    }

    syncEditor();
    updateGeometry();
    requestUpdate();
}

bool VirtualSelect::isFilterable() const noexcept
{
    return filterable_;
}

QLineEdit* VirtualSelect::editor() const noexcept
{
    return editor_;
}

void VirtualSelect::setPlaceholderText(const QString& text)
{
    if (placeholder_ == text)
        return;

    placeholder_ = text;
    syncEditor();
    requestUpdate();
}

QString VirtualSelect::placeholderText() const
{
    return placeholder_;
}

void VirtualSelect::setMaxVisibleRows(int rows)
{
    const int clamped = std::max(1, rows);
    if (maxVisibleRows_ == clamped)
        return;

    maxVisibleRows_ = clamped;
    syncPopupLayout();
    ensurePositionVisible(highlighted_);
    requestUpdate();
}

int VirtualSelect::maxVisibleRows() const noexcept
{
    return maxVisibleRows_;
}

int VirtualSelect::currentIndex() const noexcept
{
    return current_;
}

void VirtualSelect::setCurrentIndex(int index)
{
    const int count = itemCount();
    const int next = (count <= 0 || index < 0) ? -1 : std::min(index, count - 1);
    const int previous = current_;

    current_ = next;

    if (filtering()) {
        const int position = positionOfSourceIndex(next);
        if (position >= 0)
            highlighted_ = position;
    } else {
        highlighted_ = next;
    }

    if (!popupOpen_)
        syncEditor();

    requestUpdate();

    if (previous != current_)
        emit currentChanged(current_);
}

QVariant VirtualSelect::currentValue() const
{
    if (source_ == nullptr || current_ < 0 || current_ >= source_->count())
        return {};

    return source_->item(current_);
}

QString VirtualSelect::currentText() const
{
    if (source_ == nullptr || current_ < 0 || current_ >= source_->count())
        return placeholder_;

    return labelForSourceIndex(current_);
}

int VirtualSelect::itemCount() const
{
    return source_ != nullptr ? source_->count() : 0;
}

int VirtualSelect::filteredCount() const
{
    return positionCount();
}

int VirtualSelect::visibleRowCount() const
{
    if (!popupOpen_)
        return 0;

    return std::min(maxVisibleRows_, positionCount());
}

int VirtualSelect::firstVisibleRow() const
{
    if (!popupOpen_ || popup_ == nullptr)
        return 0;

    return static_cast<ListContent*>(popup_)->firstRow();
}

int VirtualSelect::sourceIndexAt(int position) const
{
    if (position < 0)
        return -1;

    if (filtering()) {
        if (position >= filtered_.size())
            return -1;
        return filtered_.at(position);
    }

    return position < itemCount() ? position : -1;
}

QString VirtualSelect::labelAt(int position) const
{
    const int index = sourceIndexAt(position);
    if (index < 0)
        return {};

    return labelForSourceIndex(index);
}

int VirtualSelect::highlightedPosition() const noexcept
{
    return highlighted_;
}

int VirtualSelect::currentPosition() const
{
    return positionOfSourceIndex(current_);
}

int VirtualSelect::rowAt(const QPointF& popupPoint) const
{
    if (!popupOpen_ || popup_ == nullptr)
        return -1;

    return static_cast<ListContent*>(popup_)->rowAtPoint(popupPoint);
}

QRectF VirtualSelect::rowRect(int visibleRow) const
{
    if (!popupOpen_ || popup_ == nullptr)
        return {};

    return static_cast<ListContent*>(popup_)->rowRectFor(visibleRow);
}

QRectF VirtualSelect::scrollBarRect() const
{
    if (!popupOpen_ || popup_ == nullptr)
        return {};

    return static_cast<ListContent*>(popup_)->scrollBarRect();
}

QRectF VirtualSelect::fieldRect() const
{
    const Density& density = Theme::forWidget(this).density();
    const qreal fieldHeight = std::min(qreal(height()), density.dp(kFieldHeightDp));
    return QRectF(0.0, 0.0, qreal(width()), fieldHeight);
}

void VirtualSelect::showPopup()
{
    if (!isEnabled() || source_ == nullptr || source_->count() <= 0)
        return;

    if (dropdown_ == nullptr) {
        auto* content = new ListContent(this);
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

    filterText_.clear();
    filtered_.clear();
    labelCache_.clear();

    highlighted_ = (current_ >= 0 && current_ < source_->count()) ? current_ : -1;
    popupOpen_ = true;

    auto* content = static_cast<ListContent*>(popup_);
    content->setFirstRow(0);
    if (highlighted_ >= 0)
        content->ensurePositionVisible(highlighted_);

    // The Dropdown owns the placement: it measures from the anchor's global
    // rectangle, keeps the panel as wide as the field and flips it above when
    // the screen has no room below.
    dropdown_->open();

    prefetchLabels();

    if (filterable_ && editor_ != nullptr) {
        editor_->setReadOnly(false);
        editor_->blockSignals(true);
        editor_->clear();
        editor_->blockSignals(false);
        editor_->setPlaceholderText(currentText());
        editor_->setFocus(Qt::PopupFocusReason);
    } else {
        content->setFocus(Qt::PopupFocusReason);
    }

    requestUpdate();
}

void VirtualSelect::hidePopup()
{
    const bool wasOpen = popupOpen_;
    popupOpen_ = false;

    if (dropdown_ != nullptr && dropdown_->isOpen())
        dropdown_->close();

    if (!wasOpen)
        return;

    filterText_.clear();
    filtered_.clear();
    labelCache_.clear();

    highlighted_ = current_ >= 0 ? current_ : -1;

    if (editor_ != nullptr)
        editor_->setReadOnly(true);

    syncEditor();
    requestUpdate();
    restoreFocus();
}

bool VirtualSelect::isPopupVisible() const
{
    return popupOpen_ && dropdown_ != nullptr && dropdown_->isOpen();
}

QWidget* VirtualSelect::popupWidget() const noexcept
{
    return popup_;
}

Dropdown* VirtualSelect::dropdown() const noexcept
{
    return dropdown_;
}

void VirtualSelect::activatePosition(int position)
{
    if (position < 0 || position >= positionCount())
        return;

    moveHighlight(position);

    const QVariant value = currentValue();
    hidePopup();
    emit selected(value);
}

void VirtualSelect::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(240.0)),
                           height.resolve(density.dp(kFieldHeightDp))));
}

void VirtualSelect::arrangeOverride(const QRectF& content)
{
    Q_UNUSED(content);

    if (editor_ == nullptr)
        return;

    const Density& density = Theme::forWidget(this).density();
    const QRectF field = fieldRect();
    const qreal padding = density.dp(kHorizontalPaddingDp);
    const qreal chevronSpace = density.dp(32.0);

    const int width = std::max(0, static_cast<int>(std::lround(field.width() - padding * 2.0
                                                               - chevronSpace)));
    editor_->setGeometry(QRect(static_cast<int>(std::lround(field.left() + padding)),
                               static_cast<int>(std::lround(field.top())),
                               width,
                               static_cast<int>(std::lround(field.height()))));
}

void VirtualSelect::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const bool enabled = isEnabled();
    const bool focused = hasFocus() || (editor_ != nullptr && editor_->hasFocus());
    const bool editorActive = filterable_ && editor_ != nullptr && !editor_->isHidden();

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
    const bool showingPlaceholder = current_ < 0;
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

    // With a filterable field the editor paints the text; drawing it here as
    // well would double it.
    if (!editorActive) {
        const QFont valueFont = themeFont(theme, theme.type().bodyLarge);
        painter.setFont(valueFont);
        painter.setPen(textColour);

        const QString label = currentText();
        const qreal textWidth = std::max(qreal(0.0), field.width() - padding * 2.0 - density.dp(32.0));
        painter.drawText(QRectF(field.left() + padding, field.top(), textWidth, field.height()),
                         static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                         elided(valueFont, label, textWidth));
    }

    // The chevron: down when closed, up while the menu is open.
    QPainterPath chevron;
    const QPointF centre(field.right() - density.dp(24.0), field.center().y());
    const qreal size = density.dp(4.5);
    if (popupOpen_) {
        chevron.moveTo(centre.x() - size, centre.y() + size * 0.5);
        chevron.lineTo(centre.x(), centre.y() - size * 0.5);
        chevron.lineTo(centre.x() + size, centre.y() + size * 0.5);
    } else {
        chevron.moveTo(centre.x() - size, centre.y() - size * 0.5);
        chevron.lineTo(centre.x(), centre.y() + size * 0.5);
        chevron.lineTo(centre.x() + size, centre.y() - size * 0.5);
    }

    painter.setPen(QPen(enabled ? colours.onSurfaceVariant
                                : withAlpha(colours.onSurfaceVariant, 0.38),
                        density.dp(2.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(chevron);

    painter.restore();
}

void VirtualSelect::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressed_ = fieldRect().contains(event->position());

    View::mousePressEvent(event);
}

void VirtualSelect::mouseReleaseEvent(QMouseEvent* event)
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

void VirtualSelect::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        View::keyPressEvent(event);
        return;
    }

    if (handleKey(event)) {
        event->accept();
        return;
    }

    View::keyPressEvent(event);
}

void VirtualSelect::wheelEvent(QWheelEvent* event)
{
    if (!popupOpen_ || popup_ == nullptr) {
        event->ignore();
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }

    auto* popup = static_cast<ListContent*>(popup_);
    popup->setFirstRow(popup->firstRow() + (delta < 0 ? 3 : -3));
    event->accept();
}

void VirtualSelect::hideEvent(QHideEvent* event)
{
    View::hideEvent(event);
    hidePopup();
}

void VirtualSelect::onThemeChanged()
{
    // The panel's placement is density-dependent: re-run it after a theme
    // switch instead of leaving the old side and offsets behind.
    if (popupOpen_ && dropdown_ != nullptr)
        dropdown_->reposition();

    requestUpdate();
}

bool VirtualSelect::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == editor_) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton && isEnabled() && !popupOpen_)
                showPopup();
            return false;
        }
        case QEvent::KeyPress: {
            auto* key = static_cast<QKeyEvent*>(event);
            if (popupOpen_ && handleKey(key))
                return true;

            if (!popupOpen_
                && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Return
                    || key->key() == Qt::Key_Enter)) {
                showPopup();
                return true;
            }

            return false;
        }
        case QEvent::FocusIn:
        case QEvent::FocusOut:
            requestUpdate();
            return false;
        default:
            return false;
        }
    }

    // Outside presses are the Dropdown's business: it dismisses itself and every
    // other control's surface, so a click can never leave two menus open.
    return View::eventFilter(watched, event);
}

bool VirtualSelect::focusable() const
{
    return isEnabled();
}

// ---------------------------------------------------------------------------
// Order, filtering and the label cache
// ---------------------------------------------------------------------------

int VirtualSelect::positionCount() const
{
    if (filtering())
        return static_cast<int>(filtered_.size());

    return itemCount();
}

bool VirtualSelect::filtering() const
{
    return !filterText_.isEmpty();
}

QString VirtualSelect::labelForSourceIndex(int index) const
{
    if (source_ == nullptr || index < 0 || index >= source_->count())
        return {};

    const auto cached = labelCache_.constFind(index);
    if (cached != labelCache_.constEnd())
        return cached.value();

    const QString label = source_->label(index);
    labelCache_.insert(index, label);
    return label;
}

int VirtualSelect::positionOfSourceIndex(int index) const
{
    if (index < 0)
        return -1;

    if (!filtering())
        return index;

    for (int position = 0; position < filtered_.size(); ++position) {
        if (filtered_.at(position) == index)
            return position;
    }
    return -1;
}

void VirtualSelect::applyFilter()
{
    filtered_.clear();
    labelCache_.clear();

    if (source_ != nullptr && !filterText_.isEmpty()) {
        const int count = source_->count();
        filtered_.reserve(count);
        for (int index = 0; index < count; ++index) {
            if (source_->label(index).contains(filterText_, Qt::CaseInsensitive))
                filtered_.append(index);
        }
    }

    highlighted_ = positionCount() > 0 ? 0 : -1;

    if (popup_ != nullptr)
        static_cast<ListContent*>(popup_)->setFirstRow(0);

    syncPopupLayout();
    requestUpdate();
}

void VirtualSelect::moveHighlight(int position)
{
    const int count = positionCount();
    if (count <= 0) {
        highlighted_ = -1;
        requestUpdate();
        return;
    }

    highlighted_ = std::clamp(position, 0, count - 1);

    const int next = sourceIndexAt(highlighted_);
    const int previous = current_;
    current_ = next;

    ensurePositionVisible(highlighted_);
    requestUpdate();

    if (previous != current_)
        emit currentChanged(current_);
}

void VirtualSelect::ensurePositionVisible(int position)
{
    if (!popupOpen_ || popup_ == nullptr || position < 0)
        return;

    static_cast<ListContent*>(popup_)->ensurePositionVisible(position);
}

void VirtualSelect::prefetchLabels()
{
    // A small margin around the painted window keeps a scroll from going back
    // to the source for the row that is about to appear, without turning the
    // menu into a full materialisation.
    if (source_ == nullptr || popup_ == nullptr)
        return;

    const int total = positionCount();
    const int first = std::max(0, firstVisibleRow() - kPrefetchRows);
    const int last = std::min(total - 1, firstVisibleRow() + maxVisibleRows_ + kPrefetchRows);
    for (int position = first; position <= last; ++position) {
        const int index = sourceIndexAt(position);
        if (index < 0)
            continue;

        // Reading the label is the side effect here: it warms the cache.
        const QString warmed = labelForSourceIndex(index);
        Q_UNUSED(warmed);
    }
}

void VirtualSelect::syncEditor()
{
    if (editor_ == nullptr)
        return;

    const Theme& theme = Theme::forWidget(this);
    QPalette palette = editor_->palette();
    palette.setColor(QPalette::Base, Qt::transparent);
    palette.setColor(QPalette::Text, isEnabled() ? theme.colours().onSurface
                                                 : withAlpha(theme.colours().onSurface, 0.38));
    palette.setColor(QPalette::PlaceholderText, theme.colours().onSurfaceVariant);
    palette.setColor(QPalette::Highlight, theme.colours().primary);
    palette.setColor(QPalette::HighlightedText, theme.colours().onPrimary);
    editor_->setPalette(palette);
    editor_->setFont(themeFont(theme, theme.type().bodyLarge));
    editor_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    if (!filterable_) {
        editor_->hide();
        return;
    }

    editor_->show();
    editor_->setReadOnly(!popupOpen_);

    if (!popupOpen_) {
        editor_->blockSignals(true);
        editor_->setText(current_ >= 0 ? currentText() : QString());
        editor_->setPlaceholderText(placeholder_);
        editor_->blockSignals(false);
    }
}

void VirtualSelect::syncPopupLayout()
{
    if (!popupOpen_ || dropdown_ == nullptr)
        return;

    // The row window (and with it the panel height) may have changed; the
    // Dropdown re-measures the content and re-applies the placement.
    dropdown_->reposition();
}

void VirtualSelect::handlePopupClosed()
{
    // The Dropdown dismissed itself (an outside press, Escape, the anchor
    // hiding): mirror that in the control's own state exactly once.
    if (!popupOpen_)
        return;

    popupOpen_ = false;

    filterText_.clear();
    filtered_.clear();
    labelCache_.clear();

    highlighted_ = current_ >= 0 ? current_ : -1;

    if (editor_ != nullptr)
        editor_->setReadOnly(true);

    syncEditor();
    requestUpdate();
    restoreFocus();
}

void VirtualSelect::restoreFocus()
{
    if (!isVisible() || !isEnabled())
        return;

    if (filterable_ && editor_ != nullptr && !editor_->isHidden())
        editor_->setFocus(Qt::PopupFocusReason);
    else
        setFocus(Qt::PopupFocusReason);
}

bool VirtualSelect::handleKey(QKeyEvent* event)
{
    const int count = positionCount();

    switch (event->key()) {
    case Qt::Key_Down:
        if (!popupOpen_) {
            showPopup();
            if (popupOpen_ && highlighted_ < 0 && positionCount() > 0)
                moveHighlight(0);
        } else if (count > 0) {
            moveHighlight(highlighted_ < 0 ? 0 : highlighted_ + 1);
        }
        return true;
    case Qt::Key_Up:
        if (!popupOpen_) {
            showPopup();
            const int opened = positionCount();
            if (popupOpen_ && opened > 0)
                moveHighlight(opened - 1);
        } else if (count > 0) {
            moveHighlight(highlighted_ <= 0 ? 0 : highlighted_ - 1);
        }
        return true;
    case Qt::Key_Home:
        if (popupOpen_ && count > 0)
            moveHighlight(0);
        return true;
    case Qt::Key_End:
        if (popupOpen_ && count > 0)
            moveHighlight(count - 1);
        return true;
    case Qt::Key_PageDown:
        if (popupOpen_ && count > 0) {
            const int base = highlighted_ < 0 ? -1 : highlighted_;
            moveHighlight(std::min(count - 1, base + maxVisibleRows_));
        }
        return true;
    case Qt::Key_PageUp:
        if (popupOpen_ && count > 0) {
            const int base = highlighted_ < 0 ? 0 : highlighted_;
            moveHighlight(std::max(0, base - maxVisibleRows_));
        }
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!popupOpen_) {
            showPopup();
        } else {
            // With no highlight yet (a freshly opened menu), Enter commits the
            // first row rather than doing nothing.
            const int target = highlighted_ >= 0 ? highlighted_ : (count > 0 ? 0 : -1);
            if (target >= 0)
                activatePosition(target);
        }
        return true;
    case Qt::Key_Escape:
        if (popupOpen_) {
            hidePopup();
            return true;
        }
        return false;
    case Qt::Key_Space:
        if (popupOpen_ && filterable_)
            return false; // a space belongs to the filter text

        if (popupOpen_) {
            if (highlighted_ >= 0)
                activatePosition(highlighted_);
            return true;
        }

        showPopup();
        return true;
    default:
        return false;
    }
}

} // namespace md
