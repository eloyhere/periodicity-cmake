// Periodicity — Badge (framework.md §6.3).
//
// The badge owns one decision: how big it has to be. Everything else follows —
// an attached badge resizes itself to its measured size and then re-anchors on
// the target's corner; a detached one waits for its owner to place it.

#include <md/components/badge.h>

#include <md/theme/theme.h>

#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPoint>
#include <QRectF>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace md {

namespace {

QFont labelFont(const Theme& theme)
{
    const TextStyle& style = theme.type().labelSmall;
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

// Black or white, whichever reads on `fill`. A badge may be given any colour,
// so the text colour cannot come from a contrast pair.
QColor readableOn(const QColor& fill)
{
    const int luminance =
        (fill.red() * 299 + fill.green() * 587 + fill.blue() * 114) / 1000;
    return luminance > 150 ? QColor(0, 0, 0) : QColor(255, 255, 255);
}

} // namespace

Badge::Badge(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setBackgroundColour(QColor(Qt::transparent));
}

Badge::~Badge() = default;

void Badge::setText(QString text)
{
    dot_ = false;
    count_ = 0;

    if (text_ == text) {
        refreshGeometry();
        return;
    }

    text_ = std::move(text);
    refreshGeometry();
}

void Badge::setCount(int count)
{
    const int value = std::max(0, count);

    dot_ = false;
    count_ = value;

    if (value == 0) {
        text_.clear();
        hiddenByCount_ = !isHidden();
        hide();
    } else {
        text_ = value > maxCount_ ? QString::number(maxCount_) + QLatin1Char('+')
                                  : QString::number(value);
        if (hiddenByCount_) {
            hiddenByCount_ = false;
            show();
        }
    }

    refreshGeometry();
}

void Badge::setMaxCount(int max)
{
    const int value = std::max(1, max);
    if (maxCount_ == value)
        return;

    maxCount_ = value;

    // Re-render the current number under the new ceiling.
    const int current = count_;
    if (!dot_ && current > 0) {
        text_ = current > maxCount_ ? QString::number(maxCount_) + QLatin1Char('+')
                                    : QString::number(current);
    }

    refreshGeometry();
}

void Badge::setDot(bool dot)
{
    if (dot_ == dot)
        return;

    dot_ = dot;
    if (dot_) {
        text_.clear();
        count_ = 0;
    }

    refreshGeometry();
}

void Badge::setColour(QColor colour)
{
    if (colour_ == colour)
        return;

    colour_ = std::move(colour);
    requestUpdate();
}

void Badge::setTarget(QWidget* target)
{
    if (target_.data() == target) {
        positionOverTarget();
        return;
    }

    if (target_ != nullptr)
        target_->removeEventFilter(this);

    target_ = target;

    if (target_ == nullptr) {
        // Detached again: the badge keeps its geometry and becomes an ordinary
        // widget its owner can place.
        updateGeometry();
        requestUpdate();
        return;
    }

    target_->installEventFilter(this);

    // A badge overhangs the corner, so it cannot be clipped by its target: it
    // becomes a sibling. A target that has no parent (a window) has to host the
    // badge itself.
    QWidget* host = target_->parentWidget();
    if (host == nullptr)
        host = target_;

    if (parentWidget() != host)
        setParent(host);

    raise();
    refreshGeometry();
    show();
}

QWidget* Badge::target() const
{
    return target_.data();
}

void Badge::refreshGeometry()
{
    updateGeometry();
    requestUpdate();

    if (target_ == nullptr)
        return;

    resize(sizeHint());
    positionOverTarget();
}

void Badge::positionOverTarget()
{
    if (target_ == nullptr)
        return;

    QWidget* host = parentWidget();
    if (host == nullptr)
        return;

    if (host == target_) {
        move(layoutDirection() == Qt::RightToLeft ? 0 : qMax(0, target_->width() - width()),
             0);
        return;
    }

    // Centre the badge on the target's top-trailing corner. mapTo() walks the
    // parent chain, so a target nested in a layout still lands correctly. The
    // trailing corner is the left one in a right-to-left widget.
    const QPoint corner = layoutDirection() == Qt::RightToLeft
        ? target_->mapTo(host, QPoint(0, 0))
        : target_->mapTo(host, QPoint(target_->width(), 0));
    move(corner - QPoint(width() / 2, height() / 2));
}

bool Badge::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == target_.data()) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::LayoutRequest:
            positionOverTarget();
            break;
        case QEvent::Show:
            show();
            positionOverTarget();
            break;
        case QEvent::Hide:
            hide();
            break;
        default:
            break;
        }
    }

    return View::eventFilter(watched, event);
}

void Badge::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFont font = labelFont(theme);
    const QFontMetricsF metrics(font);

    // M3 measurements are dp, not raw pixels: the badge used to keep its 16 px
    // minimum and its 4 px text insets at every density, so it drifted off the
    // 4 dp grid as soon as the application zoomed.
    const qreal minimum = density.dp(16.0);
    const qreal diameter = qMax(minimum, metrics.height() + density.dp(4.0));

    if (dot_ || text_.isEmpty()) {
        setMeasuredSize(QSizeF(width.resolve(diameter), height.resolve(diameter)));
        return;
    }

    const qreal textWidth = metrics.horizontalAdvance(text_) + density.dp(8.0);
    setMeasuredSize(QSizeF(width.resolve(qMax(diameter, textWidth)),
                           height.resolve(diameter)));
}

void Badge::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const QColor fill = colour_.isValid() ? colour_ : theme.colours().error;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);

    if (dot_ || text_.isEmpty()) {
        const qreal diameter = qMin(content.width(), content.height());
        const QRectF circle(content.center().x() - diameter / 2.0,
                            content.center().y() - diameter / 2.0, diameter, diameter);
        painter.drawEllipse(circle);
    } else {
        painter.drawRoundedRect(content.adjusted(0.5, 0.5, -0.5, -0.5), content.height() / 2.0,
                                content.height() / 2.0);

        painter.setFont(labelFont(theme));
        painter.setPen(readableOn(fill));
        painter.drawText(content, Qt::AlignCenter, text_);
    }

    painter.restore();
}

} // namespace md
