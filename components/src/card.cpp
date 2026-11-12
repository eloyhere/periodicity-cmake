// Periodicity — Card (media/content batch).
//
// The layout is one pass over the stacked parts: media, title, subtitle, body,
// action row. Everything is resolved from theme tokens — 16 dp padding, a 12 dp
// medium shape, titleMedium / bodySmall / bodyMedium type roles — so a theme
// swap re-measures and re-arranges through onThemeChanged() instead of leaving
// stale geometry behind.
//
// The container itself is painted by View's background and elevation layers;
// paintContent() only adds the outlined contour and the three text blocks. The
// media Image and the actions are real children, arranged inside the padding,
// which is why the card is a ViewGroup rather than a leaf.

#include <md/components/card.h>

#include <md/components/image.h>
#include <md/components/font.h>
#include <md/theme/theme.h>
#include <md/tokens/elevation.h>
#include <md/tokens/state_layer.h>

#include <QCursor>
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
#include <utility>

namespace md {

namespace {

constexpr qreal kPaddingDp = 16.0;
constexpr qreal kGapDp = 12.0;
constexpr qreal kHeaderGapDp = 8.0;
constexpr qreal kActionGapDp = 8.0;
constexpr qreal kDefaultWidthDp = 320.0;
constexpr qreal kMediaMinHeightDp = 80.0;
constexpr qreal kMediaMaxHeightDp = 200.0;

QFont fontFor(const TextStyle& style, const Density& density, const QWidget* context)
{
    return Font::resolve(context, style, density);
}

QFont titleFont(const Theme& theme, const QWidget* context)
{
    return fontFor(theme.type().titleMedium, theme.density(), context);
}

QFont subtitleFont(const Theme& theme, const QWidget* context)
{
    return fontFor(theme.type().bodySmall, theme.density(), context);
}

QFont bodyFont(const Theme& theme, const QWidget* context)
{
    return fontFor(theme.type().bodyMedium, theme.density(), context);
}

QColor withAlpha(QColor colour, qreal alpha)
{
    colour.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return colour;
}

} // namespace

Card::Card(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::NoFocus);
    syncAppearance();
}

void Card::setVariant(CardVariant variant)
{
    if (variant_ == variant)
        return;

    variant_ = variant;
    syncAppearance();
}

Card::CardVariant Card::variant() const noexcept
{
    return variant_;
}

void Card::setTitle(const QString& title)
{
    if (title_ == title)
        return;

    title_ = title;
    relayoutNow();
}

QString Card::title() const
{
    return title_;
}

void Card::setSubtitle(const QString& subtitle)
{
    if (subtitle_ == subtitle)
        return;

    subtitle_ = subtitle;
    relayoutNow();
}

QString Card::subtitle() const
{
    return subtitle_;
}

void Card::setBody(const QString& body)
{
    if (body_ == body)
        return;

    body_ = body;
    relayoutNow();
}

QString Card::body() const
{
    return body_;
}

void Card::setMedia(Image* media)
{
    if (media_ == media)
        return;

    if (media_ != nullptr) {
        QObject::disconnect(media_, nullptr, this, nullptr);
        removeView(media_);
    }

    media_ = media;

    if (media_ != nullptr) {
        addView(media_);
        connect(media_, &QObject::destroyed, this, [this, media] {
            if (media_ == media) {
                media_ = nullptr;
                relayoutNow();
            }
        });
    }

    relayoutNow();
}

Image* Card::media() const
{
    return media_;
}

void Card::addAction(View* action)
{
    if (action == nullptr || action == this || action == media_ || actions_.contains(action))
        return;

    addView(action);
    actions_.append(action);

    connect(action, &QObject::destroyed, this, [this, action] {
        actions_.removeAll(action);
        relayoutNow();
    });

    relayoutNow();
}

QList<View*> Card::actions() const
{
    return actions_;
}

void Card::clearActions()
{
    if (actions_.isEmpty())
        return;

    for (View* action : actions_) {
        QObject::disconnect(action, nullptr, this, nullptr);
        removeView(action);
    }

    actions_.clear();
    relayoutNow();
}

void Card::setInteractive(bool interactive)
{
    if (interactive_ == interactive)
        return;

    interactive_ = interactive;
    setFocusPolicy(interactive_ ? Qt::StrongFocus : Qt::NoFocus);
    setCursor(interactive_ ? QCursor(Qt::PointingHandCursor) : QCursor());
    syncAppearance();
}

bool Card::isInteractive() const noexcept
{
    return interactive_;
}

void Card::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const qreal resolvedWidth =
        width.resolve(Theme::forWidget(this).density().dp(kDefaultWidthDp));
    const CardLayout layout = computeLayout(QRectF(0.0, 0.0, resolvedWidth, 0.0));

    setMeasuredSize(QSizeF(resolvedWidth, height.resolve(layout.desiredHeight)));
}

void Card::arrangeChildren(const QRectF& content)
{
    const CardLayout layout = computeLayout(content);

    if (media_ != nullptr && layout.hasMedia) {
        media_->measure(MeasureSpec::exactly(layout.media.width()),
                        MeasureSpec::exactly(layout.media.height()));
        media_->arrange(layout.media.toAlignedRect());
    }

    if (!layout.hasActions)
        return;

    const Theme& theme = Theme::forWidget(this);
    const qreal gap = theme.density().dp(kActionGapDp);

    struct Entry
    {
        View* view = nullptr;
        QSizeF size;
    };

    QList<Entry> entries;
    qreal totalWidth = 0.0;
    for (View* action : actions_) {
        if (action == nullptr || action->isHidden())
            continue;

        action->measure(MeasureSpec::atMost(layout.actions.width()),
                        MeasureSpec::unspecified());
        QSizeF size = action->measuredSize();
        size.setWidth(std::max(qreal(0.0), std::min(size.width(), layout.actions.width())));
        size.setHeight(std::max(qreal(0.0), layout.actions.height()));
        entries.append(Entry{action, size});
        totalWidth += size.width();
    }

    if (entries.isEmpty())
        return;

    totalWidth += gap * static_cast<qreal>(entries.size() - 1);

    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;

    // The action group sits on the trailing edge — right in LTR, left in RTL —
    // and the actions keep their logical order inside it, so the first action
    // is the leading one in both directions.
    const qreal groupWidth = totalWidth;
    const qreal groupLeft =
        rightToLeft ? layout.actions.left()
                    : std::max(layout.actions.left(),
                               layout.actions.right() - groupWidth);

    qreal logical = 0.0;
    for (const Entry& entry : entries) {
        const qreal x = groupLeft
            + (rightToLeft ? groupWidth - logical - entry.size.width() : logical);
        const QRectF geometry(x, layout.actions.top(), entry.size.width(),
                              entry.size.height());
        entry.view->measure(MeasureSpec::exactly(entry.size.width()),
                            MeasureSpec::exactly(entry.size.height()));
        entry.view->arrange(geometry.toAlignedRect());
        logical += entry.size.width() + gap;
    }
}

void Card::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const CardLayout layout = computeLayout(content);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    if (variant_ == CardVariant::Outlined) {
        const qreal inset = density.dp(0.5);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
        painter.drawPath(shape().path(content.adjusted(inset, inset, -inset, -inset)));
    }

    const QColor titleColour =
        isEnabled() ? colours.onSurface : withAlpha(colours.onSurface, kDisabledContentOpacity);
    const QColor supportingColour = isEnabled()
        ? colours.onSurfaceVariant
        : withAlpha(colours.onSurfaceVariant, kDisabledContentOpacity);

    if (!title_.isEmpty() && layout.title.width() > 0.0) {
        const QFont font = titleFont(theme, this);
        const QString elided =
            QFontMetricsF(font).elidedText(title_, Qt::ElideRight, layout.title.width());
        painter.setFont(font);
        painter.setPen(titleColour);
        painter.drawText(layout.title,
                         static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), elided);
    }

    if (!subtitle_.isEmpty() && layout.subtitle.width() > 0.0) {
        const QFont font = subtitleFont(theme, this);
        const QString elided =
            QFontMetricsF(font).elidedText(subtitle_, Qt::ElideRight, layout.subtitle.width());
        painter.setFont(font);
        painter.setPen(supportingColour);
        painter.drawText(layout.subtitle,
                         static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), elided);
    }

    if (!body_.isEmpty() && layout.body.width() > 0.0) {
        painter.setFont(bodyFont(theme, this));
        painter.setPen(supportingColour);
        painter.drawText(layout.body,
                         static_cast<int>(Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop),
                         body_);
    }

    painter.restore();
}

void Card::mousePressEvent(QMouseEvent* event)
{
    pressedInside_ = interactive_ && isEnabled() && event->button() == Qt::LeftButton
                     && contentRect().contains(event->position());

    ViewGroup::mousePressEvent(event);
}

void Card::mouseReleaseEvent(QMouseEvent* event)
{
    const bool inside = contentRect().contains(event->position());
    const bool activate = interactive_ && isEnabled() && pressedInside_ && inside
                          && event->button() == Qt::LeftButton;
    pressedInside_ = false;

    // The base class dispatches PointerUp first, so the pressed state is gone
    // before a listener reacts to clicked().
    ViewGroup::mouseReleaseEvent(event);

    if (activate)
        emit clicked();
}

void Card::keyReleaseEvent(QKeyEvent* event)
{
    const bool activate = interactive_ && isEnabled() && !event->isAutoRepeat()
                          && (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return
                              || event->key() == Qt::Key_Enter);

    ViewGroup::keyReleaseEvent(event);

    if (activate)
        emit clicked();
}

void Card::onThemeChanged()
{
    syncAppearance();
    relayoutNow();
}

bool Card::focusable() const
{
    return interactive_ && isEnabled();
}

Card::CardLayout Card::computeLayout(const QRectF& frame) const
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal padding = paddingPx();
    const qreal gap = density.dp(kGapDp);
    const qreal headerGap = density.dp(kHeaderGapDp);
    const qreal innerWidth = std::max(qreal(0.0), frame.width() - padding * 2.0);
    const qreal left = frame.left() + padding;
    const qreal top = frame.top() + padding;

    CardLayout layout;
    qreal cursor = top;

    if (media_ != nullptr && !media_->isHidden()) {
        const qreal preferred = media_->sizeHint().height();
        const qreal mediaHeight = std::clamp(preferred, density.dp(kMediaMinHeightDp),
                                             density.dp(kMediaMaxHeightDp));
        layout.media = QRectF(left, cursor, innerWidth, mediaHeight);
        layout.hasMedia = true;
        cursor += mediaHeight + gap;
    }

    if (!title_.isEmpty()) {
        const qreal height = QFontMetricsF(titleFont(theme, this)).height();
        layout.title = QRectF(left, cursor, innerWidth, height);
        cursor += height;
    }

    if (!subtitle_.isEmpty()) {
        const qreal height = QFontMetricsF(subtitleFont(theme, this)).height();
        layout.subtitle = QRectF(left, cursor, innerWidth, height);
        cursor += height;
    }

    if (!body_.isEmpty()) {
        if (!title_.isEmpty() || !subtitle_.isEmpty())
            cursor += headerGap;

        const qreal height = wrappedBodyHeight(innerWidth);
        layout.body = QRectF(left, cursor, innerWidth, height);
        cursor += height;
    }

    const qreal actionHeight = actionRowHeight(innerWidth);
    layout.hasActions = actionHeight > 0.0;

    qreal desiredHeight = (cursor - top) + padding * 2.0;
    if (layout.hasActions)
        desiredHeight += gap + actionHeight;

    layout.desiredHeight = desiredHeight;

    if (layout.hasActions) {
        layout.actions = QRectF(left, frame.top() + padding + (cursor - top) + gap,
                                innerWidth, actionHeight);
    }

    return layout;
}

qreal Card::paddingPx() const
{
    return Theme::forWidget(this).density().dp(kPaddingDp);
}

qreal Card::actionRowHeight(qreal maxWidth) const
{
    qreal height = 0.0;
    const qreal available = std::max(qreal(0.0), maxWidth);

    for (View* action : actions_) {
        if (action == nullptr || action->isHidden())
            continue;

        action->measure(MeasureSpec::atMost(available), MeasureSpec::unspecified());
        height = std::max(height, action->measuredSize().height());
    }

    return height;
}

qreal Card::wrappedBodyHeight(qreal width) const
{
    const Theme& theme = Theme::forWidget(this);
    const QFont font = bodyFont(theme, this);
    const QFontMetricsF metrics(font);
    const qreal available = std::max(qreal(0.0), width);

    if (available <= 0.0)
        return metrics.height();

    const QRectF bounds =
        metrics.boundingRect(QRectF(0.0, 0.0, available, 1000000.0),
                             static_cast<int>(Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop),
                             body_);
    return std::max(metrics.height(), bounds.height());
}

void Card::syncAppearance()
{
    const Theme& theme = Theme::forWidget(this);

    setShape(theme.shapes().medium);

    QColor container;
    Elevation level = Elevation::Level0;

    switch (variant_) {
    case CardVariant::Elevated:
        container = theme.colours().surfaceContainerLow;
        level = Elevation::Level1;
        break;
    case CardVariant::Filled:
        container = theme.colours().surfaceContainerHighest;
        break;
    case CardVariant::Outlined:
        container = theme.colours().surface;
        break;
    }

    setBackgroundColour(container);
    setElevation(level);

    // A non-interactive card gets no ripple and no hover state layer: it is a
    // surface, not a button.
    setContentColour(interactive_ ? theme.colours().onSurface : QColor(Qt::transparent));
    requestUpdate();
}

void Card::relayoutNow()
{
    updateGeometry();
    requestUpdate();
    arrangeChildren(contentRect());
}

} // namespace md
