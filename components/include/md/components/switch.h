// Periodicity — the Switch component (framework.md §5.9).
//
// A binary toggle drawn as a 52 x 32 dp pill track with a 24 dp thumb inset
// 4 dp from the track edge, centred in a 48 dp touch target (WCAG 2.5.5). The
// unchecked pair is a surfaceContainerHighest track with an outline thumb; the
// checked pair is a primary track with an onPrimary thumb. Checking it slides
// the thumb to the trailing edge while both colours interpolate, driven by the
// short4 motion token through md::animation::Animator. A theme that reports
// reduced motion — and a widget that is not visible — snaps to the end state
// instead of animating.
//
// Clicking anywhere inside the target toggles; Space does the same while the
// widget has focus. toggled(bool) reports the new state, and a setChecked()
// that does not change it emits nothing. The hover/press state layer and the
// keyboard focus ring come from View's pipeline and follow the pill shape of
// the whole touch target, the way a Button's feedback covers its target.

#pragma once

#include <md/animation/animator.h>
#include <md/view/view.h>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class Switch : public View
{
    Q_OBJECT

public:
    explicit Switch(QWidget* parent = nullptr);

    // Sets the state. Emits toggled() only when the value actually changes, so
    // a binding can push the same value repeatedly without feedback loops.
    void setChecked(bool checked);
    [[nodiscard]] bool isChecked() const noexcept;

signals:
    void toggled(bool checked);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void onHidden() override;
    void onThemeChanged() override;

private:
    // Applies the new state, starts the transition and emits toggled().
    void applyChecked(bool checked);

    // Starts the thumb and colour transition towards the state checked_
    // reports. Reduced motion, a hidden widget and a zero-length token snap.
    void startTransition();

    bool checked_ = false;
    // 0 is unchecked and 1 is checked. The painted state, interpolated over the
    // motion token, so a retarget mid-run continues from where the thumb is.
    qreal position_ = 0.0;
    bool pressedInside_ = false;
    animation::Animator animator_;
};

} // namespace md
