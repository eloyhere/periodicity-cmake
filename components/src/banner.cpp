// Periodicity — Banner.
//
// The surface is painted by the banner rather than by View's background layer,
// because the reveal animation has to fade and slide the container together
// with its text; the background layer always fills the whole rectangle at full
// opacity. The action buttons remain real child widgets and are moved by the
// same offset, so the whole banner appears to slide as one piece.

#include <md/components/banner.h>

#include <md/components/button.h>
#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QEasingCurve>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>

#include <algorithm>

namespace md {

namespace {

constexpr qreal kHorizontalPaddingDp = 16.0;
constexpr qreal kVerticalPaddingDp = 14.0;
constexpr qreal kIconSizeDp = 24.0;
constexpr qreal kIconGapDp = 16.0;
constexpr qreal kActionGapDp = 8.0;
constexpr qreal kCloseSizeDp = 40.0;
constexpr qreal kMinWidthDp = 320.0;

QFont fontFor(const TextStyle& style, const Density& density, const QWidget* context)
{
    return Font::resolve(context, style, density);
}

// Draws at most two wrapped lines, clipped to `rect`. A banner is a compact
// surface: a description that needs more than two lines belongs in a dialog.
void drawWrappedText(QPainter& painter, const QString& text, const QFont& font,
                     qreal lineHeight, const QRectF& rect, const QColor& colour)
{
    if (text.isEmpty() || rect.isEmpty())
        return;

    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignLeft);

    QTextLayout layout(text, font);
    layout.setTextOption(option);
    layout.beginLayout();

    qreal y = 0.0;
    int lines = 0;
    while (lines < 2) {
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

} // namespace

Banner::Banner(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setBackgroundColour(QColor(Qt::transparent));

    primary_ = new Button(this);
    primary_->setVariant(Button::ButtonVariant::Text);
    addView(primary_);
    primary_->hide();
    connect(primary_, &Button::clicked, this, [this] { emit actionClicked(Action::Primary); });

    secondary_ = new Button(this);
    secondary_->setVariant(Button::ButtonVariant::Text);
    addView(secondary_);
    secondary_->hide();
    connect(secondary_, &Button::clicked, this, [this] { emit actionClicked(Action::Secondary); });

    revealTimer_.setInterval(16);
    connect(&revealTimer_, &QTimer::timeout, this, [this] { tickReveal(); });

    syncAppearance();
    syncButtons();
}

void Banner::syncAppearance()
{
    const Theme& theme = Theme::forWidget(this);
    setShape(theme.shapes().medium);
    setElevation(Elevation::Level0);
    setBackgroundColour(QColor(Qt::transparent));
    requestUpdate();
}

void Banner::setIcon(const QIcon& icon)
{
    iconDrawable_.setIcon(icon);
    requestUpdate();
}

QIcon Banner::icon() const
{
    return iconDrawable_.icon();
}

void Banner::setTitle(const QString& title)
{
    if (title_ == title)
        return;

    title_ = title;
    requestUpdate();
}

QString Banner::title() const
{
    return title_;
}

void Banner::setDescription(const QString& description)
{
    if (description_ == description)
        return;

    description_ = description;
    if (isVisible())
        applyRevealLayout();
    requestUpdate();
}

QString Banner::description() const
{
    return description_;
}

void Banner::setPrimaryAction(const QString& text)
{
    if (primaryText_ == text)
        return;

    primaryText_ = text;
    syncButtons();
    applyRevealLayout();
    requestUpdate();
}

QString Banner::primaryAction() const
{
    return primaryText_;
}

void Banner::setSecondaryAction(const QString& text)
{
    if (secondaryText_ == text)
        return;

    secondaryText_ = text;
    syncButtons();
    applyRevealLayout();
    requestUpdate();
}

QString Banner::secondaryAction() const
{
    return secondaryText_;
}

void Banner::setCloseButtonVisible(bool visible)
{
    if (closeVisible_ == visible)
        return;

    closeVisible_ = visible;
    applyRevealLayout();
    requestUpdate();
}

bool Banner::isCloseButtonVisible() const
{
    return closeVisible_;
}

Button* Banner::primaryButton() const
{
    return primary_;
}

Button* Banner::secondaryButton() const
{
    return secondary_;
}

void Banner::syncButtons()
{
    const Density& density = Theme::forWidget(this).density();
    const qreal gap = density.dp(kActionGapDp);

    primary_->setText(primaryText_);
    secondary_->setText(secondaryText_);

    actionsWidth_ = 0.0;
    if (!primaryText_.isEmpty()) {
        primary_->measure(MeasureSpec::unspecified(), MeasureSpec::unspecified());
        actionsWidth_ += primary_->measuredSize().width();
    }
    if (!secondaryText_.isEmpty()) {
        secondary_->measure(MeasureSpec::unspecified(), MeasureSpec::unspecified());
        actionsWidth_ += secondary_->measuredSize().width();
    }
    if (!primaryText_.isEmpty() && !secondaryText_.isEmpty())
        actionsWidth_ += gap;

    requestUpdate();
}

qreal Banner::barHeightPx() const
{
    const Density& density = Theme::forWidget(this).density();
    return density.dp(description_.isEmpty() ? 72.0 : 80.0);
}

qreal Banner::revealOffset() const
{
    return (reveal_ - 1.0) * barHeightPx();
}

QRectF Banner::closeButtonArea() const
{
    const Density& density = Theme::forWidget(this).density();
    const qreal side = density.dp(kCloseSizeDp);
    const qreal margin = density.dp(8.0);

    return QRectF(static_cast<qreal>(width()) - margin - side,
                  (static_cast<qreal>(height()) - side) / 2.0, side, side);
}

QRectF Banner::closeButtonRect() const
{
    return closeButtonArea().translated(0.0, revealOffset());
}

void Banner::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(kMinWidthDp)),
                           height.resolve(barHeightPx())));
}

void Banner::arrangeChildren(const QRectF& content)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal gap = density.dp(kActionGapDp);
    const qreal offset = revealOffset();
    const bool visible = reveal_ > 0.001;

    const QRectF closeArea = closeButtonArea();
    qreal right = closeVisible_ ? closeArea.left() - gap : content.right() - density.dp(kHorizontalPaddingDp);

    const auto place = [&](Button* button, bool present) {
        if (!present) {
            button->hide();
            return;
        }

        button->measure(MeasureSpec::unspecified(), MeasureSpec::unspecified());
        const QSizeF size = button->measuredSize();
        const qreal x = right - size.width();
        button->arrange(QRect(qRound(x),
                              qRound((content.height() - size.height()) / 2.0 + offset),
                              qRound(size.width()), qRound(size.height())));
        button->setVisible(visible);
        right = x - gap;
    };

    place(primary_, !primaryText_.isEmpty());
    place(secondary_, !secondaryText_.isEmpty());
}

void Banner::applyRevealLayout()
{
    arrangeChildren(contentRect());
}

void Banner::startReveal(qreal target)
{
    revealTarget_ = target;
    revealFrom_ = reveal_;

    const MotionSpec spec = Theme::forWidget(this).motion().medium2();
    if (spec.duration.count() <= 0) {
        reveal_ = target;
        finishReveal();
        return;
    }

    revealClock_.restart();
    revealTimer_.start();
}

void Banner::tickReveal()
{
    const MotionSpec spec = Theme::forWidget(this).motion().medium2();
    const qint64 total = std::max<qint64>(1, spec.duration.count());
    const qreal raw = std::clamp(
        static_cast<qreal>(revealClock_.elapsed()) / static_cast<qreal>(total), qreal(0.0),
        qreal(1.0));

    const QEasingCurve curve = spec.easing.toEasingCurve();
    const qreal eased = curve.valueForProgress(raw);
    reveal_ = revealFrom_ + (revealTarget_ - revealFrom_) * eased;

    applyRevealLayout();
    requestUpdate();

    if (raw >= 1.0)
        finishReveal();
}

void Banner::finishReveal()
{
    revealTimer_.stop();
    reveal_ = revealTarget_;
    applyRevealLayout();
    requestUpdate();

    if (reveal_ <= 0.001)
        hide();
}

void Banner::showAnimated()
{
    closedEmitted_ = false;

    if (!isVisible()) {
        reveal_ = 0.0;
        show();
    }

    if (reveal_ >= 1.0) {
        applyRevealLayout();
        requestUpdate();
        return;
    }

    startReveal(1.0);
}

void Banner::hideAnimated()
{
    if (!isVisible()) {
        reveal_ = 0.0;
        applyRevealLayout();
        requestUpdate();
        return;
    }

    startReveal(0.0);
}

void Banner::close()
{
    if (closedEmitted_)
        return;

    closedEmitted_ = true;
    hideAnimated();
    emit closed();
}

qreal Banner::revealProgress() const noexcept
{
    return reveal_;
}

bool Banner::isRevealed() const noexcept
{
    return reveal_ >= 0.999;
}

void Banner::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const qreal padding = density.dp(kHorizontalPaddingDp);
    const qreal gap = density.dp(kActionGapDp);
    const qreal iconSize = density.dp(kIconSizeDp);
    const bool hasIcon = !iconDrawable_.icon().isNull() || !iconDrawable_.pixmap().isNull();

    painter.save();
    painter.setClipRect(content);
    painter.translate(0.0, revealOffset());
    painter.setOpacity(reveal_);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // The container.
    painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
    painter.setBrush(colours.surfaceContainerHigh);
    painter.drawPath(shape().path(content));

    // Text limits: after the icon and before the actions and the close glyph.
    qreal x = content.left() + padding;
    if (hasIcon) {
        const QRectF iconRect(x, content.center().y() - iconSize / 2.0, iconSize, iconSize);
        iconDrawable_.setSize(QSizeF(iconSize, iconSize));
        iconDrawable_.setColour(colours.primary);
        iconDrawable_.draw(painter, iconRect);
        x += iconSize + density.dp(kIconGapDp);
    }

    qreal textRight = closeVisible_ ? closeButtonArea().left() - gap
                                    : content.right() - padding;
    if (actionsWidth_ > 0.0)
        textRight -= actionsWidth_ + gap;

    const qreal textWidth = std::max(textRight - x, qreal(0.0));
    const qreal titleLineHeight = density.sp(theme.type().titleMedium.lineHeightSp);
    const qreal bodyLineHeight = density.sp(theme.type().bodyMedium.lineHeightSp);

    qreal titleTop = content.center().y() - titleLineHeight / 2.0;
    if (!description_.isEmpty())
        titleTop = content.top() + density.dp(kVerticalPaddingDp);

    if (!title_.isEmpty() && textWidth > 0.0) {
        const QFont titleFont = fontFor(theme.type().titleMedium, density, this);
        const QFontMetricsF titleMetrics(titleFont);
        const QString label = titleMetrics.elidedText(title_, Qt::ElideRight, textWidth);

        painter.setFont(titleFont);
        painter.setPen(colours.onSurface);
        painter.drawText(QRectF(x, titleTop, textWidth, titleLineHeight),
                         static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), label);
    }

    if (!description_.isEmpty() && textWidth > 0.0) {
        const QFont bodyFont = fontFor(theme.type().bodyMedium, density, this);
        const QRectF descriptionRect(x, titleTop + titleLineHeight + density.dp(2.0),
                                     textWidth,
                                     content.bottom() - density.dp(8.0)
                                         - (titleTop + titleLineHeight));
        drawWrappedText(painter, description_, bodyFont, bodyLineHeight, descriptionRect,
                        colours.onSurfaceVariant);
    }

    if (closeVisible_) {
        const QRectF closeArea = closeButtonArea();

        if (hoverClose_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.stateLayer(colours.onSurface, ViewState::Hovered));
            painter.drawEllipse(closeArea);
        }

        const qreal inset = density.dp(12.0);
        const QRectF glyph = closeArea.adjusted(inset, inset, -inset, -inset);

        QPen pen(colours.onSurfaceVariant, density.dp(1.8));
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(glyph.topLeft(), glyph.bottomRight());
        painter.drawLine(glyph.topRight(), glyph.bottomLeft());
    }

    painter.restore();
}

void Banner::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);

    if (event->button() == Qt::LeftButton)
        pressedClose_ = closeButtonRect().contains(event->position());
}

void Banner::mouseReleaseEvent(QMouseEvent* event)
{
    View::mouseReleaseEvent(event);

    const bool activate = pressedClose_ && event->button() == Qt::LeftButton
        && closeButtonRect().contains(event->position());
    pressedClose_ = false;

    if (activate)
        close();
}

void Banner::mouseMoveEvent(QMouseEvent* event)
{
    View::mouseMoveEvent(event);

    const bool hovering = closeButtonRect().contains(event->position());
    if (hovering == hoverClose_)
        return;

    hoverClose_ = hovering;
    requestUpdate();
}

void Banner::leaveEvent(QEvent* event)
{
    View::leaveEvent(event);

    if (!hoverClose_)
        return;

    hoverClose_ = false;
    requestUpdate();
}

void Banner::onThemeChanged()
{
    syncAppearance();
    syncButtons();
    applyRevealLayout();
    requestUpdate();
}

} // namespace md
