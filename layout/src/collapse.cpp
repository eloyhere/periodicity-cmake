// Periodicity — Collapse (framework.md §6.3).
//
// The section keeps one number — expansion_ — and derives everything from it:
// its preferred height, the content rectangle and the chevron. A QVariantAnimation
// moves that number; there is no second source of truth to fall out of step
// with the geometry.

#include <md/layout/collapse.h>

#include <md/layout/alignment.h>
#include <md/theme/theme.h>

#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSignalBlocker>

#include <algorithm>
#include <cmath>

namespace md {

namespace {

QFont titleFont(const Theme& theme)
{
    const TextStyle& style = theme.type().titleMedium;
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

} // namespace

Collapse::Collapse(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setBackgroundColour(QColor(Qt::transparent));

    animation_.setObjectName(QStringLiteral("md.layout.Collapse.height"));

    connect(&animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        expansion_ = qBound(qreal(0.0), value.toReal(), qreal(1.0));
        applyExpansion();
    });

    connect(&animation_, &QVariantAnimation::finished, this, [this] {
        // The animation may stop short of the end; the settled state is the
        // target, not the last emitted tick.
        expansion_ = expanded_ ? 1.0 : 0.0;
        applyExpansion();
        syncContentVisibility();
        emit animationFinished();
    });
}

Collapse::~Collapse() = default;

void Collapse::setTitle(QString title)
{
    if (title_ == title)
        return;

    title_ = std::move(title);
    relayoutNow();
}

void Collapse::setHeaderHeight(qreal height)
{
    // Zero is a legal header height; a negative or non-finite value is ignored
    // instead of being replaced by a number the caller never asked for.
    const qreal value = layout_detail::length(height, headerHeight_);
    if (headerHeight_ == value)
        return;

    headerHeight_ = value;
    relayoutNow();
}

void Collapse::setContentWidget(View* content)
{
    if (content_ == content)
        return;

    if (content_ != nullptr) {
        // The caller keeps ownership of the replaced view.
        removeView(content_);
        content_ = nullptr;
    }

    if (content != nullptr) {
        if (!views().contains(content))
            addView(content);
        content_ = content;

        connect(content, &QObject::destroyed, this, [this, content] {
            if (content_ == content)
                content_ = nullptr;
        });
    }

    syncContentVisibility();
    updateGeometry();
    requestUpdate();
    arrangeChildren(contentRect());
}

void Collapse::setExpanded(bool expanded)
{
    if (expanded_ == expanded)
        return;

    expanded_ = expanded;

    // The content has to be visible while it animates out, not only after the
    // section has finished expanding.
    if (content_ != nullptr && expanded_)
        content_->show();

    const Theme& theme = Theme::forWidget(this);
    const MotionSpec spec = theme.motion().medium1();
    const int duration = animated_ ? static_cast<int>(spec.duration.count()) : 0;

    animation_.stop();

    if (duration <= 0) {
        expansion_ = expanded_ ? 1.0 : 0.0;
        applyExpansion();
        emit expandedChanged(expanded_);
        emit animationFinished();
        return;
    }

    // QVariantAnimation re-evaluates its value from the *old* currentTime as
    // soon as a property changes. After stop() that time is still the middle of
    // the previous run, so setStartValue()/setEndValue() would each emit a
    // value computed from the old curve — expansion_ would jump to a height
    // that belongs to neither the old nor the new target before start() resets
    // it. Reconfigure with the signals blocked; start() then emits the real
    // starting value (which is the current expansion, so the motion continues
    // from where it is instead of jumping).
    {
        const QSignalBlocker blocker(&animation_);
        animation_.setDuration(duration);
        animation_.setEasingCurve(spec.easing.toEasingCurve());
        animation_.setStartValue(expansion_);
        animation_.setEndValue(expanded_ ? 1.0 : 0.0);
    }
    animation_.start();

    emit expandedChanged(expanded_);
}

void Collapse::toggle()
{
    setExpanded(!expanded_);
}

qreal Collapse::preferredHeight() const
{
    return headerHeight_ + expansion_ * contentHeight_;
}

void Collapse::relayoutNow()
{
    updateGeometry();
    requestUpdate();
    arrangeChildren(contentRect());
}

qreal Collapse::contentNaturalHeight(qreal width)
{
    if (content_ == nullptr)
        return 0.0;

    content_->measure(width > 0.0 ? MeasureSpec::atMost(width) : MeasureSpec::unspecified(),
                      MeasureSpec::unspecified());

    const qreal height = content_->measuredSize().height();
    return height > 0.0 ? height : 0.0;
}

qreal Collapse::headerNaturalWidth() const
{
    const Theme& theme = Theme::forWidget(this);
    const qreal padding = theme.density().dp(16.0);
    const qreal chevron = theme.density().dp(24.0);
    const qreal text = QFontMetricsF(titleFont(theme)).horizontalAdvance(title_);
    return 2.0 * padding + text + chevron;
}

void Collapse::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const qreal availableWidth = width.mode() == MeasureSpec::Mode::Unspecified
                                     ? 0.0
                                     : qMax(qreal(0.0), width.size());

    contentHeight_ = contentNaturalHeight(availableWidth);

    qreal naturalWidth = headerNaturalWidth();
    if (content_ != nullptr && !content_->isHidden())
        naturalWidth = qMax(naturalWidth, content_->measuredSize().width());

    setMeasuredSize(QSizeF(width.resolve(naturalWidth), height.resolve(preferredHeight())));
}

void Collapse::arrangeChildren(const QRectF& content)
{
    const qreal width = qMax(qreal(0.0), content.width());
    const qreal availableHeight = qMax(qreal(0.0), content.height());

    headerRect_ = QRectF(content.left(), content.top(), width,
                         qMin(headerHeight_, availableHeight));

    if (content_ == nullptr || content_->isHidden())
        return;

    const qreal requested = expansion_ * contentHeight_;
    const qreal contentHeight =
        qMax(qreal(0.0), qMin(requested, availableHeight - headerRect_.height()));

    content_->measure(MeasureSpec::exactly(width), MeasureSpec::exactly(contentHeight));
    content_->arrange(QRectF(content.left(), headerRect_.bottom(), width, contentHeight)
                          .toAlignedRect());
}

void Collapse::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;

    const qreal padding = theme.density().dp(16.0);
    const qreal chevronBox = theme.density().dp(24.0);
    const qreal gap = theme.density().dp(8.0);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    painter.fillRect(headerRect_, colours.surfaceContainerLow);

    // The title, elided between the padding and the chevron.
    const QFont font = titleFont(theme);
    painter.setFont(font);

    QColor titleColour = colours.onSurface;
    if (!isEnabled())
        titleColour.setAlphaF(0.38F);
    painter.setPen(titleColour);

    const qreal textInset = padding;
    const qreal textWidth = qMax(qreal(0.0), headerRect_.width() - 2.0 * textInset
                                                    - chevronBox - gap);
    const QRectF textRect(headerRect_.left() + textInset, headerRect_.top(), textWidth,
                          headerRect_.height());
    if (textWidth > 0.0 && !title_.isEmpty()) {
        const QString elided =
            QFontMetricsF(font).elidedText(title_, Qt::ElideRight, textWidth);
        const Qt::Alignment alignment =
            Qt::AlignVCenter | (rightToLeft ? Qt::AlignRight : Qt::AlignLeft);
        painter.drawText(textRect, static_cast<int>(alignment), elided);
    }

    // The chevron: a filled triangle that points at the content when the
    // section is collapsed and back at the header when it is expanded.
    const qreal chevronX = rightToLeft ? headerRect_.left() + padding + chevronBox / 2.0
                                       : headerRect_.right() - padding - chevronBox / 2.0;
    const qreal chevronY = headerRect_.center().y();
    const qreal half = theme.density().dp(4.0);

    QPolygonF chevron;
    if (expanded_) {
        chevron << QPointF(chevronX - half, chevronY + half / 2.0)
                << QPointF(chevronX + half, chevronY + half / 2.0)
                << QPointF(chevronX, chevronY - half / 2.0);
    } else {
        chevron << QPointF(chevronX - half, chevronY - half / 2.0)
                << QPointF(chevronX - half, chevronY + half / 2.0)
                << QPointF(chevronX + half, chevronY);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(titleColour);
    painter.drawPolygon(chevron);

    // A hairline separates the header from whatever the section exposes.
    painter.fillRect(QRectF(headerRect_.left(), headerRect_.bottom() - 1.0,
                            headerRect_.width(), 1.0),
                     colours.outlineVariant);

    painter.restore();
}

void Collapse::mousePressEvent(QMouseEvent* event)
{
    // The header row toggles the section; the rest of the header area still
    // gets the base class's press handling, so the ripple plays either way.
    const bool onHeader = event->button() == Qt::LeftButton
                          && headerRect_.contains(event->position());

    ViewGroup::mousePressEvent(event);

    if (onHeader)
        toggle();
}

void Collapse::changeEvent(QEvent* event)
{
    ViewGroup::changeEvent(event);

    // The title and the chevron mirror with the direction, so the header has to
    // be repainted and re-placed.
    if (event->type() == QEvent::LayoutDirectionChange)
        relayoutNow();
}

void Collapse::onHidden()
{
    // A hidden section has nothing to animate: stop the curve and settle on
    // the target so the next show starts from a consistent height instead of
    // resuming a half-finished expand.
    if (animation_.state() != QVariantAnimation::Stopped)
        animation_.stop();

    const qreal target = expanded_ ? 1.0 : 0.0;
    if (expansion_ != target) {
        expansion_ = target;
        applyExpansion();
        syncContentVisibility();
    }
}

void Collapse::applyExpansion()
{
    if (content_ != nullptr)
        contentHeight_ = contentNaturalHeight(qMax(qreal(0.0), qreal(width())));

    if (parentWidget() == nullptr) {
        // A standalone section owns its height, so it grows and shrinks as it
        // animates. Inside a container the parent decides, and updateGeometry()
        // asks it to.
        const int target = qRound(headerHeight_ + expansion_ * contentHeight_);
        if (target != height())
            resize(width(), target);
    }

    updateGeometry();
    requestUpdate();
    arrangeChildren(contentRect());
    syncContentVisibility();
}

void Collapse::syncContentVisibility()
{
    if (content_ == nullptr)
        return;

    content_->setVisible(expanded_ || expansion_ > 0.0);
}

} // namespace md
