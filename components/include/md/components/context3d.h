// Periodicity — the software/OpenGL 3D context used by Canvas.
//
// Context3D keeps a small retained scene of coloured primitives (lines,
// triangles, quads and point sets) and a camera. The default backend projects
// the scene into the Canvas content rectangle with QPainter: vertices are
// transformed by model → view → projection, depth-sorted with the painter's
// algorithm, optionally back-face culled, and then drawn. This path is always
// compiled and is what the headless tests exercise.
//
// When the components target was built with OpenGLWidgets available
// (PERIODICITY_HAS_OPENGL == 1) and a Canvas is visible, setBackend(OpenGL)
// embeds a QOpenGLWidget child. It draws the same primitive list through a
// minimal position+colour shader, with depth testing handling occlusion. If no
// GL context is available at paint time the software path is used instead and
// the fallback is logged once.
//
// Coordinate and projection convention (software path):
//   * right-handed world/view space; the camera looks down view-space -Z;
//   * `project()` returns widget coordinates, with the y axis pointing down;
//   * perspective uses a vertical field of view in degrees and a positive
//     near/far slab; orthographic uses either a symmetric vertical span (x
//     follows the viewport aspect) or explicit left/right/bottom/top bounds;
//   * a point at or behind the near plane, beyond the far plane, or with a
//     non-finite coordinate produces std::nullopt.
// Both the software and GL paths use the same model/view/projection matrices,
// so the rendered image matches (modulo rasterisation).

#pragma once

#include <QColor>
#include <QMatrix4x4>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QVector>
#include <QVector3D>
#include <QWidget>
#include <QtGlobal>

#include <optional>

class QPainter;

namespace md {

class Canvas;
class Context3DGlWidget;

class Context3D
{
public:
    enum class Backend {
        Software,
        OpenGL,
    };

    enum class Projection {
        Perspective,
        Orthographic,
    };

    explicit Context3D(QWidget* host = nullptr);
    ~Context3D();

    Context3D(const Context3D&) = delete;
    Context3D& operator=(const Context3D&) = delete;

    // Camera --------------------------------------------------------------
    // A degenerate camera (position == target, a zero up vector, or up parallel
    // to the view direction) is rejected with a warning and the previous camera
    // is kept.
    void setCamera(const QVector3D& position, const QVector3D& target,
                   const QVector3D& up = QVector3D(0.0f, 1.0f, 0.0f));
    void setCameraPosition(const QVector3D& position);
    void setCameraTarget(const QVector3D& target);
    void setCameraUp(const QVector3D& up);

    [[nodiscard]] QVector3D cameraPosition() const noexcept;
    [[nodiscard]] QVector3D cameraTarget() const noexcept;
    [[nodiscard]] QVector3D cameraUp() const noexcept;

    // Projection. `perspective()` takes a vertical field of view in degrees.
    // The three-argument orthographic form takes the world-space vertical span
    // at the view plane and derives the horizontal span from the viewport
    // aspect; the six-argument form takes explicit view-space bounds.
    void perspective(qreal fovYDegrees, qreal nearPlane, qreal farPlane);
    void orthographic(qreal verticalSpan, qreal nearPlane, qreal farPlane);
    void orthographic(qreal left, qreal right, qreal bottom, qreal top,
                      qreal nearPlane, qreal farPlane);

    void setPerspective(qreal fovYDegrees, qreal nearPlane, qreal farPlane)
    {
        perspective(fovYDegrees, nearPlane, farPlane);
    }
    void setOrthographic(qreal verticalSpan, qreal nearPlane, qreal farPlane)
    {
        orthographic(verticalSpan, nearPlane, farPlane);
    }
    void setOrthographic(qreal left, qreal right, qreal bottom, qreal top,
                         qreal nearPlane, qreal farPlane)
    {
        orthographic(left, right, bottom, top, nearPlane, farPlane);
    }

    [[nodiscard]] Projection projectionMode() const noexcept;
    [[nodiscard]] qreal fieldOfView() const noexcept;
    [[nodiscard]] qreal nearPlane() const noexcept;
    [[nodiscard]] qreal farPlane() const noexcept;

    // The widget-space rectangle the scene is projected into. Canvas updates
    // this from its content rectangle before painting. A null/empty viewport
    // makes project() return nullopt and paint() a no-op.
    void setViewport(const QRectF& viewport);
    [[nodiscard]] QRectF viewport() const noexcept;

    // Model transform, applied before the camera. Non-finite matrices are
    // rejected with a warning.
    void setModelMatrix(const QMatrix4x4& matrix);
    void resetModelMatrix();
    [[nodiscard]] QMatrix4x4 modelMatrix() const;

    // Projects a world-space point to widget coordinates. Returns nullopt for
    // points behind the near plane, beyond the far plane, or non-finite.
    [[nodiscard]] std::optional<QPointF> project(const QVector3D& point) const;

    // Scene ---------------------------------------------------------------
    // Primitive colours default to the theme's primary role. The quad vertices
    // are expected in winding order; back-face culling (when enabled) treats
    // counter-clockwise, camera-facing triangles as front faces.
    void line3D(const QVector3D& from, const QVector3D& to,
                const QColor& colour = QColor());
    void triangle3D(const QVector3D& a, const QVector3D& b, const QVector3D& c,
                    const QColor& colour = QColor());
    void quad3D(const QVector3D& a, const QVector3D& b, const QVector3D& c,
                const QVector3D& d, const QColor& colour = QColor());
    void points3D(const QVector<QVector3D>& points,
                  const QColor& colour = QColor());

    void clearPrimitives();
    [[nodiscard]] int primitiveCount() const noexcept;

    void setBackFaceCulling(bool enabled);
    [[nodiscard]] bool backFaceCulling() const noexcept;

    // Screen-space stroke width and point radius, in dp.
    void setLineWidth(qreal widthDp);
    [[nodiscard]] qreal lineWidth() const noexcept;
    void setPointRadius(qreal radiusDp);
    [[nodiscard]] qreal pointRadius() const noexcept;

    // Backend -------------------------------------------------------------
    // `backend()` is the requested backend; `effectiveBackend()` is what the
    // next paint will actually use. The two differ while a GL context is not
    // available, in which case painting falls back to Software and logs once.
    void setBackend(Backend backend);
    [[nodiscard]] Backend backend() const noexcept;
    [[nodiscard]] Backend effectiveBackend() const noexcept;

    // Used by the embedded GL widget to clear its framebuffer. An invalid
    // colour restores the theme surface colour.
    void setBackground(const QColor& colour);
    [[nodiscard]] QColor background() const;

    // Software painting. Canvas calls this from paintContent(); a requested
    // OpenGL backend with no ready context falls back here.
    void paint(QPainter& painter, const QRectF& content);

    // Schedules a repaint of the host Canvas and, when it exists, the embedded
    // GL widget.
    void requestDraw();

private:
    friend class Canvas;
    friend class Context3DGlWidget;

    struct Primitive
    {
        enum class Kind {
            Line,
            Triangle,
            Quad,
            Points,
        };

        Kind kind = Kind::Line;
        QVector<QVector3D> vertices;
        QColor colour;
    };

    [[nodiscard]] QMatrix4x4 viewMatrix() const;
    [[nodiscard]] QVector3D viewPoint(const QVector3D& point) const;
    [[nodiscard]] std::optional<QPointF> projectView(const QVector3D& viewPoint,
                                                     qreal aspect) const;
    [[nodiscard]] bool isFrontFacing(const Primitive& primitive) const;

    void renderSoftware(QPainter& painter, const QRectF& content);
    void warnFallbackOnce();
    void requestOpenGLUpdate();

    // Canvas plumbing. These are private because the QOpenGLWidget lifetime is
    // an implementation detail of the Canvas/Context3D pair.
    void setHostModeActive(bool active);
    void ensureOpenGLWidget();
    void updateOpenGLGeometry();

    QPointer<QWidget> host_;
    QRectF viewport_{0.0, 0.0, 640.0, 480.0};

    QVector3D cameraPosition_{0.0f, 0.0f, 5.0f};
    QVector3D cameraTarget_{0.0f, 0.0f, 0.0f};
    QVector3D cameraUp_{0.0f, 1.0f, 0.0f};

    Projection projection_ = Projection::Perspective;
    qreal fovYDegrees_ = 60.0;
    qreal nearPlane_ = 0.1;
    qreal farPlane_ = 1000.0;
    qreal orthoVerticalSpan_ = 4.0;
    qreal orthoLeft_ = -2.0;
    qreal orthoRight_ = 2.0;
    qreal orthoBottom_ = -2.0;
    qreal orthoTop_ = 2.0;
    bool orthoSymmetric_ = true;

    QMatrix4x4 modelMatrix_;
    QVector<Primitive> primitives_;
    bool backFaceCulling_ = false;
    qreal lineWidthDp_ = 1.5;
    qreal pointRadiusDp_ = 3.0;

    Backend requestedBackend_ = Backend::Software;
    QColor background_;
    QPointer<QWidget> glWidget_;
    bool glReady_ = false;
    bool fallbackWarned_ = false;
    bool hostModeActive_ = false;
};

} // namespace md
