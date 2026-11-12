// Periodicity — Tag (media/content batch).
//
// Every dimension is a theme dp constant, and every colour comes from the role
// set: the container chooses primary / container / transparent by variant and
// selection, while the ink is the matching on-role. The icon is drawn through
// IconDrawable so it inherits the tag's ink instead of keeping its source
// colours, and the close glyph is drawn geometrically — no font dependency for
// the multiplication sign.
//
// Measured width is one pass over the visible parts; painting re-derives the
// same layout and elides the label into whatever space the close region leaves.

#include <md/components/tag.h>

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

constexpr qreal kHeightDp = 32.0;
// Design baseline chip measurements: 16 dp on a text side, 8 dp on an icon side, 8 dp
// between elements, 18 dp icons, 32 dp container with an 8 dp corner radius.
constexpr qreal kStartPaddingDp = 16.0;
constexpr qreal kEndPaddingDp = 16.0;
constexpr qreal kIconStartPaddingDp = 8.0;
constexpr qreal kCloseEndPaddingDp = 8.0;
constexpr qreal kDotSizeDp = 8.0;
constexpr qreal kIconSizeDp = 18.0;
constexpr qreal kCloseSizeDp = 18.0;
constexpr qreal kGapDp = 8.0;
constexpr qreal kCloseGapDp = 8.0;
// The close affordance keeps its 18 dp glyph but gets a 24 dp interaction
// disc: an 18 dp disc hides the glyph it is meant to highlight.
constexpr qreal kCloseLayerDp = 24.0;

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

Tag::Tag(QWidget* parent)
    : View(parent)
{
    setBackgroundColour(QColor(Qt::transparent));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    syncAppearance();
}

Tag::Tag(const QString& text, QWidget* parent)
    : Tag(parent)
{
    setText(text);
}

void Tag::setText(const QString& text)
{
    if (text_ == text)
        return;

    text_ = text;
    updateGeometry();
    requestUpdate();
}

QString Tag::text() const
{
    return text_;
}

void Tag::setIcon(const QIcon& icon)
{
    if (iconDrawable_.icon().cacheKey() == icon.cacheKey())
        return;

    iconDrawable_.setIcon(icon);
    updateGeometry();
    requestUpdate();
}

QIcon Tag::icon() const
{
    return iconDrawable_.icon();
}

void Tag::setVariant(TagVariant variant)
{
    if (variant_ == variant)
        return;

    variant_ = variant;
    syncAppearance();
}

Tag::TagVariant Tag::variant() const noexcept
{
    return variant_;
}

void Tag::setClosable(bool closable)
{
    if (closable_ == closable)
        return;

    closable_ = closable;
    closePressed_ = false;
    closeHovered_ = false;
    updateGeometry();
    requestUpdate();
}

bool Tag::isClosable() const noexcept
{
    return closable_;
}

void Tag::setSelectable(bool selectable)
{
    if (selectable_ == selectable)
        return;

    selectable_ = selectable;
    requestUpdate();
}

bool Tag::isSelectable() const noexcept
{
    return selectable_;
}

void Tag::setSelected(bool selected)
{
    if (selected_ == selected)
        return;

    selected_ = selected;
    syncAppearance();
    emit selectedChanged(selected_);
}

bool Tag::isSelected() const noexcept
{
    return selected_;
}

void Tag::setColourDot(bool visible)
{
    if (colourDot_ == visible)
        return;

    colourDot_ = visible;
    updateGeometry();
    requestUpdate();
}

bool Tag::hasColourDot() const noexcept
{
    return colourDot_;
}

void Tag::setDotColour(const QColor& colour)
{
    if (dotColour_ == colour)
        return;

    dotColour_ = colour;
    requestUpdate();
}

QColor Tag::dotColour() const
{
    return dotColour_;
}

void Tag::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFontMetricsF metrics(labelFont(theme));
    const bool close = closable_;
    const bool hasLeading = colourDot_ || hasIcon();
    const qreal startPadding = density.dp(hasLeading ? kIconStartPaddingDp : kStartPaddingDp);
    const qreal endPadding = density.dp(close ? kCloseEndPaddingDp : kEndPaddingDp);
    const qreal gap = density.dp(kGapDp);

    qreal desired = startPadding + endPadding + metrics.horizontalAdvance(text_);
    if (colourDot_)
        desired += density.dp(kDotSizeDp) + gap;
    if (hasIcon())
        desired += density.dp(kIconSizeDp) + gap;
    if (close)
        desired += density.dp(kCloseSizeDp) + density.dp(kCloseGapDp);

    const qreal boxHeight = density.dp(kHeightDp);
    desired = std::max(desired, boxHeight);

    setMeasuredSize(QSizeF(width.resolve(desired), height.resolve(boxHeight)));
}

void Tag::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const TagLayout layout = layoutFor(content);
    const QColor container = containerColour();
    const QColor ink = contentColour();
    const QPainterPath path = shape().path(content);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    if (container.alpha() > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(container);
        painter.drawPath(path);
    }

    if (variant_ == TagVariant::Outlined) {
        // Design baseline: a selected filter chip fills with the secondary container and
        // drops the outline; only the unselected form is stroked, with the
        // softened outlineVariant role (1 dp).
        if (!selected_) {
            const qreal inset = density.dp(0.5);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(theme.colours().outlineVariant, density.dp(1.0)));
            painter.drawPath(shape().path(content.adjusted(inset, inset, -inset, -inset)));
        }
    }

    if (colourDot_ && layout.dot.width() > 0.0) {
        const QColor dot = dotColour_.isValid() ? dotColour_ : theme.colours().primary;
        painter.setPen(Qt::NoPen);
        painter.setBrush(dot);
        painter.drawEllipse(layout.dot);
    }

    if (hasIcon() && layout.icon.width() > 0.0) {
        const QSizeF iconBox(density.dp(kIconSizeDp), density.dp(kIconSizeDp));
        if (iconDrawable_.size() != iconBox)
            iconDrawable_.setSize(iconBox);
        if (iconDrawable_.colour() != ink)
            iconDrawable_.setColour(ink);
        iconDrawable_.draw(painter, layout.icon);
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

    if (closable_ && layout.close.width() > 0.0) {
        if (closeHovered_) {
            const qreal layerSide = density.dp(kCloseLayerDp);
            painter.setPen(Qt::NoPen);
            painter.setBrush(theme.colours().stateLayer(ink, ViewState::Hovered));
            painter.drawEllipse(QRectF(layout.close.center().x() - layerSide / 2.0,
                                       layout.close.center().y() - layerSide / 2.0,
                                       layerSide, layerSide));
        }

        const qreal inset = layout.close.width() * 0.28;
        const QRectF glyph = layout.close.adjusted(inset, inset, -inset, -inset);
        QPen pen = QPen(ink, density.dp(1.6));
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(glyph.topLeft(), glyph.bottomRight());
        painter.drawLine(glyph.topRight(), glyph.bottomLeft());
    }

    painter.restore();
}

void Tag::mousePressEvent(QMouseEvent* event)
{
    pressedInside_ = false;
    closePressed_ = false;

    if (isEnabled() && event->button() == Qt::LeftButton) {
        const QRectF content = contentRect();
        if (closable_ && closeRectFor(content).contains(event->position()))
            closePressed_ = true;
        else
            pressedInside_ = content.contains(event->position());
    }

    View::mousePressEvent(event);
}

void Tag::mouseReleaseEvent(QMouseEvent* event)
{
    const QRectF content = contentRect();
    const bool inside = content.contains(event->position());
    const bool inClose = closable_ && closeRectFor(content).contains(event->position());

    const bool close = closePressed_ && inClose;
    const bool body = pressedInside_ && inside && !inClose;

    pressedInside_ = false;
    closePressed_ = false;

    // The base class dispatches PointerUp first, so the pressed state is gone
    // before a listener reacts to clicked().
    View::mouseReleaseEvent(event);

    if (!isEnabled())
        return;

    if (close) {
        emit closeClicked();
        return;
    }

    if (body) {
        emit clicked();
        if (selectable_)
            toggleSelected();
    }
}

void Tag::mouseMoveEvent(QMouseEvent* event)
{
    View::mouseMoveEvent(event);

    if (!closable_)
        return;

    const bool hovered = closeRectFor(contentRect()).contains(event->position());
    if (hovered == closeHovered_)
        return;

    closeHovered_ = hovered;
    requestUpdate();
}

void Tag::leaveEvent(QEvent* event)
{
    View::leaveEvent(event);

    if (!closeHovered_)
        return;

    closeHovered_ = false;
    requestUpdate();
}

void Tag::keyReleaseEvent(QKeyEvent* event)
{
    if (!isEnabled() || event->isAutoRepeat()) {
        View::keyReleaseEvent(event);
        return;
    }

    const bool activate = event->key() == Qt::Key_Space || event->key() == Qt::Key_Return
                          || event->key() == Qt::Key_Enter;
    const bool close = closable_
                       && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace);

    View::keyReleaseEvent(event);

    if (close) {
        emit closeClicked();
        return;
    }

    if (activate) {
        emit clicked();
        if (selectable_)
            toggleSelected();
    }
}

void Tag::onThemeChanged()
{
    syncAppearance();
    updateGeometry();
    requestUpdate();
}

void Tag::onEnabledChanged(bool enabled)
{
    Q_UNUSED(enabled);
    syncAppearance();
}

bool Tag::focusable() const
{
    return isEnabled();
}

Tag::TagLayout Tag::layoutFor(const QRectF& content) const
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal gap = density.dp(kGapDp);
    const qreal dotSize = density.dp(kDotSizeDp);
    const qreal iconSize = density.dp(kIconSizeDp);
    const qreal closeSize = density.dp(kCloseSizeDp);
    const qreal closeGap = density.dp(kCloseGapDp);
    const qreal endPadding = density.dp(closable_ ? kCloseEndPaddingDp : kEndPaddingDp);
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;

    // Everything is laid out in logical coordinates — distance from the
    // leading edge — and mirrored once at the end, so the dot and the icon
    // start on the right and the close glyph ends on the left in RTL.
    TagLayout layout;
    QRectF dot;
    QRectF icon;
    QRectF text;
    QRectF close;

    qreal leading = density.dp((colourDot_ || hasIcon()) ? kIconStartPaddingDp
                                                         : kStartPaddingDp);

    if (colourDot_) {
        dot = QRectF(leading, (content.height() - dotSize) / 2.0, dotSize, dotSize);
        leading += dotSize + gap;
    }

    if (hasIcon()) {
        icon = QRectF(leading, (content.height() - iconSize) / 2.0, iconSize, iconSize);
        leading += iconSize + gap;
    }

    qreal textTrailing = content.width() - endPadding;
    if (closable_) {
        close = QRectF(content.width() - endPadding - closeSize,
                       (content.height() - closeSize) / 2.0, closeSize, closeSize);
        textTrailing = close.left() - closeGap;
    }

    text = QRectF(leading, 0.0, std::max(qreal(0.0), textTrailing - leading), content.height());

    const auto toPhysical = [&](const QRectF& logical) {
        return rightToLeft
            ? QRectF(content.right() - logical.right(), content.top() + logical.top(),
                     logical.width(), logical.height())
            : QRectF(content.left() + logical.left(), content.top() + logical.top(),
                     logical.width(), logical.height());
    };

    layout.dot = toPhysical(dot);
    layout.icon = toPhysical(icon);
    layout.text = toPhysical(text);
    layout.close = toPhysical(close);
    return layout;
}

QRectF Tag::closeRectFor(const QRectF& content) const
{
    const Density& density = Theme::forWidget(this).density();
    const qreal size = density.dp(kCloseSizeDp);
    const qreal padding = density.dp(kCloseEndPaddingDp);
    const qreal logicalLeft = content.width() - padding - size;

    const qreal x = layoutDirection() == Qt::RightToLeft
        ? content.right() - logicalLeft - size
        : content.left() + logicalLeft;

    return QRectF(x, content.center().y() - size / 2.0, size, size);
}

bool Tag::hasIcon() const
{
    return !iconDrawable_.icon().isNull();
}

QColor Tag::containerColour() const
{
    const ColorScheme& colours = Theme::forWidget(this).colours();

    if (!isEnabled()) {
        return variant_ == TagVariant::Outlined
            ? QColor(Qt::transparent)
            : withAlpha(colours.onSurface, kDisabledContainerOpacity);
    }

    switch (variant_) {
    case TagVariant::Filled:
        return selected_ ? colours.primary : colours.surfaceContainerHighest;
    case TagVariant::Tonal:
        return selected_ ? colours.secondaryContainer : colours.surfaceContainerHigh;
    case TagVariant::Outlined:
        return selected_ ? colours.secondaryContainer : QColor(Qt::transparent);
    }

    return QColor(Qt::transparent);
}

QColor Tag::contentColour() const
{
    const ColorScheme& colours = Theme::forWidget(this).colours();

    if (!isEnabled())
        return withAlpha(colours.onSurface, kDisabledContentOpacity);

    switch (variant_) {
    case TagVariant::Filled:
        return selected_ ? colours.onPrimary : colours.onSurface;
    case TagVariant::Tonal:
        return selected_ ? colours.onSecondaryContainer : colours.onSurface;
    case TagVariant::Outlined:
        return selected_ ? colours.onSecondaryContainer : colours.onSurfaceVariant;
    }

    return colours.onSurface;
}

void Tag::syncAppearance()
{
    const Theme& theme = Theme::forWidget(this);
    // Design baseline chips are 8 dp rounded rectangles, not pills.
    setShape(theme.shapes().small);
    setContentColour(contentColour());
    requestUpdate();
}

void Tag::toggleSelected()
{
    setSelected(!selected_);
}

} // namespace md
