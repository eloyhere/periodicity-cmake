// Periodicity — Notification.

#include <md/components/notification.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSizeF>
#include <QWidget>

#include <algorithm>

namespace md {

namespace {

constexpr qreal kMarginDp = 16.0;
constexpr qreal kMaxWidthDp = 344.0;
constexpr qreal kHeightDp = 48.0;
constexpr qreal kHorizontalPaddingDp = 16.0;
constexpr qreal kActionPaddingDp = 12.0;
constexpr qreal kActionGapDp = 8.0;
constexpr qreal kCloseAreaDp = 40.0;
constexpr qreal kAnchorGapDp = 8.0;

QFont fontFor(const TextStyle& style, const Density& density, const QWidget* context)
{
    return Font::resolve(context, style, density);
}

} // namespace

Notification::Notification(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::NoFocus);

    timer_.setSingleShot(true);
    QObject::connect(&timer_, &QTimer::timeout, this, [this] { dismissCurrent(true); });

    const Theme& theme = Theme::forWidget(this);
    setShape(theme.shapes().extraSmall);
    setElevation(Elevation::Level0);
    setBackgroundColour(theme.colours().inverseSurface);
    setContentColour(theme.colours().inverseOnSurface);

    // A snackbar is hidden until showMessage() gives it something to say.
    hide();
}

void Notification::setAnchor(QWidget* anchor)
{
    if (anchor_.data() == anchor)
        return;

    if (showing_ && anchor_ != nullptr)
        anchor_->removeEventFilter(this);

    anchor_ = anchor;

    if (anchor != nullptr) {
        QObject::connect(anchor, &QObject::destroyed, this, [this](QObject* object) {
            if (!anchor_.isNull() && static_cast<QObject*>(anchor_.data()) != object)
                return;

            anchor_ = nullptr;
            if (showing_) {
                removeFilters();
                installFilters();
                updatePosition();
            }
        });
    }

    if (showing_) {
        installFilters();
        updatePosition();
    }
}

QWidget* Notification::anchor() const
{
    return anchor_.data();
}

qreal Notification::surfaceWidth() const
{
    const Density& density = Theme::forWidget(this).density();
    const qreal margin = density.dp(kMarginDp);
    const qreal maximum = density.dp(kMaxWidthDp);

    if (parentWidget() == nullptr)
        return maximum;

    const qreal available =
        std::max(qreal(0.0), static_cast<qreal>(parentWidget()->width()) - margin * 2.0);

    return std::min(maximum, available);
}

void Notification::showMessage(const QString& text, const QString& actionText, int durationMs)
{
    Message message;
    message.text = text;
    message.actionText = actionText;
    message.durationMs = durationMs;
    queue_.append(message);

    if (!showing_)
        startNext();
}

void Notification::startNext()
{
    if (queue_.isEmpty()) {
        if (showing_) {
            showing_ = false;
            removeFilters();
            hide();
        }
        return;
    }

    const Message message = queue_.takeFirst();
    text_ = message.text;
    actionText_ = message.actionText;
    durationMs_ = message.durationMs;
    showing_ = true;
    hoverAction_ = false;
    hoverClose_ = false;
    pressedAction_ = false;
    pressedClose_ = false;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    if (actionText_.isEmpty()) {
        actionWidth_ = 0.0;
    } else {
        const QFont actionFont = fontFor(theme.type().labelLarge, density, this);
        const QFontMetricsF metrics(actionFont);
        actionWidth_ = metrics.horizontalAdvance(actionText_)
            + density.dp(kActionPaddingDp) * 2.0;
    }

    updatePosition();
    show();
    raise();
    installFilters();
    updateTimer();
    requestUpdate();
}

void Notification::dismissCurrent(bool notifyWhenEmpty)
{
    timer_.stop();

    if (queue_.isEmpty()) {
        const bool wasShowing = showing_;
        showing_ = false;
        text_.clear();
        actionText_.clear();
        actionWidth_ = 0.0;
        removeFilters();
        hide();
        requestUpdate();

        if (wasShowing && notifyWhenEmpty)
            emit closed();
        return;
    }

    startNext();
}

void Notification::updatePosition()
{
    if (parentWidget() == nullptr)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal margin = density.dp(kMarginDp);

    const qreal width = surfaceWidth();
    measure(MeasureSpec::exactly(width), MeasureSpec::unspecified());

    const QSizeF size = measuredSize();
    if (size.width() <= 0.0 || size.height() <= 0.0)
        return;

    const QRect host = parentWidget()->rect();

    qreal x = static_cast<qreal>(host.right() + 1) - margin - size.width();
    qreal y = static_cast<qreal>(host.bottom() + 1) - margin - size.height();

    if (anchor_ != nullptr) {
        const QPoint anchorGlobal = anchor_->mapToGlobal(QPoint(0, 0));
        const QPoint anchorLocal = parentWidget()->mapFromGlobal(anchorGlobal);
        const QRect anchorRect(anchorLocal, anchor_->size());
        const qreal gap = density.dp(kAnchorGapDp);

        x = static_cast<qreal>(anchorRect.right() + 1) - size.width();
        y = static_cast<qreal>(anchorRect.bottom() + 1) + gap;

        if (y + size.height() > static_cast<qreal>(host.bottom() + 1) - margin)
            y = static_cast<qreal>(anchorRect.top()) - gap - size.height();
    }

    const qreal maxX =
        std::max(margin, static_cast<qreal>(host.right() + 1) - margin - size.width());
    const qreal maxY =
        std::max(margin, static_cast<qreal>(host.bottom() + 1) - margin - size.height());

    x = std::clamp(x, margin, maxX);
    y = std::clamp(y, margin, maxY);

    arrange(QRect(qRound(x), qRound(y), qCeil(size.width()), qCeil(size.height())));
}

void Notification::updateTimer()
{
    timer_.stop();

    if (durationMs_ > 0)
        timer_.start(durationMs_);
}

void Notification::installFilters()
{
    if (QWidget* parent = parentWidget())
        parent->installEventFilter(this);

    if (anchor_ != nullptr)
        anchor_->installEventFilter(this);
}

void Notification::removeFilters()
{
    if (QWidget* parent = parentWidget())
        parent->removeEventFilter(this);

    if (anchor_ != nullptr)
        anchor_->removeEventFilter(this);
}

QString Notification::currentText() const
{
    return text_;
}

QString Notification::currentActionText() const
{
    return actionText_;
}

int Notification::queuedCount() const
{
    return static_cast<int>(queue_.size());
}

bool Notification::isShowing() const noexcept
{
    return showing_;
}

void Notification::close()
{
    timer_.stop();
    queue_.clear();

    const bool wasShowing = showing_;
    showing_ = false;
    text_.clear();
    actionText_.clear();
    actionWidth_ = 0.0;

    removeFilters();
    hide();
    requestUpdate();

    if (wasShowing)
        emit closed();
}

void Notification::setClosable(bool closable)
{
    if (closable_ == closable)
        return;

    closable_ = closable;
    requestUpdate();
}

bool Notification::isClosable() const noexcept
{
    return closable_;
}

QRectF Notification::actionRect() const
{
    if (actionText_.isEmpty() || actionWidth_ <= 0.0)
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal margin = density.dp(kHorizontalPaddingDp);
    const qreal closeArea = closable_ ? density.dp(kCloseAreaDp) : 0.0;

    return QRectF(static_cast<qreal>(width()) - margin - closeArea - actionWidth_, 0.0,
                  actionWidth_, static_cast<qreal>(height()));
}

QRectF Notification::closeButtonRect() const
{
    if (!closable_)
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal side = density.dp(kCloseAreaDp);
    const qreal margin = density.dp(kHorizontalPaddingDp);

    return QRectF(static_cast<qreal>(width()) - margin - side,
                  (static_cast<qreal>(height()) - side) / 2.0, side, side);
}

void Notification::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(kMaxWidthDp)),
                           height.resolve(density.dp(kHeightDp))));
}

void Notification::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const qreal padding = density.dp(kHorizontalPaddingDp);
    const qreal actionGap = density.dp(kActionGapDp);
    const qreal closeArea = closable_ ? density.dp(kCloseAreaDp) : 0.0;

    const QFont labelFont = fontFor(theme.type().labelLarge, density, this);
    const QFontMetricsF labelMetrics(labelFont);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setClipPath(shape().path(content));

    qreal textRight = content.right() - padding - closeArea;
    if (actionWidth_ > 0.0)
        textRight -= actionWidth_;

    const qreal textWidth = std::max(textRight - content.left() - padding, qreal(0.0));
    if (textWidth > 0.0 && !text_.isEmpty()) {
        const QString label = labelMetrics.elidedText(text_, Qt::ElideRight, textWidth);
        painter.setFont(labelFont);
        painter.setPen(colours.inverseOnSurface);
        painter.drawText(QRectF(content.left() + padding, content.top(), textWidth,
                                content.height()),
                         static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), label);
    }

    if (actionWidth_ > 0.0 && !actionText_.isEmpty()) {
        const QRectF action = actionRect();

        if (hoverAction_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.stateLayer(colours.inversePrimary, ViewState::Hovered));
            painter.drawPath(theme.shapes().extraSmall.path(action.adjusted(
                density.dp(2.0), density.dp(4.0), -density.dp(2.0), -density.dp(4.0))));
        }

        painter.setFont(labelFont);
        painter.setPen(colours.inversePrimary);
        const QString label =
            labelMetrics.elidedText(actionText_, Qt::ElideRight,
                                    std::max(action.width() - actionGap, qreal(0.0)));
        painter.drawText(action, static_cast<int>(Qt::AlignCenter), label);
    }

    if (closable_) {
        const QRectF close = closeButtonRect();

        if (hoverClose_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(
                colours.stateLayer(colours.inverseOnSurface, ViewState::Hovered));
            painter.drawEllipse(close);
        }

        const qreal inset = close.width() * 0.3;
        QPen pen(colours.inverseOnSurface, density.dp(1.6));
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(close.topLeft() + QPointF(inset, inset),
                         close.bottomRight() - QPointF(inset, inset));
        painter.drawLine(close.topRight() + QPointF(-inset, inset),
                         close.bottomLeft() + QPointF(inset, -inset));
    }

    painter.restore();
}

void Notification::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    pressedAction_ = actionWidth_ > 0.0 && actionRect().contains(event->position());
    pressedClose_ = closable_ && closeButtonRect().contains(event->position());
}

void Notification::mouseReleaseEvent(QMouseEvent* event)
{
    View::mouseReleaseEvent(event);

    if (event->button() != Qt::LeftButton) {
        pressedAction_ = false;
        pressedClose_ = false;
        return;
    }

    const bool action = pressedAction_ && actionWidth_ > 0.0
        && actionRect().contains(event->position());
    const bool closePressed = pressedClose_ && closable_
        && closeButtonRect().contains(event->position());

    pressedAction_ = false;
    pressedClose_ = false;

    if (action) {
        emit actionTriggered();
        dismissCurrent(true);
    } else if (closePressed) {
        dismissCurrent(true);
    }
}

void Notification::mouseMoveEvent(QMouseEvent* event)
{
    View::mouseMoveEvent(event);

    const bool hoverAction =
        actionWidth_ > 0.0 && actionRect().contains(event->position());
    const bool hoverClose = closable_ && closeButtonRect().contains(event->position());

    if (hoverAction == hoverAction_ && hoverClose == hoverClose_)
        return;

    hoverAction_ = hoverAction;
    hoverClose_ = hoverClose;
    requestUpdate();
}

void Notification::leaveEvent(QEvent* event)
{
    View::leaveEvent(event);

    pressedAction_ = false;
    pressedClose_ = false;

    if (!hoverAction_ && !hoverClose_)
        return;

    hoverAction_ = false;
    hoverClose_ = false;
    requestUpdate();
}

void Notification::hideEvent(QHideEvent* event)
{
    View::hideEvent(event);

    if (!showing_)
        return;

    showing_ = false;
    timer_.stop();
    removeFilters();

    emit closed();
}

bool Notification::eventFilter(QObject* watched, QEvent* event)
{
    if (parentWidget() != nullptr && watched == parentWidget()) {
        if (event->type() == QEvent::Resize)
            updatePosition();
        else if (event->type() == QEvent::Hide)
            close();

        return View::eventFilter(watched, event);
    }

    if (anchor_ != nullptr && watched == anchor_.data()) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
            updatePosition();
            break;
        case QEvent::Hide:
            close();
            break;
        default:
            break;
        }

        return View::eventFilter(watched, event);
    }

    return View::eventFilter(watched, event);
}

void Notification::onThemeChanged()
{
    const Theme& theme = Theme::forWidget(this);

    setShape(theme.shapes().extraSmall);
    setBackgroundColour(theme.colours().inverseSurface);
    setContentColour(theme.colours().inverseOnSurface);

    if (showing_)
        updatePosition();

    requestUpdate();
}

bool Notification::focusable() const
{
    return false;
}

} // namespace md
