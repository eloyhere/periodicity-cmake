// Periodicity — the Button component (framework.md §5.9).
//
// The variant decides the container and content roles; the enabled bit decides
// the disabled opacities (framework.md §3.6). The container is drawn here
// rather than by View's background layer because the visual surface is the
// 40 dp height of the design baseline centred inside the 48 dp touch target, while the
// background layer always fills the whole widget rectangle. The shape still comes from
// View's shape token, so the ripple, the state overlay and the focus ring all
// agree with the painted pill.

#include <md/components/button.h>

#include <md/theme/theme.h>
#include <md/tokens/elevation.h>
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

constexpr qreal kTouchHeightDp = 48.0;
constexpr qreal kVisualHeightDp = 40.0;
// The expressive small-button padding of the design baseline is 16 dp on both sides,
// whether or not the button carries a leading icon (24 dp is no longer recommended);
// the icon and the label stay 8 dp apart.
constexpr qreal kPaddingDp = 16.0;
constexpr qreal kIconGapDp = 8.0;
constexpr qreal kMinWidthDp = 48.0;

QFont labelFontFor(const Theme& theme)
{
    const TextStyle& style = theme.type().labelLarge;
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return result;
}

QColor containerColourFor(Button::ButtonVariant variant, bool enabled, const Theme& theme)
{
    const ColorScheme& colours = theme.colours();

    if (!enabled) {
        switch (variant) {
        case Button::ButtonVariant::Filled:
        case Button::ButtonVariant::FilledTonal:
        case Button::ButtonVariant::Elevated:
            return withAlpha(colours.onSurface, kDisabledContainerOpacity);
        case Button::ButtonVariant::Outlined:
        case Button::ButtonVariant::Text:
            return QColor(Qt::transparent);
        }
    }

    switch (variant) {
    case Button::ButtonVariant::Filled:
        return colours.primary;
    case Button::ButtonVariant::FilledTonal:
        return colours.secondaryContainer;
    case Button::ButtonVariant::Elevated:
        return colours.surfaceAtElevation(Elevation::Level1);
    case Button::ButtonVariant::Outlined:
    case Button::ButtonVariant::Text:
        break;
    }

    return QColor(Qt::transparent);
}

QColor contentColourFor(Button::ButtonVariant variant, bool enabled, const Theme& theme)
{
    const ColorScheme& colours = theme.colours();

    if (!enabled)
        return withAlpha(colours.onSurface, kDisabledContentOpacity);

    switch (variant) {
    case Button::ButtonVariant::Filled:
        return colours.onPrimary;
    case Button::ButtonVariant::FilledTonal:
        return colours.onSecondaryContainer;
    case Button::ButtonVariant::Elevated:
    case Button::ButtonVariant::Outlined:
    case Button::ButtonVariant::Text:
        break;
    }

    return colours.primary;
}

} // namespace

Button::Button(QWidget* parent)
    : View(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));
    syncAppearance();
}

Button::Button(const QString& text, QWidget* parent)
    : Button(parent)
{
    setText(text);
}

void Button::setText(const QString& text)
{
    if (text_ == text)
        return;

    text_ = text;
    requestUpdate();
}

QString Button::text() const
{
    return text_;
}

void Button::setVariant(ButtonVariant variant)
{
    if (variant_ == variant)
        return;

    variant_ = variant;
    syncAppearance();
}

Button::ButtonVariant Button::variant() const noexcept
{
    return variant_;
}

void Button::setIcon(const QIcon& icon)
{
    iconDrawable_.setIcon(icon);
    requestUpdate();
}

QIcon Button::icon() const
{
    return iconDrawable_.icon();
}

void Button::setIconSize(qreal dp)
{
    const qreal clamped = dp > 0.0 ? dp : 0.0;
    if (iconSizeDp_ == clamped)
        return;

    iconSizeDp_ = clamped;
    requestUpdate();
}

qreal Button::iconSize() const noexcept
{
    return iconSizeDp_;
}

bool Button::hasIcon() const
{
    return !iconDrawable_.icon().isNull();
}

void Button::syncAppearance()
{
    const Theme& theme = Theme::forWidget(this);

    setShape(theme.shapes().full);
    setElevation(variant_ == ButtonVariant::Elevated && isEnabled() ? Elevation::Level1
                                                                    : Elevation::Level0);
    setContentColour(contentColourFor(variant_, isEnabled(), theme));
    requestUpdate();
}

void Button::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFontMetricsF metrics(labelFontFor(theme));
    const bool icon = hasIcon();

    qreal desired = metrics.horizontalAdvance(text_);
    if (icon) {
        desired += density.dp(iconSizeDp_);
        if (!text_.isEmpty())
            desired += density.dp(kIconGapDp);
    }

    // Both sides are inset by the same 16 dp, so the group is centred in a box
    // that is symmetric for the text-only and the icon form.
    desired += density.dp(kPaddingDp) * 2.0;
    desired = std::max(desired, density.dp(kMinWidthDp));

    setMeasuredSize(QSizeF(width.resolve(desired),
                           height.resolve(density.dp(kTouchHeightDp))));
}

void Button::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const bool enabled = isEnabled();

    // The 40 dp visual surface, centred in the 48 dp touch target.
    const qreal visualHeight = std::min(content.height(), density.dp(kVisualHeightDp));
    const QRectF visual(content.left(), content.center().y() - visualHeight / 2.0,
                        content.width(), visualHeight);

    const QColor container = containerColourFor(variant_, enabled, theme);
    if (container.alpha() > 0) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(container);
        painter.drawPath(shape().path(visual));
        painter.restore();
    }

    if (variant_ == ButtonVariant::Outlined) {
        const QColor outline = enabled
            ? theme.colours().outline
            : withAlpha(theme.colours().onSurface, kDisabledContainerOpacity);
        QPen pen(outline, density.dp(1.0));
        pen.setJoinStyle(Qt::RoundJoin);
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const qreal inset = density.dp(1.0);
        painter.drawPath(shape().path(visual.adjusted(inset, inset, -inset, -inset)));
        painter.restore();
    }

    const QColor contentColour = contentColourFor(variant_, enabled, theme);
    const QFont font = labelFontFor(theme);
    const QFontMetricsF metrics(font);
    const bool icon = hasIcon();
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
    const qreal gap = density.dp(kIconGapDp);
    const qreal iconSize = density.dp(iconSizeDp_);
    const qreal startPadding = density.dp(kPaddingDp);
    const qreal endPadding = density.dp(kPaddingDp);

    // The group budget is the padded content box minus the icon box: eliding
    // against the full width let an icon button's label run underneath the
    // icon.
    const qreal groupAvailable =
        std::max(qreal(0.0), visual.width() - startPadding - endPadding);
    const qreal labelBudget = icon
        ? std::max(qreal(0.0), groupAvailable - iconSize - gap)
        : groupAvailable;

    QString label = text_;
    if (!label.isEmpty() && metrics.horizontalAdvance(label) > labelBudget) {
        label = labelBudget > 0.0
            ? metrics.elidedText(label, rightToLeft ? Qt::ElideLeft : Qt::ElideRight,
                                 labelBudget)
            : QString();
    }

    const qreal textWidth = label.isEmpty() ? 0.0 : metrics.horizontalAdvance(label);
    qreal groupWidth = textWidth;
    if (icon)
        groupWidth += iconSize + (textWidth > 0.0 ? gap : 0.0);

    // The group is centred inside the padded box, in logical order: icon
    // first, then the label. Mirroring the finished rectangles puts the icon on
    // the leading edge — the right one — in a right-to-left button.
    const qreal groupLeft = startPadding + (groupAvailable - groupWidth) / 2.0;
    const qreal centreY = visual.center().y();
    const auto mirrorX = [&](qreal logicalX, qreal width) {
        return visual.left()
            + (rightToLeft ? visual.width() - logicalX - width : logicalX);
    };

    qreal logicalX = groupLeft;
    QRectF iconRect;
    QRectF textRect;

    if (icon) {
        iconRect = QRectF(mirrorX(logicalX, iconSize), centreY - iconSize / 2.0, iconSize,
                          iconSize);
        logicalX += iconSize + (textWidth > 0.0 ? gap : 0.0);
    }

    if (textWidth > 0.0) {
        textRect = QRectF(mirrorX(logicalX, textWidth), visual.top(), textWidth,
                          visual.height());
    }

    if (icon) {
        const QSizeF iconBox(iconSize, iconSize);
        if (iconDrawable_.size() != iconBox)
            iconDrawable_.setSize(iconBox);
        if (iconDrawable_.colour() != contentColour)
            iconDrawable_.setColour(contentColour);
        iconDrawable_.draw(painter, iconRect);
    }

    if (!label.isEmpty()) {
        painter.save();
        painter.setFont(font);
        painter.setPen(contentColour);
        painter.drawText(textRect, static_cast<int>(Qt::AlignCenter), label);
        painter.restore();
    }
}

void Button::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressedInside_ = QRectF(rect()).contains(event->position());

    View::mousePressEvent(event);
}

void Button::mouseReleaseEvent(QMouseEvent* event)
{
    const bool inside = QRectF(rect()).contains(event->position());
    const bool activate = pressedInside_ && inside && event->button() == Qt::LeftButton;
    pressedInside_ = false;

    // The base class dispatches PointerUp first so the pressed state is gone
    // before a listener reacts to clicked().
    View::mouseReleaseEvent(event);

    if (activate && isEnabled())
        emit clicked();
}

void Button::keyReleaseEvent(QKeyEvent* event)
{
    const bool activate = isEnabled() && !event->isAutoRepeat()
        && (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return
            || event->key() == Qt::Key_Enter);

    View::keyReleaseEvent(event);

    if (activate)
        emit clicked();
}

void Button::changeEvent(QEvent* event)
{
    View::changeEvent(event);

    if (event->type() == QEvent::EnabledChange || event->type() == QEvent::ThemeChange)
        syncAppearance();
}

} // namespace md
