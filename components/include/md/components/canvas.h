// Periodicity — the Canvas component.
//
// Canvas is the View that hosts one of three drawing contexts:
//
//   OneD   — a Context1D scalar strip (sparkline);
//   TwoD   — a Context2D retained painter façade;
//   ThreeD — a Context3D software/OpenGL scene.
//
// The contexts are owned by the Canvas and are always alive, so switching the
// mode preserves each mode's scene. Canvas::paintContent() only dispatches to
// the active context; the context paints into the content rectangle after the
// View background and before the View foreground layers.
//
// The Canvas background is the View background. setBackground() is a typed
// convenience that also informs Context3D so the embedded QOpenGLWidget clears
// to the same colour.
//
// Painting and every public entry point are exception-guarded: a failing
// context or scene is reported with qWarning and the Canvas remains paintable.

#pragma once

#include <md/components/context1d.h>
#include <md/components/context2d.h>
#include <md/components/context3d.h>
#include <md/view/view.h>

#include <QColor>
#include <QRectF>
#include <QtGlobal>

class QPainter;
class QShowEvent;

namespace md {

class Canvas : public View
{
    Q_OBJECT

public:
    enum class Mode {
        OneD,
        TwoD,
        ThreeD,
    };

    explicit Canvas(QWidget* parent = nullptr);

    // Switching modes keeps every context's data and requests a repaint. The
    // embedded GL widget (if any) is shown only in ThreeD mode with the
    // OpenGL backend selected.
    void setMode(Mode mode);
    [[nodiscard]] Mode mode() const noexcept;

    [[nodiscard]] Context1D* context1d() noexcept;
    [[nodiscard]] const Context1D* context1d() const noexcept;
    [[nodiscard]] Context2D* context2d() noexcept;
    [[nodiscard]] const Context2D* context2d() const noexcept;
    [[nodiscard]] Context3D* context3d() noexcept;
    [[nodiscard]] const Context3D* context3d() const noexcept;

    // The Canvas fill. An invalid colour restores the theme's default surface
    // background. The corresponding Context3D is informed for its GL clear.
    void setBackground(const QColor& colour);
    [[nodiscard]] QColor background() const;

    // Schedules a repaint of this Canvas and, for ThreeD, of the embedded GL
    // widget. Contexts deliberately do not schedule paints themselves; call
    // this after changing their data or scene.
    void requestDraw();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeOverride(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void showEvent(QShowEvent* event) override;

private:
    Mode mode_ = Mode::OneD;
    QColor background_;
    Context1D context1d_;
    Context2D context2d_;
    Context3D context3d_;
};

} // namespace md
