// Periodicity — the CheckBox component (framework.md §5.9).
//
// The glyph is painted from tokens only: an 18 dp rounded box with a 2 dp
// contour, a primary fill when selected and a QPainterPath check mark in
// onPrimary. The 48 dp widget rectangle is the touch target, so a click
// anywhere inside it toggles the state.

#include <md/components/checkbox.h>

#include <md/theme/theme.h>
#include <md/tokens/state_layer.h>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>

#include <algorithm>

namespace md {

namespace {

constexpr qreal kBoxSizeDp = 18.0;
constexpr qreal kBorderDp = 2.0;
constexpr qreal kTouchSizeDp = 48.0;

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return result;
}

QColor disabledContent(const Theme& theme)
{
    return withAlpha(theme.colours().onSurface, kDisabledContentOpacity);
}

bool isActivationKey(int key) noexcept
{
    return key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter;
}

} // namespace

CheckBox::CheckBox(QWidget* parent)
    : View(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));
}

void CheckBox::setCheckState(Qt::CheckState state)
{
    applyState(state);
}

Qt::CheckState CheckBox::checkState() const noexcept
{
    return checkState_;
}

void CheckBox::setChecked(bool checked)
{
    applyState(checked ? Qt::Checked : Qt::Unchecked);
}

bool CheckBox::isChecked() const noexcept
{
    return checkState_ == Qt::Checked;
}

void CheckBox::setTristate(bool tristate)
{
    tristate_ = tristate;
}

bool CheckBox::isTristate() const noexcept
{
    return tristate_;
}

void CheckBox::applyState(Qt::CheckState state)
{
    if (checkState_ == state)
        return;

    checkState_ = state;
    requestUpdate();
    emit toggled(checkState_ == Qt::Checked);
}

void CheckBox::cycle()
{
    if (tristate_) {
        switch (checkState_) {
        case Qt::Unchecked:
            applyState(Qt::PartiallyChecked);
            return;
        case Qt::PartiallyChecked:
            applyState(Qt::Checked);
            return;
        case Qt::Checked:
            applyState(Qt::Unchecked);
            return;
        }
    }

    applyState(checkState_ == Qt::Checked ? Qt::Unchecked : Qt::Checked);
}

void CheckBox::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const qreal side = Theme::forWidget(this).density().dp(kTouchSizeDp);
    setMeasuredSize(QSizeF(width.resolve(side), height.resolve(side)));
}

void CheckBox::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const bool enabled = isEnabled();

    const qreal boxSize = density.dp(kBoxSizeDp);
    const QRectF box(content.center().x() - boxSize / 2.0,
                     content.center().y() - boxSize / 2.0, boxSize, boxSize);
    const qreal radius = density.dp(2.0);
    const qreal border = density.dp(kBorderDp);

    QPainterPath boxPath;
    boxPath.addRoundedRect(box, radius, radius);

    const bool checked = checkState_ == Qt::Checked;
    const bool partial = checkState_ == Qt::PartiallyChecked;
    const bool filled = checked || partial;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (filled) {
        const QColor fill = enabled ? colours.primary
                                    : withAlpha(colours.onSurface, kDisabledContainerOpacity);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawPath(boxPath);

        const QColor mark = enabled ? colours.onPrimary : disabledContent(theme);
        painter.setPen(QPen(mark, border, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);

        if (checked) {
            QPainterPath check;
            check.moveTo(box.left() + box.width() * 0.24, box.top() + box.height() * 0.52);
            check.lineTo(box.left() + box.width() * 0.42, box.top() + box.height() * 0.70);
            check.lineTo(box.left() + box.width() * 0.76, box.top() + box.height() * 0.32);
            painter.drawPath(check);
        } else {
            painter.drawLine(QPointF(box.left() + box.width() * 0.25, box.center().y()),
                             QPointF(box.left() + box.width() * 0.75, box.center().y()));
        }
    } else {
        const QColor contour = enabled ? colours.onSurfaceVariant : disabledContent(theme);
        painter.setPen(QPen(contour, border, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(boxPath);
    }

    painter.restore();
}

void CheckBox::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressedInside_ = QRectF(rect()).contains(event->position());

    View::mousePressEvent(event);
}

void CheckBox::mouseReleaseEvent(QMouseEvent* event)
{
    const bool inside = QRectF(rect()).contains(event->position());
    const bool activate = pressedInside_ && inside && event->button() == Qt::LeftButton;
    pressedInside_ = false;

    View::mouseReleaseEvent(event);

    if (activate && isEnabled())
        cycle();
}

void CheckBox::keyPressEvent(QKeyEvent* event)
{
    if (isEnabled() && !event->isAutoRepeat() && isActivationKey(event->key())) {
        cycle();
        event->accept();
        return;
    }

    View::keyPressEvent(event);
}

} // namespace md
