// Periodicity — the single-view GPU canvas (components module).
//
// GpuCanvas is the small sibling of md::GpuCompositor: instead of uploading
// textures it puts QPainter directly on the OpenGL surface of a QOpenGLWidget
// and renders one hosted md::View into it. Qt implements that QPainter target
// with its OpenGL paint engine (the "raster-on-GL" path), so the view's normal
// paintContent() pipeline runs unchanged and the rasterisation is accelerated
// by the GPU.
//
// This is the least invasive entry point: a caller already holding a View can
// wrap it once. GpuCompositor is the better choice when several widgets must
// be composited with per-child texture caching.
//
// Compiled only when PERIODICITY_HAS_OPENGL is 1. Without Qt6::OpenGLWidgets
// the header exposes a QWidget-based stub with the same interface: it reports
// the backend unavailable and keeps hosting the view through ordinary QWidget
// painting, so application code that uses GpuCanvas still compiles and still
// renders correctly on the software path.

#pragma once

#include <md/view/render_backend.h>

#include <QEvent>
#include <QPointer>
#include <QWidget>

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
#    include <QOpenGLWidget>
#endif

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QHideEvent;

namespace md {

class View;

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
class GpuCanvas final : public QOpenGLWidget
#else
class GpuCanvas final : public QWidget
#endif
{
    Q_OBJECT

public:
    explicit GpuCanvas(QWidget* parent = nullptr);
    explicit GpuCanvas(View* view, QWidget* parent = nullptr);
    ~GpuCanvas() override;

    GpuCanvas(const GpuCanvas&) = delete;
    GpuCanvas& operator=(const GpuCanvas&) = delete;

    // True when this build and platform can use the QPainter-on-GL path.
    [[nodiscard]] static bool isAvailable();

    // True once the GL context was created and the view is being painted
    // through it. False on the offscreen test platform, where the hosted view
    // stays an ordinary child widget.
    [[nodiscard]] bool isUsingGpu() const noexcept;
    [[nodiscard]] RenderBackend effectiveBackend() const noexcept;

    // Hosts `view` (reparents it to this canvas) and resizes it to the canvas.
    // Passing nullptr releases the previously hosted view without deleting it.
    void setView(View* view);
    [[nodiscard]] View* view() const noexcept;

    // Schedules one canvas repaint. A hosted view calls this indirectly: the
    // canvas filters its UpdateRequest events and coalesces them per frame.
    void requestViewUpdate();

signals:
    void gpuStateChanged(bool usingGpu);

protected:
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
#else
    void paintEvent(QPaintEvent* event) override;
#endif

    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;

private:
    void setUsingGpu(bool usingGpu);
    void syncHostedView();
    void forwardMouse(QMouseEvent* event, QEvent::Type type);

    QPointer<View> view_;
    bool gpuReady_ = false;
    bool frameQueued_ = false;
};

} // namespace md
