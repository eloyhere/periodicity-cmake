// Periodicity — pointer-origin ripple feedback (framework.md §5.11).
//
// A RippleController paints interaction ripples: a low-opacity circle for a
// hover and a stronger one for a press. Each is a one-shot animation — it grows
// from the pointer position until it covers the whole component, then fades out
// and is removed. Nothing follows the pointer and nothing persists as a static
// circle: a hover plays once, exactly like a click.
//
// The controller is widget-agnostic. The host forwards pointer positions and
// calls paint() from its own paint pipeline; repaintRequested() tells the host
// that the frame changed, so it can schedule update() without polling
// isAnimating().
//
// Animation is an implementation detail behind the pimpl boundary: this header
// deliberately does not include <md/animation/animator.h>, because the layering
// matrix (framework.md §13.1) permits ripple public headers to include only
// core, tokens and theme. The animator lives in ripple/src/ripple.cpp.
//
// The controller is a GUI-thread object: it owns QVariantAnimation-based
// animators, so it needs an event loop and has the thread affinity of its
// parent QObject.

#pragma once

#include <md/tokens/motion.h>
#include <md/tokens/state_layer.h>
#include <md/theme/theme.h>

#include <QColor>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QtGlobal>

#include <memory>

class QPainter;

namespace md::ripple {

// The resolved motion and opacity values a controller paints with.
//
// The opacities are the state-layer tokens from framework.md §3.6 rather than
// component colours: a hover is 8% of the content colour and a press 12%, and
// a caller overrides them only to build a non-standard component, never to
// hard-code a grey.
struct RippleSpec
{
    // Radius growth from 0 to the maximum radius. The animator applies the
    // curve, so a callback receives an already-eased 0..1 progress.
    MotionSpec expansion;

    // Fade-out after the pointer leaves or a press is released. The progress
    // maps to opacity, not to radius: the circle keeps its size and lowers its
    // alpha.
    MotionSpec fade;

    // Opacity of the hover circle; the hover state-layer token by default.
    qreal hoverOpacity = stateLayerOpacity(ViewState::Hovered);

    // Opacity of the press circle; the pressed state-layer token by default.
    qreal pressedOpacity = stateLayerOpacity(ViewState::Pressed);

    // Unused since hover ripples stopped following the pointer; kept so that a
    // caller configuring it does not break. Hover always plays once.
    qreal hoverFollowThresholdDp = 4.0;

    // The maximum radius as a fraction of the distance from the ripple origin
    // to the farthest corner of the bounds. The default 1.0 covers the whole
    // shape, which is the Material behaviour: the ripple ends up filling the
    // component rather than stopping as a small disc.
    qreal maxRadiusScale = 1.0;

    // Resolves the baseline specification for `theme`: the opacities from the
    // state-layer tokens, expansion from medium2/standard-decelerate and fade
    // from long1/standard. A reduced-motion scheme yields zero-duration specs
    // because the durations come from the theme, and the animator then snaps
    // on the next event-loop turn.
    [[nodiscard]] static RippleSpec fromTheme(const Theme& theme);
};

// Owns the hover and press ripple state of one surface.
//
// A host forwards pointer positions in widget-local coordinates (the same
// coordinate system as setBounds) and calls paint() after painting its own
// background. The controller keeps at most one hover ripple and at most eight
// simultaneous press ripples; each ripple owns one animator parented to the
// controller, so destroying the controller cancels every run.
class RippleController : public QObject
{
    Q_OBJECT

public:
    explicit RippleController(QObject* parent = nullptr);
    ~RippleController() override;

    RippleController(const RippleController&) = delete;
    RippleController& operator=(const RippleController&) = delete;

    // Replaces the theme snapshot the controller resolves against and
    // refreshes the motion specification from RippleSpec::fromTheme. The
    // content colour follows the theme's onSurface unless setContentColour was
    // called explicitly. Ripples that are already running keep their geometry
    // and pick up the new colours and motion on their next transition.
    void setTheme(Theme theme);

    // The widget-local rectangle the ripple is clipped to, normally the host's
    // rect() in floating-point form. Ripples whose origin lies outside are
    // still clipped rather than dropped, so a growing circle can enter the
    // visible area from any edge.
    void setBounds(const QRectF& bounds);

    // Rounded clip radius in device-independent pixels; 0 keeps the clip
    // square. The radius is clamped to half the shorter side of the bounds.
    void setCornerRadius(qreal radius);

    // The colour the ripple circles are filled with. The default is the
    // theme's onSurface; calling this method pins the colour so a later
    // setTheme does not overwrite it.
    void setContentColour(const QColor& colour);

    // While disabled the controller ignores pointer input, drops every ripple
    // and paints nothing. Re-enabling starts from a clean, idle state.
    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const noexcept;

    // Replaces the painting specification. setTheme() also replaces it (with
    // RippleSpec::fromTheme), so configure a custom spec after a theme change.
    void setSpec(const RippleSpec& spec);
    [[nodiscard]] const RippleSpec& spec() const noexcept;

    // The pointer entered the surface at `position`. Plays one hover ripple:
    // expansion from that point to full coverage, then a fade. A second entry
    // replaces the previous hover ripple rather than stacking circles.
    void pointerEntered(const QPointF& position);

    // Accepted for interface completeness; hover ripples no longer follow the
    // pointer, so this does nothing.
    void pointerMoved(const QPointF& position);

    // A press started at `position`. Plays one press ripple with
    // spec.pressedOpacity, expanding to full coverage and fading out.
    void pointerPressed(const QPointF& position);

    // Accepted for interface completeness; a press ripple is one-shot and does
    // not wait for the release.
    void pointerReleased(const QPointF& position);

    // Accepted for interface completeness; a hover ripple is one-shot and does
    // not wait for the pointer to leave.
    void pointerLeft();

    // True while any ripple animator is running, including a fade that has
    // been requested but not started yet. The host can use this to decide
    // whether a timer-driven repaint is still needed.
    [[nodiscard]] bool isAnimating() const noexcept;

    // Paints every active ripple into `painter`, clipped to the bounds and the
    // corner radius. The painter may target a QImage with no widget behind it;
    // the controller neither saves nor assumes a widget state.
    void paint(QPainter& painter) const;

signals:
    // Emitted on every animation value change and whenever a ripple is created
    // or removed, so the host can call update() exactly as often as needed.
    void repaintRequested();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace md::ripple
