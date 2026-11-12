// Periodicity — the ripple paint stage (framework.md §5.5, §5.11).
//
// A RippleLayer is the pipeline slot for pointer feedback: the host forwards
// pointer positions to controller(), and the layer paints the resulting circles
// exactly where every other stage paints — between the state overlay and the
// focus ring. That keeps the z-order in one place instead of letting each
// component decide when to stamp a ripple on top.
//
// The controller is a value member, so its lifetime is exactly the layer's: a
// surface needs one object per ripple stage rather than a controller plus a
// paint call in its own paintEvent. The constructor's `parent` argument is
// accepted for source compatibility but is deliberately ignored — handing it to
// the controller would let an external QObject own a subobject and delete it
// before (or after) the layer does.

#pragma once

#include <md/layer/layer.h>
#include <md/ripple/ripple.h>
#include <md/theme/theme.h>

#include <QRectF>
#include <QtGlobal>

namespace md::layer {

class RippleLayer : public Layer
{
public:
    // The controller is owned by value and is never re-parented; `parent` is
    // ignored (see the file comment) and may be null. Do not use it to extend
    // the controller's lifetime beyond this layer's.
    explicit RippleLayer(Theme theme, QObject* parent = nullptr);
    ~RippleLayer() override;

    RippleLayer(const RippleLayer&) = delete;
    RippleLayer& operator=(const RippleLayer&) = delete;

    // Replaces the theme the controller resolves against.
    void setTheme(Theme theme);

    // The clip radius of the rounded surface, in device-independent pixels.
    void setCornerRadius(qreal radius);

    // The layer-local rectangle the ripple is clipped to. Callers normally
    // refresh this from resizeEvent; paint() deliberately does not mutate state.
    void setBounds(const QRectF& bounds);

    // The controller the host forwards pointer events to. Kept accessible so a
    // widget can connect repaintRequested without the layer needing to be a
    // QObject. Never re-parent it: it is a value member and its lifetime is
    // exactly this layer's.
    [[nodiscard]] ripple::RippleController& controller() noexcept;
    [[nodiscard]] const ripple::RippleController& controller() const noexcept;

    // Paints the controller's ripples clipped to `bounds`, which is the
    // rectangle the caller asked this stage to paint. An empty bounds or an
    // inactive painter paints nothing, like every other stage.
    void paint(QPainter& painter, const QRectF& bounds,
               const SurfaceContext& context) const override;

    [[nodiscard]] bool isAnimated() const noexcept override;

private:
    ripple::RippleController controller_;
};

} // namespace md::layer
