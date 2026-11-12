// Periodicity — the opt-in GPU compositor (framework.md §5.7, §5.12).
//
// GpuCompositor is a GPU-backed container for md::View children. The working
// model is deliberately narrow and honest:
//
//   * each direct View child that opted in with View::setGpuAccelerated(true)
//     is painted once by the ordinary QPainter pipeline, into an offscreen
//     QImage (the same code path a software widget uses);
//   * the image is uploaded to a QOpenGLTexture only when that child reported
//     a change (its UpdateRequest event, a resize, a theme or palette change),
//     never on every frame;
//   * the textures are composited on the GPU in stacking order with
//     QOpenGLTextureBlitter, Qt's supported texture-blit helper for a
//     QOpenGLWidget.
//
// So the GPU accelerates the compositing of already-painted frames. The first
// paint of a changed child still rasterizes on the CPU, exactly as the docs say.
// This pays off with many moving or overlapping widgets; one static label is
// cheaper on the plain software path.
//
// A child that did not opt in is left as a normal QWidget: Qt paints it through
// the backing store, on top of the GL surface. That is also what happens when
// no GL context is available at all: the compositor never claims gpu use, all
// children stay normal widgets, and the application looks and behaves exactly
// as it did without a compositor.
//
// Limitations (inherit from QOpenGLWidget, and are documented in docs/gpu.md):
// a QOpenGLWidget is a native child window with its own context; stacking with
// sibling native widgets and clipping against the rest of the widget tree are
// constrained, and the context is destroyed with the widget. A compositor is
// therefore best placed as a leaf-ish container, not as a transparent overlay
// spread across an arbitrary hierarchy.
//
// Threading: everything here — context creation, QImage painting, texture
// upload and shader blits — happens on the UI thread. There is no worker-side
// GL call. Repaint scheduling goes through md::schedule::UiThread when it is
// installed, so a burst of child updates becomes one paint per frame.

#pragma once

#include <md/view/render_backend.h>

#include <QEvent>
#include <QList>
#include <QPoint>
#include <QWidget>

#include <memory>

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
class GpuCompositor final : public QOpenGLWidget
#else
class GpuCompositor final : public QWidget
#endif
{
    Q_OBJECT

public:
    explicit GpuCompositor(QWidget* parent = nullptr);
    ~GpuCompositor() override;

    GpuCompositor(const GpuCompositor&) = delete;
    GpuCompositor& operator=(const GpuCompositor&) = delete;

    // True when this build and this QPA platform can create a usable GL
    // surface. False under the offscreen platform used by tests.
    [[nodiscard]] static bool isAvailable();

    // True once initializeGL() succeeded and the compositor is uploading
    // textures. Until then — and forever on a platform without GL — children
    // are painted by QWidget itself.
    [[nodiscard]] bool isUsingGpu() const noexcept;
    [[nodiscard]] RenderBackend effectiveBackend() const noexcept;

    // Managing children. addView() reparents a view into the compositor and
    // keeps its current geometry; setRootView() additionally resizes it to the
    // compositor's rectangle on every resize. The compositor never changes a
    // view's GPU flag: the caller opted in on the view, as the flag documents.
    void addView(View* view);
    void removeView(View* view);
    void setRootView(View* view);
    [[nodiscard]] View* rootView() const noexcept;
    [[nodiscard]] QList<View*> views() const;

    // Drops the cached textures. Called automatically on child updates; an
    // embedder that paints outside View::paintEvent() calls it after drawing.
    void invalidate();
    void invalidate(View* view);

signals:
    // Emitted when the compositor moves between the software and GPU paths,
    // for example when initializeGL() succeeds or the context is lost.
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
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    struct Private;

    // Brings the layer list in step with findChildren<View*>(DirectChildren).
    void syncViews();

    // Enables or disables the texture path for all children. A switch to
    // software restores the normal QWidget painting of every child.
    void setUsingGpu(bool usingGpu);

    void markDirty(View* view);
    void requestRepaint();
    [[nodiscard]] View* viewAt(const QPoint& position) const;
    void forwardMouse(QMouseEvent* event, QEvent::Type type);

    std::unique_ptr<Private> d_;
};

} // namespace md
