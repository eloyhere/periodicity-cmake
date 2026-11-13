// Periodicity — implementation of the optional OpenGL device.
//
// Everything that needs a GL context is compiled only when the render target was
// built with PERIODICITY_HAS_OPENGL. The stub at the bottom of this file provides
// the same interface (unavailable, with a precise reason) for builds without
// Qt6::OpenGL, so callers never need a preprocessor branch.
//
// The device deliberately owns an offscreen context rather than a widget: the
// command buffer is already in target pixels, so there is no window to size and
// no native child surface to stack. Coordinate conventions are chosen so the
// GL result matches the software rasterizer after the read-back flip:
//
//   * 2D vertices are target pixels; the vertex shader maps them to NDC with a
//     Y flip that matches QImage's top-left origin.
//   * 3D vertices go through the pipeline's mvp and the command viewport is
//     applied with glViewport (bottom-left origin); the read-back flip turns
//     that back into the software rasterizer's top-left screen convention.
//   * depth is the same [0, 1] range, nearer is smaller, and the test is LESS.

#include <md/render/gl_device.h>

#include <md/render/software_device.h>

#include <md/gfx/shader.h>

#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QSurfaceFormat>
#include <QVector2D>
#include <QVector4D>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL

#include <md/render/text_painter.h>

#include <QFontMetricsF>
#include <QMatrix4x4>
#include <QOffscreenSurface>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPainter>
#include <QScopeGuard>
#include <QtMath>

namespace md::render {

namespace {

// ---------------------------------------------------------------------------
// Command-buffer scaling (the same contract SoftwareDevice applies)
// ---------------------------------------------------------------------------

QRect scaledRect(const QRect& rectangle, qreal ratio)
{
    if (rectangle.isEmpty())
        return QRect();

    return QRect(qRound(rectangle.x() * ratio), qRound(rectangle.y() * ratio),
                 qRound(rectangle.width() * ratio),
                 qRound(rectangle.height() * ratio));
}

QFont scaledFont(const QFont& font, qreal ratio)
{
    QFont scaled = font;
    if (font.pixelSize() > 0) {
        scaled.setPixelSize(qMax(1, qRound(font.pixelSize() * ratio)));
    } else if (font.pointSizeF() > 0.0) {
        scaled.setPointSizeF(font.pointSizeF() * ratio);
    }
    return scaled;
}

gfx::CommandBuffer scaledCommands(const gfx::CommandBuffer& commands, qreal ratio)
{
    if (qFuzzyCompare(ratio, 1.0))
        return commands;

    const QTransform transform = QTransform::fromScale(ratio, ratio);

    gfx::CommandBuffer scaled;
    for (const gfx::DrawCommand& original : commands.commands()) {
        gfx::DrawCommand command = original;
        command.viewport = scaledRect(original.viewport, ratio);
        command.scissor = scaledRect(original.scissor, ratio);

        switch (original.kind) {
        case gfx::CommandKind::DrawMesh2D:
            command.mesh2d = original.mesh2d.transformed(transform);
            break;
        case gfx::CommandKind::DrawMesh3D:
            break;
        case gfx::CommandKind::DrawText:
            command.textPosition = original.textPosition * ratio;
            command.font = scaledFont(original.font, ratio);
            break;
        case gfx::CommandKind::DrawPath: {
            command.path = transform.map(original.path);
            QPen pen = original.pen;
            if (pen.widthF() > 0.0)
                pen.setWidthF(pen.widthF() * ratio);
            command.pen = pen;
            break;
        }
        case gfx::CommandKind::DrawImage:
        case gfx::CommandKind::SetScissor:
        case gfx::CommandKind::Clear:
            break;
        }
        scaled.add(std::move(command));
    }
    return scaled;
}

qint64 primitiveCount(const gfx::Mesh2D& mesh)
{
    if (!mesh.isValid())
        return 0;

    const qsizetype indices = mesh.indices().size();
    switch (mesh.topology()) {
    case gfx::PrimitiveTopology::Triangles:
        return indices / 3;
    case gfx::PrimitiveTopology::Lines:
        return indices / 2;
    case gfx::PrimitiveTopology::Points:
        return indices;
    }
    return 0;
}

qint64 primitiveCount(const gfx::Mesh3D& mesh)
{
    if (!mesh.isValid())
        return 0;

    const qsizetype indices = mesh.indices().size();
    switch (mesh.topology()) {
    case gfx::PrimitiveTopology::Triangles:
        return indices / 3;
    case gfx::PrimitiveTopology::Lines:
        return indices / 2;
    case gfx::PrimitiveTopology::Points:
        return indices;
    }
    return 0;
}

qint64 primitiveCount(const gfx::CommandBuffer& commands)
{
    qint64 primitives = 0;
    for (const gfx::DrawCommand& command : commands.commands()) {
        switch (command.kind) {
        case gfx::CommandKind::DrawMesh2D:
            primitives += primitiveCount(command.mesh2d);
            break;
        case gfx::CommandKind::DrawMesh3D:
            primitives += primitiveCount(command.mesh3d);
            break;
        case gfx::CommandKind::DrawImage:
            primitives += 2;
            break;
        case gfx::CommandKind::DrawText:
        case gfx::CommandKind::DrawPath:
        case gfx::CommandKind::SetScissor:
        case gfx::CommandKind::Clear:
            break;
        }
    }
    return primitives;
}

QColor validColour(const QColor& colour)
{
    return colour.isValid() ? colour : QColor(Qt::white);
}

// ---------------------------------------------------------------------------
// GLSL sources
// ---------------------------------------------------------------------------

constexpr const char* kVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aUv;

uniform mat4 uMvp;
uniform bool uIs3D;
uniform vec2 uTargetSize;

out vec3 vNormal;
out vec4 vColor;
out vec2 vUv;
out vec3 vWorld;

void main()
{
    vNormal = aNormal;
    vColor = aColor;
    vUv = aUv;

    if (uIs3D) {
        vWorld = aPosition;
        gl_Position = uMvp * vec4(aPosition, 1.0);
    } else {
        vec2 screen = aPosition.xy;
        vec2 ndc = vec2(2.0 * screen.x / uTargetSize.x - 1.0,
                        1.0 - 2.0 * screen.y / uTargetSize.y);
        gl_Position = vec4(ndc, 0.0, 1.0);
        vWorld = vec3(aPosition.xy, 0.0);
    }
}
)";

constexpr const char* kCopyFragment = R"(
#version 330 core
uniform sampler2D uTexture;
in vec2 vUv;
out vec4 fragColor;
void main()
{
    fragColor = texture(uTexture, vUv);
}
)";

constexpr const char* kSolidFragment = R"(
#version 330 core
uniform vec4 uColour;
out vec4 fragColor;
void main()
{
    fragColor = vec4(uColour.rgb * uColour.a, uColour.a);
}
)";

constexpr const char* kVertexColourFragment = R"(
#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main()
{
    fragColor = vec4(vColor.rgb * vColor.a, vColor.a);
}
)";

constexpr const char* kLambertFragment = R"(
#version 330 core
in vec3 vNormal;
in vec4 vColor;
uniform vec4 uBaseColour;
uniform vec3 uLightDirection;
uniform float uAmbient;
out vec4 fragColor;
void main()
{
    vec3 light = uLightDirection;
    if (dot(light, light) <= 0.0)
        light = vec3(0.0, 0.0, 1.0);
    else
        light = normalize(light);

    vec3 normal = vNormal;
    if (dot(normal, normal) > 0.0)
        normal = normalize(normal);

    float intensity = clamp(dot(normal, light), 0.0, 1.0);
    float ambient = clamp(uAmbient, 0.0, 1.0);
    float factor = ambient + (1.0 - ambient) * intensity;
    vec3 base = uBaseColour.rgb * vColor.rgb;
    float alpha = uBaseColour.a * vColor.a;
    vec3 colour = base * factor;
    fragColor = vec4(colour * alpha, alpha);
}
)";

constexpr const char* kUnlitFragment = R"(
#version 330 core
in vec4 vColor;
in vec2 vUv;
uniform sampler2D uTexture;
uniform bool uHasTexture;
out vec4 fragColor;
void main()
{
    vec4 base = vColor;
    vec4 texel = uHasTexture ? texture(uTexture, vUv) : vec4(1.0);
    vec4 colour = vec4(base.rgb * texel.rgb, base.a * texel.a);
    fragColor = vec4(colour.rgb * colour.a, colour.a);
}
)";

// ---------------------------------------------------------------------------
// Small GL helpers
// ---------------------------------------------------------------------------

struct Vertex
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float nx = 0.0F;
    float ny = 0.0F;
    float nz = 1.0F;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
    float u = 0.0F;
    float v = 0.0F;
};

constexpr int kFloatsPerVertex = 12;
constexpr int kStride = kFloatsPerVertex * static_cast<int>(sizeof(float));

Vertex makeVertex(const QPointF& position, const QColor& colour, const QPointF& uv)
{
    const QColor c = validColour(colour);
    Vertex vertex;
    vertex.x = static_cast<float>(position.x());
    vertex.y = static_cast<float>(position.y());
    vertex.r = static_cast<float>(c.redF());
    vertex.g = static_cast<float>(c.greenF());
    vertex.b = static_cast<float>(c.blueF());
    vertex.a = static_cast<float>(c.alphaF());
    vertex.u = static_cast<float>(uv.x());
    vertex.v = static_cast<float>(uv.y());
    return vertex;
}

Vertex makeVertex3D(const gfx::Vertex3D& source)
{
    const QColor c = validColour(source.colour);
    Vertex vertex;
    vertex.x = source.position.x();
    vertex.y = source.position.y();
    vertex.z = source.position.z();
    vertex.nx = source.normal.x();
    vertex.ny = source.normal.y();
    vertex.nz = source.normal.z();
    vertex.r = static_cast<float>(c.redF());
    vertex.g = static_cast<float>(c.greenF());
    vertex.b = static_cast<float>(c.blueF());
    vertex.a = static_cast<float>(c.alphaF());
    vertex.u = static_cast<float>(source.uv.x());
    vertex.v = static_cast<float>(source.uv.y());
    return vertex;
}

QVector4D premultiplied(const QColor& colour)
{
    const QColor c = validColour(colour);
    const float alpha = static_cast<float>(c.alphaF());
    return QVector4D(static_cast<float>(c.redF()) * alpha,
                     static_cast<float>(c.greenF()) * alpha,
                     static_cast<float>(c.blueF()) * alpha, alpha);
}

GLint wrapMode(gfx::Wrap wrap)
{
    switch (wrap) {
    case gfx::Wrap::Clamp:
        return GL_CLAMP_TO_EDGE;
    case gfx::Wrap::Repeat:
        return GL_REPEAT;
    case gfx::Wrap::Mirror:
        return GL_MIRRORED_REPEAT;
    }
    return GL_CLAMP_TO_EDGE;
}

GLint filterMode(gfx::Filter filter)
{
    switch (filter) {
    case gfx::Filter::Nearest:
        return GL_NEAREST;
    case gfx::Filter::Bilinear:
    case gfx::Filter::MipmapBilinear:
        return GL_LINEAR;
    }
    return GL_LINEAR;
}

QRect viewportFor(const gfx::DrawCommand& command, const QRect& target)
{
    if (command.viewport.isEmpty())
        return target;
    return command.viewport.intersected(target);
}

QRect clipFor(const gfx::DrawCommand& command, const QRect& target, const QRect& scissor)
{
    QRect clip = viewportFor(command, target).intersected(scissor);
    if (!command.scissor.isEmpty())
        clip = clip.intersected(command.scissor);
    return clip.intersected(target);
}

bool isFinitePoint(const QPointF& point)
{
    return std::isfinite(static_cast<double>(point.x()))
        && std::isfinite(static_cast<double>(point.y()));
}

} // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct GlDevice::Impl
{
    std::unique_ptr<SoftwareDevice> software = std::make_unique<SoftwareDevice>();

    bool initialized = false;
    bool available = false;
    bool degraded = false;
    bool recoveryUsed = false;
    bool contextLost = false;
    bool imageDirty = false;
    int framePixelsWritten = 0;

    bool customSolid = false;
    QString customVertex;
    QString customFragment;

    QString lastError;
    QString shaderLog;

    std::unique_ptr<QOpenGLContext> context;
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QOpenGLFramebufferObject> fbo;
    std::unique_ptr<QOpenGLBuffer> vbo;
    std::unique_ptr<QOpenGLVertexArrayObject> vao;
    std::unique_ptr<QOpenGLShaderProgram> copyProgram;
    std::unique_ptr<QOpenGLShaderProgram> solidProgram;
    std::unique_ptr<QOpenGLShaderProgram> vertexColourProgram;
    std::unique_ptr<QOpenGLShaderProgram> lambertProgram;
    std::unique_ptr<QOpenGLShaderProgram> unlitProgram;
    QOpenGLFunctions* gl = nullptr;
    QOpenGLExtraFunctions* extra = nullptr;
    QSize fboSize;

    void appendFailure(const QString& reason);
    bool makeCurrent();
    bool createContext();
    bool buildPrograms();
    bool createBuffer();
    bool ensureFramebuffer(const QSize& size);
    bool uploadImage(const QImage& image);
    bool readFramebuffer(QImage& image);
    void releaseGl();
    bool buildContextAndResources();
    bool recover();
    void degrade(const QString& reason);
    void ensureNoBinding();

    QImage toPremultiplied(const QImage& image) const;
    QImage fromPremultiplied(const QImage& image, QImage::Format format) const;

    QOpenGLShaderProgram* programFor(const QString& shaderName) const;
    bool bindProgramPipeline(QOpenGLShaderProgram* program,
                             const gfx::Pipeline& pipeline, bool is3D,
                             const QSize& size, const QRect& glViewport,
                             bool textureBound);
    void drawVertices(const std::vector<Vertex>& vertices, GLenum mode);
    bool beginQuery();
    int endQuery();

    void setScissor(const QRect& clip, const QSize& size);
    void clear(const gfx::DrawCommand& command, const QRect& clip,
               const QSize& size);
    bool drawMesh2D(const gfx::DrawCommand& command, const QRect& viewport);
    bool drawMesh3D(const gfx::DrawCommand& command, const QRect& viewport,
                    const QSize& size);
    bool drawImage(const gfx::DrawCommand& command, const QRect& target,
                   const QRect& scissor);
    void drawText(const gfx::DrawCommand& command, const QRect& viewport,
                  const QRect& clip, QImage& image);
    void drawPath(const gfx::DrawCommand& command, const QRect& viewport,
                  const QRect& clip, QImage& image);
    void countTriangles(const gfx::DrawCommand& command, const QRect& viewport,
                        gfx::RasterStats& stats) const;

    GLuint uploadTexture(const QImage& image, gfx::Wrap wrap, gfx::Filter filter,
                         bool premultipliedSource);
    void deleteTexture(GLuint texture);

    QSize targetSize;
    GLuint query = 0;
    bool queryActive = false;
};

void GlDevice::Impl::appendFailure(const QString& reason)
{
    if (reason.isEmpty())
        return;
    if (lastError.isEmpty())
        lastError = reason;
    else
        lastError += QStringLiteral("; ") + reason;
}

bool GlDevice::Impl::makeCurrent()
{
    if (!context || !surface || !context->isValid() || !surface->isValid())
        return false;
    if (QOpenGLContext::currentContext() == context.get())
        return true;
    return context->makeCurrent(surface.get());
}

void GlDevice::Impl::releaseGl()
{
    if (context && context->isValid()) {
        bool current = QOpenGLContext::currentContext() == context.get();
        if (!current && surface && surface->isValid())
            current = context->makeCurrent(surface.get());

        fbo.reset();
        copyProgram.reset();
        solidProgram.reset();
        vertexColourProgram.reset();
        lambertProgram.reset();
        unlitProgram.reset();
        vao.reset();
        vbo.reset();
        fboSize = QSize();
        if (current)
            context->doneCurrent();
    } else {
        fbo.reset();
        copyProgram.reset();
        solidProgram.reset();
        vertexColourProgram.reset();
        lambertProgram.reset();
        unlitProgram.reset();
        vao.reset();
        vbo.reset();
        fboSize = QSize();
    }

    gl = nullptr;
    extra = nullptr;
    context.reset();
    surface.reset();
    initialized = false;
    available = false;
}

bool GlDevice::Impl::createContext()
{
    if (QGuiApplication::instance() == nullptr) {
        appendFailure(QStringLiteral(
            "no QGuiApplication instance is alive, so no GL context can be created"));
        return false;
    }

    QSurfaceFormat requested;
    requested.setRenderableType(QSurfaceFormat::OpenGL);
    requested.setVersion(3, 3);
    requested.setProfile(QSurfaceFormat::CoreProfile);
    requested.setDepthBufferSize(24);
    requested.setStencilBufferSize(8);

    auto newContext = std::make_unique<QOpenGLContext>();
    newContext->setFormat(requested);
    if (!newContext->create()) {
        appendFailure(QStringLiteral("QOpenGLContext::create() failed"));
        return false;
    }

    const QSurfaceFormat actual = newContext->format();
    if (actual.majorVersion() < 3
        || (actual.majorVersion() == 3 && actual.minorVersion() < 3)) {
        appendFailure(QStringLiteral("the GL context is version %1.%2; version 3.3 is required")
                          .arg(actual.majorVersion())
                          .arg(actual.minorVersion()));
        return false;
    }

    auto newSurface = std::make_unique<QOffscreenSurface>();
    newSurface->setFormat(actual);
    newSurface->create();
    if (!newSurface->isValid()) {
        appendFailure(QStringLiteral("QOffscreenSurface::create() failed"));
        return false;
    }

    if (!newContext->makeCurrent(newSurface.get())) {
        appendFailure(QStringLiteral("QOpenGLContext::makeCurrent() failed for the offscreen surface"));
        return false;
    }

    // 3.3 core includes FBOs; the extension tests keep the diagnostic precise
    // on drivers that expose the symbols through compatibility contexts too.
    const bool fboSupported =
        actual.majorVersion() > 3
        || (actual.majorVersion() == 3 && actual.minorVersion() >= 0)
        || newContext->hasExtension(QByteArrayLiteral("GL_ARB_framebuffer_object"))
        || newContext->hasExtension(QByteArrayLiteral("GL_EXT_framebuffer_object"));
    if (!fboSupported) {
        newContext->doneCurrent();
        appendFailure(QStringLiteral(
            "the GL context reports neither framebuffer objects nor an FBO extension"));
        return false;
    }

    gl = newContext->functions();
    extra = newContext->extraFunctions();
    if (gl == nullptr || extra == nullptr) {
        newContext->doneCurrent();
        appendFailure(QStringLiteral("the GL context exposes no function table"));
        return false;
    }

    context = std::move(newContext);
    surface = std::move(newSurface);
    return true;
}

bool GlDevice::Impl::buildPrograms()
{
    auto build = [this](const QString& label, const char* vertexSource,
                        const char* fragmentSource) -> std::unique_ptr<QOpenGLShaderProgram> {
        auto program = std::make_unique<QOpenGLShaderProgram>();
        QString programLog;
        const bool vertexOk = program->addShaderFromSourceCode(
            QOpenGLShader::Vertex, QString::fromUtf8(vertexSource));
        const bool fragmentOk = program->addShaderFromSourceCode(
            QOpenGLShader::Fragment, QString::fromUtf8(fragmentSource));
        const bool linked = vertexOk && fragmentOk && program->link();

        if (!linked) {
            programLog = program->log().trimmed();
            if (programLog.isEmpty())
                programLog = QStringLiteral("compilation or linking failed without a driver log");
            shaderLog = QStringLiteral("%1: %2").arg(label, programLog);
            appendFailure(QStringLiteral("GLSL program '%1' failed: %2")
                              .arg(label, programLog));
        }
        return linked ? std::move(program) : nullptr;
    };

    copyProgram = build(QStringLiteral("copy"), kVertexShader, kCopyFragment);
    if (!copyProgram)
        return false;

    const QByteArray customVertexBytes = customSolid
        ? customVertex.toUtf8() : QByteArray(kVertexShader);
    const QByteArray customFragmentBytes = customSolid
        ? customFragment.toUtf8() : QByteArray(kSolidFragment);
    solidProgram = build(QStringLiteral("solid"), customVertexBytes.constData(),
                         customFragmentBytes.constData());
    if (!solidProgram)
        return false;

    vertexColourProgram = build(QStringLiteral("vertexColour"), kVertexShader,
                                kVertexColourFragment);
    if (!vertexColourProgram)
        return false;

    lambertProgram = build(QStringLiteral("lambert"), kVertexShader, kLambertFragment);
    if (!lambertProgram)
        return false;

    unlitProgram = build(QStringLiteral("unlit"), kVertexShader, kUnlitFragment);
    return unlitProgram != nullptr;
}

bool GlDevice::Impl::createBuffer()
{
    vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    if (!vbo->create()) {
        appendFailure(QStringLiteral("could not create the vertex buffer"));
        return false;
    }
    vbo->setUsagePattern(QOpenGLBuffer::DynamicDraw);

    vao = std::make_unique<QOpenGLVertexArrayObject>();
    if (!vao->create()) {
        appendFailure(QStringLiteral("could not create the vertex array object"));
        return false;
    }

    vao->bind();
    if (!vbo->bind()) {
        vao->release();
        appendFailure(QStringLiteral("could not bind the vertex buffer"));
        return false;
    }

    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(0));
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(3 * sizeof(float)));
    gl->glEnableVertexAttribArray(2);
    gl->glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(6 * sizeof(float)));
    gl->glEnableVertexAttribArray(3);
    gl->glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(10 * sizeof(float)));

    vbo->release();
    vao->release();
    return true;
}

bool GlDevice::Impl::ensureFramebuffer(const QSize& size)
{
    if (fbo && fboSize == size && fbo->isValid())
        return true;

    fbo.reset();
    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    format.setInternalTextureFormat(GL_RGBA8);
    format.setSamples(0);

    fbo = std::make_unique<QOpenGLFramebufferObject>(size, format);
    if (!fbo->isValid()) {
        fbo.reset();
        appendFailure(QStringLiteral("could not create a %1x%2 framebuffer object")
                          .arg(size.width())
                          .arg(size.height()));
        return false;
    }
    fboSize = size;
    return true;
}

bool GlDevice::Impl::buildContextAndResources()
{
    if (!createContext())
        return false;

    const bool built = buildPrograms() && createBuffer()
        && ensureFramebuffer(QSize(1, 1));
    if (!built) {
        if (context && context->isValid())
            context->doneCurrent();
        fbo.reset();
        copyProgram.reset();
        solidProgram.reset();
        vertexColourProgram.reset();
        lambertProgram.reset();
        unlitProgram.reset();
        vbo.reset();
        vao.reset();
        gl = nullptr;
        extra = nullptr;
        context.reset();
        surface.reset();
        return false;
    }

    context->doneCurrent();
    initialized = true;
    available = true;
    return true;
}

bool GlDevice::Impl::recover()
{
    if (recoveryUsed || degraded)
        return false;
    recoveryUsed = true;

    releaseGl();
    if (!buildContextAndResources()) {
        degraded = true;
        available = false;
        initialized = false;
        appendFailure(QStringLiteral(
            "the OpenGL context was lost and could not be re-initialised; "
            "the device is now permanently using the software fallback"));
        return false;
    }

    contextLost = false;
    lastError = QStringLiteral(
        "the OpenGL context was lost; it was re-initialised once for this device");
    return true;
}

void GlDevice::Impl::degrade(const QString& reason)
{
    appendFailure(reason);
    degraded = true;
    available = false;
    initialized = false;
    releaseGl();
}

QImage GlDevice::Impl::toPremultiplied(const QImage& image) const
{
    return image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
}

QImage GlDevice::Impl::fromPremultiplied(const QImage& image,
                                         QImage::Format format) const
{
    if (image.isNull() || format == QImage::Format_Invalid)
        return image;
    return image.convertToFormat(format);
}

QOpenGLShaderProgram* GlDevice::Impl::programFor(const QString& shaderName) const
{
    if (shaderName == QStringLiteral("solid"))
        return solidProgram.get();
    if (shaderName == QStringLiteral("vertexColour"))
        return vertexColourProgram.get();
    if (shaderName == QStringLiteral("lambert"))
        return lambertProgram.get();
    if (shaderName == QStringLiteral("unlit"))
        return unlitProgram.get();
    return nullptr;
}

bool GlDevice::Impl::bindProgramPipeline(QOpenGLShaderProgram* program,
                                         const gfx::Pipeline& pipeline, bool is3D,
                                         const QSize& size, const QRect& glViewport,
                                         bool textureBound)
{
    if (program == nullptr)
        return false;
    if (!program->bind()) {
        appendFailure(QStringLiteral("could not bind a GL shader program"));
        return false;
    }

    program->setUniformValue("uIs3D", is3D ? 1 : 0);
    program->setUniformValue("uTargetSize",
                             QVector2D(static_cast<float>(size.width()),
                                       static_cast<float>(size.height())));

    if (textureBound)
        program->setUniformValue("uTexture", 0);

    gl->glViewport(glViewport.x(), glViewport.y(), glViewport.width(),
                   glViewport.height());

    if (is3D) {
        if (pipeline.depth.test)
            gl->glEnable(GL_DEPTH_TEST);
        else
            gl->glDisable(GL_DEPTH_TEST);
        gl->glDepthMask(pipeline.depth.write ? GL_TRUE : GL_FALSE);
        gl->glDepthFunc(GL_LESS);
    } else {
        gl->glDisable(GL_DEPTH_TEST);
        gl->glDepthMask(GL_FALSE);
    }

    if (is3D && pipeline.raster.cullBack) {
        gl->glEnable(GL_CULL_FACE);
        gl->glCullFace(GL_BACK);
        gl->glFrontFace(GL_CCW);
    } else {
        gl->glDisable(GL_CULL_FACE);
    }

    switch (pipeline.blend) {
    case gfx::BlendMode::None:
        gl->glDisable(GL_BLEND);
        break;
    case gfx::BlendMode::Alpha:
        gl->glEnable(GL_BLEND);
        gl->glBlendEquation(GL_FUNC_ADD);
        gl->glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                                GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case gfx::BlendMode::Additive:
        gl->glEnable(GL_BLEND);
        gl->glBlendEquation(GL_FUNC_ADD);
        gl->glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
        break;
    case gfx::BlendMode::Multiply:
        gl->glEnable(GL_BLEND);
        gl->glBlendEquation(GL_FUNC_ADD);
        gl->glBlendFuncSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA,
                                GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }

    return true;
}

void GlDevice::Impl::drawVertices(const std::vector<Vertex>& vertices, GLenum mode)
{
    if (vertices.empty())
        return;
    if (!vao || !vbo)
        return;

    const int bytes = static_cast<int>(vertices.size() * sizeof(Vertex));
    vao->bind();
    vbo->bind();
    vbo->allocate(vertices.data(), bytes);
    gl->glDrawArrays(mode, 0, static_cast<GLsizei>(vertices.size()));
    vbo->release();
    vao->release();
}

bool GlDevice::Impl::beginQuery()
{
    if (!extra || query == 0)
        return false;
    extra->glBeginQuery(GL_SAMPLES_PASSED, query);
    queryActive = true;
    return true;
}

int GlDevice::Impl::endQuery()
{
    if (!extra || !queryActive)
        return 0;
    extra->glEndQuery(GL_SAMPLES_PASSED);
    GLuint result = 0;
    extra->glGetQueryObjectuiv(query, GL_QUERY_RESULT, &result);
    queryActive = false;
    return static_cast<int>(std::min<GLuint>(
        result, static_cast<GLuint>(std::numeric_limits<int>::max())));
}

void GlDevice::Impl::setScissor(const QRect& clip, const QSize& size)
{
    if (clip.isEmpty()) {
        gl->glDisable(GL_SCISSOR_TEST);
        return;
    }
    gl->glEnable(GL_SCISSOR_TEST);
    gl->glScissor(clip.x(), size.height() - (clip.y() + clip.height()),
                  clip.width(), clip.height());
}

void GlDevice::Impl::clear(const gfx::DrawCommand& command, const QRect& clip,
                           const QSize& size)
{
    if (clip.isEmpty())
        return;

    setScissor(clip, size);
    const QVector4D colour = premultiplied(command.colour);
    gl->glClearColor(colour.x(), colour.y(), colour.z(), colour.w());
    gl->glDepthMask(GL_TRUE);
    gl->glClearDepthf(1.0F);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

bool GlDevice::Impl::drawMesh2D(const gfx::DrawCommand& command, const QRect& viewport)
{
    if (!command.pipeline.isValid() || !command.mesh2d.isValid())
        return true;

    QOpenGLShaderProgram* program = programFor(command.pipeline.shader.name());
    if (program == nullptr) {
        appendFailure(QStringLiteral("the GL device has no program for shader '%1'")
                          .arg(command.pipeline.shader.name()));
        return false;
    }

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(command.mesh2d.indices().size()));
    for (const quint32 index : command.mesh2d.indices()) {
        const gfx::Vertex2D& source = command.mesh2d.vertices().at(static_cast<qsizetype>(index));
        vertices.push_back(makeVertex(QPointF(viewport.x() + source.position.x(),
                                              viewport.y() + source.position.y()),
                                      source.colour, source.uv));
    }

    const gfx::Texture texture = command.uniforms.texture(QStringLiteral("texture"));
    GLuint textureId = 0;
    if (command.pipeline.shader.name() == QStringLiteral("unlit") && texture.isValid()) {
        textureId = uploadTexture(texture.image(), texture.wrap(), texture.filter(), false);
        if (textureId == 0) {
            appendFailure(QStringLiteral("could not upload a Mesh2D texture"));
            return false;
        }
    }

    if (!bindProgramPipeline(program, command.pipeline, false, targetSize,
                             QRect(0, 0, targetSize.width(), targetSize.height()),
                             textureId != 0)) {
        deleteTexture(textureId);
        return false;
    }

    if (command.pipeline.shader.name() == QStringLiteral("solid")) {
        const QColor c = command.uniforms.colour(QStringLiteral("colour"), QColor(Qt::white));
        program->setUniformValue("uColour",
                                 QVector4D(static_cast<float>(c.redF()),
                                           static_cast<float>(c.greenF()),
                                           static_cast<float>(c.blueF()),
                                           static_cast<float>(c.alphaF())));
    } else if (command.pipeline.shader.name() == QStringLiteral("unlit")) {
        program->setUniformValue("uHasTexture", textureId != 0 ? 1 : 0);
    }

    GLenum mode = GL_TRIANGLES;
    switch (command.mesh2d.topology()) {
    case gfx::PrimitiveTopology::Triangles:
        mode = GL_TRIANGLES;
        break;
    case gfx::PrimitiveTopology::Lines:
        mode = GL_LINES;
        break;
    case gfx::PrimitiveTopology::Points:
        mode = GL_POINTS;
        break;
    }

    beginQuery();
    drawVertices(vertices, mode);
    framePixelsWritten += endQuery();
    program->release();
    deleteTexture(textureId);
    return true;
}

bool GlDevice::Impl::drawMesh3D(const gfx::DrawCommand& command, const QRect& viewport,
                                const QSize& size)
{
    if (!command.pipeline.isValid() || !command.mesh3d.isValid())
        return true;

    QOpenGLShaderProgram* program = programFor(command.pipeline.shader.name());
    if (program == nullptr) {
        appendFailure(QStringLiteral("the GL device has no program for shader '%1'")
                          .arg(command.pipeline.shader.name()));
        return false;
    }

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(command.mesh3d.indices().size()));
    for (const quint32 index : command.mesh3d.indices())
        vertices.push_back(makeVertex3D(command.mesh3d.vertices().at(static_cast<qsizetype>(index))));

    const gfx::Texture texture = command.uniforms.texture(QStringLiteral("texture"));
    GLuint textureId = 0;
    if (command.pipeline.shader.name() == QStringLiteral("unlit") && texture.isValid()) {
        textureId = uploadTexture(texture.image(), texture.wrap(), texture.filter(), false);
        if (textureId == 0) {
            appendFailure(QStringLiteral("could not upload a Mesh3D texture"));
            return false;
        }
    }

    const int glY = size.height() - (viewport.y() + viewport.height());
    if (!bindProgramPipeline(program, command.pipeline, true, size,
                             QRect(viewport.x(), glY, viewport.width(), viewport.height()),
                             textureId != 0)) {
        deleteTexture(textureId);
        return false;
    }

    const QMatrix4x4 mvp = command.uniforms.matrix(QStringLiteral("mvp"), QMatrix4x4());
    program->setUniformValue("uMvp", mvp);

    if (command.pipeline.shader.name() == QStringLiteral("solid")) {
        const QColor c = command.uniforms.colour(QStringLiteral("colour"), QColor(Qt::white));
        program->setUniformValue("uColour",
                                 QVector4D(static_cast<float>(c.redF()),
                                           static_cast<float>(c.greenF()),
                                           static_cast<float>(c.blueF()),
                                           static_cast<float>(c.alphaF())));
    } else if (command.pipeline.shader.name() == QStringLiteral("lambert")) {
        const QColor base = command.uniforms.colour(QStringLiteral("baseColour"),
                                                     QColor(Qt::white));
        program->setUniformValue("uBaseColour",
                                 QVector4D(static_cast<float>(base.redF()),
                                           static_cast<float>(base.greenF()),
                                           static_cast<float>(base.blueF()),
                                           static_cast<float>(base.alphaF())));
        QVector3D light = command.uniforms.vector(QStringLiteral("lightDirection"),
                                                  QVector3D(0.0F, 0.0F, 1.0F));
        if (light.lengthSquared() <= 0.0F)
            light = QVector3D(0.0F, 0.0F, 1.0F);
        else
            light.normalize();
        program->setUniformValue("uLightDirection", light);
        program->setUniformValue("uAmbient",
                                 static_cast<GLfloat>(qBound(
                                     0.0, command.uniforms.real(QStringLiteral("ambient"), 0.2), 1.0)));
    } else if (command.pipeline.shader.name() == QStringLiteral("unlit")) {
        program->setUniformValue("uHasTexture", textureId != 0 ? 1 : 0);
    }

    GLenum mode = GL_TRIANGLES;
    switch (command.mesh3d.topology()) {
    case gfx::PrimitiveTopology::Triangles:
        mode = GL_TRIANGLES;
        break;
    case gfx::PrimitiveTopology::Lines:
        mode = GL_LINES;
        break;
    case gfx::PrimitiveTopology::Points:
        mode = GL_POINTS;
        break;
    }

    beginQuery();
    drawVertices(vertices, mode);
    framePixelsWritten += endQuery();
    program->release();
    deleteTexture(textureId);
    return true;
}

GLuint GlDevice::Impl::uploadTexture(const QImage& image, gfx::Wrap wrap,
                                     gfx::Filter filter, bool premultipliedSource)
{
    if (image.isNull())
        return 0;

    const QImage source = image.convertToFormat(
        premultipliedSource ? QImage::Format_RGBA8888_Premultiplied
                            : QImage::Format_RGBA8888);
    if (source.isNull())
        return 0;

    GLuint texture = 0;
    gl->glGenTextures(1, &texture);
    if (texture == 0)
        return 0;

    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, texture);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode(wrap));
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode(wrap));
    const GLint filterValue = filterMode(filter);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filterValue);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filterValue);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, source.width(), source.height(), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, source.constBits());
    // The sampler stays on unit 0 until the draw that uses it completes; the
    // caller deletes the texture afterwards.
    return texture;
}

void GlDevice::Impl::deleteTexture(GLuint texture)
{
    if (texture != 0)
        gl->glDeleteTextures(1, &texture);
}

bool GlDevice::Impl::uploadImage(const QImage& image)
{
    if (!fbo || !fbo->bind()) {
        appendFailure(QStringLiteral("could not bind the framebuffer object for upload"));
        return false;
    }
    if (image.size() != fbo->size()) {
        appendFailure(QStringLiteral("the upload image is %1x%2, the FBO is %3x%4")
                          .arg(image.width()).arg(image.height())
                          .arg(fbo->size().width()).arg(fbo->size().height()));
        return false;
    }

    const GLuint texture = uploadTexture(image, gfx::Wrap::Clamp, gfx::Filter::Nearest,
                                         true);
    if (texture == 0) {
        appendFailure(QStringLiteral("could not upload the target image as a GL texture"));
        return false;
    }

    gl->glDisable(GL_SCISSOR_TEST);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_FALSE);
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_CULL_FACE);
    gl->glViewport(0, 0, targetSize.width(), targetSize.height());

    copyProgram->bind();
    copyProgram->setUniformValue("uIs3D", 0);
    copyProgram->setUniformValue("uTargetSize",
                                 QVector2D(static_cast<float>(targetSize.width()),
                                           static_cast<float>(targetSize.height())));
    copyProgram->setUniformValue("uTexture", 0);

    std::vector<Vertex> quad;
    quad.reserve(6);
    const float w = static_cast<float>(targetSize.width());
    const float h = static_cast<float>(targetSize.height());
    quad.push_back(makeVertex(QPointF(0.0, 0.0), QColor(Qt::white), QPointF(0.0, 0.0)));
    quad.push_back(makeVertex(QPointF(w, 0.0), QColor(Qt::white), QPointF(1.0, 0.0)));
    quad.push_back(makeVertex(QPointF(w, h), QColor(Qt::white), QPointF(1.0, 1.0)));
    quad.push_back(makeVertex(QPointF(0.0, 0.0), QColor(Qt::white), QPointF(0.0, 0.0)));
    quad.push_back(makeVertex(QPointF(w, h), QColor(Qt::white), QPointF(1.0, 1.0)));
    quad.push_back(makeVertex(QPointF(0.0, h), QColor(Qt::white), QPointF(0.0, 1.0)));
    drawVertices(quad, GL_TRIANGLES);
    copyProgram->release();
    deleteTexture(texture);
    return true;
}

bool GlDevice::Impl::readFramebuffer(QImage& image)
{
    if (!fbo || !fbo->isValid() || !fbo->bind()) {
        appendFailure(QStringLiteral("could not bind the framebuffer object for read-back"));
        return false;
    }

    QImage read = fbo->toImage(true);
    if (read.isNull()) {
        appendFailure(QStringLiteral("QOpenGLFramebufferObject::toImage() returned a null image"));
        return false;
    }

    image = read.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        appendFailure(QStringLiteral("could not convert the GL read-back to ARGB32"));
        return false;
    }
    return true;
}

bool GlDevice::Impl::drawImage(const gfx::DrawCommand& command, const QRect& target,
                               const QRect& scissor)
{
    if (command.image.isNull())
        return true;

    const QRect destination = command.viewport.isEmpty()
        ? QRect(0, 0, command.image.width(), command.image.height())
        : command.viewport;
    if (destination.width() <= 0 || destination.height() <= 0)
        return true;

    QRect clip = destination.intersected(scissor).intersected(target);
    if (!command.scissor.isEmpty())
        clip = clip.intersected(command.scissor);
    if (clip.isEmpty())
        return true;

    const GLuint texture = uploadTexture(command.image, gfx::Wrap::Clamp,
                                         gfx::Filter::Bilinear, false);
    if (texture == 0) {
        appendFailure(QStringLiteral("could not upload a DrawImage texture"));
        return false;
    }

    std::vector<Vertex> vertices;
    vertices.reserve(6);
    const QPointF topLeft(destination.x(), destination.y());
    const QPointF bottomRight(destination.x() + destination.width(),
                              destination.y() + destination.height());
    vertices.push_back(makeVertex(topLeft, QColor(Qt::white), QPointF(0.0, 0.0)));
    vertices.push_back(makeVertex(QPointF(bottomRight.x(), topLeft.y()),
                                  QColor(Qt::white), QPointF(1.0, 0.0)));
    vertices.push_back(makeVertex(bottomRight, QColor(Qt::white), QPointF(1.0, 1.0)));
    vertices.push_back(makeVertex(topLeft, QColor(Qt::white), QPointF(0.0, 0.0)));
    vertices.push_back(makeVertex(bottomRight, QColor(Qt::white), QPointF(1.0, 1.0)));
    vertices.push_back(makeVertex(QPointF(topLeft.x(), bottomRight.y()),
                                  QColor(Qt::white), QPointF(0.0, 1.0)));

    setScissor(clip, targetSize);
    if (!bindProgramPipeline(unlitProgram.get(), command.pipeline, false, targetSize,
                             QRect(0, 0, targetSize.width(), targetSize.height()), true)) {
        deleteTexture(texture);
        return false;
    }
    unlitProgram->setUniformValue("uHasTexture", 1);

    beginQuery();
    drawVertices(vertices, GL_TRIANGLES);
    framePixelsWritten += endQuery();
    unlitProgram->release();
    deleteTexture(texture);
    return true;
}

void GlDevice::Impl::drawText(const gfx::DrawCommand& command, const QRect& viewport,
                              const QRect& clip, QImage& image)
{
    if (clip.isEmpty() || command.text.isEmpty())
        return;

    const QFontMetricsF metrics(command.font);
    const qreal x = viewport.x() + command.textPosition.x();
    const qreal y = viewport.y() + command.textPosition.y();
    const qreal width = qMax<qreal>(1.0, metrics.horizontalAdvance(command.text) + 8.0);
    const qreal height = qMax<qreal>(1.0, metrics.height() + 4.0);
    const QRectF rectangle(x, y - metrics.ascent(), width, height);

    QPainter painter(&image);
    painter.setClipRect(clip);
    TextPainter::drawText(painter, rectangle, command.text, command.font,
                          command.colour, Qt::AlignLeft | Qt::AlignTop);
    painter.end();
}

void GlDevice::Impl::drawPath(const gfx::DrawCommand& command, const QRect& viewport,
                              const QRect& clip, QImage& image)
{
    if (clip.isEmpty() || command.path.isEmpty())
        return;

    QPainter painter(&image);
    painter.setClipRect(clip);
    painter.translate(viewport.topLeft());
    TextPainter::drawPath(painter, command.path, command.pen, command.brush);
    painter.end();
}

void GlDevice::Impl::countTriangles(const gfx::DrawCommand& command,
                                    const QRect& viewport, gfx::RasterStats& stats) const
{
    if (!command.pipeline.isValid())
        return;

    if (command.kind == gfx::CommandKind::DrawMesh2D) {
        if (!command.mesh2d.isValid()
            || command.mesh2d.topology() != gfx::PrimitiveTopology::Triangles)
            return;

        const QVector<gfx::Vertex2D>& vertices = command.mesh2d.vertices();
        const QVector<quint32>& indices = command.mesh2d.indices();
        for (qsizetype i = 0; i + 2 < indices.size(); i += 3) {
            const QPointF a(viewport.x() + vertices.at(static_cast<qsizetype>(indices.at(i))).position.x(),
                            viewport.y() + vertices.at(static_cast<qsizetype>(indices.at(i))).position.y());
            const QPointF b(viewport.x() + vertices.at(static_cast<qsizetype>(indices.at(i + 1))).position.x(),
                            viewport.y() + vertices.at(static_cast<qsizetype>(indices.at(i + 1))).position.y());
            const QPointF c(viewport.x() + vertices.at(static_cast<qsizetype>(indices.at(i + 2))).position.x(),
                            viewport.y() + vertices.at(static_cast<qsizetype>(indices.at(i + 2))).position.y());
            const qreal area = (b.x() - a.x()) * (c.y() - a.y())
                - (b.y() - a.y()) * (c.x() - a.x());
            if (isFinitePoint(a) && isFinitePoint(b) && isFinitePoint(c) && area != 0.0)
                ++stats.trianglesDrawn;
        }
        return;
    }

    if (command.kind != gfx::CommandKind::DrawMesh3D)
        return;
    if (!command.mesh3d.isValid()
        || command.mesh3d.topology() != gfx::PrimitiveTopology::Triangles)
        return;

    const QMatrix4x4 mvp = command.uniforms.matrix(QStringLiteral("mvp"), QMatrix4x4());
    const QVector<gfx::Vertex3D>& vertices = command.mesh3d.vertices();
    const QVector<quint32>& indices = command.mesh3d.indices();
    for (qsizetype i = 0; i + 2 < indices.size(); i += 3) {
        QPointF screen[3];
        bool valid = true;
        for (int j = 0; j < 3; ++j) {
            const gfx::Vertex3D& source = vertices.at(static_cast<qsizetype>(indices.at(i + j)));
            const QVector4D clip = mvp * QVector4D(source.position, 1.0F);
            if (!std::isfinite(clip.x()) || !std::isfinite(clip.y())
                || !std::isfinite(clip.z()) || !std::isfinite(clip.w())
                || clip.w() <= 0.0F) {
                valid = false;
                break;
            }
            const qreal inverseW = 1.0 / static_cast<qreal>(clip.w());
            const qreal ndcX = static_cast<qreal>(clip.x()) * inverseW;
            const qreal ndcY = static_cast<qreal>(clip.y()) * inverseW;
            screen[j] = QPointF(viewport.x() + (ndcX * 0.5 + 0.5) * viewport.width(),
                                viewport.y() + (0.5 - ndcY * 0.5) * viewport.height());
        }
        if (!valid)
            continue;
        const qreal area = (screen[1].x() - screen[0].x()) * (screen[2].y() - screen[0].y())
            - (screen[1].y() - screen[0].y()) * (screen[2].x() - screen[0].x());
        if (area == 0.0)
            continue;
        if (command.pipeline.raster.cullBack && area >= 0.0)
            continue;
        ++stats.trianglesDrawn;
    }
}

void GlDevice::Impl::ensureNoBinding()
{
    if (!gl)
        return;
    gl->glUseProgram(0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (extra)
        extra->glBindVertexArray(0);
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_FALSE);
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_CULL_FACE);
}

// ---------------------------------------------------------------------------
// GlDevice public interface
// ---------------------------------------------------------------------------

GlDevice::GlDevice()
    : impl_(std::make_unique<Impl>())
{
    initialize();
}

GlDevice::~GlDevice()
{
    if (impl_)
        impl_->releaseGl();
}

bool GlDevice::initialize()
{
    impl_->lastError.clear();
    impl_->shaderLog.clear();
    impl_->degraded = false;
    impl_->recoveryUsed = false;
    impl_->contextLost = false;
    impl_->releaseGl();
    if (!impl_->buildContextAndResources())
        return false;
    return true;
}

bool GlDevice::initializeWithShaderSources(const QString& vertexSource,
                                           const QString& fragmentSource)
{
    impl_->customSolid = true;
    impl_->customVertex = vertexSource;
    impl_->customFragment = fragmentSource;
    return initialize();
}

bool GlDevice::isInitialized() const
{
    return impl_->initialized;
}

QString GlDevice::shaderLog() const
{
    return impl_->shaderLog;
}

void GlDevice::simulateContextLoss()
{
    impl_->releaseGl();
    impl_->contextLost = true;
    impl_->lastError = QStringLiteral("the OpenGL context was dropped between frames");
}

gfx::RasterStats GlDevice::submit(RenderTarget& target,
                                  const gfx::CommandBuffer& commands)
{
    // The software device is the reference for invalid input too, so both
    // devices produce the same diagnostic and the same untouched target.
    if (!target.isValid()) {
        const gfx::RasterStats stats = impl_->software->submit(target, commands);
        impl_->lastError = impl_->software->lastError();
        return stats;
    }

    const QSize pixelSize = target.pixelSize();
    const qint64 pixels = static_cast<qint64>(pixelSize.width())
        * static_cast<qint64>(pixelSize.height());
    const gfx::RasterCaps caps;
    if (pixels > caps.maxTargetPixels || primitiveCount(commands) > caps.maxTriangles) {
        const gfx::RasterStats stats = impl_->software->submit(target, commands);
        impl_->lastError = impl_->software->lastError();
        return stats;
    }

    if (commands.isEmpty())
        return {};

    if (!impl_->available) {
        const bool canRecover = impl_->contextLost && !impl_->degraded
            && !impl_->recoveryUsed;
        if (!canRecover || !impl_->recover()) {
            const gfx::RasterStats stats = impl_->software->submit(target, commands);
            if (impl_->lastError.isEmpty())
                impl_->lastError = impl_->software->lastError();
            return stats;
        }
    }

    const bool alreadyCurrent = QOpenGLContext::currentContext() == impl_->context.get();
    if (!alreadyCurrent && !impl_->makeCurrent()) {
        if (!impl_->recover()) {
            const gfx::RasterStats stats = impl_->software->submit(target, commands);
            if (impl_->lastError.isEmpty())
                impl_->lastError = impl_->software->lastError();
            return stats;
        }
        if (!impl_->makeCurrent()) {
            impl_->degrade(QStringLiteral("could not make the recovered GL context current"));
            const gfx::RasterStats stats = impl_->software->submit(target, commands);
            return stats;
        }
    }

    const auto restoreCurrent = qScopeGuard([this, alreadyCurrent] {
        if (!alreadyCurrent && impl_->context && impl_->context->isValid())
            impl_->context->doneCurrent();
    });

    impl_->targetSize = pixelSize;
    if (!impl_->ensureFramebuffer(pixelSize)) {
        impl_->appendFailure(QStringLiteral("could not resize the offscreen framebuffer"));
        impl_->ensureNoBinding();
        const gfx::RasterStats stats = impl_->software->submit(target, commands);
        return stats;
    }

    impl_->query = 0;
    impl_->queryActive = false;
    if (impl_->extra)
        impl_->extra->glGenQueries(1, &impl_->query);

    // Drain diagnostics from the previous frame so a stale error cannot be
    // mistaken for a failure in this one.
    while (impl_->gl->glGetError() != GL_NO_ERROR) {
    }

    gfx::RasterStats stats;
    impl_->framePixelsWritten = 0;
    const QImage original = target.image();
    const QImage::Format targetFormat = original.format();
    QImage working = original.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (working.isNull()) {
        impl_->appendFailure(QStringLiteral("could not convert the target for GL upload"));
        const gfx::RasterStats fallback = impl_->software->submit(target, commands);
        return fallback;
    }

    if (!impl_->uploadImage(working)) {
        impl_->fbo.reset();
        impl_->ensureNoBinding();
        const gfx::RasterStats fallback = impl_->software->submit(target, commands);
        return fallback;
    }

    impl_->gl->glDisable(GL_SCISSOR_TEST);
    impl_->gl->glDepthMask(GL_TRUE);
    impl_->gl->glClearDepthf(1.0F);
    impl_->gl->glClear(GL_DEPTH_BUFFER_BIT);

    const gfx::CommandBuffer scaled = scaledCommands(commands, target.devicePixelRatio());
    const QRect targetRect(0, 0, pixelSize.width(), pixelSize.height());
    QRect scissor = targetRect;
    impl_->imageDirty = false;

    bool frameOk = true;
    auto failFrame = [this, &frameOk](const QString& reason) {
        impl_->appendFailure(reason);
        frameOk = false;
    };

    for (const gfx::DrawCommand& command : scaled.commands()) {
        if (!frameOk)
            break;

        switch (command.kind) {
        case gfx::CommandKind::SetScissor:
            scissor = command.scissor.isEmpty()
                ? targetRect
                : command.scissor.intersected(targetRect);
            continue;

        case gfx::CommandKind::DrawText:
        case gfx::CommandKind::DrawPath: {
            if (!impl_->imageDirty && !impl_->readFramebuffer(working)) {
                frameOk = false;
                break;
            }
            impl_->imageDirty = false;
            const QRect viewport = viewportFor(command, targetRect);
            const QRect clip = clipFor(command, targetRect, scissor);
            if (command.kind == gfx::CommandKind::DrawText)
                impl_->drawText(command, viewport, clip, working);
            else
                impl_->drawPath(command, viewport, clip, working);
            impl_->imageDirty = true;
            continue;
        }

        default:
            break;
        }

        if (impl_->imageDirty) {
            if (!impl_->uploadImage(working)) {
                frameOk = false;
                break;
            }
            impl_->imageDirty = false;
        }

        const QRect viewport = viewportFor(command, targetRect);
        const QRect clip = clipFor(command, targetRect, scissor);
        switch (command.kind) {
        case gfx::CommandKind::Clear:
            impl_->clear(command, clip, pixelSize);
            break;
        case gfx::CommandKind::DrawMesh2D:
            if (clip.isEmpty())
                break;
            impl_->countTriangles(command, viewport, stats);
            impl_->setScissor(clip, pixelSize);
            if (!impl_->drawMesh2D(command, viewport))
                frameOk = false;
            break;
        case gfx::CommandKind::DrawMesh3D:
            if (clip.isEmpty())
                break;
            impl_->countTriangles(command, viewport, stats);
            impl_->setScissor(clip, pixelSize);
            if (!impl_->drawMesh3D(command, viewport, pixelSize))
                frameOk = false;
            break;
        case gfx::CommandKind::DrawImage:
            if (!impl_->drawImage(command, targetRect, scissor))
                frameOk = false;
            break;
        case gfx::CommandKind::DrawText:
        case gfx::CommandKind::DrawPath:
        case gfx::CommandKind::SetScissor:
            break;
        }
    }

    if (impl_->query != 0 && impl_->extra) {
        impl_->extra->glDeleteQueries(1, &impl_->query);
        impl_->query = 0;
    }
    impl_->queryActive = false;

    if (frameOk && impl_->gl->glGetError() != GL_NO_ERROR)
        failFrame(QStringLiteral("OpenGL reported an error while executing the frame"));

    if (frameOk && !impl_->imageDirty && !impl_->readFramebuffer(working))
        frameOk = false;

    impl_->ensureNoBinding();

    if (!frameOk) {
        impl_->fbo.reset();
        impl_->appendFailure(QStringLiteral("the frame fell back to the software device"));
        const gfx::RasterStats fallback = impl_->software->submit(target, commands);
        return fallback;
    }

    const QImage converted = impl_->fromPremultiplied(working, targetFormat);
    if (converted.isNull()) {
        impl_->appendFailure(QStringLiteral("could not convert the GL result to the target format"));
        const gfx::RasterStats fallback = impl_->software->submit(target, commands);
        return fallback;
    }
    target.imageRef() = converted;
    stats.pixelsWritten = impl_->framePixelsWritten;
    return stats;
}

DeviceCapabilities GlDevice::capabilities() const
{
    DeviceCapabilities capabilities;
    const bool usable = impl_->available;
    capabilities.text = usable;
    capabilities.paths = usable;
    capabilities.images = usable;
    capabilities.depthBuffer = usable;
    capabilities.shaders = usable;
    capabilities.maxTriangles = gfx::RasterCaps{}.maxTriangles;
    capabilities.maxTargetPixels = gfx::RasterCaps{}.maxTargetPixels;
    capabilities.backendName = name();
    return capabilities;
}

QString GlDevice::name() const
{
    return QStringLiteral("opengl");
}

bool GlDevice::isAvailable() const
{
    return impl_->available;
}

QString GlDevice::lastError() const
{
    return impl_->lastError;
}

} // namespace md::render

#else // PERIODICITY_HAS_OPENGL

namespace md::render {

namespace {

QString noOpenGLReason()
{
    return QStringLiteral(
        "Qt6::OpenGL was not found at build time, so the OpenGL device is not "
        "compiled in; using the software device");
}

} // namespace

struct GlDevice::Impl
{
    SoftwareDevice software;
    QString lastError = noOpenGLReason();
    QString shaderLog;
};

GlDevice::GlDevice()
    : impl_(std::make_unique<Impl>())
{
}

GlDevice::~GlDevice() = default;

bool GlDevice::initialize()
{
    return false;
}

bool GlDevice::initializeWithShaderSources(const QString&, const QString&)
{
    impl_->shaderLog = noOpenGLReason();
    return false;
}

bool GlDevice::isInitialized() const
{
    return false;
}

QString GlDevice::shaderLog() const
{
    return impl_->shaderLog;
}

void GlDevice::simulateContextLoss()
{
}

gfx::RasterStats GlDevice::submit(RenderTarget& target,
                                  const gfx::CommandBuffer& commands)
{
    return impl_->software.submit(target, commands);
}

DeviceCapabilities GlDevice::capabilities() const
{
    DeviceCapabilities capabilities;
    capabilities.backendName = name();
    return capabilities;
}

QString GlDevice::name() const
{
    return QStringLiteral("opengl");
}

bool GlDevice::isAvailable() const
{
    return false;
}

QString GlDevice::lastError() const
{
    return impl_->lastError;
}

} // namespace md::render

#endif // PERIODICITY_HAS_OPENGL
