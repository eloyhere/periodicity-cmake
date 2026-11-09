// Periodicity — the Icon component (framework.md §5.9, §7.6).
//
// All painting is delegated to drawable::IconDrawable, which owns the SourceIn
// tint cache keyed by source, colour, device pixel ratio and direction. The
// component therefore only resolves the tint and the dp box from the theme and
// forwards its content rectangle; it never rasterises anything itself.

#include <md/components/icon.h>

#include <md/theme/theme.h>

#include <QPainter>
#include <QRectF>
#include <QSizeF>

namespace md {

Icon::Icon(QWidget* parent)
    : View(parent)
{
    // An icon is a glyph with no container of its own.
    setBackgroundColour(QColor(Qt::transparent));
    setFocusPolicy(Qt::NoFocus);
}

Icon::Icon(const QIcon& icon, QWidget* parent)
    : Icon(parent)
{
    setIcon(icon);
}

void Icon::setIcon(const QIcon& icon)
{
    drawable_.setIcon(icon);
    requestUpdate();
}

QIcon Icon::icon() const
{
    return drawable_.icon();
}

void Icon::setPixmap(const QPixmap& pixmap)
{
    drawable_.setPixmap(pixmap);
    requestUpdate();
}

QPixmap Icon::pixmap() const
{
    return drawable_.pixmap();
}

void Icon::setSize(qreal dp)
{
    const qreal clamped = dp > 0.0 ? dp : 0.0;
    if (sizeDp_ == clamped)
        return;

    sizeDp_ = clamped;
    requestUpdate();
}

qreal Icon::size() const noexcept
{
    return sizeDp_;
}

void Icon::setColour(const QColor& colour)
{
    if (colour_ == colour)
        return;

    colour_ = colour;
    requestUpdate();
}

QColor Icon::colour() const
{
    return colour_;
}

void Icon::setMirrored(bool mirrored)
{
    if (drawable_.isMirrored() == mirrored)
        return;

    drawable_.setMirrored(mirrored);
    requestUpdate();
}

bool Icon::isMirrored() const noexcept
{
    return drawable_.isMirrored();
}

void Icon::measureOverride(MeasureSpec width, MeasureSpec height)
{
    // sizeHint() is exactly the requested box; the source aspect ratio is
    // honoured inside it by IconDrawable, never by stretching the widget.
    const qreal side = Theme::forWidget(this).density().dp(sizeDp_);
    setMeasuredSize(QSizeF(width.resolve(side), height.resolve(side)));
}

void Icon::paintContent(QPainter& painter, const QRectF& content)
{
    if (drawable_.icon().isNull() && drawable_.pixmap().isNull())
        return;

    const Theme& theme = Theme::forWidget(this);
    const qreal side = theme.density().dp(sizeDp_);
    const QSizeF box(side, side);
    if (drawable_.size() != box)
        drawable_.setSize(box);

    const QColor tint = colour_.isValid() ? colour_ : theme.colours().onSurface;
    if (drawable_.colour() != tint)
        drawable_.setColour(tint);

    drawable_.draw(painter, content);
}

bool Icon::focusable() const
{
    return false;
}

} // namespace md
