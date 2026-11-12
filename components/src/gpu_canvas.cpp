// Periodicity — implementation of the single-view QPainter-on-GL canvas.
//
// paintGL() deliberately does not write a shader. It opens a QPainter on the
// QOpenGLWidget, which Qt backs with its OpenGL paint engine, and calls
// QWidget::render() on the hosted View. That is the documented "QPainter on a
// GL surface" path: all of the component's paintContent() code runs unchanged,
// while the rasterisation of the resulting primitives uses the GL paint engine.
//
// The hosted view is a child widget with WA_DontShowOnScreen while the GL path
// is active, so Qt's backing store does not also paint it. Without a context
// the attribute is never set and the view remains an ordinary child: the
// software fallback is the same widget, painted by Qt.

#include <md/components/gpu_canvas.h>

#include <md/schedule/frame_scheduler.h>
#include <md/view/view.h>

#include <QCoreApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QThread>
#include <QtGlobal>

#include <algorithm>

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
#    include <QOpenGLContext>
#    include <QOpenGLFunctions>
#    include <QSurfaceFormat>
#endif

namespace md {

GpuCanvas::GpuCanvas(QWidget* parent)
    : 
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
      QOpenGLWidget(parent)
#else
      QWidget(parent)
#endif
{
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(8);
    setFormat(format);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
#endif

    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    // Marks this widget as a GPU container for View::setGpuAccelerated().
    setProperty("mdGpuHost", true);
}

GpuCanvas::GpuCanvas(View* view, QWidget* parent)
    : GpuCanvas(parent)
{
    setView(view);
}

GpuCanvas::~GpuCanvas()
{
    if (view_ != nullptr)
        view_->removeEventFilter(this);
}

bool GpuCanvas::isAvailable()
{
    return isBackendAvailable(RenderBackend::OpenGL);
}

bool GpuCanvas::isUsingGpu() const noexcept
{
    return gpuReady_;
}

RenderBackend GpuCanvas::effectiveBackend() const noexcept
{
    return gpuReady_ ? RenderBackend::OpenGL : RenderBackend::Software;
}

void GpuCanvas::setView(View* view)
{
    if (view_ == view) {
        syncHostedView();
        return;
    }

    if (view_ != nullptr) {
        view_->removeEventFilter(this);
        view_->setAttribute(Qt::WA_DontShowOnScreen, false);
        if (view_->parentWidget() == this)
            view_->setParent(nullptr);
    }

    view_ = view;

    if (view_ != nullptr) {
        view_->setParent(this);
        view_->installEventFilter(this);
        setFocusProxy(view_);
        syncHostedView();
        view_->show();
    } else {
        setFocusProxy(nullptr);
    }

    update();
}

View* GpuCanvas::view() const noexcept
{
    return view_.data();
}

void GpuCanvas::setUsingGpu(bool usingGpu)
{
    if (gpuReady_ == usingGpu)
        return;

    gpuReady_ = usingGpu;
    syncHostedView();
    emit gpuStateChanged(gpuReady_);
    update();
}

void GpuCanvas::syncHostedView()
{
    if (view_ == nullptr)
        return;

    view_->setGeometry(rect());

    // Only the GL path hides the view from the backing store. The software
    // path leaves it as a normal child, so Qt paints it exactly as before.
    const bool hideFromBackingStore =
        gpuReady_ && view_->testAttribute(Qt::WA_DontShowOnScreen);
    if (gpuReady_ && !hideFromBackingStore)
        view_->setAttribute(Qt::WA_DontShowOnScreen, true);
    else if (!gpuReady_ && view_->testAttribute(Qt::WA_DontShowOnScreen))
        view_->setAttribute(Qt::WA_DontShowOnScreen, false);
}

void GpuCanvas::requestViewUpdate()
{
    if (frameQueued_)
        return;

    if (auto* uiThread = schedule::UiThread::instance()) {
        frameQueued_ = true;
        const QPointer<GpuCanvas> guard(this);
        uiThread->requestFrame(schedule::FrameRequest::Update);
        uiThread->postTask(schedule::Priority::UiUpdate, [guard] {
            if (guard == nullptr)
                return;
            guard->frameQueued_ = false;
            guard->update();
        });
        return;
    }

    update();
}

void GpuCanvas::forwardMouse(QMouseEvent* event, QEvent::Type type)
{
    if (view_ == nullptr) {
        event->ignore();
        return;
    }

    view_->setFocus(Qt::MouseFocusReason);
    setFocus(Qt::MouseFocusReason);

    QMouseEvent translated(type, event->position(), event->globalPosition(),
                           event->button(), event->buttons(), event->modifiers(),
                           event->pointingDevice());
    QCoreApplication::sendEvent(view_, &translated);
    event->accept();
}

void GpuCanvas::mousePressEvent(QMouseEvent* event)
{
    if (gpuReady_)
        forwardMouse(event, QEvent::MouseButtonPress);
    else
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
        QOpenGLWidget::mousePressEvent(event);
#else
        QWidget::mousePressEvent(event);
#endif
}

void GpuCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (gpuReady_)
        forwardMouse(event, QEvent::MouseButtonRelease);
    else
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
        QOpenGLWidget::mouseReleaseEvent(event);
#else
        QWidget::mouseReleaseEvent(event);
#endif
}

void GpuCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (gpuReady_)
        forwardMouse(event, QEvent::MouseMove);
    else
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
        QOpenGLWidget::mouseMoveEvent(event);
#else
        QWidget::mouseMoveEvent(event);
#endif
}

void GpuCanvas::keyPressEvent(QKeyEvent* event)
{
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    QOpenGLWidget::keyPressEvent(event);
#else
    QWidget::keyPressEvent(event);
#endif
}

void GpuCanvas::keyReleaseEvent(QKeyEvent* event)
{
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    QOpenGLWidget::keyReleaseEvent(event);
#else
    QWidget::keyReleaseEvent(event);
#endif
}

void GpuCanvas::focusInEvent(QFocusEvent* event)
{
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    QOpenGLWidget::focusInEvent(event);
#else
    QWidget::focusInEvent(event);
#endif

    if (view_ != nullptr)
        view_->setFocus(event->reason());
}

void GpuCanvas::resizeEvent(QResizeEvent* event)
{
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    QOpenGLWidget::resizeEvent(event);
#else
    QWidget::resizeEvent(event);
#endif

    syncHostedView();
    requestViewUpdate();
}

void GpuCanvas::showEvent(QShowEvent* event)
{
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    QOpenGLWidget::showEvent(event);
#else
    QWidget::showEvent(event);
#endif

    if (view_ != nullptr)
        view_->show();
    if (!isAvailable())
        setUsingGpu(false);
}

void GpuCanvas::hideEvent(QHideEvent* event)
{
    if (view_ != nullptr)
        view_->hide();

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    QOpenGLWidget::hideEvent(event);
#else
    QWidget::hideEvent(event);
#endif
}

bool GpuCanvas::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == view_) {
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
            requestViewUpdate();
            break;
        default:
            break;
        }
    }

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    return QOpenGLWidget::eventFilter(watched, event);
#else
    return QWidget::eventFilter(watched, event);
#endif
}

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL

void GpuCanvas::initializeGL()
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());

    if (context() == nullptr || !context()->isValid()) {
        setUsingGpu(false);
        return;
    }

    QOpenGLFunctions* gl = context()->functions();
    if (gl == nullptr) {
        setUsingGpu(false);
        return;
    }

    const QColor background = palette().color(QPalette::Window);
    gl->glClearColor(static_cast<GLfloat>(background.redF()),
                     static_cast<GLfloat>(background.greenF()),
                     static_cast<GLfloat>(background.blueF()),
                     static_cast<GLfloat>(background.alphaF()));

    setUsingGpu(true);
    if (view_ != nullptr)
        view_->show();
}

void GpuCanvas::resizeGL(int width, int height)
{
    QOpenGLFunctions* gl = context() != nullptr ? context()->functions() : nullptr;
    if (gl == nullptr)
        return;

    const qreal ratio = devicePixelRatioF();
    gl->glViewport(0, 0, std::max(1, qRound(width * ratio)),
                   std::max(1, qRound(height * ratio)));
}

void GpuCanvas::paintGL()
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());

    frameQueued_ = false;

    if (!gpuReady_ || view_ == nullptr)
        return;

    QOpenGLFunctions* gl = context() != nullptr ? context()->functions() : nullptr;
    if (gl == nullptr)
        return;

    gl->glClear(GL_COLOR_BUFFER_BIT);

    // QPainter on a QOpenGLWidget uses Qt's OpenGL paint engine. The hosted
    // view is rendered through its normal paint pipeline, unchanged.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    view_->render(&painter, QPoint(0, 0), QRegion(),
                  QWidget::DrawWindowBackground | QWidget::DrawChildren);
}

#else // no OpenGLWidgets

void GpuCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
}

#endif // PERIODICITY_HAS_OPENGL

} // namespace md
