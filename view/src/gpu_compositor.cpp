// Periodicity — implementation of the optional texture compositor.
//
// The CPU side of each composited child is an ordinary QWidget::render() into a
// QImage: that keeps every component's paintContent() pipeline untouched.
// Everything below that — QOpenGLTexture upload and QOpenGLTextureBlitter —
// runs on the UI thread with the compositor's context current.
//
// A child is re-rendered only after it reports a change. View::requestUpdate()
// posts an UpdateRequest to the widget; the compositor's event filter sees it
// and marks the layer dirty. A repaint of the compositor then re-uploads that
// one texture. A plain compositor repaint with no dirty layer only re-blits the
// existing textures.

#include <md/view/gpu_compositor.h>

#include <md/schedule/frame_scheduler.h>
#include <md/view/view.h>

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QResizeEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QThread>
#include <QtGlobal>

#include <algorithm>
#include <utility>

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
#    include <QOpenGLContext>
#    include <QOpenGLFunctions>
#    include <QOpenGLTexture>
#    include <QOpenGLTextureBlitter>
#    include <QSurfaceFormat>
#endif

namespace md {
namespace {

struct GpuLayer
{
    QPointer<View> view;
    bool dirty = true;

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    QImage image;
    std::unique_ptr<QOpenGLTexture> texture;
#endif
};

[[nodiscard]] bool isGpuLayer(const GpuLayer& layer)
{
    return layer.view != nullptr && layer.view->isGpuAccelerated()
        && layer.view->isVisible();
}

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL

// Paints one composited child into layer.image, in logical coordinates scaled
// by the child's device pixel ratio. A transparent fill is intentional: the
// child's own background layer draws whatever the component decided to draw.
[[nodiscard]] bool renderLayerImage(GpuLayer& layer)
{
    View* view = layer.view;
    if (view == nullptr)
        return false;

    const qreal ratio = view->devicePixelRatioF();
    const QSize logical = view->size();
    const QSize pixels(std::max(1, qRound(logical.width() * ratio)),
                       std::max(1, qRound(logical.height() * ratio)));

    if (layer.image.size() != pixels) {
        layer.image = QImage(pixels, QImage::Format_RGBA8888_Premultiplied);
        layer.image.setDevicePixelRatio(ratio);
        layer.dirty = true;
    }

    if (!layer.dirty)
        return true;

    layer.image.fill(Qt::transparent);

    QPainter painter(&layer.image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.scale(ratio, ratio);
    view->render(&painter, QPoint(0, 0), QRegion(),
                 QWidget::DrawWindowBackground | QWidget::DrawChildren);

    layer.dirty = false;
    return true;
}

// Uploads the CPU-rendered frame. The texture is reallocated only when the
// child's pixel size changed; a same-size change is a plain setData() upload.
[[nodiscard]] bool uploadLayerTexture(GpuLayer& layer)
{
    if (layer.image.isNull())
        return false;

    const QSize pixels = layer.image.size();
    if (pixels.isEmpty())
        return false;

    if (!layer.texture || layer.texture->width() != pixels.width()
        || layer.texture->height() != pixels.height()) {
        layer.texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
        layer.texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
        layer.texture->setSize(pixels.width(), pixels.height());
        layer.texture->setMipLevels(1);
        layer.texture->setMinMagFilters(QOpenGLTexture::Linear,
                                        QOpenGLTexture::Linear);
        layer.texture->setWrapMode(QOpenGLTexture::ClampToEdge);

        if (!layer.texture->create()) {
            layer.texture.reset();
            return false;
        }
        layer.texture->allocateStorage();
    }

    layer.texture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                           layer.image.constBits());
    return true;
}

#endif // PERIODICITY_HAS_OPENGL

} // namespace

struct GpuCompositor::Private
{
    std::vector<GpuLayer> layers;
    QPointer<View> root;
    QPointer<View> pressedView;
    QPointer<View> focusView;

    bool gpuReady = false;
    bool usingGpu = false;
    bool frameQueued = false;

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    QOpenGLTextureBlitter blitter;
#endif
};

GpuCompositor::GpuCompositor(QWidget* parent)
    : 
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
      QOpenGLWidget(parent)
#else
      QWidget(parent)
#endif
    , d_(std::make_unique<Private>())
{
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    setFormat(format);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
#endif

    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    // View checks this dynamic property to decide whether a GPU opt-in has a
    // container that can honour it. Using a property keeps the kernel free of
    // a dependency on this class (or on components/).
    setProperty("mdGpuHost", true);
}

GpuCompositor::~GpuCompositor()
{
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    // Texture and blitter destruction must happen with the context current.
    // The base QOpenGLWidget destroys the context only after this destructor.
    if (d_->gpuReady && context() != nullptr && context()->isValid()) {
        makeCurrent();
        d_->layers.clear();
        if (d_->blitter.isCreated())
            d_->blitter.destroy();
        doneCurrent();
    }
#endif

    for (GpuLayer& layer : d_->layers) {
        if (layer.view != nullptr)
            layer.view->removeEventFilter(this);
    }
}

bool GpuCompositor::isAvailable()
{
    return isBackendAvailable(RenderBackend::OpenGL);
}

bool GpuCompositor::isUsingGpu() const noexcept
{
    return d_->usingGpu;
}

RenderBackend GpuCompositor::effectiveBackend() const noexcept
{
    return d_->usingGpu ? RenderBackend::OpenGL : RenderBackend::Software;
}

void GpuCompositor::addView(View* view)
{
    if (view == nullptr)
        return;

    if (view->parentWidget() != this) {
        view->setParent(this);
        view->move(QPoint(0, 0));
    }

    syncViews();
    view->show();
    requestRepaint();
}

void GpuCompositor::removeView(View* view)
{
    if (view == nullptr)
        return;

    for (auto it = d_->layers.begin(); it != d_->layers.end(); ++it) {
        if (it->view == view) {
            view->removeEventFilter(this);
            d_->layers.erase(it);
            break;
        }
    }

    if (d_->root == view)
        d_->root = nullptr;

    if (view->parentWidget() == this)
        view->setParent(nullptr);
    requestRepaint();
}

void GpuCompositor::setRootView(View* view)
{
    if (view == nullptr) {
        d_->root = nullptr;
        return;
    }

    addView(view);
    d_->root = view;
    view->setGeometry(rect());
}

View* GpuCompositor::rootView() const noexcept
{
    return d_->root.data();
}

QList<View*> GpuCompositor::views() const
{
    // Pick up direct children that were parented to the compositor without
    // going through addView() first.
    const_cast<GpuCompositor*>(this)->syncViews();

    QList<View*> result;
    for (const GpuLayer& layer : d_->layers) {
        if (layer.view != nullptr)
            result.append(layer.view.data());
    }
    return result;
}

void GpuCompositor::invalidate()
{
    for (GpuLayer& layer : d_->layers)
        layer.dirty = true;
    requestRepaint();
}

void GpuCompositor::invalidate(View* view)
{
    markDirty(view);
}

void GpuCompositor::syncViews()
{
    const QList<View*> direct = findChildren<View*>(Qt::FindDirectChildrenOnly);

    // Drop layers whose view was destroyed or reparented away.
    d_->layers.erase(
        std::remove_if(d_->layers.begin(), d_->layers.end(),
                       [&direct](const GpuLayer& layer) {
            return layer.view == nullptr || !direct.contains(layer.view.data());
        }),
        d_->layers.end());

    for (View* view : direct) {
        const bool known = std::any_of(
            d_->layers.cbegin(), d_->layers.cend(),
            [view](const GpuLayer& layer) { return layer.view == view; });
        if (known)
            continue;

        GpuLayer layer;
        layer.view = view;
        d_->layers.push_back(std::move(layer));

        connect(view, &View::gpuAccelerationChanged, this,
                [this, view](bool) {
                    if (!d_->usingGpu)
                        return;
                    view->setAttribute(Qt::WA_DontShowOnScreen,
                                       view->isGpuAccelerated());
                    markDirty(view);
                    requestRepaint();
                });

        view->installEventFilter(this);
    }

    if (!d_->usingGpu)
        return;

    for (GpuLayer& layer : d_->layers) {
        if (layer.view != nullptr)
            layer.view->setAttribute(Qt::WA_DontShowOnScreen,
                                     layer.view->isGpuAccelerated());
    }
}

void GpuCompositor::setUsingGpu(bool usingGpu)
{
    if (d_->usingGpu == usingGpu) {
        return;
    }

    d_->usingGpu = usingGpu;

    // A switch to software restores ordinary QWidget composition for every
    // child, so no view is left invisible if the GL context goes away.
    for (GpuLayer& layer : d_->layers) {
        if (layer.view != nullptr) {
            layer.view->setAttribute(Qt::WA_DontShowOnScreen,
                                     usingGpu && layer.view->isGpuAccelerated());
            layer.dirty = true;
            layer.view->update();
        }
    }

    emit gpuStateChanged(d_->usingGpu);
    update();
}

void GpuCompositor::markDirty(View* view)
{
    if (view == nullptr) {
        invalidate();
        return;
    }

    for (GpuLayer& layer : d_->layers) {
        if (layer.view == view) {
            layer.dirty = true;
            break;
        }
    }
    requestRepaint();
}

void GpuCompositor::requestRepaint()
{
    if (d_->frameQueued)
        return;

    if (auto* uiThread = schedule::UiThread::instance()) {
        d_->frameQueued = true;
        const QPointer<GpuCompositor> guard(this);
        uiThread->requestFrame(schedule::FrameRequest::Update);
        uiThread->postTask(schedule::Priority::UiUpdate, [guard] {
            if (guard == nullptr)
                return;
            guard->d_->frameQueued = false;
            guard->update();
        });
        return;
    }

    update();
}

View* GpuCompositor::viewAt(const QPoint& position) const
{
    // Last child wins: QWidget children are painted in insertion order, so the
    // last matching layer is the topmost one.
    for (auto it = d_->layers.crbegin(); it != d_->layers.crend(); ++it) {
        View* view = it->view;
        if (view != nullptr && isGpuLayer(*it) && view->isEnabled()
            && view->geometry().contains(position)) {
            return view;
        }
    }
    return nullptr;
}

void GpuCompositor::forwardMouse(QMouseEvent* event, QEvent::Type type)
{
    View* target = type == QEvent::MouseButtonRelease ? d_->pressedView.data()
                                                      : viewAt(event->position().toPoint());

    if (type == QEvent::MouseButtonPress && target != nullptr) {
        d_->pressedView = target;
        d_->focusView = target;
        target->setFocus(Qt::MouseFocusReason);
        setFocus(Qt::MouseFocusReason);
    }

    if (target == nullptr) {
        event->ignore();
        return;
    }

    const QPointF local = event->position() - QPointF(target->pos());
    QMouseEvent translated(type, local, event->globalPosition(), event->button(),
                           event->buttons(), event->modifiers(),
                           event->pointingDevice());
    QCoreApplication::sendEvent(target, &translated);
    event->accept();

    if (type == QEvent::MouseButtonRelease)
        d_->pressedView = nullptr;
}

void GpuCompositor::mousePressEvent(QMouseEvent* event)
{
    forwardMouse(event, QEvent::MouseButtonPress);
}

void GpuCompositor::mouseReleaseEvent(QMouseEvent* event)
{
    forwardMouse(event, QEvent::MouseButtonRelease);
}

void GpuCompositor::mouseMoveEvent(QMouseEvent* event)
{
    forwardMouse(event, QEvent::MouseMove);
}

void GpuCompositor::keyPressEvent(QKeyEvent* event)
{
    View* target = d_->focusView;
    if (target == nullptr && !d_->layers.empty())
        target = d_->layers.back().view;
    if (target != nullptr)
        QCoreApplication::sendEvent(target, event);
    else
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
        QOpenGLWidget::keyPressEvent(event);
#else
        QWidget::keyPressEvent(event);
#endif
}

void GpuCompositor::keyReleaseEvent(QKeyEvent* event)
{
    View* target = d_->focusView;
    if (target == nullptr && !d_->layers.empty())
        target = d_->layers.back().view;
    if (target != nullptr)
        QCoreApplication::sendEvent(target, event);
    else
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
        QOpenGLWidget::keyReleaseEvent(event);
#else
        QWidget::keyReleaseEvent(event);
#endif
}

void GpuCompositor::resizeEvent(QResizeEvent* event)
{
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    QOpenGLWidget::resizeEvent(event);
#else
    QWidget::resizeEvent(event);
#endif

    if (d_->root != nullptr)
        d_->root->setGeometry(rect());

    for (GpuLayer& layer : d_->layers)
        layer.dirty = true;
    requestRepaint();
}

void GpuCompositor::showEvent(QShowEvent* event)
{
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    QOpenGLWidget::showEvent(event);
#else
    QWidget::showEvent(event);
#endif

    syncViews();
    if (!isAvailable())
        setUsingGpu(false);
}

void GpuCompositor::hideEvent(QHideEvent* event)
{
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    QOpenGLWidget::hideEvent(event);
#else
    QWidget::hideEvent(event);
#endif
}

bool GpuCompositor::event(QEvent* event)
{
    if (event->type() == QEvent::ChildAdded
        || event->type() == QEvent::ChildRemoved) {
        // The child is still under construction while ChildAdded is delivered;
        // a queued pass sees the finished View.
        QMetaObject::invokeMethod(
            this, [this] { syncViews(); }, Qt::QueuedConnection);
    }

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    return QOpenGLWidget::event(event);
#else
    return QWidget::event(event);
#endif
}

bool GpuCompositor::eventFilter(QObject* watched, QEvent* event)
{
    for (GpuLayer& layer : d_->layers) {
        if (layer.view != watched)
            continue;

        switch (event->type()) {
        case QEvent::UpdateRequest:
        case QEvent::Paint:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::ThemeChange:
        case QEvent::PaletteChange:
        case QEvent::FontChange:
        case QEvent::EnabledChange:
        case QEvent::LayoutRequest:
        case QEvent::DynamicPropertyChange:
            layer.dirty = true;
            requestRepaint();
            break;
        default:
            break;
        }
        break;
    }

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    return QOpenGLWidget::eventFilter(watched, event);
#else
    return QWidget::eventFilter(watched, event);
#endif
}

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL

void GpuCompositor::initializeGL()
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());

    syncViews();

    if (context() == nullptr || !context()->isValid()) {
        setUsingGpu(false);
        return;
    }

    if (!d_->blitter.isCreated() && !d_->blitter.create()) {
        qWarning("md::GpuCompositor: could not create the texture blitter; "
                 "keeping the software path");
        setUsingGpu(false);
        return;
    }

    d_->gpuReady = true;
    setUsingGpu(true);
}

void GpuCompositor::resizeGL(int width, int height)
{
    QOpenGLFunctions* gl = context() != nullptr ? context()->functions() : nullptr;
    if (gl == nullptr)
        return;

    const qreal ratio = devicePixelRatioF();
    gl->glViewport(0, 0, std::max(1, qRound(width * ratio)),
                   std::max(1, qRound(height * ratio)));
}

void GpuCompositor::paintGL()
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());

    d_->frameQueued = false;

    if (!d_->usingGpu || !d_->blitter.isCreated())
        return;

    QOpenGLFunctions* gl = context() != nullptr ? context()->functions() : nullptr;
    if (gl == nullptr)
        return;

    const qreal ratio = devicePixelRatioF();
    const int pixelWidth = std::max(1, qRound(width() * ratio));
    const int pixelHeight = std::max(1, qRound(height() * ratio));

    const QColor background = palette().color(QPalette::Window);
    gl->glViewport(0, 0, pixelWidth, pixelHeight);
    gl->glClearColor(static_cast<GLfloat>(background.redF()),
                     static_cast<GLfloat>(background.greenF()),
                     static_cast<GLfloat>(background.blueF()),
                     static_cast<GLfloat>(background.alphaF()));
    gl->glClear(GL_COLOR_BUFFER_BIT);

    gl->glDisable(GL_DEPTH_TEST);
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glEnable(GL_BLEND);
    // The frames are premultiplied (QImage::Format_RGBA8888_Premultiplied).
    gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    d_->blitter.bind();

    for (GpuLayer& layer : d_->layers) {
        if (!isGpuLayer(layer))
            continue;

        View* view = layer.view;
        const QRect geometry = view->geometry().intersected(rect());
        if (geometry.isEmpty())
            continue;

        if (!renderLayerImage(layer) || !uploadLayerTexture(layer))
            continue;

        const int x = qRound(geometry.x() * ratio);
        const int y = qRound((height() - geometry.y() - geometry.height()) * ratio);
        const int w = std::max(1, qRound(geometry.width() * ratio));
        const int h = std::max(1, qRound(geometry.height() * ratio));

        gl->glViewport(x, y, w, h);
        const QMatrix4x4 target = QOpenGLTextureBlitter::targetTransform(
            QRectF(0, 0, w, h), QRect(0, 0, w, h));
        d_->blitter.blit(layer.texture->textureId(), target,
                         QOpenGLTextureBlitter::OriginTopLeft);
    }

    d_->blitter.release();
}

#else // no OpenGLWidgets

void GpuCompositor::paintEvent(QPaintEvent* event)
{
    // Without a GL surface the compositor is an ordinary QWidget container:
    // its children paint themselves through Qt's backing store. Nothing to do
    // here; in particular no QPainter call is needed to keep them correct.
    Q_UNUSED(event);
}

#endif // PERIODICITY_HAS_OPENGL

} // namespace md
