// Periodicity — implementation of the 3D context.
//
// The software path is deliberately small: every primitive is transformed on
// the CPU, projected through the current camera and drawn with QPainter after
// a stable back-to-front depth sort. The optional QOpenGLWidget path shares the
// retained primitive list but lets the GPU depth buffer resolve occlusion.

#include <md/components/context3d.h>

#include <md/core/density.h>
#include <md/theme/theme.h>

#include <QBrush>
#include <QGuiApplication>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QScopeGuard>
#include <QString>
#include <QVector4D>
#include <QWidget>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
#include <utility>
#include <vector>

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QSurfaceFormat>

#include <memory>
#endif

namespace md {
namespace {

constexpr float kCameraEpsilon = 1.0e-6f;
constexpr qreal kMinimumPerspectiveDepth = 1.0e-9;

template <class Fn>
void guardContext3D(const char* where, Fn&& fn)
{
    try {
        std::forward<Fn>(fn)();
    } catch (const std::exception& error) {
        qWarning("md::Context3D: %s failed: %s", where, error.what());
    } catch (...) {
        qWarning("md::Context3D: %s failed: unknown exception", where);
    }
}

[[nodiscard]] bool finiteVector(const QVector3D& vector) noexcept
{
    return std::isfinite(vector.x()) && std::isfinite(vector.y())
        && std::isfinite(vector.z());
}

[[nodiscard]] QColor themePrimitiveColour(QWidget* host, const QColor& requested)
{
    if (requested.isValid())
        return requested;

    return Theme::forWidget(host).colours().primary;
}

[[nodiscard]] QColor themeSurfaceColour(QWidget* host)
{
    return Theme::forWidget(host).colours().surface;
}

} // namespace

Context3D::Context3D(QWidget* host)
    : host_(host)
{
    try {
        background_ = themeSurfaceColour(host);
    } catch (...) {
        background_ = QColor(Qt::gray);
    }
}

Context3D::~Context3D()
{
    // The GL widget is normally a child of the Canvas and would be deleted by
    // Qt anyway; deleting it explicitly while Context3D is still alive keeps
    // the lifetime deterministic. QPointer makes a second deletion impossible.
    if (glWidget_)
        delete glWidget_.data();
}

void Context3D::setCamera(const QVector3D& position, const QVector3D& target,
                          const QVector3D& up)
{
    guardContext3D("Context3D::setCamera", [this, &position, &target, &up] {
        if (!finiteVector(position) || !finiteVector(target) || !finiteVector(up)) {
            qWarning("md::Context3D: ignoring a non-finite camera");
            return;
        }

        const QVector3D direction = target - position;
        if (direction.lengthSquared() <= kCameraEpsilon
            || up.lengthSquared() <= kCameraEpsilon
            || QVector3D::crossProduct(direction, up).lengthSquared()
                <= kCameraEpsilon) {
            qWarning("md::Context3D: ignoring a degenerate camera");
            return;
        }

        cameraPosition_ = position;
        cameraTarget_ = target;
        cameraUp_ = up;
        requestDraw();
    });
}

void Context3D::setCameraPosition(const QVector3D& position)
{
    setCamera(position, cameraTarget_, cameraUp_);
}

void Context3D::setCameraTarget(const QVector3D& target)
{
    setCamera(cameraPosition_, target, cameraUp_);
}

void Context3D::setCameraUp(const QVector3D& up)
{
    setCamera(cameraPosition_, cameraTarget_, up);
}

QVector3D Context3D::cameraPosition() const noexcept
{
    return cameraPosition_;
}

QVector3D Context3D::cameraTarget() const noexcept
{
    return cameraTarget_;
}

QVector3D Context3D::cameraUp() const noexcept
{
    return cameraUp_;
}

void Context3D::perspective(qreal fovYDegrees, qreal nearPlane, qreal farPlane)
{
    guardContext3D("Context3D::perspective", [this, fovYDegrees, nearPlane, farPlane] {
        if (!std::isfinite(fovYDegrees) || !std::isfinite(nearPlane)
            || !std::isfinite(farPlane) || fovYDegrees <= 0.0
            || fovYDegrees >= 180.0 || nearPlane <= 0.0 || farPlane <= nearPlane) {
            qWarning("md::Context3D: ignoring an invalid perspective projection");
            return;
        }

        projection_ = Projection::Perspective;
        fovYDegrees_ = fovYDegrees;
        nearPlane_ = nearPlane;
        farPlane_ = farPlane;
        requestDraw();
    });
}

void Context3D::orthographic(qreal verticalSpan, qreal nearPlane, qreal farPlane)
{
    guardContext3D("Context3D::orthographic",
                   [this, verticalSpan, nearPlane, farPlane] {
        if (!std::isfinite(verticalSpan) || !std::isfinite(nearPlane)
            || !std::isfinite(farPlane) || verticalSpan <= 0.0
            || nearPlane <= 0.0 || farPlane <= nearPlane) {
            qWarning("md::Context3D: ignoring an invalid orthographic projection");
            return;
        }

        projection_ = Projection::Orthographic;
        orthoSymmetric_ = true;
        orthoVerticalSpan_ = verticalSpan;
        nearPlane_ = nearPlane;
        farPlane_ = farPlane;
        requestDraw();
    });
}

void Context3D::orthographic(qreal left, qreal right, qreal bottom, qreal top,
                             qreal nearPlane, qreal farPlane)
{
    guardContext3D("Context3D::orthographic",
                   [this, left, right, bottom, top, nearPlane, farPlane] {
        if (!std::isfinite(left) || !std::isfinite(right)
            || !std::isfinite(bottom) || !std::isfinite(top)
            || !std::isfinite(nearPlane) || !std::isfinite(farPlane)
            || right <= left || top <= bottom || nearPlane <= 0.0
            || farPlane <= nearPlane) {
            qWarning("md::Context3D: ignoring an invalid orthographic projection");
            return;
        }

        projection_ = Projection::Orthographic;
        orthoSymmetric_ = false;
        orthoLeft_ = left;
        orthoRight_ = right;
        orthoBottom_ = bottom;
        orthoTop_ = top;
        nearPlane_ = nearPlane;
        farPlane_ = farPlane;
        requestDraw();
    });
}

Context3D::Projection Context3D::projectionMode() const noexcept
{
    return projection_;
}

qreal Context3D::fieldOfView() const noexcept
{
    return fovYDegrees_;
}

qreal Context3D::nearPlane() const noexcept
{
    return nearPlane_;
}

qreal Context3D::farPlane() const noexcept
{
    return farPlane_;
}

void Context3D::setViewport(const QRectF& viewport)
{
    guardContext3D("Context3D::setViewport", [this, &viewport] {
        if (!std::isfinite(viewport.x()) || !std::isfinite(viewport.y())
            || !std::isfinite(viewport.width()) || !std::isfinite(viewport.height())
            || viewport.width() < 0.0 || viewport.height() < 0.0) {
            qWarning("md::Context3D: ignoring a non-finite viewport");
            return;
        }

        viewport_ = viewport;
        updateOpenGLGeometry();
    });
}

QRectF Context3D::viewport() const noexcept
{
    return viewport_;
}

void Context3D::setModelMatrix(const QMatrix4x4& matrix)
{
    guardContext3D("Context3D::setModelMatrix", [this, &matrix] {
        const float* values = matrix.constData();
        for (int index = 0; index < 16; ++index) {
            if (!std::isfinite(values[index])) {
                qWarning("md::Context3D: ignoring a non-finite model matrix");
                return;
            }
        }

        modelMatrix_ = matrix;
        requestDraw();
    });
}

void Context3D::resetModelMatrix()
{
    guardContext3D("Context3D::resetModelMatrix", [this] {
        modelMatrix_.setToIdentity();
        requestDraw();
    });
}

QMatrix4x4 Context3D::modelMatrix() const
{
    return modelMatrix_;
}

std::optional<QPointF> Context3D::project(const QVector3D& point) const
{
    try {
        if (!finiteVector(point))
            return std::nullopt;

        const QRectF area = viewport_;
        if (!(area.width() > 0.0) || !(area.height() > 0.0))
            return std::nullopt;

        const QVector3D view = viewPoint(point);
        if (!finiteVector(view))
            return std::nullopt;

        return projectView(view, area.width() / area.height());
    } catch (const std::exception& error) {
        qWarning("md::Context3D: project failed: %s", error.what());
        return std::nullopt;
    } catch (...) {
        qWarning("md::Context3D: project failed: unknown exception");
        return std::nullopt;
    }
}

void Context3D::line3D(const QVector3D& from, const QVector3D& to,
                       const QColor& colour)
{
    guardContext3D("Context3D::line3D", [this, &from, &to, &colour] {
        if (!finiteVector(from) || !finiteVector(to))
            return;

        Primitive primitive;
        primitive.kind = Primitive::Kind::Line;
        primitive.vertices.append(from);
        primitive.vertices.append(to);
        primitive.colour = themePrimitiveColour(host_.data(), colour);
        primitives_.append(std::move(primitive));
    });
}

void Context3D::triangle3D(const QVector3D& a, const QVector3D& b,
                           const QVector3D& c, const QColor& colour)
{
    guardContext3D("Context3D::triangle3D", [this, &a, &b, &c, &colour] {
        if (!finiteVector(a) || !finiteVector(b) || !finiteVector(c))
            return;

        Primitive primitive;
        primitive.kind = Primitive::Kind::Triangle;
        primitive.vertices.append(a);
        primitive.vertices.append(b);
        primitive.vertices.append(c);
        primitive.colour = themePrimitiveColour(host_.data(), colour);
        primitives_.append(std::move(primitive));
    });
}

void Context3D::quad3D(const QVector3D& a, const QVector3D& b,
                       const QVector3D& c, const QVector3D& d,
                       const QColor& colour)
{
    guardContext3D("Context3D::quad3D", [this, &a, &b, &c, &d, &colour] {
        if (!finiteVector(a) || !finiteVector(b) || !finiteVector(c)
            || !finiteVector(d)) {
            return;
        }

        Primitive primitive;
        primitive.kind = Primitive::Kind::Quad;
        primitive.vertices.append(a);
        primitive.vertices.append(b);
        primitive.vertices.append(c);
        primitive.vertices.append(d);
        primitive.colour = themePrimitiveColour(host_.data(), colour);
        primitives_.append(std::move(primitive));
    });
}

void Context3D::points3D(const QVector<QVector3D>& points, const QColor& colour)
{
    guardContext3D("Context3D::points3D", [this, &points, &colour] {
        Primitive primitive;
        primitive.kind = Primitive::Kind::Points;
        primitive.vertices.reserve(points.size());
        for (const QVector3D& point : points) {
            if (finiteVector(point))
                primitive.vertices.append(point);
        }

        if (primitive.vertices.isEmpty())
            return;

        primitive.colour = themePrimitiveColour(host_.data(), colour);
        primitives_.append(std::move(primitive));
    });
}

void Context3D::clearPrimitives()
{
    guardContext3D("Context3D::clearPrimitives",
                   [this] { primitives_.clear(); });
}

int Context3D::primitiveCount() const noexcept
{
    return static_cast<int>(primitives_.size());
}

void Context3D::setBackFaceCulling(bool enabled)
{
    guardContext3D("Context3D::setBackFaceCulling",
                   [this, enabled] { backFaceCulling_ = enabled; });
}

bool Context3D::backFaceCulling() const noexcept
{
    return backFaceCulling_;
}

void Context3D::setLineWidth(qreal widthDp)
{
    guardContext3D("Context3D::setLineWidth", [this, widthDp] {
        lineWidthDp_ = std::max<qreal>(0.0, std::isfinite(widthDp) ? widthDp : 1.5);
        requestDraw();
    });
}

qreal Context3D::lineWidth() const noexcept
{
    return lineWidthDp_;
}

void Context3D::setPointRadius(qreal radiusDp)
{
    guardContext3D("Context3D::setPointRadius", [this, radiusDp] {
        pointRadiusDp_ = std::max<qreal>(0.5, std::isfinite(radiusDp) ? radiusDp : 3.0);
        requestDraw();
    });
}

qreal Context3D::pointRadius() const noexcept
{
    return pointRadiusDp_;
}

void Context3D::setBackend(Backend backend)
{
    guardContext3D("Context3D::setBackend", [this, backend] {
        if (requestedBackend_ == backend) {
            if (backend == Backend::OpenGL)
                ensureOpenGLWidget();
            updateOpenGLGeometry();
            return;
        }

        requestedBackend_ = backend;
        if (backend == Backend::OpenGL)
            ensureOpenGLWidget();
        updateOpenGLGeometry();
        requestDraw();
    });
}

Context3D::Backend Context3D::backend() const noexcept
{
    return requestedBackend_;
}

Context3D::Backend Context3D::effectiveBackend() const noexcept
{
    if (requestedBackend_ == Backend::OpenGL && hostModeActive_ && glWidget_
        && glWidget_->isVisible() && glReady_) {
        return Backend::OpenGL;
    }

    return Backend::Software;
}

void Context3D::setBackground(const QColor& colour)
{
    guardContext3D("Context3D::setBackground", [this, &colour] {
        background_ = colour.isValid() ? colour : themeSurfaceColour(host_.data());
        requestOpenGLUpdate();
    });
}

QColor Context3D::background() const
{
    return background_;
}

void Context3D::paint(QPainter& painter, const QRectF& content)
{
    guardContext3D("Context3D::paint", [this, &painter, &content] {
        setViewport(content);
        if (!painter.isActive() || content.isEmpty())
            return;

        if (requestedBackend_ == Backend::OpenGL) {
            if (effectiveBackend() == Backend::OpenGL) {
                // The embedded QOpenGLWidget paints the same primitive list on
                // top of the parent's background.
                return;
            }

            warnFallbackOnce();
        }

        renderSoftware(painter, content);
    });
}

void Context3D::requestDraw()
{
    guardContext3D("Context3D::requestDraw", [this] {
        if (host_)
            host_->update();
        requestOpenGLUpdate();
    });
}

QMatrix4x4 Context3D::viewMatrix() const
{
    QMatrix4x4 view;
    view.lookAt(cameraPosition_, cameraTarget_, cameraUp_);
    return view;
}

QVector3D Context3D::viewPoint(const QVector3D& point) const
{
    const QVector4D worldPoint(point, 1.0f);
    const QVector4D view = viewMatrix() * (modelMatrix_ * worldPoint);
    if (!std::isfinite(view.w()) || std::abs(view.w()) <= 1.0e-12f) {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        return QVector3D(nan, nan, nan);
    }

    return view.toVector3D() / view.w();
}

std::optional<QPointF> Context3D::projectView(const QVector3D& view,
                                              qreal aspect) const
{
    if (!finiteVector(view) || !std::isfinite(aspect) || aspect <= 0.0)
        return std::nullopt;

    const QRectF area = viewport_;
    if (!(area.width() > 0.0) || !(area.height() > 0.0))
        return std::nullopt;

    const qreal depth = -static_cast<qreal>(view.z());
    if (depth < nearPlane_ || depth > farPlane_)
        return std::nullopt;

    qreal ndcX = 0.0;
    qreal ndcY = 0.0;

    if (projection_ == Projection::Perspective) {
        const qreal tangent = std::tan(qDegreesToRadians(fovYDegrees_ * 0.5));
        if (!std::isfinite(tangent) || tangent <= kMinimumPerspectiveDepth
            || depth <= kMinimumPerspectiveDepth) {
            return std::nullopt;
        }

        ndcX = static_cast<qreal>(view.x()) / (aspect * tangent * depth);
        ndcY = static_cast<qreal>(view.y()) / (tangent * depth);
    } else if (orthoSymmetric_) {
        const qreal halfHeight = orthoVerticalSpan_ * 0.5;
        const qreal halfWidth = halfHeight * aspect;
        if (halfWidth <= 0.0 || halfHeight <= 0.0)
            return std::nullopt;

        ndcX = static_cast<qreal>(view.x()) / halfWidth;
        ndcY = static_cast<qreal>(view.y()) / halfHeight;
    } else {
        const qreal width = orthoRight_ - orthoLeft_;
        const qreal height = orthoTop_ - orthoBottom_;
        if (width <= 0.0 || height <= 0.0)
            return std::nullopt;

        ndcX = 2.0 * (static_cast<qreal>(view.x()) - orthoLeft_) / width - 1.0;
        ndcY = 2.0 * (static_cast<qreal>(view.y()) - orthoBottom_) / height - 1.0;
    }

    if (!std::isfinite(ndcX) || !std::isfinite(ndcY))
        return std::nullopt;

    const qreal screenX = area.left() + (ndcX * 0.5 + 0.5) * area.width();
    const qreal screenY = area.top() + (0.5 - ndcY * 0.5) * area.height();
    if (!std::isfinite(screenX) || !std::isfinite(screenY))
        return std::nullopt;

    return QPointF(screenX, screenY);
}

bool Context3D::isFrontFacing(const Primitive& primitive) const
{
    if (primitive.vertices.size() < 3)
        return false;

    const QVector3D first = viewPoint(primitive.vertices.at(0));
    const QVector3D second = viewPoint(primitive.vertices.at(1));
    const QVector3D third = viewPoint(primitive.vertices.at(2));
    if (!finiteVector(first) || !finiteVector(second) || !finiteVector(third))
        return false;

    const QVector3D normal = QVector3D::crossProduct(second - first, third - first);
    if (normal.lengthSquared() <= kCameraEpsilon)
        return false;

    const QVector3D centre = (first + second + third) / 3.0f;
    const QVector3D towardCamera = projection_ == Projection::Perspective
        ? -centre
        : QVector3D(0.0f, 0.0f, 1.0f);

    return QVector3D::dotProduct(normal, towardCamera) > 0.0f;
}

void Context3D::renderSoftware(QPainter& painter, const QRectF& content)
{
    struct Projected
    {
        const Primitive* primitive = nullptr;
        QVector<QPointF> points;
        qreal depth = 0.0;
    };

    const qreal aspect = content.width() / content.height();
    QVector<Projected> visible;

    for (const Primitive& primitive : primitives_) {
        if (backFaceCulling_
            && (primitive.kind == Primitive::Kind::Triangle
                || primitive.kind == Primitive::Kind::Quad)
            && !isFrontFacing(primitive)) {
            continue;
        }

        Projected projected;
        projected.primitive = &primitive;
        qreal depthSum = 0.0;
        int depthSamples = 0;
        bool complete = true;

        for (const QVector3D& vertex : primitive.vertices) {
            const QVector3D view = viewPoint(vertex);
            const std::optional<QPointF> screen = projectView(view, aspect);
            if (!screen) {
                if (primitive.kind == Primitive::Kind::Points)
                    continue;

                complete = false;
                break;
            }

            projected.points.append(*screen);
            depthSum += -static_cast<qreal>(view.z());
            ++depthSamples;
        }

        if (!complete || projected.points.isEmpty() || depthSamples == 0)
            continue;

        projected.depth = depthSum / static_cast<qreal>(depthSamples);
        visible.append(std::move(projected));
    }

    // Painter's algorithm: far primitives first. stable_sort keeps the scene
    // order for primitives at the same depth, which makes overlapping coplanar
    // geometry deterministic.
    std::stable_sort(visible.begin(), visible.end(),
                     [](const Projected& lhs, const Projected& rhs) {
        return lhs.depth > rhs.depth;
    });

    if (visible.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(host_.data());
    const Density& density = theme.density();
    const qreal strokeWidth = std::max<qreal>(0.0, density.dp(lineWidthDp_));
    const qreal pointRadius = std::max<qreal>(0.5, density.dp(pointRadiusDp_));

    painter.save();
    const auto restorePainter = qScopeGuard([&painter] { painter.restore(); });
    painter.setTransform(QTransform());
    painter.setClipRect(content, Qt::IntersectClip);
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (const Projected& projected : visible) {
        const Primitive& primitive = *projected.primitive;
        painter.setBrush(primitive.colour);

        switch (primitive.kind) {
        case Primitive::Kind::Line: {
            if (projected.points.size() < 2)
                break;

            QPen pen(primitive.colour);
            pen.setWidthF(strokeWidth);
            pen.setCapStyle(Qt::RoundCap);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(projected.points.at(0), projected.points.at(1));
            break;
        }
        case Primitive::Kind::Triangle:
        case Primitive::Kind::Quad: {
            QPolygonF polygon;
            polygon.reserve(projected.points.size());
            for (const QPointF& point : projected.points)
                polygon.append(point);

            painter.setPen(Qt::NoPen);
            painter.drawPolygon(polygon, Qt::WindingFill);
            break;
        }
        case Primitive::Kind::Points: {
            painter.setPen(Qt::NoPen);
            for (const QPointF& point : projected.points)
                painter.drawEllipse(point, pointRadius, pointRadius);
            break;
        }
        }
    }
}

void Context3D::warnFallbackOnce()
{
    if (fallbackWarned_)
        return;

    fallbackWarned_ = true;
    qWarning("md::Context3D: OpenGL backend unavailable (no current GL context); "
             "falling back to software");
}

void Context3D::requestOpenGLUpdate()
{
    if (glWidget_)
        glWidget_->update();
}

void Context3D::setHostModeActive(bool active)
{
    hostModeActive_ = active;
    if (active) {
        ensureOpenGLWidget();
        updateOpenGLGeometry();
    } else if (glWidget_) {
        glWidget_->hide();
    }
}

void Context3D::updateOpenGLGeometry()
{
    if (requestedBackend_ == Backend::OpenGL && hostModeActive_)
        ensureOpenGLWidget();

    if (!glWidget_)
        return;

    if (!hostModeActive_ || requestedBackend_ != Backend::OpenGL || !host_) {
        glWidget_->hide();
        return;
    }

    const QRect area = viewport_.toAlignedRect().intersected(host_->rect());
    if (area.isEmpty()) {
        glWidget_->hide();
        return;
    }

    glWidget_->setGeometry(area);
    glWidget_->show();
}

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL

namespace {

constexpr int kFloatsPerVertex = 7;

const char* const kVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
uniform mat4 uMvp;
uniform float uPointSize;
out vec4 vColor;
void main()
{
    vColor = aColor;
    gl_PointSize = uPointSize;
    gl_Position = uMvp * vec4(aPosition, 1.0);
}
)";

const char* const kFragmentShader = R"(
#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main()
{
    fragColor = vColor;
}
)";

} // namespace

class Context3DGlWidget final : public QOpenGLWidget
{
public:
    explicit Context3DGlWidget(Context3D* context, QWidget* parent = nullptr);
    ~Context3DGlWidget() override;

    Context3DGlWidget(const Context3DGlWidget&) = delete;
    Context3DGlWidget& operator=(const Context3DGlWidget&) = delete;

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

private:
    struct Batch
    {
        GLenum mode = GL_TRIANGLES;
        GLint first = 0;
        GLsizei count = 0;
        bool line = false;
    };

    Context3D* context_ = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> program_;
    std::unique_ptr<QOpenGLBuffer> buffer_;
    std::unique_ptr<QOpenGLVertexArrayObject> vertexArray_;
};

Context3DGlWidget::Context3DGlWidget(Context3D* context, QWidget* parent)
    : QOpenGLWidget(parent)
    , context_(context)
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    setFormat(format);

    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

Context3DGlWidget::~Context3DGlWidget()
{
    if (context_ != nullptr)
        context_->glReady_ = false;

    if (context() != nullptr && context()->isValid()) {
        makeCurrent();
        program_.reset();
        if (buffer_) {
            buffer_->destroy();
            buffer_.reset();
        }
        if (vertexArray_) {
            vertexArray_->destroy();
            vertexArray_.reset();
        }
        doneCurrent();
    }
}

void Context3DGlWidget::initializeGL()
{
    if (context_ == nullptr || context() == nullptr)
        return;

    try {
        QOpenGLFunctions* gl = context()->functions();
        if (gl == nullptr)
            return;

        program_ = std::make_unique<QOpenGLShaderProgram>();
        if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)
            || !program_->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                                  kFragmentShader)
            || !program_->link()) {
            qWarning("md::Context3D: OpenGL shader setup failed: %s",
                     qUtf8Printable(program_->log()));
            program_.reset();
            return;
        }

        vertexArray_ = std::make_unique<QOpenGLVertexArrayObject>();
        if (!vertexArray_->create()) {
            qWarning("md::Context3D: could not create a vertex array object");
            vertexArray_.reset();
            program_.reset();
            return;
        }

        vertexArray_->bind();

        buffer_ = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
        if (!buffer_->create()) {
            qWarning("md::Context3D: could not create a vertex buffer");
            vertexArray_->release();
            vertexArray_.reset();
            buffer_.reset();
            program_.reset();
            return;
        }

        buffer_->setUsagePattern(QOpenGLBuffer::DynamicDraw);
        buffer_->bind();
        buffer_->allocate(4096);

        const GLsizei stride = static_cast<GLsizei>(kFloatsPerVertex * sizeof(float));
        gl->glEnableVertexAttribArray(0);
        gl->glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<const void*>(static_cast<quintptr>(0)));
        gl->glEnableVertexAttribArray(1);
        gl->glVertexAttribPointer(
            1, 4, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<const void*>(static_cast<quintptr>(3 * sizeof(float))));

        buffer_->release();
        vertexArray_->release();

        context_->glReady_ = true;
    } catch (const std::exception& error) {
        qWarning("md::Context3D: OpenGL initialization failed: %s", error.what());
        context_->glReady_ = false;
    } catch (...) {
        qWarning("md::Context3D: OpenGL initialization failed: unknown exception");
        context_->glReady_ = false;
    }
}

void Context3DGlWidget::resizeGL(int width, int height)
{
    if (context() == nullptr)
        return;

    QOpenGLFunctions* gl = context()->functions();
    if (gl == nullptr)
        return;

    gl->glViewport(0, 0, width, height);
}

void Context3DGlWidget::paintGL()
{
    if (context_ == nullptr || context() == nullptr)
        return;

    QOpenGLFunctions* gl = context()->functions();
    if (gl == nullptr)
        return;

    try {
        const qreal ratio = devicePixelRatioF();
        const int pixelWidth = std::max(1, static_cast<int>(width() * ratio));
        const int pixelHeight = std::max(1, static_cast<int>(height() * ratio));
        gl->glViewport(0, 0, pixelWidth, pixelHeight);

        const QColor clear = context_->background_.isValid()
            ? context_->background_
            : QColor(Qt::transparent);
        gl->glClearColor(static_cast<GLfloat>(clear.redF()),
                         static_cast<GLfloat>(clear.greenF()),
                         static_cast<GLfloat>(clear.blueF()),
                         static_cast<GLfloat>(clear.alphaF()));
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (!program_ || !program_->isLinked() || !buffer_ || !vertexArray_)
            return;

        const qreal aspect = height() > 0
            ? static_cast<qreal>(width()) / static_cast<qreal>(height())
            : 1.0;

        QMatrix4x4 projection;
        if (context_->projection_ == Context3D::Projection::Perspective) {
            projection.perspective(
                static_cast<float>(context_->fovYDegrees_),
                static_cast<float>(aspect),
                static_cast<float>(context_->nearPlane_),
                static_cast<float>(context_->farPlane_));
        } else if (context_->orthoSymmetric_) {
            const float halfHeight =
                static_cast<float>(context_->orthoVerticalSpan_ * 0.5);
            const float halfWidth = halfHeight * static_cast<float>(aspect);
            projection.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight,
                             static_cast<float>(context_->nearPlane_),
                             static_cast<float>(context_->farPlane_));
        } else {
            projection.ortho(static_cast<float>(context_->orthoLeft_),
                             static_cast<float>(context_->orthoRight_),
                             static_cast<float>(context_->orthoBottom_),
                             static_cast<float>(context_->orthoTop_),
                             static_cast<float>(context_->nearPlane_),
                             static_cast<float>(context_->farPlane_));
        }

        const QMatrix4x4 view = context_->viewMatrix();
        const QMatrix4x4 mvp = projection * view * context_->modelMatrix_;

        std::vector<GLfloat> vertices;
        std::vector<Batch> batches;

        const auto appendVertex = [&vertices](const QVector3D& point,
                                               const QColor& colour) {
            vertices.push_back(static_cast<GLfloat>(point.x()));
            vertices.push_back(static_cast<GLfloat>(point.y()));
            vertices.push_back(static_cast<GLfloat>(point.z()));
            vertices.push_back(static_cast<GLfloat>(colour.redF()));
            vertices.push_back(static_cast<GLfloat>(colour.greenF()));
            vertices.push_back(static_cast<GLfloat>(colour.blueF()));
            vertices.push_back(static_cast<GLfloat>(colour.alphaF()));
        };

        for (const Context3D::Primitive& primitive : context_->primitives_) {
            const GLint first = static_cast<GLint>(
                vertices.size() / static_cast<std::size_t>(kFloatsPerVertex));
            Batch batch;
            batch.first = first;

            switch (primitive.kind) {
            case Context3D::Primitive::Kind::Line:
                if (primitive.vertices.size() < 2)
                    continue;
                appendVertex(primitive.vertices.at(0), primitive.colour);
                appendVertex(primitive.vertices.at(1), primitive.colour);
                batch.mode = GL_LINES;
                batch.count = 2;
                batch.line = true;
                break;
            case Context3D::Primitive::Kind::Triangle:
                if (primitive.vertices.size() < 3)
                    continue;
                for (int index = 0; index < 3; ++index)
                    appendVertex(primitive.vertices.at(index), primitive.colour);
                batch.mode = GL_TRIANGLES;
                batch.count = 3;
                break;
            case Context3D::Primitive::Kind::Quad:
                if (primitive.vertices.size() < 4)
                    continue;
                appendVertex(primitive.vertices.at(0), primitive.colour);
                appendVertex(primitive.vertices.at(1), primitive.colour);
                appendVertex(primitive.vertices.at(2), primitive.colour);
                appendVertex(primitive.vertices.at(0), primitive.colour);
                appendVertex(primitive.vertices.at(2), primitive.colour);
                appendVertex(primitive.vertices.at(3), primitive.colour);
                batch.mode = GL_TRIANGLES;
                batch.count = 6;
                break;
            case Context3D::Primitive::Kind::Points:
                for (const QVector3D& point : primitive.vertices)
                    appendVertex(point, primitive.colour);
                if (primitive.vertices.isEmpty()) {
                    vertices.resize(static_cast<std::size_t>(first)
                                    * static_cast<std::size_t>(kFloatsPerVertex));
                    continue;
                }
                batch.mode = GL_POINTS;
                batch.count = static_cast<GLsizei>(primitive.vertices.size());
                break;
            }

            batches.push_back(batch);
        }

        gl->glEnable(GL_DEPTH_TEST);
        gl->glDepthFunc(GL_LEQUAL);
        gl->glEnable(GL_BLEND);
        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // GL_PROGRAM_POINT_SIZE: the vertex shader supplies gl_PointSize. The
        // constant is not exposed by the GLES2-focused QOpenGLFunctions header.
        constexpr GLenum programPointSize = 0x8642;
        gl->glEnable(programPointSize);
        if (context_->backFaceCulling_) {
            gl->glEnable(GL_CULL_FACE);
            gl->glCullFace(GL_BACK);
            gl->glFrontFace(GL_CCW);
        } else {
            gl->glDisable(GL_CULL_FACE);
        }

        if (vertices.empty()) {
            return;
        }

        program_->bind();
        program_->setUniformValue("uMvp", mvp);
        program_->setUniformValue(
            "uPointSize",
            static_cast<GLfloat>(
                std::max<qreal>(1.0, context_->pointRadiusDp_ * 2.0 * ratio)));

        vertexArray_->bind();
        buffer_->bind();
        buffer_->allocate(vertices.data(),
                          static_cast<int>(vertices.size() * sizeof(GLfloat)));

        for (const Batch& batch : batches) {
            if (batch.line) {
                gl->glLineWidth(static_cast<GLfloat>(
                    std::max<qreal>(1.0, context_->lineWidthDp_ * ratio)));
            }
            gl->glDrawArrays(batch.mode, batch.first, batch.count);
        }

        buffer_->release();
        vertexArray_->release();
        program_->release();
    } catch (const std::exception& error) {
        qWarning("md::Context3D: OpenGL paint failed: %s", error.what());
    } catch (...) {
        qWarning("md::Context3D: OpenGL paint failed: unknown exception");
    }
}

#endif // PERIODICITY_HAS_OPENGL

void Context3D::ensureOpenGLWidget()
{
#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
    if (glWidget_ || !hostModeActive_ || !host_ || !host_->isVisible())
        return;

    // The offscreen QPA plugin used by the test suite has no GL surface. Do not
    // create a widget that can never obtain a context; the software path is the
    // documented headless behaviour.
    if (QGuiApplication::platformName().compare(QStringLiteral("offscreen"),
                                                Qt::CaseInsensitive) == 0) {
        return;
    }

    auto* widget = new Context3DGlWidget(this, host_.data());
    glWidget_ = widget;
    widget->setGeometry(viewport_.toAlignedRect().intersected(host_->rect()));
    widget->show();
#endif
}

} // namespace md
