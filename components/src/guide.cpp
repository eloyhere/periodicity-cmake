// Periodicity — GuideView, a guided-tour overlay.
//
// The scrim is a path subtraction, the bubble is one rounded rectangle plus
// the text runs, and the three actions are real Button children so keyboard
// focus and ripples behave exactly as they do elsewhere. The overlay is modal
// by construction: a child widget that covers its parent receives the pointer
// events first, so the tour cannot be dismissed by clicking through it.

#include <md/components/guide.h>

#include <md/components/button.h>
#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QResizeEvent>
#include <QSizeF>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>
#include <QWidget>

#include <algorithm>

namespace md {

namespace {

constexpr qreal kBubblePaddingDp = 20.0;
constexpr qreal kBubbleGapDp = 8.0;
constexpr qreal kScreenMarginDp = 8.0;
constexpr qreal kButtonHeightDp = 48.0;
constexpr qreal kHighlightRadiusDp = 12.0;
constexpr qreal kHighlightWidthDp = 2.0;
constexpr int kMaxDescriptionLines = 6;

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

void drawWrapped(QPainter& painter, const QString& text, const QFont& font, qreal lineHeight,
                 const QRectF& area, const QColor& colour)
{
    if (text.isEmpty() || area.isEmpty())
        return;

    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignLeft);

    QTextLayout layout(text, font);
    layout.setTextOption(option);
    layout.beginLayout();

    qreal y = 0.0;
    int lines = 0;
    while (lines < kMaxDescriptionLines) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;

        line.setLineWidth(area.width());
        line.setPosition(QPointF(0.0, y));
        y += lineHeight;
        ++lines;
    }
    layout.endLayout();

    painter.save();
    painter.setClipRect(area);
    painter.setPen(colour);
    layout.draw(&painter, QPointF(area.left(), area.top()));
    painter.restore();
}

} // namespace

GuideStep::GuideStep(QWidget* targetWidget, QString titleText, QString descriptionText)
    : target(targetWidget)
    , title(std::move(titleText))
    , description(std::move(descriptionText))
{
}

GuideView::GuideView(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setBackgroundColour(QColor(Qt::transparent));

    previousButton_ = new Button(this);
    previousButton_->setVariant(Button::ButtonVariant::Text);
    addView(previousButton_);
    previousButton_->hide();
    connect(previousButton_, &Button::clicked, this, [this] { previous(); });

    nextButton_ = new Button(this);
    nextButton_->setVariant(Button::ButtonVariant::Filled);
    addView(nextButton_);
    nextButton_->hide();
    connect(nextButton_, &Button::clicked, this, [this] { next(); });

    skipButton_ = new Button(this);
    skipButton_->setVariant(Button::ButtonVariant::Text);
    addView(skipButton_);
    skipButton_->hide();
    connect(skipButton_, &Button::clicked, this, [this] { skip(); });

    syncButtons();
}

void GuideView::setSteps(const QList<GuideStep>& steps)
{
    steps_ = steps;

    if (stepCount() == 0)
        index_ = 0;
    else
        index_ = std::clamp(index_, 0, stepCount() - 1);

    if (active_) {
        installTargetFilters();
        syncButtons();
        reposition();
    }

    requestUpdate();
}

QList<GuideStep> GuideView::steps() const
{
    return steps_;
}

int GuideView::stepCount() const
{
    return static_cast<int>(steps_.size());
}

int GuideView::currentStep() const noexcept
{
    return index_;
}

void GuideView::setCurrentStep(int index)
{
    if (stepCount() == 0)
        return;

    const int clamped = std::clamp(index, 0, stepCount() - 1);
    const bool changed = clamped != index_;
    index_ = clamped;

    if (active_) {
        syncButtons();
        reposition();
    }

    requestUpdate();

    if (changed)
        emit stepChanged(index_);
}

void GuideView::setScrimOpacity(qreal opacity)
{
    const qreal clamped = std::clamp(opacity, qreal(0.0), qreal(1.0));
    if (qFuzzyCompare(scrimOpacity_, clamped))
        return;

    scrimOpacity_ = clamped;
    requestUpdate();
}

qreal GuideView::scrimOpacity() const noexcept
{
    return scrimOpacity_;
}

void GuideView::setSkipVisible(bool visible)
{
    if (skipVisible_ == visible)
        return;

    skipVisible_ = visible;
    if (active_)
        reposition();

    requestUpdate();
}

bool GuideView::isSkipVisible() const
{
    return skipVisible_;
}

void GuideView::setLabels(const QString& previous, const QString& next, const QString& skip)
{
    previousLabel_ = previous;
    nextLabel_ = next;
    skipLabel_ = skip;
    syncButtons();

    if (active_)
        reposition();

    requestUpdate();
}

QString GuideView::previousLabel() const
{
    return previousLabel_;
}

QString GuideView::nextLabel() const
{
    return nextLabel_;
}

QString GuideView::skipLabel() const
{
    return skipLabel_;
}

void GuideView::setBubbleWidth(qreal dp)
{
    const qreal clamped = std::max(qreal(0.0), dp);
    if (qFuzzyCompare(bubbleWidthDp_, clamped))
        return;

    bubbleWidthDp_ = clamped;
    if (active_)
        reposition();

    requestUpdate();
}

qreal GuideView::bubbleWidth() const noexcept
{
    return bubbleWidthDp_;
}

const GuideStep* GuideView::currentStepData() const
{
    if (index_ < 0 || index_ >= stepCount())
        return nullptr;

    return &steps_.at(index_);
}

void GuideView::syncButtons()
{
    previousButton_->setText(previousLabel_);
    nextButton_->setText(nextLabel_);
    skipButton_->setText(skipLabel_);
}

void GuideView::start()
{
    if (parentWidget() == nullptr || steps_.isEmpty())
        return;

    active_ = true;
    index_ = 0;

    syncButtons();
    arrange(parentWidget()->rect());
    show();
    raise();
    installTargetFilters();
    reposition();

    emit stepChanged(index_);
}

void GuideView::next()
{
    if (!active_ || steps_.isEmpty())
        return;

    if (index_ + 1 < stepCount())
        setCurrentStep(index_ + 1);
    else
        finish();
}

void GuideView::previous()
{
    if (!active_ || index_ <= 0)
        return;

    setCurrentStep(index_ - 1);
}

void GuideView::stopTour()
{
    active_ = false;
    removeTargetFilters();
    hide();
}

void GuideView::skip()
{
    if (!active_)
        return;

    stopTour();
    emit skipped();
}

void GuideView::finish()
{
    if (!active_)
        return;

    stopTour();
    emit finished();
}

bool GuideView::isActive() const noexcept
{
    return active_;
}

QRectF GuideView::highlightRect() const
{
    return highlight_;
}

QRectF GuideView::bubbleRect() const
{
    return bubble_;
}

Button* GuideView::previousButton() const
{
    return previousButton_;
}

Button* GuideView::nextButton() const
{
    return nextButton_;
}

Button* GuideView::skipButton() const
{
    return skipButton_;
}

void GuideView::installTargetFilters()
{
    removeTargetFilters();

    for (const GuideStep& step : steps_) {
        QWidget* target = step.target.data();
        if (target == nullptr)
            continue;

        target->installEventFilter(this);
        filteredTargets_.append(target);
    }

    if (parentWidget() != nullptr)
        parentWidget()->installEventFilter(this);
}

void GuideView::removeTargetFilters()
{
    for (const QPointer<QWidget>& target : filteredTargets_) {
        if (target != nullptr)
            target->removeEventFilter(this);
    }
    filteredTargets_.clear();

    if (parentWidget() != nullptr)
        parentWidget()->removeEventFilter(this);
}

void GuideView::updateHighlight()
{
    highlight_ = QRectF();

    const GuideStep* step = currentStepData();
    if (step == nullptr)
        return;

    QWidget* target = step->target.data();
    if (target == nullptr)
        return;

    const QPoint topLeft = mapFromGlobal(target->mapToGlobal(QPoint(0, 0)));
    const QRectF targetRect(QPointF(topLeft), QSizeF(target->size()));
    const qreal padding = Theme::forWidget(this).density().dp(step->paddingDp);

    highlight_ = targetRect.adjusted(-padding, -padding, padding, padding);
}

void GuideView::reposition()
{
    if (!active_)
        return;

    updateHighlight();
    arrangeChildren(contentRect());
    requestUpdate();
}

qreal GuideView::descriptionHeight(qreal width) const
{
    const GuideStep* step = currentStepData();
    if (step == nullptr || step->description.isEmpty() || width <= 0.0)
        return 0.0;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFont font = fontFor(theme.type().bodyMedium, density, this);
    const qreal lineHeight = density.sp(theme.type().bodyMedium.lineHeightSp);

    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    QTextLayout layout(step->description, font);
    layout.setTextOption(option);
    layout.beginLayout();

    qreal y = 0.0;
    int lines = 0;
    while (lines < kMaxDescriptionLines) {
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

void GuideView::measureOverride(MeasureSpec width, MeasureSpec height)
{
    QSizeF natural(640.0, 480.0);
    if (parentWidget() != nullptr)
        natural = QSizeF(parentWidget()->size());

    setMeasuredSize(QSizeF(width.resolve(natural.width()), height.resolve(natural.height())));
}

void GuideView::arrangeChildren(const QRectF& content)
{
    if (!active_ || steps_.isEmpty() || content.isEmpty()) {
        previousButton_->hide();
        nextButton_->hide();
        skipButton_->hide();
        bubble_ = QRectF();
        return;
    }

    updateHighlight();

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal padding = density.dp(kBubblePaddingDp);
    const qreal gap = density.dp(kBubbleGapDp);
    const qreal margin = density.dp(kScreenMarginDp);
    const qreal buttonHeight = density.dp(kButtonHeightDp);
    const qreal titleHeight = density.sp(theme.type().titleMedium.lineHeightSp);

    const qreal availableWidth = std::max(content.width() - margin * 2.0, qreal(0.0));
    const qreal bubbleWidth = std::min(density.dp(bubbleWidthDp_), availableWidth);
    if (bubbleWidth <= 0.0) {
        bubble_ = QRectF();
        return;
    }

    const qreal innerWidth = std::max(bubbleWidth - padding * 2.0, qreal(1.0));
    const qreal descriptionSpace = descriptionHeight(innerWidth);
    const qreal bubbleHeight = padding * 2.0 + titleHeight
        + (descriptionSpace > 0.0 ? gap + descriptionSpace : 0.0) + gap + buttonHeight;

    QRectF anchor = highlight_.isEmpty() ? QRectF(content.center(), QSizeF(0.0, 0.0))
                                         : highlight_;

    const qreal spaceBelow = content.bottom() - anchor.bottom();
    const qreal spaceAbove = anchor.top() - content.top();

    qreal y = 0.0;
    if (spaceBelow >= bubbleHeight + gap || spaceBelow >= spaceAbove)
        y = anchor.bottom() + gap;
    else
        y = anchor.top() - gap - bubbleHeight;

    qreal x = anchor.center().x() - bubbleWidth / 2.0;

    const qreal minX = content.left() + margin;
    const qreal maxX = std::max(minX, content.right() - margin - bubbleWidth);
    const qreal minY = content.top() + margin;
    const qreal maxY = std::max(minY, content.bottom() - margin - bubbleHeight);

    x = std::clamp(x, minX, maxX);
    y = std::clamp(y, minY, maxY);

    bubble_ = QRectF(x, y, bubbleWidth, bubbleHeight);

    // --- actions -----------------------------------------------------------
    const qreal rowTop = bubble_.bottom() - padding - buttonHeight;
    qreal right = bubble_.right() - padding;

    nextButton_->measure(MeasureSpec::unspecified(), MeasureSpec::unspecified());
    const QSizeF nextSize = nextButton_->measuredSize();
    nextButton_->arrange(QRect(qRound(right - nextSize.width()),
                               qRound(rowTop + (buttonHeight - nextSize.height()) / 2.0),
                               qRound(nextSize.width()), qRound(nextSize.height())));
    nextButton_->show();
    right -= nextSize.width() + gap;

    if (index_ > 0) {
        previousButton_->measure(MeasureSpec::unspecified(), MeasureSpec::unspecified());
        const QSizeF previousSize = previousButton_->measuredSize();
        previousButton_->arrange(QRect(qRound(right - previousSize.width()),
                                       qRound(rowTop + (buttonHeight - previousSize.height()) / 2.0),
                                       qRound(previousSize.width()),
                                       qRound(previousSize.height())));
        previousButton_->show();
    } else {
        previousButton_->hide();
    }

    if (skipVisible_) {
        skipButton_->measure(MeasureSpec::unspecified(), MeasureSpec::unspecified());
        const QSizeF skipSize = skipButton_->measuredSize();
        skipButton_->arrange(QRect(qRound(bubble_.left() + padding),
                                   qRound(rowTop + (buttonHeight - skipSize.height()) / 2.0),
                                   qRound(skipSize.width()), qRound(skipSize.height())));
        skipButton_->show();
    } else {
        skipButton_->hide();
    }

    requestUpdate();
}

void GuideView::paintContent(QPainter& painter, const QRectF& content)
{
    if (!active_ || steps_.isEmpty() || content.isEmpty())
        return;

    const GuideStep* step = currentStepData();
    if (step == nullptr)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = density.dp(kHighlightRadiusDp);
    QPainterPath hole;
    if (!highlight_.isEmpty()) {
        if (step->highlight == GuideStep::Highlight::Circle)
            hole.addEllipse(highlight_);
        else
            hole.addRoundedRect(highlight_, radius, radius);
    }

    QPainterPath scrim;
    scrim.addRect(content);
    if (!hole.isEmpty())
        scrim = scrim.subtracted(hole);

    painter.fillPath(scrim, withAlpha(colours.scrim, scrimOpacity_));

    // The ring makes the cut-out read as a highlight rather than as a hole.
    if (!hole.isEmpty()) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(colours.primary, density.dp(kHighlightWidthDp)));
        painter.drawPath(hole);
    }

    if (!bubble_.isEmpty()) {
        const qreal bubbleRadius = density.dp(16.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(withAlpha(colours.shadow, 0.18));
        painter.drawRoundedRect(bubble_.translated(0.0, density.dp(2.0)), bubbleRadius,
                                bubbleRadius);

        painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
        painter.setBrush(colours.surfaceContainerHigh);
        painter.drawRoundedRect(bubble_, bubbleRadius, bubbleRadius);

        const qreal padding = density.dp(kBubblePaddingDp);
        const qreal gap = density.dp(kBubbleGapDp);
        const qreal titleLineHeight = density.sp(theme.type().titleMedium.lineHeightSp);
        const qreal bodyLineHeight = density.sp(theme.type().bodyMedium.lineHeightSp);

        const QFont counterFont = fontFor(theme.type().labelSmall, density, this);
        const QFontMetricsF counterMetrics(counterFont);
        const QString counter = QStringLiteral("%1 / %2").arg(index_ + 1).arg(stepCount());
        const qreal counterWidth = counterMetrics.horizontalAdvance(counter);
        const qreal innerWidth = std::max(bubble_.width() - padding * 2.0, qreal(1.0));

        painter.setFont(counterFont);
        painter.setPen(colours.onSurfaceVariant);
        painter.drawText(QRectF(bubble_.right() - padding - counterWidth, bubble_.top() + padding,
                                counterWidth, titleLineHeight),
                         static_cast<int>(Qt::AlignRight | Qt::AlignVCenter), counter);

        const QFont titleFont = fontFor(theme.type().titleMedium, density, this);
        const QFontMetricsF titleMetrics(titleFont);
        const qreal titleWidth =
            std::max(innerWidth - counterWidth - gap, qreal(0.0));
        const QString label = titleMetrics.elidedText(step->title, Qt::ElideRight, titleWidth);

        painter.setFont(titleFont);
        painter.setPen(colours.onSurface);
        painter.drawText(QRectF(bubble_.left() + padding, bubble_.top() + padding, titleWidth,
                                titleLineHeight),
                         static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), label);

        if (!step->description.isEmpty()) {
            const QFont bodyFont = fontFor(theme.type().bodyMedium, density, this);
            const qreal descriptionTop = bubble_.top() + padding + titleLineHeight + gap;
            const qreal descriptionBottom =
                bubble_.bottom() - padding - density.dp(kButtonHeightDp) - gap;

            drawWrapped(painter, step->description, bodyFont, bodyLineHeight,
                        QRectF(bubble_.left() + padding, descriptionTop, innerWidth,
                               std::max(descriptionBottom - descriptionTop, qreal(0.0))),
                        colours.onSurfaceVariant);
        }
    }

    painter.restore();
}

bool GuideView::eventFilter(QObject* watched, QEvent* event)
{
    if (!active_)
        return ViewGroup::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::Hide:
        if (watched == parentWidget() && event->type() == QEvent::Resize
            && parentWidget() != nullptr) {
            setGeometry(parentWidget()->rect());
        }
        reposition();
        break;
    default:
        break;
    }

    return ViewGroup::eventFilter(watched, event);
}

void GuideView::resizeEvent(QResizeEvent* event)
{
    View::resizeEvent(event);

    if (active_)
        reposition();
}

void GuideView::onThemeChanged()
{
    if (active_)
        reposition();

    requestUpdate();
}

} // namespace md
