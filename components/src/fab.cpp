// Periodicity — the Fab component (design baseline floating action button).
//
// The container is an ordinary View surface: shape, background colour and
// elevation are set from the theme and painted by View's background and
// elevation layers, so the ripple, the state overlay and the focus ring clip to
// the same outline. paintContent() only places the icon and, for the extended
// form, the label.
//
// Measurement is token-driven. Small, regular and large are square containers
// of 40, 56 and 96 dp; the extended form is a 56 dp tall pill whose width is the
// icon, an 18 dp gap, the labelLarge label and the leading/trailing padding.
// Only the extended width changes with setText(); with no label the extended
// form is the 56 dp square that holds just the icon.

#include <md/components/fab.h>

#include <md/components/font.h>
#include <md/theme/theme.h>
#include <md/tokens/elevation.h>
#include <md/tokens/state_layer.h>

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRectF>
#include <QSizeF>

#include <algorithm>

namespace md {

namespace {

constexpr qreal kSmallSizeDp = 40.0;
constexpr qreal kRegularSizeDp = 56.0;
constexpr qreal kLargeSizeDp = 96.0;

constexpr qreal kSmallIconSizeDp = 24.0;
constexpr qreal kLargeIconSizeDp = 36.0;

constexpr qreal kExtendedHeightDp = 56.0;
constexpr qreal kExtendedIconGapDp = 18.0;
// The design baseline's extended FAB uses a 16 dp leading inset and a 20 dp trailing
// inset; only the icon/label gap is overridden by the component contract.
constexpr qreal kExtendedLeadingPaddingDp = 16.0;
constexpr qreal kExtendedTrailingPaddingDp = 20.0;

qreal containerSizeDp(Fab::Size size) noexcept
{
    switch (size) {
    case Fab::Size::Small:
        return kSmallSizeDp;
    case Fab::Size::Regular:
        return kRegularSizeDp;
    case Fab::Size::Large:
        return kLargeSizeDp;
    }
    return kRegularSizeDp;
}

qreal defaultIconSizeDp(Fab::Size size) noexcept
{
    return size == Fab::Size::Large ? kLargeIconSizeDp : kSmallIconSizeDp;
}

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return result;
}

} // namespace

Fab::Fab(QWidget* parent)
    : View(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    iconSizeDp_ = defaultIconSizeDp(size_);
    syncAppearance();
}

Fab::Fab(const QIcon& value, QWidget* parent)
    : Fab(parent)
{
    setIcon(value);
}

void Fab::setIcon(const QIcon& value)
{
    iconDrawable_.setIcon(value);

    // An extended FAB is measured from its icon as well as its label.
    updateGeometry();
    requestUpdate();
}

QIcon Fab::icon() const
{
    return iconDrawable_.icon();
}

void Fab::setIconSize(qreal dp)
{
    const qreal clamped = dp > 0.0 ? dp : 0.0;
    iconSizeExplicit_ = true;

    if (iconSizeDp_ == clamped)
        return;

    iconSizeDp_ = clamped;
    updateGeometry();
    requestUpdate();
}

qreal Fab::iconSize() const noexcept
{
    return iconSizeDp_;
}

void Fab::setSize(Size value)
{
    if (size_ == value)
        return;

    size_ = value;

    if (!iconSizeExplicit_)
        iconSizeDp_ = defaultIconSizeDp(size_);

    syncAppearance();
    updateGeometry();
}

Fab::Size Fab::size() const noexcept
{
    return size_;
}

void Fab::setExtended(bool value)
{
    if (extended_ == value)
        return;

    extended_ = value;
    syncAppearance();
    updateGeometry();
}

bool Fab::isExtended() const noexcept
{
    return extended_;
}

void Fab::setText(const QString& value)
{
    if (text_ == value)
        return;

    text_ = value;

    // Only the extended form measures the label; updateGeometry() is a no-op
    // for a layout that has no parent but is what invalidates a hosted FAB.
    updateGeometry();
    requestUpdate();
}

QString Fab::text() const
{
    return text_;
}

bool Fab::hasIcon() const
{
    return !iconDrawable_.icon().isNull();
}

void Fab::syncAppearance()
{
    const Theme& theme = Theme::forWidget(this);
    const bool enabled = isEnabled();

    Shape outline = theme.shapes().full;
    if (!extended_)
        outline = size_ == Size::Large ? theme.shapes().extraLarge : theme.shapes().large;

    setShape(outline);
    setBackgroundColour(enabled
                            ? theme.colours().primaryContainer
                            : withAlpha(theme.colours().onSurface, kDisabledContainerOpacity));
    setContentColour(enabled
                         ? theme.colours().onPrimaryContainer
                         : withAlpha(theme.colours().onSurface, kDisabledContentOpacity));
    setElevation(enabled ? Elevation::Level3 : Elevation::Level0);
    requestUpdate();
}

void Fab::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal extendedHeight = density.dp(kExtendedHeightDp);

    if (extended_) {
        qreal desired = extendedHeight;
        if (!text_.isEmpty()) {
            const QFontMetricsF metrics(Font::resolve(this, theme.type().labelLarge,
                                                      density));
            desired = density.dp(kExtendedLeadingPaddingDp)
                      + density.dp(kExtendedTrailingPaddingDp)
                      + metrics.horizontalAdvance(text_);
            if (hasIcon())
                desired += density.dp(iconSizeDp_) + density.dp(kExtendedIconGapDp);

            // A pill shorter than its own height would not be a pill; clamping
            // the width also covers a label narrower than the container.
            desired = std::max(desired, extendedHeight);
        }

        setMeasuredSize(QSizeF(width.resolve(desired), height.resolve(extendedHeight)));
        return;
    }

    const qreal side = density.dp(containerSizeDp(size_));
    setMeasuredSize(QSizeF(width.resolve(side), height.resolve(side)));
}

void Fab::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const bool enabled = isEnabled();
    const QColor contentColour =
        enabled ? theme.colours().onPrimaryContainer
                : withAlpha(theme.colours().onSurface, kDisabledContentOpacity);
    const bool showIcon = hasIcon();
    const bool showLabel = extended_ && !text_.isEmpty();

    const qreal iconPx = density.dp(iconSizeDp_);
    const qreal centreY = content.center().y();
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;

    QRectF iconRect;

    if (showLabel) {
        const qreal leading = density.dp(kExtendedLeadingPaddingDp);
        const qreal trailing = density.dp(kExtendedTrailingPaddingDp);
        const qreal gap = density.dp(kExtendedIconGapDp);
        const auto mirrorX = [&](qreal logicalX, qreal width) {
            return content.left()
                   + (rightToLeft ? content.width() - logicalX - width : logicalX);
        };

        // The group is laid out in logical order — icon, gap, label — from the
        // leading inset; mirroring the finished rectangles puts the icon on the
        // leading edge in a right-to-left FAB.
        qreal logicalX = leading;
        if (showIcon) {
            iconRect = QRectF(mirrorX(logicalX, iconPx), centreY - iconPx / 2.0, iconPx,
                              iconPx);
            logicalX += iconPx + gap;
        }

        const QFont font = Font::resolve(this, theme.type().labelLarge, density);
        const QFontMetricsF metrics(font);
        const qreal labelBudget =
            std::max(qreal(0.0), content.width() - logicalX - trailing);

        QString label = text_;
        if (metrics.horizontalAdvance(label) > labelBudget) {
            label = labelBudget > 0.0
                        ? metrics.elidedText(label,
                                             rightToLeft ? Qt::ElideLeft : Qt::ElideRight,
                                             labelBudget)
                        : QString();
        }

        if (!label.isEmpty()) {
            const qreal labelWidth = metrics.horizontalAdvance(label);
            const QRectF labelRect(mirrorX(logicalX, labelWidth), content.top(), labelWidth,
                                   content.height());

            painter.save();
            painter.setFont(font);
            painter.setPen(contentColour);
            painter.drawText(labelRect,
                             static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), label);
            painter.restore();
        }
    } else if (showIcon) {
        iconRect = QRectF(content.center().x() - iconPx / 2.0, centreY - iconPx / 2.0,
                          iconPx, iconPx);
    }

    if (showIcon) {
        const QSizeF iconBox(iconPx, iconPx);
        if (iconDrawable_.size() != iconBox)
            iconDrawable_.setSize(iconBox);
        if (iconDrawable_.colour() != contentColour)
            iconDrawable_.setColour(contentColour);

        iconDrawable_.draw(painter, iconRect);
    }
}

void Fab::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressedInside_ = QRectF(rect()).contains(event->position());

    View::mousePressEvent(event);
}

void Fab::mouseReleaseEvent(QMouseEvent* event)
{
    const bool inside = QRectF(rect()).contains(event->position());
    const bool activate =
        isEnabled() && pressedInside_ && inside && event->button() == Qt::LeftButton;
    pressedInside_ = false;

    // The base class dispatches PointerUp first so the pressed state is gone
    // before a listener reacts to clicked().
    View::mouseReleaseEvent(event);

    if (activate)
        emit clicked();
}

void Fab::keyReleaseEvent(QKeyEvent* event)
{
    const bool activate = isEnabled() && !event->isAutoRepeat()
                          && (event->key() == Qt::Key_Space
                              || event->key() == Qt::Key_Return
                              || event->key() == Qt::Key_Enter);

    View::keyReleaseEvent(event);

    if (activate)
        emit clicked();
}

void Fab::onThemeChanged()
{
    syncAppearance();
    updateGeometry();
}

void Fab::onEnabledChanged(bool)
{
    syncAppearance();
}

} // namespace md
