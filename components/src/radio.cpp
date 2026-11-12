// Periodicity — the Radio component (framework.md §5.9).
//
// A 20 dp outer circle with a 2 dp contour and, when selected, a 10 dp inner
// dot, all in a 48 dp touch target. Exclusivity is implemented here rather
// than by QAbstractButton: checking one radio walks the sibling list of the
// parent widget and unchecks every Radio that shares the group id, which is
// the smallest mechanism that still behaves like a button group.
//
// M3 gives a selection control a 40 dp state-layer circle, not an overlay over
// the whole target, so the shared overlay is switched off and the circle is
// painted under the ring. The pill shape keeps the focus ring circular.

#include <md/components/radio.h>

#include <md/theme/theme.h>
#include <md/tokens/state_layer.h>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>

#include <algorithm>

namespace md {

namespace {

constexpr qreal kDiameterDp = 20.0;
constexpr qreal kDotDiameterDp = 10.0;
constexpr qreal kBorderDp = 2.0;
constexpr qreal kTouchSizeDp = 48.0;
// M3 measurement: the radio interaction layer is a 40 dp circle.
constexpr qreal kStateLayerDp = 40.0;

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return result;
}

// The M3 interaction overlay: onSurface at the state-layer opacity for the
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

Radio::Radio(QWidget* parent)
    : View(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));
    // A round state layer and a round focus indicator (M3).
    setShape(Theme::forWidget(this).shapes().full);
    // The shared overlay would fill the whole 48 dp target; the 40 dp circle is
    // painted in paintContent(). A radio has no ripple in M3.
    setInteractionFeedbackEnabled(false);
}

void Radio::setChecked(bool checked)
{
    if (!checked) {
        if (!checked_)
            return;

        checked_ = false;
        requestUpdate();
        emit toggled(false);
        return;
    }

    if (checked_)
        return;

    uncheckSiblings();
    checked_ = true;
    requestUpdate();
    emit toggled(true);
}

bool Radio::isChecked() const noexcept
{
    return checked_;
}

void Radio::setGroupId(int id)
{
    if (groupId_ == id)
        return;

    groupId_ = id;

    if (checked_)
        uncheckSiblings();
}

int Radio::groupId() const noexcept
{
    return groupId_;
}

void Radio::uncheckSiblings()
{
    QWidget* parent = parentWidget();
    if (parent == nullptr)
        return;

    const QObjectList siblings = parent->children();
    for (QObject* child : siblings) {
        auto* radio = qobject_cast<Radio*>(child);
        if (radio != nullptr && radio != this && radio->groupId_ == groupId_)
            radio->setChecked(false);
    }
}

void Radio::check()
{
    setChecked(true);
}

void Radio::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const qreal side = Theme::forWidget(this).density().dp(kTouchSizeDp);
    setMeasuredSize(QSizeF(width.resolve(side), height.resolve(side)));
}

void Radio::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const bool enabled = isEnabled();

    const qreal border = density.dp(kBorderDp);

    // The circle rectangle is inset by half the stroke so the *outer* diameter
    // is the 20 dp token rather than 20 dp plus the pen.
    const qreal outer = density.dp(kDiameterDp);
    const qreal ringSide = outer - border;
    const QRectF ring(content.center().x() - ringSide / 2.0,
                      content.center().y() - ringSide / 2.0, ringSide, ringSide);

    const QColor accent = enabled ? colours.primary
                                  : withAlpha(colours.onSurface, kDisabledContentOpacity);
    const QColor contour = enabled
        ? (checked_ ? colours.primary : colours.onSurfaceVariant)
        : withAlpha(colours.onSurface, kDisabledContentOpacity);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // The 40 dp interaction circle sits below the ring, centred on it.
    const qreal layerSide = density.dp(kStateLayerDp);
    const QColor layerColour = stateLayerFor(colours, state(), enabled);
    if (layerColour.alpha() > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(layerColour);
        painter.drawEllipse(QRectF(content.center().x() - layerSide / 2.0,
                                   content.center().y() - layerSide / 2.0, layerSide,
                                   layerSide));
    }

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(contour, border, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawEllipse(ring);

    if (checked_) {
        const qreal dot = density.dp(kDotDiameterDp);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawEllipse(QPointF(content.center().x(), content.center().y()),
                            dot / 2.0, dot / 2.0);
    }

    painter.restore();
}

void Radio::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressedInside_ = QRectF(rect()).contains(event->position());

    View::mousePressEvent(event);
}

void Radio::mouseReleaseEvent(QMouseEvent* event)
{
    const bool inside = QRectF(rect()).contains(event->position());
    const bool activate = pressedInside_ && inside && event->button() == Qt::LeftButton;
    pressedInside_ = false;

    View::mouseReleaseEvent(event);

    if (activate && isEnabled())
        check();
}

void Radio::keyPressEvent(QKeyEvent* event)
{
    if (isEnabled() && !event->isAutoRepeat() && isActivationKey(event->key())) {
        check();
        event->accept();
        return;
    }

    View::keyPressEvent(event);
}

} // namespace md
