// Periodicity — the Affix component (framework.md §5.9).
//
// The bar owns one child and one number: the translation derived from the
// scroll. Everything else — the tonal background, the elevation shadow and the
// bottom gradient — is presentation of the stuck state, and it is applied in
// one place (applyStuckAppearance) so the three cannot disagree.
//
// The layout is re-run on every scroll change rather than only on resize,
// because the child's rectangle moves with the translation. The bottom shadow
// is drawn in paintChildren(), after the child, so the shadow really falls on
// top of the content instead of being hidden behind it.

#include <md/components/affix.h>

#include <md/theme/theme.h>

#include <QLinearGradient>
#include <QPainter>
#include <QPen>

#include <algorithm>

namespace md {
namespace {

constexpr qreal kMinHeightDp = 48.0;
constexpr qreal kPreferredWidthDp = 320.0;
constexpr qreal kShadowBandDp = 8.0;

} // namespace

Affix::Affix(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setShape(Shape::none());
    setBackgroundColour(QColor(Qt::transparent));
    setAccessibleName(tr("Affix"));
}

bool Affix::computedStuck() const
{
    return (scrollOffset_ - targetOffset_) > threshold_;
}

qreal Affix::offset() const
{
    if (stuck_)
        return -threshold_;

    return -(scrollOffset_ - targetOffset_);
}

qreal Affix::stickPoint() const
{
    return targetOffset_ + threshold_;
}

void Affix::applyStuckAppearance()
{
    const Theme& theme = Theme::forWidget(this);

    if (stuck_) {
        setBackgroundColour(theme.colours().surfaceContainerHigh);
        setElevation(Elevation::Level2);
    } else {
        setBackgroundColour(QColor(Qt::transparent));
        setElevation(Elevation::Level0);
    }
}

void Affix::syncStuck()
{
    const bool next = computedStuck();
    if (next == stuck_)
        return;

    stuck_ = next;
    applyStuckAppearance();
    requestUpdate();
    emit stuckChanged(stuck_);
}

void Affix::setContent(View* content)
{
    if (content_ == content)
        return;

    if (content_ != nullptr)
        removeView(content_);

    content_ = content;

    if (content_ != nullptr)
        addView(content_);

    arrangeChildren(contentRect());
    requestUpdate();
}

void Affix::setScrollOffset(qreal offset)
{
    if (qFuzzyCompare(offset + 1.0, scrollOffset_ + 1.0))
        return;

    scrollOffset_ = offset;
    syncStuck();
    arrangeChildren(contentRect());
    requestUpdate();
}

void Affix::setThreshold(qreal threshold)
{
    const qreal next = std::max<qreal>(0.0, threshold);
    if (qFuzzyCompare(next + 1.0, threshold_ + 1.0))
        return;

    threshold_ = next;
    syncStuck();
    arrangeChildren(contentRect());
    requestUpdate();
}

void Affix::setTargetOffset(qreal offset)
{
    if (qFuzzyCompare(offset + 1.0, targetOffset_ + 1.0))
        return;

    targetOffset_ = offset;
    syncStuck();
    arrangeChildren(contentRect());
    requestUpdate();
}

QRectF Affix::contentViewRect() const
{
    if (content_ == nullptr)
        return QRectF();

    return QRectF(content_->geometry());
}

void Affix::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    const qreal preferredWidth = width.resolve(density.dp(kPreferredWidthDp));
    qreal desiredHeight = density.dp(kMinHeightDp);

    if (content_ != nullptr) {
        content_->measure(MeasureSpec::atMost(std::max<qreal>(0.0, preferredWidth)),
                          MeasureSpec::unspecified());
        desiredHeight = std::max(desiredHeight, content_->measuredSize().height());
    }

    setMeasuredSize(QSizeF(preferredWidth, height.resolve(desiredHeight)));
}

void Affix::arrangeChildren(const QRectF& content)
{
    if (content_ == nullptr)
        return;

    const qreal width = std::max<qreal>(0.0, content.width());
    const qreal available = std::max<qreal>(0.0, content.height());

    content_->measure(MeasureSpec::exactly(width), MeasureSpec::atMost(available));

    const qreal height = std::min(available, std::max<qreal>(0.0, content_->measuredSize().height()));
    const qreal top = content.top() + offset();

    content_->arrange(QRect(qRound(content.left()), qRound(top), qRound(width), qRound(height)));
}

void Affix::paintChildren(QPainter& painter)
{
    ViewGroup::paintChildren(painter);

    if (!stuck_)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QRectF area = contentRect();
    if (area.isEmpty() || area.height() <= 0.0)
        return;

    const qreal band = std::min(density.dp(kShadowBandDp), area.height());

    QColor clear = theme.colours().shadow;
    clear.setAlphaF(0.0f);
    QColor edge = theme.colours().shadow;
    edge.setAlphaF(0.30f);

    QLinearGradient gradient(QPointF(0.0, area.bottom() - band),
                             QPointF(0.0, area.bottom()));
    gradient.setColorAt(0.0, clear);
    gradient.setColorAt(1.0, edge);

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRect(QRectF(area.left(), area.bottom() - band, area.width(), band));

    QColor hairline = theme.colours().outlineVariant;
    hairline.setAlphaF(0.6f);
    painter.setPen(QPen(hairline, density.dp(1.0)));
    painter.drawLine(QPointF(area.left(), area.bottom() - density.dp(0.5)),
                     QPointF(area.right(), area.bottom() - density.dp(0.5)));
    painter.restore();
}

} // namespace md
