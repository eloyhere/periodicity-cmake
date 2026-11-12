// Periodicity — Link, a hyperlink as text.
//
// The component keeps no cached colour or font: both are resolved from the
// theme on every measure and paint, and the interaction states come from the
// kernel's ViewStates, so a theme change or a hover transition needs no
// bookkeeping here.

#include <md/components/link.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>

#include <algorithm>

namespace md {

namespace {

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

// Composes `overlay` onto `base` as an opaque colour. The overlay is the
// theme's onSurface, so the result darkens on a light scheme and lightens on a
// dark one — the same direction Material's state layer takes.
QColor mix(const QColor& base, const QColor& overlay, qreal amount)
{
    const qreal t = std::clamp(amount, qreal(0.0), qreal(1.0));
    const auto channel = [t](int from, int to) {
        return static_cast<int>(qRound(from + (to - from) * t));
    };

    return QColor(channel(base.red(), overlay.red()),
                  channel(base.green(), overlay.green()),
                  channel(base.blue(), overlay.blue()));
}

constexpr qreal kHoverMix = 0.14;
constexpr qreal kPressedMix = 0.24;
constexpr qreal kDisabledAlpha = 0.38;

} // namespace

Link::Link(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));
    setContentColour(Theme::forWidget(this).colours().primary);
    syncCursor();
}

Link::Link(const QString& text, const QString& href, QWidget* parent)
    : Link(parent)
{
    text_ = text;
    href_ = href;
}

void Link::setText(const QString& text)
{
    if (text_ == text)
        return;

    text_ = text;
    updateGeometry();
    requestUpdate();
}

void Link::setHref(const QString& href)
{
    if (href_ == href)
        return;

    href_ = href;
}

void Link::setVisited(bool visited)
{
    if (visited_ == visited)
        return;

    visited_ = visited;
    requestUpdate();
}

void Link::setUnderline(bool underline)
{
    if (underline_ == underline)
        return;

    underline_ = underline;
    requestUpdate();
}

void Link::setRole(Text::TextRole role)
{
    if (role_ == role && !customStyle_)
        return;

    role_ = role;
    customStyle_ = false;
    updateGeometry();
    requestUpdate();
}

void Link::setTextStyle(const TextStyle& style)
{
    explicitStyle_ = style;
    customStyle_ = true;
    updateGeometry();
    requestUpdate();
}

TextStyle Link::textStyle() const
{
    return resolvedStyle();
}

void Link::setColour(const QColor& colour)
{
    if (explicitColour_ == colour)
        return;

    explicitColour_ = colour;
    requestUpdate();
}

QColor Link::roleColour() const
{
    if (explicitColour_.isValid())
        return explicitColour_;

    const ColorScheme& colours = Theme::forWidget(this).colours();
    return visited_ ? colours.tertiary : colours.primary;
}

QColor Link::colour() const
{
    const QColor base = roleColour();
    if (!isEnabled())
        return withAlpha(base, kDisabledAlpha);

    const ColorScheme& colours = Theme::forWidget(this).colours();
    if (state().testFlag(ViewState::Pressed))
        return mix(base, colours.onSurface, kPressedMix);
    if (state().testFlag(ViewState::Hovered))
        return mix(base, colours.onSurface, kHoverMix);

    return base;
}

TextStyle Link::resolvedStyle() const
{
    if (customStyle_)
        return explicitStyle_;

    const TypeScale& type = Theme::forWidget(this).type();
    switch (role_) {
    case Text::TextRole::Caption:
        return type.bodySmall;
    case Text::TextRole::Label:
        return type.labelLarge;
    case Text::TextRole::Body:
        return type.bodyMedium;
    case Text::TextRole::Title:
        return type.titleMedium;
    }

    return type.bodyMedium;
}

QFont Link::resolvedFont() const
{
    const Theme& theme = Theme::forWidget(this);
    QFont font = fontFor(resolvedStyle(), theme.density(), this);
    font.setUnderline(underline_);
    return font;
}

QRectF Link::linkRect() const
{
    const Theme& theme = Theme::forWidget(this);
    const QFontMetricsF metrics(resolvedFont());
    const qreal lineHeight = std::max(metrics.height(),
                                      theme.density().sp(resolvedStyle().lineHeightSp));
    if (width() <= 0 || height() <= 0)
        return {};

    const qreal top = std::max(qreal(0.0), (static_cast<qreal>(height()) - lineHeight) / 2.0);
    return QRectF(0.0, top, static_cast<qreal>(width()), lineHeight);
}

QString Link::resolveElided(const QFontMetricsF& metrics) const
{
    const QRectF rect = linkRect();
    const Qt::TextElideMode mode =
        layoutDirection() == Qt::RightToLeft ? Qt::ElideLeft : Qt::ElideRight;
    return metrics.elidedText(text_, mode, std::max(qreal(0.0), rect.width()));
}

QString Link::elidedText() const
{
    if (text_.isEmpty())
        return {};

    const QFontMetricsF metrics(resolvedFont());
    return resolveElided(metrics);
}

void Link::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const TextStyle style = resolvedStyle();
    const QFontMetricsF metrics(fontFor(style, theme.density(), this));

    const qreal natural = text_.isEmpty() ? 0.0 : metrics.horizontalAdvance(text_);
    const qreal measuredWidth = width.resolve(natural);
    const qreal lineHeight = std::max(metrics.height(), theme.density().sp(style.lineHeightSp));

    setMeasuredSize(QSizeF(measuredWidth, height.resolve(lineHeight)));
}

void Link::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    if (text_.isEmpty())
        return;

    const QRectF rect = linkRect();
    if (rect.width() <= 0.0 || rect.height() <= 0.0)
        return;

    const QFont font = resolvedFont();
    const QFontMetricsF metrics(font);
    const Qt::Alignment alignment = Qt::AlignVCenter
        | (layoutDirection() == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft);

    painter.save();
    painter.setFont(font);
    painter.setPen(colour());
    painter.drawText(rect, static_cast<int>(alignment), resolveElided(metrics));
    painter.restore();
}

void Link::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);

    if (!isEnabled() || event->button() != Qt::LeftButton)
        return;

    pressedInside_ = linkRect().contains(event->position());
    if (pressedInside_)
        setFocus(Qt::MouseFocusReason);
}

void Link::mouseReleaseEvent(QMouseEvent* event)
{
    const bool wasPressed = pressedInside_;
    pressedInside_ = false;

    View::mouseReleaseEvent(event);

    if (!isEnabled() || event->button() != Qt::LeftButton)
        return;

    if (wasPressed && linkRect().contains(event->position()))
        emit clicked(href_);
}

void Link::leaveEvent(QEvent* event)
{
    pressedInside_ = false;
    View::leaveEvent(event);
}

void Link::keyPressEvent(QKeyEvent* event)
{
    if (isEnabled()) {
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            emit clicked(href_);
            event->accept();
            return;
        default:
            break;
        }
    }

    View::keyPressEvent(event);
}

void Link::changeEvent(QEvent* event)
{
    View::changeEvent(event);

    switch (event->type()) {
    case QEvent::EnabledChange:
        syncCursor();
        requestUpdate();
        break;
    case QEvent::LayoutDirectionChange:
        requestUpdate();
        break;
    default:
        break;
    }
}

void Link::onThemeChanged()
{
    setContentColour(Theme::forWidget(this).colours().primary);
    updateGeometry();
    requestUpdate();
}

bool Link::focusable() const
{
    return isEnabled();
}

void Link::syncCursor()
{
    setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

} // namespace md
