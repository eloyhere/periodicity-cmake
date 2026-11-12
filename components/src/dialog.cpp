// Periodicity — the dialog family.

#include <md/components/dialog.h>

#include <md/components/button.h>
#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSizeF>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>
#include <QWidget>
#include <QtMath>

#include <algorithm>
#include <limits>

namespace md {

namespace {

constexpr qreal kEdgeDp = 24.0;
constexpr qreal kPreferredWidthDp = 560.0;
constexpr qreal kPaddingDp = 24.0;
constexpr qreal kTitleBodyGapDp = 16.0;
constexpr qreal kActionGapDp = 8.0;
constexpr qreal kActionHeightDp = 48.0;
constexpr qreal kActionRowGapDp = 24.0;
constexpr qreal kMinSurfaceHeightDp = 120.0;
constexpr qreal kChoiceRowHeightDp = 48.0;
constexpr qreal kChoiceIndicatorDp = 18.0;
constexpr qreal kChoiceLabelGapDp = 16.0;
constexpr int kMaxTextLines = 64;

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

qreal wrappedTextHeight(const QString& text, const QFont& font, qreal width,
                        qreal lineHeight, int maxLines)
{
    if (text.isEmpty() || width <= 0.0 || maxLines <= 0)
        return 0.0;

    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignLeft);

    QTextLayout layout(text, font);
    layout.setTextOption(option);
    layout.beginLayout();

    qreal y = 0.0;
    int lines = 0;
    while (lines < maxLines) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
        line.setPosition(QPointF(0.0, y));
        y += lineHeight;
        ++lines;
    }
    layout.endLayout();

    return y;
}

void drawWrappedText(QPainter& painter, const QString& text, const QFont& font,
                     qreal lineHeight, const QRectF& rect, const QColor& colour,
                     int maxLines)
{
    if (text.isEmpty() || rect.isEmpty() || maxLines <= 0)
        return;

    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignLeft);

    QTextLayout layout(text, font);
    layout.setTextOption(option);
    layout.beginLayout();

    qreal y = 0.0;
    int lines = 0;
    while (lines < maxLines) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(rect.width());
        line.setPosition(QPointF(0.0, y));
        y += lineHeight;
        ++lines;
    }
    layout.endLayout();

    painter.save();
    painter.setClipRect(rect);
    painter.setPen(colour);
    layout.draw(&painter, QPointF(rect.left(), rect.top()));
    painter.restore();
}

void drawCheckMark(QPainter& painter, const QRectF& box, const QColor& colour,
                   const Density& density)
{
    QPen pen(colour, density.dp(2.0));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    QPainterPath check;
    check.moveTo(box.left() + box.width() * 0.18, box.top() + box.height() * 0.53);
    check.lineTo(box.left() + box.width() * 0.42, box.top() + box.height() * 0.78);
    check.lineTo(box.left() + box.width() * 0.84, box.top() + box.height() * 0.24);
    painter.drawPath(check);
}

} // namespace

Dialog::Dialog(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    // The overlay only paints a translucent scrim and the surface; the page
    // behind it must stay visible instead of a black window background.
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setBackgroundColour(QColor(Qt::transparent));
    syncAppearance();

    // A dialog is an overlay, not a layout child: until open() gives it the
    // parent's geometry it must not paint over the page.
    hide();
}

void Dialog::syncAppearance()
{
    const Theme& theme = Theme::forWidget(this);
    setShape(theme.shapes().extraLarge);
    setElevation(Elevation::Level0);
    setBackgroundColour(QColor(Qt::transparent));
    requestUpdate();
}

void Dialog::setTitle(const QString& title)
{
    if (title_ == title)
        return;

    title_ = title;
    relayout();
}

QString Dialog::title() const
{
    return title_;
}

void Dialog::setText(const QString& text)
{
    if (text_ == text)
        return;

    text_ = text;
    relayout();
}

QString Dialog::text() const
{
    return text_;
}

void Dialog::setScrimDismissible(bool dismissible)
{
    scrimDismissible_ = dismissible;
}

bool Dialog::isScrimDismissible() const noexcept
{
    return scrimDismissible_;
}

void Dialog::open()
{
    if (open_ || parentWidget() == nullptr)
        return;

    open_ = true;
    parentWidget()->installEventFilter(this);

    applyOverlayGeometry();
    show();
    raise();
    setFocus(Qt::PopupFocusReason);
}

void Dialog::close()
{
    if (!open_) {
        hide();
        return;
    }

    open_ = false;
    detachParentFilter();
    hide();
}

bool Dialog::isOpen() const noexcept
{
    return open_;
}

void Dialog::accept()
{
    if (!open_)
        return;

    open_ = false;
    detachParentFilter();
    hide();

    emit accepted();
}

void Dialog::reject()
{
    if (!open_)
        return;

    open_ = false;
    detachParentFilter();
    hide();

    emit rejected();
}

void Dialog::detachParentFilter()
{
    if (parentWidget() != nullptr)
        parentWidget()->removeEventFilter(this);
}

int Dialog::actionCount() const
{
    return static_cast<int>(actions_.size());
}

Button* Dialog::actionButton(int index) const
{
    if (index < 0 || index >= static_cast<int>(actions_.size()))
        return nullptr;
    return actions_.at(index).button;
}

int Dialog::addActionButton(const QString& text, ActionRole role)
{
    auto* button = new Button(text, this);
    button->setVariant(Button::ButtonVariant::Text);
    addView(button);

    const int index = static_cast<int>(actions_.size());
    ActionEntry entry;
    entry.button = button;
    entry.role = role;
    actions_.append(entry);

    QObject::connect(button, &Button::clicked, this, [this, index] { handleAction(index); });

    relayout();
    return index;
}

void Dialog::handleAction(int index)
{
    if (index < 0 || index >= static_cast<int>(actions_.size()))
        return;

    const ActionRole role = actions_.at(index).role;
    emit actionTriggered(index);

    switch (role) {
    case ActionRole::Accept:
        accept();
        break;
    case ActionRole::Reject:
        reject();
        break;
    case ActionRole::Custom:
        break;
    }
}

void Dialog::relayout()
{
    requestUpdate();

    if (open_)
        applyOverlayGeometry();
}

void Dialog::applyOverlayGeometry()
{
    if (parentWidget() == nullptr)
        return;

    arrange(QRect(QPoint(0, 0), parentWidget()->size()));
}

qreal Dialog::surfaceWidth() const
{
    const Density& density = Theme::forWidget(this).density();
    const qreal edge = density.dp(kEdgeDp);
    const qreal preferred = density.dp(kPreferredWidthDp);

    if (width() <= 0)
        return preferred;

    const qreal available =
        std::max(qreal(0.0), static_cast<qreal>(width()) - edge * 2.0);

    return std::min(preferred, available);
}

qreal Dialog::titleLineHeight() const
{
    const Theme& theme = Theme::forWidget(this);
    return theme.density().sp(theme.type().headlineSmall.lineHeightSp);
}

qreal Dialog::bodyHeightForWidth(qreal width) const
{
    if (text_.isEmpty() || width <= 0.0)
        return 0.0;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFont bodyFont = fontFor(theme.type().bodyMedium, density, this);

    return wrappedTextHeight(text_, bodyFont, width,
                             density.sp(theme.type().bodyMedium.lineHeightSp),
                             kMaxTextLines);
}

qreal Dialog::surfaceHeightForWidth(qreal width) const
{
    if (width <= 0.0)
        return 0.0;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal padding = density.dp(kPaddingDp);
    const qreal contentWidth = std::max(qreal(0.0), width - padding * 2.0);
    const qreal contentHeight = bodyHeightForWidth(contentWidth);

    qreal surfaceHeight = padding * 2.0 + contentHeight;

    if (!title_.isEmpty()) {
        surfaceHeight += titleLineHeight();
        if (contentHeight > 0.0)
            surfaceHeight += density.dp(kTitleBodyGapDp);
    }

    if (!actions_.isEmpty())
        surfaceHeight += density.dp(kActionRowGapDp) + density.dp(kActionHeightDp);

    const qreal minHeight = density.dp(kMinSurfaceHeightDp);
    surfaceHeight = std::max(surfaceHeight, minHeight);

    if (height() > 0) {
        const qreal maxHeight =
            std::max(minHeight, static_cast<qreal>(height()) - density.dp(kEdgeDp) * 2.0);
        surfaceHeight = std::min(surfaceHeight, maxHeight);
    }

    return surfaceHeight;
}

QRectF Dialog::surfaceRect() const
{
    if (width() <= 0 || height() <= 0)
        return QRectF();

    const qreal surfaceW = surfaceWidth();
    const qreal surfaceH = surfaceHeightForWidth(surfaceW);

    if (surfaceW <= 0.0 || surfaceH <= 0.0)
        return QRectF();

    const qreal x = (static_cast<qreal>(width()) - surfaceW) / 2.0;
    const qreal y = (static_cast<qreal>(height()) - surfaceH) / 2.0;

    return QRectF(x, y, surfaceW, surfaceH);
}

qreal Dialog::bodyTop(const QRectF& surface) const
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal padding = density.dp(kPaddingDp);

    qreal top = surface.top() + padding;

    if (!title_.isEmpty()) {
        top += titleLineHeight();

        const qreal contentWidth =
            std::max(qreal(0.0), surface.width() - padding * 2.0);
        if (bodyHeightForWidth(contentWidth) > 0.0)
            top += density.dp(kTitleBodyGapDp);
    }

    return top;
}

QRectF Dialog::actionRowRect(const QRectF& surface) const
{
    const Density& density = Theme::forWidget(this).density();
    const qreal padding = density.dp(kPaddingDp);
    const qreal rowHeight = density.dp(kActionHeightDp);

    return QRectF(surface.left() + padding, surface.bottom() - padding - rowHeight,
                  std::max(qreal(0.0), surface.width() - padding * 2.0), rowHeight);
}

QRectF Dialog::bodyRect() const
{
    const QRectF surface = surfaceRect();
    if (surface.isEmpty())
        return QRectF();

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal padding = density.dp(kPaddingDp);

    const qreal contentWidth = std::max(qreal(0.0), surface.width() - padding * 2.0);
    const qreal top = bodyTop(surface);

    qreal bottom = surface.bottom() - padding;
    if (!actions_.isEmpty())
        bottom = actionRowRect(surface).top() - density.dp(kActionRowGapDp);

    const qreal available = std::max(qreal(0.0), bottom - top);
    const qreal height = std::min(bodyHeightForWidth(contentWidth), available);

    return QRectF(surface.left() + padding, top, contentWidth, std::max(qreal(0.0), height));
}

void Dialog::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    const qreal naturalWidth = density.dp(kPreferredWidthDp);
    const qreal naturalHeight = surfaceHeightForWidth(naturalWidth);

    setMeasuredSize(QSizeF(width.resolve(naturalWidth), height.resolve(naturalHeight)));
}

void Dialog::arrangeChildren(const QRectF& content)
{
    Q_UNUSED(content);

    const QRectF surface = surfaceRect();
    if (surface.isEmpty())
        return;

    arrangeBody(bodyRect());

    if (actions_.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QRectF row = actionRowRect(surface);
    const qreal gap = density.dp(kActionGapDp);

    QList<QSizeF> sizes;
    sizes.reserve(actions_.size());

    qreal total = 0.0;
    for (const ActionEntry& entry : actions_) {
        entry.button->measure(MeasureSpec::atMost(row.width()), MeasureSpec::unspecified());
        const QSizeF size = entry.button->measuredSize();
        sizes.append(size);
        total += size.width();
    }

    if (sizes.size() > 1)
        total += gap * static_cast<qreal>(sizes.size() - 1);

    // The action row is trailing-aligned — right in a left-to-right layout, left
    // in a right-to-left one — and the logical order is mirrored with it, so the
    // confirming action stays on the outer edge in both directions.
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
    qreal x = rightToLeft ? row.left() : row.right() - total;

    for (int step = 0; step < static_cast<int>(actions_.size()); ++step) {
        const int index =
            rightToLeft ? static_cast<int>(actions_.size()) - 1 - step : step;
        Button* button = actions_.at(index).button;
        const QSizeF size = sizes.at(index);
        const qreal y = row.top() + (row.height() - size.height()) / 2.0;

        button->arrange(QRect(qRound(x), qRound(y), qCeil(size.width()),
                              qCeil(size.height())));
        button->show();
        x += size.width() + gap;
    }
}

void Dialog::paintBody(QPainter& painter, const QRectF& body) const
{
    if (text_.isEmpty() || body.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFont bodyFont = fontFor(theme.type().bodyMedium, density, this);

    drawWrappedText(painter, text_, bodyFont,
                    density.sp(theme.type().bodyMedium.lineHeightSp), body,
                    theme.colours().onSurfaceVariant, kMaxTextLines);
}

bool Dialog::handleBodyClick(const QPointF& position)
{
    Q_UNUSED(position);
    return false;
}

bool Dialog::handleBodyKey(QKeyEvent* event)
{
    Q_UNUSED(event);
    return false;
}

void Dialog::arrangeBody(const QRectF& body)
{
    Q_UNUSED(body);
}

void Dialog::paintContent(QPainter& painter, const QRectF& content)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    painter.fillRect(content, withAlpha(colours.scrim, 0.32));

    const QRectF surface = surfaceRect();
    if (surface.isEmpty()) {
        painter.restore();
        return;
    }

    const QPainterPath surfacePath = theme.shapes().extraLarge.path(surface);
    // Design baseline basic dialog: elevation level 3 (0 4px 8px 3px at 15%, plus a tight
    // 0 1px 3px at 30%). The ramp below approximates those two layers with a
    // smooth stack instead of one hard-edged drop shadow.
    const qreal shadowSpread = density.dp(8.0);
    const qreal shadowOffset = density.dp(4.0);

    for (int step = 4; step >= 1; --step) {
        const qreal spread = shadowSpread * static_cast<qreal>(step) / 4.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(withAlpha(colours.shadow, 0.07));
        painter.drawPath(theme.shapes().extraLarge.path(
            surface.adjusted(-spread, -spread + shadowOffset, spread,
                             spread + shadowOffset)));
    }

    // A basic dialog has no outline: the design baseline separates it from the page with the
    // scrim and the level-3 shadow alone. Stroking the contour here drew a
    // second border on top of that shadow and made the surface read as an
    // outlined card.
    painter.setPen(Qt::NoPen);
    painter.setBrush(colours.surfaceContainerHigh);
    painter.drawPath(surfacePath);

    const qreal padding = density.dp(kPaddingDp);

    if (!title_.isEmpty()) {
        const QFont titleFont = fontFor(theme.type().headlineSmall, density, this);
        const QFontMetricsF metrics(titleFont);
        const qreal width = std::max(qreal(0.0), surface.width() - padding * 2.0);
        const QString label = metrics.elidedText(title_, Qt::ElideRight, width);

        painter.setFont(titleFont);
        painter.setPen(colours.onSurface);
        painter.drawText(QRectF(surface.left() + padding, surface.top() + padding, width,
                                titleLineHeight()),
                         static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), label);
    }

    const QRectF body = bodyRect();
    if (!body.isEmpty()) {
        painter.save();
        painter.setClipRect(body);
        paintBody(painter, body);
        painter.restore();
    }

    painter.restore();
}

void Dialog::mousePressEvent(QMouseEvent* event)
{
    ViewGroup::mousePressEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    const QPointF position = event->position();
    pressedScrim_ = !surfaceRect().contains(position);
    pressedBody_ = bodyRect().contains(position);
}

void Dialog::mouseReleaseEvent(QMouseEvent* event)
{
    ViewGroup::mouseReleaseEvent(event);

    if (event->button() != Qt::LeftButton) {
        pressedScrim_ = false;
        pressedBody_ = false;
        return;
    }

    const QPointF position = event->position();
    const bool scrim = pressedScrim_ && !surfaceRect().contains(position);
    const bool body = pressedBody_ && bodyRect().contains(position);

    pressedScrim_ = false;
    pressedBody_ = false;

    if (scrim) {
        if (scrimDismissible_)
            reject();
        return;
    }

    if (body)
        handleBodyClick(position);
}

void Dialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        event->accept();
        return;
    }

    if (handleBodyKey(event)) {
        event->accept();
        return;
    }

    ViewGroup::keyPressEvent(event);
}

bool Dialog::eventFilter(QObject* watched, QEvent* event)
{
    if (parentWidget() != nullptr && watched == parentWidget()) {
        if (event->type() == QEvent::Resize)
            applyOverlayGeometry();
        else if (event->type() == QEvent::Hide)
            close();

        return ViewGroup::eventFilter(watched, event);
    }

    return ViewGroup::eventFilter(watched, event);
}

void Dialog::onThemeChanged()
{
    syncAppearance();
    requestUpdate();
}

bool Dialog::focusable() const
{
    return false;
}

// ---------------------------------------------------------------------------
// ConfirmDialog
// ---------------------------------------------------------------------------

ConfirmDialog::ConfirmDialog(QWidget* parent)
    : Dialog(parent)
{
    addActionButton(QStringLiteral("取消"), ActionRole::Reject);
    addActionButton(QStringLiteral("确认"), ActionRole::Accept);
}

ConfirmDialog::ConfirmDialog(const QString& title, const QString& text, QWidget* parent)
    : ConfirmDialog(parent)
{
    setTitle(title);
    setText(text);
}

void ConfirmDialog::setConfirmText(const QString& text)
{
    if (Button* button = actionButton(1))
        button->setText(text);
}

QString ConfirmDialog::confirmText() const
{
    const Button* button = actionButton(1);
    return button != nullptr ? button->text() : QString();
}

void ConfirmDialog::setCancelText(const QString& text)
{
    if (Button* button = actionButton(0))
        button->setText(text);
}

QString ConfirmDialog::cancelText() const
{
    const Button* button = actionButton(0);
    return button != nullptr ? button->text() : QString();
}

// ---------------------------------------------------------------------------
// MessageDialog
// ---------------------------------------------------------------------------

MessageDialog::MessageDialog(QWidget* parent)
    : Dialog(parent)
{
    addActionButton(QStringLiteral("知道了"), ActionRole::Accept);
}

MessageDialog::MessageDialog(const QString& title, const QString& message, QWidget* parent)
    : MessageDialog(parent)
{
    setTitle(title);
    setMessage(message);
}

void MessageDialog::setMessage(const QString& message)
{
    setText(message);
}

QString MessageDialog::message() const
{
    return text();
}

void MessageDialog::setButtonText(const QString& text)
{
    if (Button* button = actionButton(0))
        button->setText(text);
}

QString MessageDialog::buttonText() const
{
    const Button* button = actionButton(0);
    return button != nullptr ? button->text() : QString();
}

// ---------------------------------------------------------------------------
// SingleChoiceDialog
// ---------------------------------------------------------------------------

SingleChoiceDialog::SingleChoiceDialog(QWidget* parent)
    : Dialog(parent)
{
    addActionButton(cancelText_, ActionRole::Reject);
    addActionButton(confirmText_, ActionRole::Accept);
}

SingleChoiceDialog::SingleChoiceDialog(const QString& title, const QStringList& items,
                                       QWidget* parent)
    : SingleChoiceDialog(parent)
{
    setTitle(title);
    setItems(items);
}

void SingleChoiceDialog::setItems(const QStringList& items)
{
    items_ = items;
    selected_ = -1;
    highlighted_ = -1;
    relayout();
}

QStringList SingleChoiceDialog::items() const
{
    return items_;
}

void SingleChoiceDialog::applySelection(int index)
{
    selected_ = (index >= 0 && index < static_cast<int>(items_.size())) ? index : -1;

    if (selected_ >= 0)
        highlighted_ = selected_;

    requestUpdate();
    emit selectionChanged(selected_);
}

void SingleChoiceDialog::setSelectedIndex(int index)
{
    const int clamped =
        (index >= 0 && index < static_cast<int>(items_.size())) ? index : -1;

    if (selected_ == clamped)
        return;

    applySelection(clamped);
}

int SingleChoiceDialog::selectedIndex() const noexcept
{
    return selected_;
}

QString SingleChoiceDialog::selectedValue() const
{
    if (selected_ < 0 || selected_ >= static_cast<int>(items_.size()))
        return QString();
    return items_.at(selected_);
}

void SingleChoiceDialog::setConfirmText(const QString& text)
{
    confirmText_ = text;
    if (Button* button = actionButton(1))
        button->setText(text);
}

QString SingleChoiceDialog::confirmText() const
{
    return confirmText_;
}

void SingleChoiceDialog::setCancelText(const QString& text)
{
    cancelText_ = text;
    if (Button* button = actionButton(0))
        button->setText(text);
}

QString SingleChoiceDialog::cancelText() const
{
    return cancelText_;
}

QRectF SingleChoiceDialog::itemRect(int index) const
{
    if (index < 0 || index >= static_cast<int>(items_.size()))
        return QRectF();

    const QRectF body = bodyRect();
    if (body.isEmpty())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal rowHeight = density.dp(kChoiceRowHeightDp);
    const qreal y = body.top() + rowHeight * static_cast<qreal>(index);

    return QRectF(body.left(), y, body.width(), rowHeight);
}

qreal SingleChoiceDialog::bodyHeightForWidth(qreal width) const
{
    Q_UNUSED(width);

    if (items_.isEmpty())
        return 0.0;

    return Theme::forWidget(this).density().dp(kChoiceRowHeightDp)
        * static_cast<qreal>(items_.size());
}

void SingleChoiceDialog::paintBody(QPainter& painter, const QRectF& body) const
{
    if (items_.isEmpty() || body.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const qreal rowHeight = density.dp(kChoiceRowHeightDp);
    const qreal indicator = density.dp(kChoiceIndicatorDp);
    const qreal labelGap = density.dp(kChoiceLabelGapDp);

    const QFont labelFont = fontFor(theme.type().bodyLarge, density, this);
    const QFontMetricsF labelMetrics(labelFont);

    for (int index = 0; index < static_cast<int>(items_.size()); ++index) {
        const QRectF row = itemRect(index);
        if (row.isEmpty() || row.top() >= body.bottom())
            break;

        if (index == highlighted_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.stateLayer(colours.onSurface, ViewState::Hovered));
            painter.drawPath(theme.shapes().small.path(
                row.adjusted(density.dp(4.0), density.dp(2.0), -density.dp(4.0),
                             -density.dp(2.0))));
        }

        const QPointF centre(row.left() + density.dp(12.0) + indicator / 2.0,
                             row.center().y());
        const qreal radius = indicator / 2.0;
        const bool selected = index == selected_;

        QPen ring(selected ? colours.primary : colours.onSurfaceVariant,
                  density.dp(2.0));
        ring.setCapStyle(Qt::RoundCap);
        painter.setPen(ring);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(centre, radius, radius);

        if (selected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.primary);
            painter.drawEllipse(centre, radius * 0.5, radius * 0.5);
        }

        const qreal labelLeft = row.left() + density.dp(12.0) + indicator + labelGap;
        const qreal labelWidth = std::max(row.right() - density.dp(12.0) - labelLeft,
                                          qreal(0.0));
        if (labelWidth > 0.0) {
            const QString label =
                labelMetrics.elidedText(items_.at(index), Qt::ElideRight, labelWidth);
            painter.setFont(labelFont);
            painter.setPen(colours.onSurface);
            painter.drawText(QRectF(labelLeft, row.top(), labelWidth, rowHeight),
                             static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), label);
        }
    }
}

bool SingleChoiceDialog::handleBodyClick(const QPointF& position)
{
    for (int index = 0; index < static_cast<int>(items_.size()); ++index) {
        if (itemRect(index).contains(position)) {
            setSelectedIndex(index);
            highlighted_ = index;
            requestUpdate();
            return true;
        }
    }

    return false;
}

bool SingleChoiceDialog::handleBodyKey(QKeyEvent* event)
{
    if (items_.isEmpty())
        return false;

    const int count = static_cast<int>(items_.size());

    switch (event->key()) {
    case Qt::Key_Up:
        highlighted_ = std::max(0, highlighted_ - 1);
        requestUpdate();
        return true;

    case Qt::Key_Down:
        highlighted_ = std::min(count - 1, highlighted_ + 1);
        requestUpdate();
        return true;

    case Qt::Key_Home:
        highlighted_ = 0;
        requestUpdate();
        return true;

    case Qt::Key_End:
        highlighted_ = count - 1;
        requestUpdate();
        return true;

    case Qt::Key_Space:
        if (highlighted_ >= 0)
            setSelectedIndex(highlighted_);
        return true;

    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (selected_ >= 0) {
            accept();
        } else if (highlighted_ >= 0) {
            setSelectedIndex(highlighted_);
            accept();
        }
        return true;

    default:
        break;
    }

    return false;
}

// ---------------------------------------------------------------------------
// MultipleChoiceDialog
// ---------------------------------------------------------------------------

MultipleChoiceDialog::MultipleChoiceDialog(QWidget* parent)
    : Dialog(parent)
{
    addActionButton(cancelText_, ActionRole::Reject);
    addActionButton(confirmText_, ActionRole::Accept);
}

MultipleChoiceDialog::MultipleChoiceDialog(const QString& title, const QStringList& items,
                                           QWidget* parent)
    : MultipleChoiceDialog(parent)
{
    setTitle(title);
    setItems(items);
}

void MultipleChoiceDialog::setItems(const QStringList& items)
{
    items_ = items;
    checked_.clear();
    checked_.reserve(items_.size());
    for (int index = 0; index < static_cast<int>(items_.size()); ++index)
        checked_.append(false);

    highlighted_ = -1;
    relayout();
}

QStringList MultipleChoiceDialog::items() const
{
    return items_;
}

void MultipleChoiceDialog::setItemChecked(int index, bool checked)
{
    if (index < 0 || index >= static_cast<int>(items_.size()))
        return;
    if (checked_.at(index) == checked)
        return;

    checked_[index] = checked;
    requestUpdate();
    emit selectionChanged();
}

bool MultipleChoiceDialog::isItemChecked(int index) const
{
    if (index < 0 || index >= static_cast<int>(items_.size()))
        return false;
    return checked_.at(index);
}

void MultipleChoiceDialog::setCheckedIndices(const QList<int>& indices)
{
    for (int index = 0; index < static_cast<int>(checked_.size()); ++index)
        checked_[index] = indices.contains(index);

    requestUpdate();
    emit selectionChanged();
}

QList<int> MultipleChoiceDialog::selectedIndices() const
{
    QList<int> result;
    for (int index = 0; index < static_cast<int>(checked_.size()); ++index) {
        if (checked_.at(index))
            result.append(index);
    }
    return result;
}

QStringList MultipleChoiceDialog::selectedValues() const
{
    QStringList result;
    for (int index = 0; index < static_cast<int>(checked_.size()); ++index) {
        if (checked_.at(index))
            result.append(items_.value(index));
    }
    return result;
}

void MultipleChoiceDialog::setConfirmText(const QString& text)
{
    confirmText_ = text;
    if (Button* button = actionButton(1))
        button->setText(text);
}

QString MultipleChoiceDialog::confirmText() const
{
    return confirmText_;
}

void MultipleChoiceDialog::setCancelText(const QString& text)
{
    cancelText_ = text;
    if (Button* button = actionButton(0))
        button->setText(text);
}

QString MultipleChoiceDialog::cancelText() const
{
    return cancelText_;
}

QRectF MultipleChoiceDialog::itemRect(int index) const
{
    if (index < 0 || index >= static_cast<int>(items_.size()))
        return QRectF();

    const QRectF body = bodyRect();
    if (body.isEmpty())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal rowHeight = density.dp(kChoiceRowHeightDp);
    const qreal y = body.top() + rowHeight * static_cast<qreal>(index);

    return QRectF(body.left(), y, body.width(), rowHeight);
}

qreal MultipleChoiceDialog::bodyHeightForWidth(qreal width) const
{
    Q_UNUSED(width);

    if (items_.isEmpty())
        return 0.0;

    return Theme::forWidget(this).density().dp(kChoiceRowHeightDp)
        * static_cast<qreal>(items_.size());
}

void MultipleChoiceDialog::paintBody(QPainter& painter, const QRectF& body) const
{
    if (items_.isEmpty() || body.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const qreal rowHeight = density.dp(kChoiceRowHeightDp);
    const qreal indicator = density.dp(kChoiceIndicatorDp);
    const qreal labelGap = density.dp(kChoiceLabelGapDp);

    const QFont labelFont = fontFor(theme.type().bodyLarge, density, this);
    const QFontMetricsF labelMetrics(labelFont);

    for (int index = 0; index < static_cast<int>(items_.size()); ++index) {
        const QRectF row = itemRect(index);
        if (row.isEmpty() || row.top() >= body.bottom())
            break;

        if (index == highlighted_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.stateLayer(colours.onSurface, ViewState::Hovered));
            painter.drawPath(theme.shapes().small.path(
                row.adjusted(density.dp(4.0), density.dp(2.0), -density.dp(4.0),
                             -density.dp(2.0))));
        }

        const QRectF box(row.left() + density.dp(12.0),
                         row.center().y() - indicator / 2.0, indicator, indicator);
        const bool checked = checked_.value(index, false);

        if (checked) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.primary);
            painter.drawPath(theme.shapes().extraSmall.path(box));
            drawCheckMark(painter, box, colours.onPrimary, density);
        } else {
            QPen outline(colours.onSurfaceVariant, density.dp(2.0));
            painter.setPen(outline);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(theme.shapes().extraSmall.path(box));
        }

        const qreal labelLeft = box.right() + labelGap;
        const qreal labelWidth = std::max(row.right() - density.dp(12.0) - labelLeft,
                                          qreal(0.0));
        if (labelWidth > 0.0) {
            const QString label =
                labelMetrics.elidedText(items_.at(index), Qt::ElideRight, labelWidth);
            painter.setFont(labelFont);
            painter.setPen(colours.onSurface);
            painter.drawText(QRectF(labelLeft, row.top(), labelWidth, rowHeight),
                             static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), label);
        }
    }
}

bool MultipleChoiceDialog::handleBodyClick(const QPointF& position)
{
    for (int index = 0; index < static_cast<int>(items_.size()); ++index) {
        if (itemRect(index).contains(position)) {
            highlighted_ = index;
            toggle(index);
            return true;
        }
    }

    return false;
}

void MultipleChoiceDialog::toggle(int index)
{
    if (index < 0 || index >= static_cast<int>(items_.size()))
        return;

    checked_[index] = !checked_.at(index);
    requestUpdate();
    emit selectionChanged();
}

bool MultipleChoiceDialog::handleBodyKey(QKeyEvent* event)
{
    if (items_.isEmpty())
        return false;

    const int count = static_cast<int>(items_.size());

    switch (event->key()) {
    case Qt::Key_Up:
        highlighted_ = std::max(0, highlighted_ - 1);
        requestUpdate();
        return true;

    case Qt::Key_Down:
        highlighted_ = std::min(count - 1, highlighted_ + 1);
        requestUpdate();
        return true;

    case Qt::Key_Home:
        highlighted_ = 0;
        requestUpdate();
        return true;

    case Qt::Key_End:
        highlighted_ = count - 1;
        requestUpdate();
        return true;

    case Qt::Key_Space:
        if (highlighted_ >= 0)
            toggle(highlighted_);
        return true;

    case Qt::Key_Return:
    case Qt::Key_Enter:
        accept();
        return true;

    default:
        break;
    }

    return false;
}

// ---------------------------------------------------------------------------
// CustomDialog
// ---------------------------------------------------------------------------

CustomDialog::CustomDialog(QWidget* parent)
    : Dialog(parent)
{
}

CustomDialog::~CustomDialog()
{
    if (content_ != nullptr) {
        removeView(content_);
        content_->hide();
    }
}

void CustomDialog::setContent(View* content)
{
    if (content_.data() == content)
        return;

    if (content_ != nullptr) {
        removeView(content_);
        content_->hide();
    }

    content_ = content;

    if (content_ != nullptr) {
        addView(content_);
        content_->show();
    }

    relayout();
}

View* CustomDialog::content() const
{
    return content_.data();
}

int CustomDialog::addAction(const QString& text, ActionRole role)
{
    return addActionButton(text, role);
}

qreal CustomDialog::bodyHeightForWidth(qreal width) const
{
    if (content_ == nullptr)
        return 0.0;

    content_->measure(MeasureSpec::atMost(width), MeasureSpec::unspecified());
    return content_->measuredSize().height();
}

void CustomDialog::paintBody(QPainter& painter, const QRectF& body) const
{
    Q_UNUSED(painter);
    Q_UNUSED(body);
}

void CustomDialog::arrangeBody(const QRectF& body)
{
    if (content_ == nullptr)
        return;

    content_->measure(MeasureSpec::atMost(body.width()), MeasureSpec::atMost(body.height()));
    const QSizeF size = content_->measuredSize();

    content_->arrange(QRect(qRound(body.left()), qRound(body.top()),
                            qRound(std::min(size.width(), body.width())),
                            qRound(std::min(size.height(), body.height()))));
}

} // namespace md
