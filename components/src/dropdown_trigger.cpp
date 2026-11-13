#include <md/components/dropdown_trigger.h>

#include <md/animation/animator.h>
#include <md/components/font.h>
#include <md/components/row_feedback.h>
#include <md/theme/theme.h>
#include <md/tokens/motion.h>
#include <md/tokens/state_layer.h>

#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace md {
namespace {

constexpr qreal kHeightDp = 56.0;
constexpr qreal kHorizontalPaddingDp = 16.0;
constexpr qreal kChevronBoxDp = 24.0;
constexpr qreal kChevronGapDp = 8.0;
constexpr qreal kRadiusDp = 4.0;
constexpr qreal kMinWidthDp = 220.0;

} // namespace

DropdownTrigger::DropdownTrigger(QWidget* parent)
    : View(parent)
    , feedback_(this)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setInteractionFeedbackEnabled(false);
    setProperty("md.rowFeedback", QVariant::fromValue<QObject*>(&feedback_));
    connect(&feedback_, &RowFeedback::repaintRequested, this, [this] { update(); });

    chevronAnimator_ = new animation::Animator(this);
    connect(chevronAnimator_, &animation::Animator::finished, this, [this] {
        setChevronProgress(open_ ? 1.0 : 0.0);
    });

    syncAppearance();
    setAccessibleName(tr("Choose"));
}

void DropdownTrigger::setLabel(const QString& label)
{
    if (label_ == label)
        return;

    label_ = label;
    setAccessibleName(label_.isEmpty() ? tr("Choose") : label_);
    updateGeometry();
    update();
}

QString DropdownTrigger::label() const
{
    return label_;
}

void DropdownTrigger::setPlaceholder(const QString& placeholder)
{
    if (placeholder_ == placeholder)
        return;

    placeholder_ = placeholder;
    updateGeometry();
    update();
}

QString DropdownTrigger::placeholder() const
{
    return placeholder_;
}

void DropdownTrigger::setOpen(bool open)
{
    if (open_ == open)
        return;

    open_ = open;
    const Theme& theme = Theme::forWidget(this);
    MotionSpec spec = theme.motion().level(open ? MotionLevel::Level2 : MotionLevel::Level1);
    spec.easing = easingCurve(open ? Easing::EmphasizedDecelerate : Easing::EmphasizedAccelerate);

    const qreal from = chevronProgress_;
    const qreal target = open ? 1.0 : 0.0;

    if (!isVisible() || spec.duration.count() <= 0
        || qFuzzyCompare(from + 1.0, target + 1.0)) {
        chevronAnimator_->stop();
        setChevronProgress(target);
        return;
    }

    chevronAnimator_->stop();
    chevronAnimator_->start(spec, [this, from, target](qreal fraction) {
        setChevronProgress(from + (target - from) * std::clamp(fraction, qreal(0.0), qreal(1.0)));
    });
}

bool DropdownTrigger::isOpen() const noexcept
{
    return open_;
}

qreal DropdownTrigger::chevronProgress() const noexcept
{
    return chevronProgress_;
}

void DropdownTrigger::setManualAnimationClock(bool enabled)
{
    chevronAnimator_->setManualClock(enabled);
    feedback_.setManualAnimationClock(enabled);
}

bool DropdownTrigger::isManualAnimationClock() const noexcept
{
    return chevronAnimator_->isManualClock();
}

void DropdownTrigger::advanceAnimation(std::chrono::milliseconds delta)
{
    chevronAnimator_->advanceTimeline(delta);
    feedback_.advanceAnimation(delta);
}

RowFeedback* DropdownTrigger::rowFeedback() noexcept
{
    return &feedback_;
}

void DropdownTrigger::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFont textFont = Font::resolve(this, theme.type().bodyLarge, density);
    const QFontMetricsF metrics(textFont);
    const QString text = label_.isEmpty() ? placeholder_ : label_;
    const qreal natural = density.dp(kHorizontalPaddingDp) * 2.0
                          + metrics.horizontalAdvance(text)
                          + density.dp(kChevronGapDp) + density.dp(kChevronBoxDp);
    setMeasuredSize(QSizeF(width.resolve(std::max(natural, density.dp(kMinWidthDp))),
                           height.resolve(density.dp(kHeightDp))));
}

void DropdownTrigger::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setClipPath(shape().path(content));

    feedback_.paintRow(painter, 0, content, false, density.dp(kRadiusDp),
                       colours.onSurface);

    const qreal padding = density.dp(kHorizontalPaddingDp);
    const qreal chevronSize = density.dp(kChevronBoxDp);
    const QRectF textRect(content.left() + padding, content.top(),
                          std::max<qreal>(0.0, content.width() - padding * 2.0
                                                  - chevronSize - density.dp(kChevronGapDp)),
                          content.height());

    const QFont textFont = Font::resolve(this, theme.type().bodyLarge, density);
    const QFontMetricsF metrics(textFont);
    const QString text = label_.isEmpty() ? placeholder_ : label_;
    painter.setFont(textFont);
    painter.setPen(label_.isEmpty() ? colours.onSurfaceVariant : colours.onSurface);
    painter.drawText(textRect, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                     metrics.elidedText(text, Qt::ElideRight, textRect.width()));

    const QPointF centre(content.right() - padding - chevronSize * 0.5,
                         content.center().y());
    painter.save();
    painter.translate(centre);
    painter.rotate(180.0 * chevronProgress_);
    const qreal size = density.dp(4.5);
    QPainterPath chevron;
    chevron.moveTo(-size, -size * 0.45);
    chevron.lineTo(0.0, size * 0.55);
    chevron.lineTo(size, -size * 0.45);
    QPen pen(colours.onSurfaceVariant, density.dp(1.8), Qt::SolidLine,
             Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(chevron);
    painter.restore();

    painter.restore();
}

void DropdownTrigger::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);
    if (event->button() != Qt::LeftButton)
        return;

    pressed_ = true;
    feedback_.pointerPressed(0, event->position(), contentRect());
    event->accept();
}

void DropdownTrigger::mouseReleaseEvent(QMouseEvent* event)
{
    const bool activate = pressed_ && event->button() == Qt::LeftButton
                          && contentRect().contains(event->position());
    pressed_ = false;
    feedback_.pointerReleased();

    if (activate) {
        event->accept();
        emit activationRequested(false);
        return;
    }

    View::mouseReleaseEvent(event);
}

void DropdownTrigger::mouseMoveEvent(QMouseEvent* event)
{
    feedback_.pointerMoved(contentRect().contains(event->position()) ? 0 : -1);
    View::mouseMoveEvent(event);
}

void DropdownTrigger::leaveEvent(QEvent* event)
{
    feedback_.pointerLeft();
    View::leaveEvent(event);
}

void DropdownTrigger::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        event->accept();
        emit activationRequested(false);
        return;
    case Qt::Key_Down:
        event->accept();
        emit activationRequested(true);
        return;
    default:
        break;
    }

    View::keyPressEvent(event);
}

void DropdownTrigger::changeEvent(QEvent* event)
{
    View::changeEvent(event);
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        setAccessibleName(label_.isEmpty() ? tr("Choose") : label_);
        update();
    }
}

void DropdownTrigger::onThemeChanged()
{
    syncAppearance();
    update();
}

void DropdownTrigger::onHidden()
{
    feedback_.clear();
}

void DropdownTrigger::setChevronProgress(qreal progress)
{
    chevronProgress_ = std::clamp(progress, qreal(0.0), qreal(1.0));
    update();
}

void DropdownTrigger::syncAppearance()
{
    const Theme& theme = Theme::forWidget(this);
    setBackgroundColour(theme.colours().surfaceContainerHighest);
    setContentColour(theme.colours().onSurface);
    setShape(theme.shapes().extraSmall);
    setElevation(Elevation::Level0);
}

} // namespace md
