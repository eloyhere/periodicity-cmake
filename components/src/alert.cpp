// Periodicity — Alert.

#include <md/components/alert.h>

#include <md/components/button.h>
#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>

#include <algorithm>

namespace md {

namespace {

constexpr qreal kPaddingDp = 16.0;
constexpr qreal kIconSizeDp = 24.0;
constexpr qreal kIconGapDp = 16.0;
constexpr qreal kCloseAreaDp = 40.0;
constexpr qreal kCloseMarginDp = 8.0;
constexpr qreal kActionGapDp = 8.0;
constexpr qreal kDefaultWidthDp = 360.0;
constexpr qreal kMinHeightDp = 56.0;
constexpr qreal kDescriptionGapDp = 4.0;
constexpr int kMaxDescriptionLines = 3;

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

// Draws the severity glyph. The glyph is a path rather than a font character,
// so it stays crisp at every density and needs no icon asset.
void drawSeverityGlyph(QPainter& painter, const QRectF& box, Alert::Severity severity,
                       const QColor& colour, const Density& density)
{
    const QPointF centre = box.center();
    const qreal radius = std::min(box.width(), box.height()) / 2.0 - density.dp(1.0);
    if (radius <= 0.0)
        return;

    QPen pen(colour, density.dp(2.0));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (severity) {
    case Alert::Severity::Info: {
        painter.drawEllipse(centre, radius, radius);
        const qreal dot = density.dp(1.5);
        painter.setBrush(colour);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(centre.x(), centre.y() - radius * 0.42), dot, dot);
        QPen stem(colour, density.dp(2.0));
        stem.setCapStyle(Qt::RoundCap);
        painter.setPen(stem);
        painter.drawLine(QPointF(centre.x(), centre.y() - radius * 0.1),
                         QPointF(centre.x(), centre.y() + radius * 0.45));
        break;
    }

    case Alert::Severity::Success: {
        painter.drawEllipse(centre, radius, radius);
        QPainterPath check;
        check.moveTo(centre.x() - radius * 0.45, centre.y());
        check.lineTo(centre.x() - radius * 0.1, centre.y() + radius * 0.34);
        check.lineTo(centre.x() + radius * 0.5, centre.y() - radius * 0.36);
        painter.drawPath(check);
        break;
    }

    case Alert::Severity::Warning: {
        QPainterPath triangle;
        triangle.moveTo(centre.x(), centre.y() - radius);
        triangle.lineTo(centre.x() + radius * 0.95, centre.y() + radius * 0.72);
        triangle.lineTo(centre.x() - radius * 0.95, centre.y() + radius * 0.72);
        triangle.closeSubpath();
        painter.drawPath(triangle);

        QPen mark(colour, density.dp(2.0));
        mark.setCapStyle(Qt::RoundCap);
        painter.setPen(mark);
        painter.drawLine(QPointF(centre.x(), centre.y() - radius * 0.34),
                         QPointF(centre.x(), centre.y() + radius * 0.2));
        const qreal dot = density.dp(1.2);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colour);
        painter.drawEllipse(QPointF(centre.x(), centre.y() + radius * 0.46), dot, dot);
        break;
    }

    case Alert::Severity::Error: {
        painter.drawEllipse(centre, radius, radius);
        QPen mark(colour, density.dp(2.0));
        mark.setCapStyle(Qt::RoundCap);
        painter.setPen(mark);
        painter.drawLine(QPointF(centre.x(), centre.y() - radius * 0.45),
                         QPointF(centre.x(), centre.y() + radius * 0.18));
        const qreal dot = density.dp(1.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colour);
        painter.drawEllipse(QPointF(centre.x(), centre.y() + radius * 0.46), dot, dot);
        break;
    }
    }

    painter.restore();
}

} // namespace

Alert::Alert(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::NoFocus);

    action_ = new Button(this);
    action_->setVariant(Button::ButtonVariant::Text);
    addView(action_);
    action_->hide();

    QObject::connect(action_, &Button::clicked, this, [this] { emit actionClicked(); });

    syncAction();
    syncAppearance();
}

void Alert::syncAppearance()
{
    const Theme& theme = Theme::forWidget(this);
    setShape(theme.shapes().medium);
    setElevation(Elevation::Level0);
    setBackgroundColour(containerColour());
    setContentColour(contentColour());
    requestUpdate();
}

void Alert::syncAction()
{
    action_->setText(actionText_);

    if (actionText_.isEmpty()) {
        actionWidth_ = 0.0;
        action_->hide();
    } else {
        action_->measure(MeasureSpec::unspecified(), MeasureSpec::unspecified());
        actionWidth_ = action_->measuredSize().width();
    }

    requestUpdate();
}

void Alert::setSeverity(Severity severity)
{
    if (severity_ == severity)
        return;

    severity_ = severity;
    syncAppearance();
}

Alert::Severity Alert::severity() const noexcept
{
    return severity_;
}

void Alert::setTitle(const QString& title)
{
    if (title_ == title)
        return;

    title_ = title;
    requestUpdate();
}

QString Alert::title() const
{
    return title_;
}

void Alert::setDescription(const QString& description)
{
    if (description_ == description)
        return;

    description_ = description;
    requestUpdate();
}

QString Alert::description() const
{
    return description_;
}

void Alert::setActionText(const QString& text)
{
    if (actionText_ == text)
        return;

    actionText_ = text;
    syncAction();
}

QString Alert::actionText() const
{
    return actionText_;
}

void Alert::setClosable(bool closable)
{
    if (closable_ == closable)
        return;

    closable_ = closable;
    requestUpdate();
}

bool Alert::isClosable() const noexcept
{
    return closable_;
}

QColor Alert::containerColour() const
{
    const ColorScheme& colours = Theme::forWidget(this).colours();

    switch (severity_) {
    case Severity::Info:
        return colours.surfaceContainerHigh;
    case Severity::Success:
        return colours.secondaryContainer;
    case Severity::Warning:
        return colours.tertiaryContainer;
    case Severity::Error:
        return colours.errorContainer;
    }

    return colours.surfaceContainerHigh;
}

QColor Alert::contentColour() const
{
    const ColorScheme& colours = Theme::forWidget(this).colours();

    switch (severity_) {
    case Severity::Info:
        return colours.onSurface;
    case Severity::Success:
        return colours.onSecondaryContainer;
    case Severity::Warning:
        return colours.onTertiaryContainer;
    case Severity::Error:
        return colours.onErrorContainer;
    }

    return colours.onSurface;
}

qreal Alert::actionAreaWidth() const
{
    if (actionText_.isEmpty())
        return 0.0;
    return actionWidth_;
}

qreal Alert::contentTextHeight(qreal textWidth) const
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    qreal total = 0.0;

    if (!title_.isEmpty())
        total += density.sp(theme.type().titleMedium.lineHeightSp);

    if (!description_.isEmpty()) {
        if (!title_.isEmpty())
            total += density.dp(kDescriptionGapDp);

        const QFont bodyFont = fontFor(theme.type().bodyMedium, density, this);
        total += wrappedTextHeight(description_, bodyFont, textWidth,
                                   density.sp(theme.type().bodyMedium.lineHeightSp),
                                   kMaxDescriptionLines);
    }

    return total;
}

void Alert::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    const qreal resolvedWidth = width.resolve(density.dp(kDefaultWidthDp));
    const qreal padding = density.dp(kPaddingDp);
    const qreal iconSize = density.dp(kIconSizeDp);
    const qreal iconGap = density.dp(kIconGapDp);
    const qreal closeArea = density.dp(kCloseAreaDp);
    const qreal closeMargin = density.dp(kCloseMarginDp);
    const qreal actionGap = density.dp(kActionGapDp);

    qreal textWidth = resolvedWidth - padding * 2.0 - iconSize - iconGap;

    if (!actionText_.isEmpty())
        textWidth -= actionGap + actionAreaWidth();

    if (closable_)
        textWidth -= closeArea + closeMargin;

    textWidth = std::max(textWidth, qreal(0.0));

    const qreal contentHeight = contentTextHeight(textWidth);
    const qreal naturalHeight =
        std::max(density.dp(kMinHeightDp), padding * 2.0 + contentHeight);

    setMeasuredSize(QSizeF(resolvedWidth, height.resolve(naturalHeight)));
}

QRectF Alert::closeButtonRect() const
{
    if (!closable_)
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal side = density.dp(kCloseAreaDp);
    const qreal margin = density.dp(kCloseMarginDp);

    return QRectF(static_cast<qreal>(width()) - margin - side,
                  (static_cast<qreal>(height()) - side) / 2.0, side, side);
}

void Alert::arrangeChildren(const QRectF& content)
{
    if (actionText_.isEmpty()) {
        action_->hide();
        return;
    }

    const Density& density = Theme::forWidget(this).density();
    action_->measure(MeasureSpec::unspecified(), MeasureSpec::unspecified());

    const QSizeF size = action_->measuredSize();
    const qreal gap = density.dp(kActionGapDp);
    const qreal padding = density.dp(kPaddingDp);

    qreal right = content.right() - padding;
    if (closable_)
        right = closeButtonRect().left() - gap;

    const qreal x = right - size.width();
    const qreal y = content.top() + (content.height() - size.height()) / 2.0;

    action_->arrange(QRect(qRound(x), qRound(y), qCeil(size.width()), qCeil(size.height())));
    action_->show();
}

void Alert::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    const qreal padding = density.dp(kPaddingDp);
    const qreal iconSize = density.dp(kIconSizeDp);
    const qreal iconGap = density.dp(kIconGapDp);
    const qreal closeArea = density.dp(kCloseAreaDp);
    const qreal closeMargin = density.dp(kCloseMarginDp);
    const qreal actionGap = density.dp(kActionGapDp);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setClipRect(content);

    const QColor foreground = contentColour();
    const QColor secondary = withAlpha(foreground, 0.78);

    const QRectF iconRect(content.left() + padding,
                          content.top() + (content.height() - iconSize) / 2.0, iconSize,
                          iconSize);
    drawSeverityGlyph(painter, iconRect, severity_, foreground, density);

    qreal textLeft = iconRect.right() + iconGap;
    qreal textRight = content.right() - padding;

    if (closable_)
        textRight -= closeArea + closeMargin;

    if (!actionText_.isEmpty())
        textRight -= actionAreaWidth() + actionGap;

    const qreal textWidth = std::max(textRight - textLeft, qreal(0.0));
    const qreal textHeight = contentTextHeight(textWidth);

    qreal y = content.top() + (content.height() - textHeight) / 2.0;

    if (!title_.isEmpty() && textWidth > 0.0) {
        const QFont titleFont = fontFor(theme.type().titleMedium, density, this);
        const qreal titleLineHeight = density.sp(theme.type().titleMedium.lineHeightSp);
        const QFontMetricsF metrics(titleFont);
        const QString label = metrics.elidedText(title_, Qt::ElideRight, textWidth);

        painter.setFont(titleFont);
        painter.setPen(foreground);
        painter.drawText(QRectF(textLeft, y, textWidth, titleLineHeight),
                         static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), label);
        y += titleLineHeight;
    }

    if (!description_.isEmpty() && textWidth > 0.0) {
        if (!title_.isEmpty())
            y += density.dp(kDescriptionGapDp);

        const QFont bodyFont = fontFor(theme.type().bodyMedium, density, this);
        const qreal bodyLineHeight = density.sp(theme.type().bodyMedium.lineHeightSp);
        const qreal descriptionHeight = wrappedTextHeight(
            description_, bodyFont, textWidth, bodyLineHeight, kMaxDescriptionLines);

        drawWrappedText(painter, description_, bodyFont, bodyLineHeight,
                        QRectF(textLeft, y, textWidth, descriptionHeight), secondary,
                        kMaxDescriptionLines);
    }

    if (closable_) {
        const QRectF closeAreaRect = closeButtonRect();

        if (hoverClose_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(theme.colours().stateLayer(foreground, ViewState::Hovered));
            painter.drawEllipse(closeAreaRect);
        }

        const qreal inset = closeAreaRect.width() * 0.3;
        QPen pen(secondary, density.dp(1.8));
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(closeAreaRect.topLeft() + QPointF(inset, inset),
                         closeAreaRect.bottomRight() - QPointF(inset, inset));
        painter.drawLine(closeAreaRect.topRight() + QPointF(-inset, inset),
                         closeAreaRect.bottomLeft() + QPointF(inset, -inset));
    }

    painter.restore();
}

void Alert::mousePressEvent(QMouseEvent* event)
{
    ViewGroup::mousePressEvent(event);

    if (event->button() == Qt::LeftButton)
        pressedClose_ = closable_ && closeButtonRect().contains(event->position());
}

void Alert::mouseReleaseEvent(QMouseEvent* event)
{
    ViewGroup::mouseReleaseEvent(event);

    const bool activate = pressedClose_ && event->button() == Qt::LeftButton
        && closable_ && closeButtonRect().contains(event->position());
    pressedClose_ = false;

    if (activate)
        close();
}

void Alert::mouseMoveEvent(QMouseEvent* event)
{
    ViewGroup::mouseMoveEvent(event);

    const bool hovering = closable_ && closeButtonRect().contains(event->position());
    if (hovering == hoverClose_)
        return;

    hoverClose_ = hovering;
    requestUpdate();
}

void Alert::leaveEvent(QEvent* event)
{
    ViewGroup::leaveEvent(event);

    pressedClose_ = false;

    if (!hoverClose_)
        return;

    hoverClose_ = false;
    requestUpdate();
}

void Alert::close()
{
    if (closedEmitted_)
        return;

    closedEmitted_ = true;
    hide();

    emit closeClicked();
}

Button* Alert::actionButton() const
{
    return action_;
}

void Alert::onShown()
{
    closedEmitted_ = false;
}

void Alert::onThemeChanged()
{
    syncAppearance();
    syncAction();
    requestUpdate();
}

bool Alert::focusable() const
{
    return false;
}

} // namespace md
