// Periodicity — Chip (M3 assist, filter, input and suggestion chips).
//
// Measurement is one font pass over the visible parts: the optional leading
// 18 dp slot, the label and the optional trailing close target. Painting
// re-derives the same layout and elides the label into whatever space the
// close affordance leaves.
//
// The container and the outline are painted here rather than by View's
// background layer because the outline is a selected/unselected state, not a
// static surface; the shape still comes from the theme's small token so the
// ripple, the state overlay and the focus ring agree with the painted outline.
// The check mark and the close glyph are stroked geometrically — no icon theme
// or font dependency — while a caller-supplied leading icon goes through
// IconDrawable so it inherits the chip's ink instead of its source colours.

#include <md/components/chip.h>

#include <md/theme/theme.h>
#include <md/tokens/state_layer.h>

#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>
#include <QSizeF>

#include <algorithm>

namespace md {

namespace {

// M3 chip measurements: 32 dp container with an 8 dp corner radius, 16 dp on a
// text edge, 8 dp on an icon edge, 18 dp leading icons, 8 dp between elements.
constexpr qreal kHeightDp = 32.0;
constexpr qreal kStartPaddingDp = 16.0;
constexpr qreal kStartIconPaddingDp = 8.0;
constexpr qreal kEndPaddingDp = 16.0;
constexpr qreal kEndClosePaddingDp = 8.0;
constexpr qreal kIconSizeDp = 18.0;
constexpr qreal kCloseSizeDp = 18.0;
constexpr qreal kGapDp = 8.0;
constexpr qreal kOutlineWidthDp = 1.0;
constexpr qreal kCheckStrokeDp = 2.0;
constexpr qreal kCloseStrokeDp = 1.6;
// WCAG 2.5.8: the close affordance is a 24 dp target, larger than the 18 dp
// glyph it surrounds.
constexpr qreal kCloseTargetDp = 24.0;

QFont labelFont(const Theme& theme)
{
    const TextStyle& style = theme.type().labelLarge;
    const Density& density = theme.density();
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(density.sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, density.sp(style.trackingSp));
    return font;
}

QColor withAlpha(QColor colour, qreal alpha)
{
    colour.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return colour;
}

} // namespace

Chip::Chip(QWidget* parent)
    : View(parent)
{
    setBackgroundColour(QColor(Qt::transparent));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    syncAppearance();
}

Chip::Chip(const QString& text, QWidget* parent)
    : Chip(parent)
{
    setText(text);
}

void Chip::setText(const QString& text)
{
    if (text_ == text)
        return;

    text_ = text;
    updateGeometry();
    requestUpdate();
}

QString Chip::text() const
{
    return text_;
}

void Chip::setVariant(Variant variant)
{
    if (variant_ == variant)
        return;

    variant_ = variant;

    // Assist and suggestion chips have no selected state, so switching to one
    // of them clears a selection that no longer has a meaning. Switching to or
    // from a selected filter also changes the width when the check slot
    // appears or disappears, hence the geometry invalidation.
    const bool cleared = selected_ && !isSelectable();
    if (cleared)
        selected_ = false;

    syncAppearance();
    updateGeometry();

    if (cleared)
        emit selectedChanged(false);
}

Chip::Variant Chip::variant() const noexcept
{
    return variant_;
}

void Chip::setLeadingIcon(const QIcon& icon)
{
    if (leadingDrawable_.icon().cacheKey() == icon.cacheKey())
        return;

    leadingDrawable_.setIcon(icon);
    updateGeometry();
    requestUpdate();
}

QIcon Chip::leadingIcon() const
{
    return leadingDrawable_.icon();
}

void Chip::setSelected(bool selected)
{
    if (!isSelectable()) {
        // Assist and suggestion chips are actions, not toggles: they never
        // enter the selected state and never report a change.
        if (selected_) {
            selected_ = false;
            syncAppearance();
        }
        return;
    }

    if (selected_ == selected)
        return;

    selected_ = selected;
    syncAppearance();
    updateGeometry();
    emit selectedChanged(selected_);
}

bool Chip::isSelected() const noexcept
{
    return selected_;
}

void Chip::setClosable(bool closable)
{
    if (closable_ == closable)
        return;

    closable_ = closable;
    closePressed_ = false;
    closeHovered_ = false;
    updateGeometry();
    requestUpdate();
}

bool Chip::isClosable() const noexcept
{
    return closable_;
}

void Chip::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFontMetricsF metrics(labelFont(theme));
    const bool leading = showsCheck() || hasLeadingIcon();
    const bool close = showsClose();

    qreal desired = metrics.horizontalAdvance(text_);
    desired += density.dp(leading ? kStartIconPaddingDp : kStartPaddingDp);

    if (leading)
        desired += density.dp(kIconSizeDp) + density.dp(kGapDp);

    if (close) {
        desired += density.dp(kCloseTargetDp) + density.dp(kGapDp)
                   + density.dp(kEndClosePaddingDp);
    } else {
        desired += density.dp(kEndPaddingDp);
    }

    desired = std::max(desired, density.dp(kHeightDp));

    setMeasuredSize(QSizeF(width.resolve(desired),
                           height.resolve(density.dp(kHeightDp))));
}

void Chip::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QColor container = containerColour();
    const QColor ink = contentColour();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    if (container.alpha() > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(container);
        painter.drawPath(shape().path(content));
    }

    // Unselected chips are outlined; the selected form fills with the
    // secondary container and drops the stroke (M3 filter chip).
    if (!selected_) {
        const qreal inset = density.dp(kOutlineWidthDp) / 2.0;
        QPen pen(outlineColour(), density.dp(kOutlineWidthDp));
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(pen);
        painter.drawPath(shape().path(content.adjusted(inset, inset, -inset, -inset)));
    }

    const ChipLayout layout = layoutFor(content);

    if (showsCheck()) {
        // The check replaces the leading icon in the same slot, so a selected
        // filter chip does not shift its label.
        const QRectF box = layout.leading;
        QPainterPath check;
        check.moveTo(box.left() + box.width() * 0.16, box.top() + box.height() * 0.52);
        check.lineTo(box.left() + box.width() * 0.42, box.top() + box.height() * 0.76);
        check.lineTo(box.left() + box.width() * 0.84, box.top() + box.height() * 0.26);

        QPen pen(ink, density.dp(kCheckStrokeDp));
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(pen);
        painter.drawPath(check);
    } else if (hasLeadingIcon() && !layout.leading.isEmpty()) {
        const QSizeF iconBox(density.dp(kIconSizeDp), density.dp(kIconSizeDp));
        if (leadingDrawable_.size() != iconBox)
            leadingDrawable_.setSize(iconBox);
        if (leadingDrawable_.colour() != ink)
            leadingDrawable_.setColour(ink);
        leadingDrawable_.draw(painter, layout.leading);
    }

    if (!text_.isEmpty() && layout.text.width() > 0.0) {
        const QFont font = labelFont(theme);
        const QFontMetricsF metrics(font);
        const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
        const QString elided = metrics.elidedText(
            text_, rightToLeft ? Qt::ElideLeft : Qt::ElideRight, layout.text.width());

        painter.setFont(font);
        painter.setPen(ink);
        painter.drawText(layout.text,
                         static_cast<int>((rightToLeft ? Qt::AlignRight : Qt::AlignLeft)
                                          | Qt::AlignVCenter),
                         elided);
    }

    if (showsClose() && !layout.close.isEmpty()) {
        if (closeHovered_) {
            const qreal layerSide = density.dp(kCloseTargetDp);
            painter.setPen(Qt::NoPen);
            painter.setBrush(theme.colours().stateLayer(ink, ViewState::Hovered));
            painter.drawEllipse(QRectF(layout.close.center().x() - layerSide / 2.0,
                                       layout.close.center().y() - layerSide / 2.0,
                                       layerSide, layerSide));
        }

        const qreal inset = layout.close.width() * 0.28;
        const QRectF glyph = layout.close.adjusted(inset, inset, -inset, -inset);
        QPen pen = QPen(ink, density.dp(kCloseStrokeDp));
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(glyph.topLeft(), glyph.bottomRight());
        painter.drawLine(glyph.topRight(), glyph.bottomLeft());
    }

    painter.restore();
}

void Chip::mousePressEvent(QMouseEvent* event)
{
    pressedInside_ = false;
    closePressed_ = false;

    if (isEnabled() && event->button() == Qt::LeftButton) {
        const QRectF content = contentRect();
        if (showsClose() && closeRectFor(content).contains(event->position()))
            closePressed_ = true;
        else
            pressedInside_ = content.contains(event->position());
    }

    View::mousePressEvent(event);
}

void Chip::mouseReleaseEvent(QMouseEvent* event)
{
    const QRectF content = contentRect();
    const bool inside = content.contains(event->position());
    const bool inClose = showsClose() && closeRectFor(content).contains(event->position());

    const bool close = closePressed_ && inClose;
    const bool body = pressedInside_ && inside && !inClose;

    pressedInside_ = false;
    closePressed_ = false;

    // The base class dispatches PointerUp first, so the pressed state is gone
    // before a listener reacts to clicked() or closed().
    View::mouseReleaseEvent(event);

    if (!isEnabled())
        return;

    if (close) {
        emit closed();
        return;
    }

    if (body)
        activate();
}

void Chip::mouseMoveEvent(QMouseEvent* event)
{
    View::mouseMoveEvent(event);

    if (!showsClose())
        return;

    const bool hovered = closeRectFor(contentRect()).contains(event->position());
    if (hovered == closeHovered_)
        return;

    closeHovered_ = hovered;
    requestUpdate();
}

void Chip::leaveEvent(QEvent* event)
{
    View::leaveEvent(event);

    if (!closeHovered_)
        return;

    closeHovered_ = false;
    requestUpdate();
}

void Chip::keyReleaseEvent(QKeyEvent* event)
{
    if (!isEnabled() || event->isAutoRepeat()) {
        View::keyReleaseEvent(event);
        return;
    }

    const bool activateByKey = event->key() == Qt::Key_Space || event->key() == Qt::Key_Return
                               || event->key() == Qt::Key_Enter;
    const bool closeByKey = showsClose()
                            && (event->key() == Qt::Key_Delete
                                || event->key() == Qt::Key_Backspace);

    View::keyReleaseEvent(event);

    if (closeByKey) {
        emit closed();
        return;
    }

    if (activateByKey)
        activate();
}

void Chip::onThemeChanged()
{
    syncAppearance();
    updateGeometry();
    requestUpdate();
}

void Chip::onEnabledChanged(bool enabled)
{
    Q_UNUSED(enabled);
    syncAppearance();
}

Chip::ChipLayout Chip::layoutFor(const QRectF& content) const
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal gap = density.dp(kGapDp);
    const qreal iconSize = density.dp(kIconSizeDp);
    const qreal closeSize = density.dp(kCloseSizeDp);
    const qreal closeTarget = density.dp(kCloseTargetDp);
    const qreal endPadding = density.dp(showsClose() ? kEndClosePaddingDp : kEndPaddingDp);
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
    const bool leading = showsCheck() || hasLeadingIcon();

    // Everything is laid out in logical coordinates — distance from the
    // leading edge — and mirrored once at the end, so the icon starts on the
    // right and the close glyph ends on the left in RTL.
    ChipLayout layout;
    QRectF icon;
    QRectF text;
    QRectF close;

    qreal leadingEdge = density.dp(leading ? kStartIconPaddingDp : kStartPaddingDp);

    if (leading) {
        icon = QRectF(leadingEdge, (content.height() - iconSize) / 2.0, iconSize, iconSize);
        leadingEdge += iconSize + gap;
    }

    qreal textTrailing = content.width() - endPadding;
    if (showsClose()) {
        const qreal targetLeft = content.width() - endPadding - closeTarget;
        close = QRectF(targetLeft + (closeTarget - closeSize) / 2.0,
                       (content.height() - closeSize) / 2.0, closeSize, closeSize);
        textTrailing = targetLeft - gap;
    }

    text = QRectF(leadingEdge, 0.0, std::max(qreal(0.0), textTrailing - leadingEdge),
                  content.height());

    const auto toPhysical = [&](const QRectF& logical) {
        return rightToLeft
            ? QRectF(content.right() - logical.right(), content.top() + logical.top(),
                     logical.width(), logical.height())
            : QRectF(content.left() + logical.left(), content.top() + logical.top(),
                     logical.width(), logical.height());
    };

    layout.leading = toPhysical(icon);
    layout.text = toPhysical(text);
    layout.close = toPhysical(close);
    return layout;
}

QRectF Chip::closeRectFor(const QRectF& content) const
{
    const Density& density = Theme::forWidget(this).density();
    const qreal size = density.dp(kCloseTargetDp);
    const qreal padding = density.dp(kEndClosePaddingDp);
    const qreal logicalLeft = content.width() - padding - size;

    const qreal x = layoutDirection() == Qt::RightToLeft
        ? content.right() - logicalLeft - size
        : content.left() + logicalLeft;

    return QRectF(x, content.center().y() - size / 2.0, size, size);
}

bool Chip::hasLeadingIcon() const
{
    return !leadingDrawable_.icon().isNull();
}

bool Chip::showsCheck() const noexcept
{
    return variant_ == Variant::Filter && selected_;
}

bool Chip::showsClose() const noexcept
{
    return closable_;
}

bool Chip::isSelectable() const noexcept
{
    // M3 gives only the filter chip a selected state; the input chip carries a
    // selection here as well because the API exposes one and removing it would
    // silently discard a host's state. Assist and suggestion chips are always
    // stateless.
    return variant_ == Variant::Filter || variant_ == Variant::Input;
}

QColor Chip::containerColour() const
{
    const ColorScheme& colours = Theme::forWidget(this).colours();

    if (!isEnabled()) {
        return selected_ ? withAlpha(colours.onSurface, kDisabledContainerOpacity)
                         : QColor(Qt::transparent);
    }

    return selected_ ? colours.secondaryContainer : QColor(Qt::transparent);
}

QColor Chip::contentColour() const
{
    const ColorScheme& colours = Theme::forWidget(this).colours();

    if (!isEnabled())
        return withAlpha(colours.onSurface, kDisabledContentOpacity);

    return selected_ ? colours.onSecondaryContainer : colours.onSurfaceVariant;
}

QColor Chip::outlineColour() const
{
    const ColorScheme& colours = Theme::forWidget(this).colours();

    if (!isEnabled())
        return withAlpha(colours.onSurface, kDisabledContainerOpacity);

    return colours.outlineVariant;
}

void Chip::syncAppearance()
{
    const Theme& theme = Theme::forWidget(this);
    // M3 chips are 8 dp rounded rectangles, not pills.
    setShape(theme.shapes().small);
    setContentColour(contentColour());
    requestUpdate();
}

void Chip::toggleSelected()
{
    setSelected(!selected_);
}

void Chip::activate()
{
    emit clicked();

    if (isSelectable())
        toggleSelected();
}

} // namespace md
