// Periodicity — the CheckBox component (framework.md §5.9).
//
// The glyph is painted from tokens only: an 18 dp rounded box with a 2 dp
// contour, a primary fill when selected and a QPainterPath check mark in
// onPrimary. The 48 dp widget rectangle is the touch target, so a click
// anywhere inside it toggles the state.
//
// The design baseline selects interaction feedback for a selection control with a
// 40 dp state-layer circle centred on the glyph, not with an overlay over the whole
// target. The shared View overlay would fill the entire 48 dp rectangle, so it is
// switched off here and the circle is painted below the box instead. The pill
// shape keeps the keyboard focus ring on the same circular geometry.

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
// Design baseline measurement: the checkbox interaction layer is a 40 dp circle, smaller
// than the 48 dp target.
constexpr qreal kStateLayerDp = 40.0;

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return result;
}

// The design baseline interaction overlay: onSurface at the state-layer opacity for the
// strongest active interaction bit. A disabled control has no layer.
QColor stateLayerFor(const ColorScheme& colours, ViewStates state, bool enabled)
{
    if (!enabled)
        return QColor(Qt::transparent);

    const qreal opacity = stateLayerOpacity(
        static_cast<ViewState>(static_cast<quint32>(state)));
    if (opacity <= 0.0)
        return QColor(Qt::transparent);

    return withAlpha(colours.onSurface, opacity);
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
    // The state layer and the focus indicator of a selection control are round
    // (design baseline), so the shared shape is the pill rather than the default 8 dp
    // rounded rectangle.
    setShape(Theme::forWidget(this).shapes().full);
    // The framework overlay would cover the entire 48 dp target; the 40 dp
    // circle is painted in paintContent() instead. Selection controls have no
    // ripple in the design baseline, so nothing else is lost.
    setInteractionFeedbackEnabled(false);
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

    // The 40 dp interaction circle sits below the 18 dp box, centred on it.
    const qreal layerSide = density.dp(kStateLayerDp);
    const QColor layerColour = stateLayerFor(colours, state(), enabled);
    if (layerColour.alpha() > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(layerColour);
        painter.drawEllipse(QRectF(box.center().x() - layerSide / 2.0,
                                   box.center().y() - layerSide / 2.0, layerSide,
                                   layerSide));
    }

    if (filled) {
        const QColor fill = enabled ? colours.primary
                                    : withAlpha(colours.onSurface, kDisabledContainerOpacity);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawPath(boxPath);

        // Design baseline draws the disabled check as the surface role showing through the
        // 12 % onSurface container; a 38 % onSurface mark would all but vanish.
        const QColor mark = enabled ? colours.onPrimary : colours.surface;
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
        // The disabled contour is onSurfaceVariant at 38 %, not onSurface: the
        // Design baseline unselected outline role is the variant.
        const QColor contour = enabled
            ? colours.onSurfaceVariant
            : withAlpha(colours.onSurfaceVariant, kDisabledContentOpacity);
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
