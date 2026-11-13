// Periodicity — implementation of the optional QRhi device.
//
// Everything that needs a QRhi lives inside PERIODICITY_HAS_RHI. The stub at the
// bottom of this file provides the same interface (unavailable, with a precise
// reason) for builds without the shipped rhi/ headers, so callers never need a
// preprocessor branch.
//
// The device deliberately owns no window and no swap chain. Each submit() that
// contains GPU work records one offscreen QRhi frame per contiguous run of GPU
// commands, with a QRhiTexture as the colour attachment, and reads that texture
// back into the target image before returning. Commands that must touch the
// image on the CPU — Clear, DrawText, DrawPath — split a run: the image is
// read back, the command runs on it exactly as it does in SoftwareDevice and
// GlDevice, and the next run starts from the updated image.
//
// Coordinate conventions, chosen so the result matches the software rasterizer:
//
//   * 2D vertices are converted from target pixels to OpenGL-style NDC on the
//     CPU when the draw is recorded, and the vertex shader passes them through
//     an identity mvp. The Y flip that matches QImage's top-left origin is part
//     of that conversion.
//   * 3D vertices go through the pipeline's mvp and the command viewport is
//     applied with QRhiCommandBuffer::setViewport().
//   * QRhi::clipSpaceCorrMatrix() then maps the OpenGL-style clip space to the
//     backend's (for D3D11 it only remaps z to 0..1).
//   * depth is the same [0, 1] range, nearer is smaller, and the test is LESS.
//
// Nothing here compiles GLSL at run time and nothing here uses a
// QOpenGLShaderProgram: the programs are the .qsb resources that
// qt6_add_shaders() produced from shaders/, loaded once in initialize().

#include <md/render/rhi_device.h>

#include <md/render/software_device.h>

#include <md/gfx/shader.h>

#include <QColor>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QMatrix4x4>
#include <QPen>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QStringList>
#include <QTransform>
#include <QVector3D>
#include <QVector4D>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#if defined(PERIODICITY_HAS_RHI) && PERIODICITY_HAS_RHI

#include <md/render/text_painter.h>

#include <QFile>
#include <QFontMetricsF>
#include <QPainter>

// The RHI headers are the first QtGui include on purpose: they pull in
// qtguiglobal.h, and therefore define QT_FEATURE_* before the QT_CONFIG() guards
// below ask about Vulkan and OpenGL.
#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
#include <QVulkanInstance>
#endif

#if QT_CONFIG(opengl)
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#endif

// Diagnostic tracing for the RHI device. Off by default; enable with
//   cmake --preset x64-debug -DPERIODICITY_RHI_TRACE=ON
// or by defining PERIODICITY_RHI_TRACE=1 for the periodicity_render target.
// Every call compiles to nothing when disabled, so this costs nothing in a
// normal build and can be removed wholesale once the 2D investigation is done.
#ifndef PERIODICITY_RHI_TRACE
#define PERIODICITY_RHI_TRACE 0
#endif

#if PERIODICITY_RHI_TRACE
#include <QDebug>
#define PERIODICITY_RHI_LOG(...) qDebug(__VA_ARGS__)
#else
#define PERIODICITY_RHI_LOG(...) ((void)0)
#endif

// The .qsb files are compiled into :/shaders by qt6_add_shaders(). A static
// library's resource initialiser is not referenced by anything the linker can
// see on its own, so it has to be called explicitly before the first QFile
// lookup.
//
// The declaration has to be at global scope. Q_INIT_RESOURCE expands to an
// extern declaration plus a call; inside an unnamed namespace the declaration
// picks up internal linkage, the compiler then looks for
// `?qInitResources_render_shaders@?A0x...@@` instead of the symbol rcc emitted,
// and the link fails with LNK2019. Declaring the initialiser here and calling it
// from the unnamed helper keeps the symbol name intact.
extern int qInitResources_render_shaders();

namespace {

void periodicityInitRenderShaderResources()
{
    qInitResources_render_shaders();
}

} // namespace

namespace md::render {

namespace {

// ---------------------------------------------------------------------------
// Command-buffer scaling (the same contract SoftwareDevice and GlDevice apply)
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
            // Model space is untouched; the viewport carries the scale.
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
            // A set viewport (scaled above) is the dpr-correct destination.
            break;
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

// The CPU half of Clear: the same rectangle the software rasterizer fills, with
// the same colour. CompositionMode_Source replaces the pixels (including alpha)
// instead of blending over them, which is what glClear and gfx's fill do.
void clearImage(QImage& image, const QRect& area, const QColor& colour)
{
    if (image.isNull() || area.isEmpty())
        return;

    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(area, validColour(colour));
    painter.end();
}

void paintText(QImage& image, const gfx::DrawCommand& command, const QRect& viewport,
               const QRect& clip)
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

void paintPath(QImage& image, const gfx::DrawCommand& command, const QRect& viewport,
               const QRect& clip)
{
    if (clip.isEmpty() || command.path.isEmpty())
        return;

    QPainter painter(&image);
    painter.setClipRect(clip);
    painter.translate(viewport.topLeft());
    TextPainter::drawPath(painter, command.path, command.pen, command.brush);
    painter.end();
}

// The triangle count GlDevice reports: primitives that pass the same culling
// test. Copied rather than shared because it depends on gfx's conventions, not
// on any device API.
void countTriangles(const gfx::DrawCommand& command, const QRect& viewport,
                    gfx::RasterStats& stats)
{
    if (!command.pipeline.isValid())
        return;

    if (command.kind == gfx::CommandKind::DrawMesh2D) {
        if (!command.mesh2d.isValid()
            || command.mesh2d.topology() != gfx::PrimitiveTopology::Triangles) {
            return;
        }

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
        || command.mesh3d.topology() != gfx::PrimitiveTopology::Triangles) {
        return;
    }

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

// ---------------------------------------------------------------------------
// Vertices and the uniform block
// ---------------------------------------------------------------------------

constexpr int kFloatsPerVertex = 12;
constexpr int kVertexStride = kFloatsPerVertex * static_cast<int>(sizeof(float));

struct RhiVertex
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

static_assert(sizeof(RhiVertex) == kVertexStride,
              "RhiVertex must match the vertex input stride");

RhiVertex makeVertex2D(const QPointF& position, const QColor& colour, const QPointF& uv)
{
    const QColor c = validColour(colour);
    RhiVertex vertex;
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

RhiVertex makeVertex3D(const gfx::Vertex3D& source)
{
    const QColor c = validColour(source.colour);
    RhiVertex vertex;
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

// std140 offsets in the block declared by shaders/rhi.vert (and repeated by the
// fragment shaders that read it). Two mat4 then six vec4s: 208 bytes of a
// 256-byte slot, so every draw's block starts on a uniform-buffer alignment
// boundary.
constexpr int kUniformSlotSize = 256;
constexpr int kUniformBlockSize = 208;
constexpr int kUniformClip = 0;
constexpr int kUniformMvp = 64;
constexpr int kUniformColour = 128;
constexpr int kUniformBaseColour = 144;
constexpr int kUniformTargetSize = 160;
constexpr int kUniformLightDirection = 176;
constexpr int kUniformParams = 192;

struct UniformData
{
    std::array<float, kUniformSlotSize / static_cast<int>(sizeof(float))> words{};

    void setMatrix(int offset, const QMatrix4x4& matrix)
    {
        std::memcpy(words.data() + offset / static_cast<int>(sizeof(float)),
                    matrix.constData(), 16 * sizeof(float));
    }

    void setVector4(int offset, float x, float y, float z, float w)
    {
        float* target = words.data() + offset / static_cast<int>(sizeof(float));
        target[0] = x;
        target[1] = y;
        target[2] = z;
        target[3] = w;
    }

    void setColour(int offset, const QColor& colour)
    {
        const QColor c = validColour(colour);
        setVector4(offset, static_cast<float>(c.redF()), static_cast<float>(c.greenF()),
                   static_cast<float>(c.blueF()), static_cast<float>(c.alphaF()));
    }
};

struct TextureUpload
{
    QRhiTexture* texture = nullptr;
    QImage image; // already RGBA8888, straight alpha, tightly packed
};

struct GpuDraw
{
    QRhiGraphicsPipeline* pipeline = nullptr;
    QRhiShaderResourceBindings* bindings = nullptr;
    quint32 uniformSlot = 0;
    quint32 firstVertex = 0;
    quint32 vertexCount = 0;
    QRect viewport;
    QRect scissor;
};

// One contiguous run of GPU commands, built before an offscreen frame starts and
// released after its read-back has landed.
struct Run
{
    std::vector<RhiVertex> vertices;
    std::vector<UniformData> uniforms;
    std::vector<GpuDraw> draws;
    std::vector<TextureUpload> textures;
    std::vector<QRhiShaderResourceBindings*> temporaryBindings;

    void clear()
    {
        vertices.clear();
        uniforms.clear();
        draws.clear();
        textures.clear();
        temporaryBindings.clear();
    }
};

constexpr int kMaxDrawsPerFrame = 1024;
constexpr int kInitialVertexCapacity = 4096;

QByteArray pipelineKey(const QString& shader, gfx::PrimitiveTopology topology,
                       const gfx::Pipeline& state, bool is3D, bool textured,
                       quint64 generation)
{
    QByteArray key = shader.toUtf8();
    key += '|';
    key += QByteArray::number(static_cast<int>(topology));
    key += '|';
    key += QByteArray::number(static_cast<int>(state.blend));
    key += '|';
    key += state.depth.test ? '1' : '0';
    key += state.depth.write ? '1' : '0';
    key += is3D && state.raster.cullBack ? '1' : '0';
    key += textured ? '1' : '0';
    key += '|';
    key += QByteArray::number(static_cast<qulonglong>(generation));
    return key;
}

QRhiGraphicsPipeline::Topology rhiTopology(gfx::PrimitiveTopology topology)
{
    switch (topology) {
    case gfx::PrimitiveTopology::Triangles:
        return QRhiGraphicsPipeline::Triangles;
    case gfx::PrimitiveTopology::Lines:
        return QRhiGraphicsPipeline::Lines;
    case gfx::PrimitiveTopology::Points:
        return QRhiGraphicsPipeline::Points;
    }
    return QRhiGraphicsPipeline::Triangles;
}

QRhiGraphicsPipeline::TargetBlend targetBlend(gfx::BlendMode mode)
{
    QRhiGraphicsPipeline::TargetBlend blend;
    switch (mode) {
    case gfx::BlendMode::None:
        blend.enable = false;
        return blend;
    case gfx::BlendMode::Alpha:
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        return blend;
    case gfx::BlendMode::Additive:
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::One;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::One;
        return blend;
    case gfx::BlendMode::Multiply:
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::DstColor;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        return blend;
    }
    return blend;
}

QImage imageFromReadback(const QRhiReadbackResult& readback, bool yUpInFramebuffer)
{
    if (readback.pixelSize.isEmpty() || readback.data.isEmpty())
        return QImage();

    QImage::Format format = QImage::Format_Invalid;
    // EXPERIMENT (13 Nov 2026): a red rect read back as (0,0,255,255) means the
    // attachment bytes are B,G,R,A little-endian while QRhiReadbackResult reports
    // RGBA8. Interpreting them as RGBA turned red into blue; this maps them the
    // way the bytes are actually laid out. Revert this hunk if the parity tests
    // disagree.
    if (readback.format == QRhiTexture::RGBA8)
        format = QImage::Format_ARGB32_Premultiplied;
    else if (readback.format == QRhiTexture::BGRA8)
        format = QImage::Format_RGBA8888_Premultiplied;

    if (format == QImage::Format_Invalid)
        return QImage();

    const qsizetype expected = static_cast<qsizetype>(readback.pixelSize.width())
        * readback.pixelSize.height() * 4;
    if (readback.data.size() < expected)
        return QImage();

    const QImage view(reinterpret_cast<const uchar*>(readback.data.constData()),
                      readback.pixelSize.width(), readback.pixelSize.height(),
                      readback.pixelSize.width() * 4, format);
    if (view.isNull())
        return QImage();

    QImage result = view.copy();
    if (yUpInFramebuffer)
        result = result.mirrored(false, true);
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct RhiDevice::Impl
{
    std::unique_ptr<SoftwareDevice> software = std::make_unique<SoftwareDevice>();

    std::unique_ptr<QRhi> rhi;
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
    std::unique_ptr<QVulkanInstance> vulkanInstance;
#endif
#if QT_CONFIG(opengl)
    std::unique_ptr<QOffscreenSurface> glSurface;
#endif

    bool initialized = false;
    bool available = false;
    QString lastError;
    QString shaderLog;
    QString api;
    QString backendFailure;

    bool customSolid = false;
    QByteArray customSolidVertex;
    QByteArray customSolidFragment;

    QShader vertexShader;
    QShader solidShader;
    QShader vertexColourShader;
    QShader lambertShader;
    QShader unlitShader;

    // Set only by initializeWithSolidShaderData(): both stages of the solid
    // program are replaced, and the validation pipeline in initialize() proves
    // they still build against the built-in vertex layout.
    QShader solidVertexOverride;
    QShader solidFragmentOverride;

    QRhiTexture* colourTexture = nullptr;
    QRhiRenderBuffer* depthBuffer = nullptr;
    QRhiTextureRenderTarget* renderTarget = nullptr;
    QRhiRenderPassDescriptor* renderPass = nullptr;
    QSize targetSize;
    quint64 generation = 0;

    QRhiBuffer* vertexBuffer = nullptr;
    quint32 vertexCapacityBytes = 0;
    QRhiBuffer* uniformBuffer = nullptr;
    quint32 uniformSlotCapacity = 0;
    int uniformStride = kUniformSlotSize;

    QRhiTexture* dummyTexture = nullptr;
    QImage dummyImage;
    bool dummyTextureNeedsUpload = false;
    QRhiSampler* dummySampler = nullptr;
    QHash<QByteArray, QRhiSampler*> samplers;
    QRhiShaderResourceBindings* plainBindings = nullptr;
    QRhiShaderResourceBindings* dummyTexturedBindings = nullptr;
    QHash<QByteArray, QRhiGraphicsPipeline*> pipelines;

    void appendFailure(const QString& reason);
    bool loadShaders();
    bool createRhi();
    bool createSharedResources();
    bool ensureResources(const QSize& size);
    bool ensureVertexCapacity(quint32 bytes);
    void releasePipelines();
    void releaseResources();
    void releaseAll();
    void releaseRun(Run& run);

    const QShader& fragmentShaderFor(const QString& name) const;
    QRhiGraphicsPipeline* pipelineFor(const QString& shader,
                                     gfx::PrimitiveTopology topology,
                                     const gfx::Pipeline& state, bool is3D,
                                     bool textured);
    QRhiSampler* samplerFor(gfx::Wrap wrap, gfx::Filter filter);
    QRhiTexture* createTexture(const QImage& image, Run& run);
    QRhiShaderResourceBindings* bindingsFor(QRhiTexture* texture, QRhiSampler* sampler,
                                            Run& run);

    bool appendDraw(Run& run, const gfx::DrawCommand& command, const QRect& viewport,
                    const QRect& clip, const QSize& targetPixels,
                    gfx::RasterStats& stats);
    bool executeRun(QImage& image, Run& run, const QSize& targetPixels);
};

void RhiDevice::Impl::appendFailure(const QString& reason)
{
    if (reason.isEmpty())
        return;
    if (lastError.isEmpty())
        lastError = reason;
    else
        lastError += QStringLiteral("; ") + reason;
}

const QShader& RhiDevice::Impl::fragmentShaderFor(const QString& name) const
{
    if (name == QStringLiteral("solid"))
        return solidShader;
    if (name == QStringLiteral("vertexColour"))
        return vertexColourShader;
    if (name == QStringLiteral("lambert"))
        return lambertShader;
    if (name == QStringLiteral("unlit"))
        return unlitShader;

    static const QShader invalid;
    return invalid;
}

bool RhiDevice::Impl::loadShaders()
{
    const auto load = [this](const QString& name, QShader& target) {
        QFile file(QStringLiteral(":/shaders/%1.qsb").arg(name));
        if (!file.open(QIODevice::ReadOnly)) {
            shaderLog += QStringLiteral("could not open :/shaders/%1.qsb; ").arg(name);
            return false;
        }
        target = QShader::fromSerialized(file.readAll());
        if (!target.isValid()) {
            shaderLog += QStringLiteral("QShader::fromSerialized rejected :/shaders/%1.qsb; ")
                             .arg(name);
            return false;
        }
        return true;
    };

    vertexShader = QShader();
    solidShader = QShader();
    vertexColourShader = QShader();
    lambertShader = QShader();
    unlitShader = QShader();

    bool ok = load(QStringLiteral("rhi.vert"), vertexShader);

    if (customSolid) {
        solidVertexOverride = QShader::fromSerialized(customSolidVertex);
        if (!solidVertexOverride.isValid()) {
            shaderLog += QStringLiteral("QShader::fromSerialized rejected the supplied "
                                        "vertex shader for the solid program; ");
            ok = false;
        } else if (solidVertexOverride.stage() != QShader::VertexStage) {
            shaderLog += QStringLiteral("the supplied solid vertex shader is not a "
                                        "vertex shader; ");
            ok = false;
        }

        solidFragmentOverride = QShader::fromSerialized(customSolidFragment);
        if (!solidFragmentOverride.isValid()) {
            shaderLog += QStringLiteral("QShader::fromSerialized rejected the supplied "
                                        "fragment shader for the solid program; ");
            ok = false;
        } else if (solidFragmentOverride.stage() != QShader::FragmentStage) {
            shaderLog += QStringLiteral("the supplied solid fragment shader is not a "
                                        "fragment shader; ");
            ok = false;
        }
    } else {
        ok = load(QStringLiteral("rhi_solid.frag"), solidShader) && ok;
    }

    ok = load(QStringLiteral("rhi_vertex_colour.frag"), vertexColourShader) && ok;
    ok = load(QStringLiteral("rhi_lambert.frag"), lambertShader) && ok;
    ok = load(QStringLiteral("rhi_unlit.frag"), unlitShader) && ok;
    return ok;
}

bool RhiDevice::Impl::createRhi()
{
    QStringList tried;

#if defined(Q_OS_WIN)
    {
        QRhiD3D11InitParams params;
        std::unique_ptr<QRhi> candidate(QRhi::create(QRhi::D3D11, &params));
        if (candidate) {
            rhi = std::move(candidate);
            api = QStringLiteral("D3D11");
            return true;
        }
        tried << QStringLiteral("D3D11");
    }
#endif

#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
    {
        auto instance = std::make_unique<QVulkanInstance>();
        instance->setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
        if (instance->create()) {
            QRhiVulkanInitParams params;
            params.inst = instance.get();
            std::unique_ptr<QRhi> candidate(QRhi::create(QRhi::Vulkan, &params));
            if (candidate) {
                rhi = std::move(candidate);
                vulkanInstance = std::move(instance);
                api = QStringLiteral("Vulkan");
                return true;
            }
        }
        tried << QStringLiteral("Vulkan");
    }
#endif

#if QT_CONFIG(opengl)
    {
        QSurfaceFormat format;
        format.setDepthBufferSize(24);
        format.setStencilBufferSize(8);
        std::unique_ptr<QOffscreenSurface> surface(
            QRhiGles2InitParams::newFallbackSurface(format));
        if (surface) {
            surface->create();
            QRhiGles2InitParams params;
            params.format = format;
            params.fallbackSurface = surface.get();
            std::unique_ptr<QRhi> candidate(QRhi::create(QRhi::OpenGLES2, &params));
            if (candidate) {
                rhi = std::move(candidate);
                glSurface = std::move(surface);
                api = QStringLiteral("OpenGL");
                return true;
            }
        }
        tried << QStringLiteral("OpenGL");
    }
#endif

    backendFailure = tried.isEmpty()
        ? QStringLiteral("this build has no QRhi backend compiled in")
        : QStringLiteral("no QRhi backend could be created (tried %1)")
              .arg(tried.join(QStringLiteral(", ")));
    return false;
}

bool RhiDevice::Impl::createSharedResources()
{
    if (!rhi)
        return false;

    uniformStride = qMax(rhi->ubufAligned(kUniformSlotSize), kUniformSlotSize);
    uniformSlotCapacity = static_cast<quint32>(kMaxDrawsPerFrame);
    uniformBuffer = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                   static_cast<quint32>(uniformStride)
                                       * uniformSlotCapacity);
    if (!uniformBuffer || !uniformBuffer->create()) {
        appendFailure(QStringLiteral("could not allocate the uniform buffer"));
        return false;
    }

    vertexCapacityBytes = static_cast<quint32>(kInitialVertexCapacity * kVertexStride);
    vertexBuffer = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                  vertexCapacityBytes);
    if (!vertexBuffer || !vertexBuffer->create()) {
        appendFailure(QStringLiteral("could not allocate the vertex buffer"));
        return false;
    }

    dummyImage = QImage(1, 1, QImage::Format_RGBA8888);
    dummyImage.fill(Qt::white);
    dummyTexture = rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1));
    if (!dummyTexture || !dummyTexture->create()) {
        appendFailure(QStringLiteral("could not allocate the 1x1 white texture"));
        return false;
    }
    dummyTextureNeedsUpload = true;

    dummySampler = rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest,
                                   QRhiSampler::None, QRhiSampler::ClampToEdge,
                                   QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    if (!dummySampler || !dummySampler->create()) {
        appendFailure(QStringLiteral("could not create the 1x1 white sampler"));
        return false;
    }

    const auto stages = QRhiShaderResourceBinding::VertexStage
        | QRhiShaderResourceBinding::FragmentStage;

    plainBindings = rhi->newShaderResourceBindings();
    plainBindings->setBindings({QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
        0, stages, uniformBuffer, kUniformBlockSize)});
    if (!plainBindings->create()) {
        appendFailure(QStringLiteral("could not create the visible/bindings layout"));
        return false;
    }

    dummyTexturedBindings = rhi->newShaderResourceBindings();
    dummyTexturedBindings->setBindings(
        {QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
             0, stages, uniformBuffer, kUniformBlockSize),
         QRhiShaderResourceBinding::sampledTexture(
             1, QRhiShaderResourceBinding::FragmentStage, dummyTexture, dummySampler)});
    if (!dummyTexturedBindings->create()) {
        appendFailure(QStringLiteral("could not create the textured bindings layout"));
        return false;
    }

    return true;
}

bool RhiDevice::Impl::ensureVertexCapacity(quint32 bytes)
{
    if (!rhi)
        return false;
    if (vertexBuffer && vertexCapacityBytes >= bytes)
        return true;

    quint32 capacity = vertexCapacityBytes > 0 ? vertexCapacityBytes
                                               : static_cast<quint32>(kInitialVertexCapacity
                                                                      * kVertexStride);
    while (capacity < bytes) {
        const quint32 grown = capacity * 2;
        if (grown <= capacity) {
            appendFailure(QStringLiteral("the vertex buffer would exceed 4 GiB"));
            return false;
        }
        capacity = grown;
    }

    auto* replacement = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                       capacity);
    if (!replacement || !replacement->create()) {
        appendFailure(QStringLiteral("could not grow the vertex buffer to %1 bytes")
                          .arg(capacity));
        if (replacement) {
            replacement->destroy();
            delete replacement;
        }
        return false;
    }

    if (vertexBuffer) {
        vertexBuffer->destroy();
        delete vertexBuffer;
    }
    vertexBuffer = replacement;
    vertexCapacityBytes = capacity;
    return true;
}

bool RhiDevice::Impl::ensureResources(const QSize& size)
{
    if (!rhi)
        return false;
    if (renderTarget && targetSize == size) {
        PERIODICITY_RHI_LOG("rhi ensureResources: reuse %dx%d generation=%llu",
                            size.width(), size.height(),
                            static_cast<unsigned long long>(generation));
        return true;
    }
    if (size.isEmpty()) {
        appendFailure(QStringLiteral("cannot allocate a 0-pixel offscreen target"));
        return false;
    }

    releasePipelines();
    releaseResources();

    colourTexture = rhi->newTexture(
        QRhiTexture::RGBA8, size, 1,
        QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
    if (!colourTexture || !colourTexture->create()) {
        appendFailure(QStringLiteral("could not allocate the %1x%2 colour texture")
                          .arg(size.width())
                          .arg(size.height()));
        releaseResources();
        return false;
    }

    depthBuffer = rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1,
                                       {}, QRhiTexture::D24S8);
    if (!depthBuffer || !depthBuffer->create()) {
        appendFailure(QStringLiteral("could not allocate the %1x%2 depth buffer")
                          .arg(size.width())
                          .arg(size.height()));
        releaseResources();
        return false;
    }

    // Brace initialisation, not parentheses: with parentheses this line is a
    // function declaration ("most vexing parse"), which is why the two errors
    // that followed pointed at setDepthStencilBuffer and at the constructor.
    QRhiTextureRenderTargetDescription description{QRhiColorAttachment(colourTexture)};
    description.setDepthStencilBuffer(depthBuffer);
    // Each pass loads the colour attachment, because the target image is
    // uploaded into it and later passes continue from the read-back result. The
    // depth buffer is cleared by every pass (the software rasterizer clears it
    // on every submit as well).
    renderTarget = rhi->newTextureRenderTarget(
        description, QRhiTextureRenderTarget::PreserveColorContents);
    if (!renderTarget) {
        appendFailure(QStringLiteral("could not create the offscreen render target"));
        releaseResources();
        return false;
    }

    renderPass = renderTarget->newCompatibleRenderPassDescriptor();
    if (!renderPass) {
        appendFailure(QStringLiteral("could not create the offscreen render pass"));
        releaseResources();
        return false;
    }
    renderTarget->setRenderPassDescriptor(renderPass);

    if (!renderTarget->create()) {
        appendFailure(QStringLiteral("could not realise the offscreen render target"));
        releaseResources();
        return false;
    }

    targetSize = size;
    ++generation;
    PERIODICITY_RHI_LOG("rhi ensureResources: allocated %dx%d generation=%llu",
                        size.width(), size.height(),
                        static_cast<unsigned long long>(generation));
    return true;
}

void RhiDevice::Impl::releasePipelines()
{
    for (QRhiGraphicsPipeline* pipeline : pipelines) {
        if (!pipeline)
            continue;
        pipeline->destroy();
        delete pipeline;
    }
    pipelines.clear();
}

void RhiDevice::Impl::releaseResources()
{
    releasePipelines();

    if (renderTarget) {
        renderTarget->destroy();
        delete renderTarget;
        renderTarget = nullptr;
    }
    if (renderPass) {
        renderPass->destroy();
        delete renderPass;
        renderPass = nullptr;
    }
    if (colourTexture) {
        colourTexture->destroy();
        delete colourTexture;
        colourTexture = nullptr;
    }
    if (depthBuffer) {
        depthBuffer->destroy();
        delete depthBuffer;
        depthBuffer = nullptr;
    }

    targetSize = QSize();
}

void RhiDevice::Impl::releaseAll()
{
    releaseResources();

    for (QRhiShaderResourceBindings* bindings :
         {plainBindings, dummyTexturedBindings}) {
        if (bindings) {
            bindings->destroy();
            delete bindings;
        }
    }
    plainBindings = nullptr;
    dummyTexturedBindings = nullptr;

    for (QRhiSampler* sampler : samplers) {
        if (!sampler)
            continue;
        sampler->destroy();
        delete sampler;
    }
    samplers.clear();

    if (dummySampler) {
        dummySampler->destroy();
        delete dummySampler;
        dummySampler = nullptr;
    }
    if (dummyTexture) {
        dummyTexture->destroy();
        delete dummyTexture;
        dummyTexture = nullptr;
    }
    if (vertexBuffer) {
        vertexBuffer->destroy();
        delete vertexBuffer;
        vertexBuffer = nullptr;
    }
    if (uniformBuffer) {
        uniformBuffer->destroy();
        delete uniformBuffer;
        uniformBuffer = nullptr;
    }

    vertexCapacityBytes = 0;
    uniformSlotCapacity = 0;
    dummyTextureNeedsUpload = false;
    api.clear();

    rhi.reset();
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
    vulkanInstance.reset();
#endif
#if QT_CONFIG(opengl)
    glSurface.reset();
#endif

    initialized = false;
    available = false;
}

void RhiDevice::Impl::releaseRun(Run& run)
{
    for (const TextureUpload& upload : run.textures) {
        if (upload.texture)
            upload.texture->deleteLater();
    }
    for (QRhiShaderResourceBindings* bindings : run.temporaryBindings) {
        if (bindings)
            bindings->deleteLater();
    }
    run.clear();
}

QRhiSampler* RhiDevice::Impl::samplerFor(gfx::Wrap wrap, gfx::Filter filter)
{
    QByteArray key = QByteArray::number(static_cast<int>(wrap));
    key += ':';
    key += QByteArray::number(static_cast<int>(filter));

    const auto existing = samplers.constFind(key);
    if (existing != samplers.cend())
        return existing.value();

    const QRhiSampler::Filter filtering =
        filter == gfx::Filter::Nearest ? QRhiSampler::Nearest : QRhiSampler::Linear;
    const QRhiSampler::Filter mipmap =
        filter == gfx::Filter::MipmapBilinear ? QRhiSampler::Linear : QRhiSampler::None;

    QRhiSampler::AddressMode address = QRhiSampler::ClampToEdge;
    switch (wrap) {
    case gfx::Wrap::Clamp:
        address = QRhiSampler::ClampToEdge;
        break;
    case gfx::Wrap::Repeat:
        address = QRhiSampler::Repeat;
        break;
    case gfx::Wrap::Mirror:
        address = QRhiSampler::Mirror;
        break;
    }

    auto* sampler = rhi->newSampler(filtering, filtering, mipmap, address, address,
                                    address);
    if (!sampler || !sampler->create()) {
        appendFailure(QStringLiteral("could not create a texture sampler"));
        if (sampler) {
            sampler->destroy();
            delete sampler;
        }
        return nullptr;
    }

    samplers.insert(key, sampler);
    return sampler;
}

QRhiTexture* RhiDevice::Impl::createTexture(const QImage& image, Run& run)
{
    if (!rhi || image.isNull()) {
        appendFailure(QStringLiteral("refused to upload a null texture image"));
        return nullptr;
    }

    // Straight RGBA: the unlit fragment stage premultiplies, exactly like the
    // OpenGL device's GL_RGBA8 upload.
    const QImage source = image.convertToFormat(QImage::Format_RGBA8888);
    if (source.isNull()) {
        appendFailure(QStringLiteral("could not convert a texture image to RGBA8888"));
        return nullptr;
    }

    auto* texture = rhi->newTexture(QRhiTexture::RGBA8, source.size());
    if (!texture || !texture->create()) {
        appendFailure(QStringLiteral("could not allocate a %1x%2 texture")
                          .arg(source.width())
                          .arg(source.height()));
        if (texture) {
            texture->destroy();
            delete texture;
        }
        return nullptr;
    }

    TextureUpload upload;
    upload.texture = texture;
    upload.image = source;
    run.textures.push_back(upload);
    return texture;
}

QRhiShaderResourceBindings* RhiDevice::Impl::bindingsFor(QRhiTexture* texture,
                                                         QRhiSampler* sampler,
                                                         Run& run)
{
    if (!texture || !sampler || !uniformBuffer) {
        appendFailure(QStringLiteral("cannot bind a textured draw without its resources"));
        return nullptr;
    }

    const auto stages = QRhiShaderResourceBinding::VertexStage
        | QRhiShaderResourceBinding::FragmentStage;

    auto* bindings = rhi->newShaderResourceBindings();
    bindings->setBindings(
        {QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
             0, stages, uniformBuffer, kUniformBlockSize),
         QRhiShaderResourceBinding::sampledTexture(
             1, QRhiShaderResourceBinding::FragmentStage, texture, sampler)});
    if (!bindings->create()) {
        appendFailure(QStringLiteral("could not create per-texture shader bindings"));
        bindings->destroy();
        delete bindings;
        return nullptr;
    }

    run.temporaryBindings.push_back(bindings);
    return bindings;
}

QRhiGraphicsPipeline* RhiDevice::Impl::pipelineFor(const QString& shader,
                                                   gfx::PrimitiveTopology topology,
                                                   const gfx::Pipeline& state,
                                                   bool is3D, bool textured)
{
    if (!rhi || !renderPass) {
        appendFailure(QStringLiteral("the RHI device has no render pass to build against"));
        return nullptr;
    }

    const QByteArray key =
        pipelineKey(shader, topology, state, is3D, textured, generation);
    const auto existing = pipelines.constFind(key);
    if (existing != pipelines.cend()) {
        PERIODICITY_RHI_LOG(
            "rhi pipeline: cache hit shader=%s topology=%d is3D=%d textured=%d -> %p",
            qPrintable(shader), static_cast<int>(topology), is3D ? 1 : 0,
            textured ? 1 : 0, static_cast<void*>(existing.value()));
        return existing.value();
    }

    const bool hasOverride = shader == QStringLiteral("solid")
        && solidVertexOverride.isValid() && solidFragmentOverride.isValid();
    const QShader& fragment = fragmentShaderFor(shader);
    if (!hasOverride && !fragment.isValid()) {
        appendFailure(QStringLiteral("the RHI device has no program for shader '%1'")
                          .arg(shader));
        return nullptr;
    }

    auto* pipeline = rhi->newGraphicsPipeline();
    if (!pipeline) {
        appendFailure(QStringLiteral("could not allocate a graphics pipeline"));
        return nullptr;
    }

    const QShader& vertex = hasOverride ? solidVertexOverride : vertexShader;
    const QShader& fragmentStageShader = hasOverride ? solidFragmentOverride : fragment;

    QRhiShaderStage vertexStage(QRhiShaderStage::Vertex, vertex);
    QRhiShaderStage fragmentStage(QRhiShaderStage::Fragment, fragmentStageShader);
    pipeline->setShaderStages({vertexStage, fragmentStage});

    QRhiVertexInputLayout layout;
    layout.setBindings({QRhiVertexInputBinding(kVertexStride)});
    layout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float3, 0),
        QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float3,
                                 3 * sizeof(float)),
        QRhiVertexInputAttribute(0, 2, QRhiVertexInputAttribute::Float4,
                                 6 * sizeof(float)),
        QRhiVertexInputAttribute(0, 3, QRhiVertexInputAttribute::Float2,
                                 10 * sizeof(float)),
    });
    pipeline->setVertexInputLayout(layout);

    // The pipeline cache is keyed by the gfx pipeline state, including the
    // topology the command was recorded with.
    pipeline->setTopology(rhiTopology(topology));

    // gfx treats a counter-clockwise triangle in clip space as front-facing,
    // which is counter-clockwise in a Y-up framebuffer and clockwise in a Y-down
    // one. QRhi's front face is expressed in framebuffer coordinates.
    pipeline->setCullMode(is3D && state.raster.cullBack ? QRhiGraphicsPipeline::Back
                                                        : QRhiGraphicsPipeline::None);
    pipeline->setFrontFace(rhi->isYUpInFramebuffer() ? QRhiGraphicsPipeline::CCW
                                                     : QRhiGraphicsPipeline::CW);
    pipeline->setTargetBlends({targetBlend(state.blend)});
    pipeline->setDepthTest(is3D && state.depth.test);
    pipeline->setDepthWrite(is3D && state.depth.write);
    pipeline->setDepthOp(QRhiGraphicsPipeline::Less);
    pipeline->setRenderPassDescriptor(renderPass);
    pipeline->setShaderResourceBindings(textured ? dummyTexturedBindings : plainBindings);
    // Every draw sets a scissor; the flag tells QRhi the pipeline expects it.
    pipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);

    if (!pipeline->create()) {
        appendFailure(QStringLiteral("could not create the RHI pipeline for shader '%1'")
                          .arg(shader));
        pipeline->destroy();
        delete pipeline;
        return nullptr;
    }

    pipelines.insert(key, pipeline);
    PERIODICITY_RHI_LOG(
        "rhi pipeline: created shader=%s topology=%d is3D=%d depthTest=%d depthWrite=%d "
        "textured=%d generation=%llu vsValid=%d fsValid=%d fsStage=%d pass=%p -> %p",
        qPrintable(shader), static_cast<int>(topology), is3D ? 1 : 0,
        (is3D && state.depth.test) ? 1 : 0, (is3D && state.depth.write) ? 1 : 0,
        textured ? 1 : 0, static_cast<unsigned long long>(generation),
        vertexStage.shader().isValid() ? 1 : 0,
        fragmentStage.shader().isValid() ? 1 : 0,
        static_cast<int>(fragmentStage.shader().stage()),
        static_cast<void*>(renderPass), static_cast<void*>(pipeline));
    return pipeline;
}

bool RhiDevice::Impl::appendDraw(Run& run, const gfx::DrawCommand& command,
                                 const QRect& viewport, const QRect& clip,
                                 const QSize& targetPixels, gfx::RasterStats& stats)
{
    PERIODICITY_RHI_LOG(
        "rhi appendDraw: kind=%d viewport=(%d,%d %dx%d) clip=(%d,%d %dx%d) target=%dx%d",
        static_cast<int>(command.kind), viewport.x(), viewport.y(), viewport.width(),
        viewport.height(), clip.x(), clip.y(), clip.width(), clip.height(),
        targetPixels.width(), targetPixels.height());

    const bool imageDraw = command.kind == gfx::CommandKind::DrawImage;

    // A DrawImage carries no gfx::Pipeline: it is the device's own textured
    // blit, exactly like GlDevice's drawImage(), so it uses the built-in unlit
    // program regardless of command.pipeline.
    if (!imageDraw && !command.pipeline.isValid())
        return true;

    const QString shaderName = imageDraw ? QStringLiteral("unlit")
                                         : command.pipeline.shader.name();
    const bool is3D = command.kind == gfx::CommandKind::DrawMesh3D;
    const bool unlit = shaderName == QStringLiteral("unlit");

    PERIODICITY_RHI_LOG(
        "rhi appendDraw: is3D=%d shader=%s image=%d pipelineValid=%d mesh2DValid=%d",
        is3D ? 1 : 0, qPrintable(shaderName), imageDraw ? 1 : 0,
        command.pipeline.isValid() ? 1 : 0,
        command.kind == gfx::CommandKind::DrawMesh2D
            ? (command.mesh2d.isValid() ? 1 : 0) : -1);

    QRhiTexture* texture = nullptr;
    QRhiSampler* sampler = dummySampler;

    // 2D and image vertices are converted to OpenGL-style NDC here, on the CPU,
    // so the vertex shader has a single path. uMvp stays identity for them and
    // no per-draw target-size uniform is involved in the transform.
    const auto toClip = [&targetPixels](const QPointF& pixel) {
        const qreal width = qMax(1, targetPixels.width());
        const qreal height = qMax(1, targetPixels.height());
        return QPointF(2.0 * pixel.x() / width - 1.0,
                       1.0 - 2.0 * pixel.y() / height);
    };

    std::vector<RhiVertex> vertices;
    QRect drawViewport = targetPixels.isEmpty()
        ? QRect()
        : QRect(QPoint(0, 0), targetPixels);
    QRect drawScissor = clip;

    switch (command.kind) {
    case gfx::CommandKind::DrawMesh2D: {
        if (!command.mesh2d.isValid())
            return true;

        const gfx::Texture source = command.uniforms.texture(QStringLiteral("texture"));
        if (unlit && source.isValid()) {
            sampler = samplerFor(source.wrap(), source.filter());
            if (!sampler)
                return false;
            texture = createTexture(source.image(), run);
            if (!texture)
                return false;
        }

        vertices.reserve(static_cast<std::size_t>(command.mesh2d.indices().size()));
        for (const quint32 index : command.mesh2d.indices()) {
            const gfx::Vertex2D& sourceVertex =
                command.mesh2d.vertices().at(static_cast<qsizetype>(index));
            vertices.push_back(makeVertex2D(
                toClip(QPointF(viewport.x() + sourceVertex.position.x(),
                               viewport.y() + sourceVertex.position.y())),
                sourceVertex.colour, sourceVertex.uv));
        }
        break;
    }

    case gfx::CommandKind::DrawMesh3D: {
        if (!command.mesh3d.isValid())
            return true;

        const gfx::Texture source = command.uniforms.texture(QStringLiteral("texture"));
        if (unlit && source.isValid()) {
            sampler = samplerFor(source.wrap(), source.filter());
            if (!sampler)
                return false;
            texture = createTexture(source.image(), run);
            if (!texture)
                return false;
        }

        vertices.reserve(static_cast<std::size_t>(command.mesh3d.indices().size()));
        for (const quint32 index : command.mesh3d.indices())
            vertices.push_back(makeVertex3D(
                command.mesh3d.vertices().at(static_cast<qsizetype>(index))));

        drawViewport = viewport;
        break;
    }

    case gfx::CommandKind::DrawImage: {
        if (command.image.isNull())
            return true;

        const QRect destination = command.viewport.isEmpty()
            ? QRect(0, 0, command.image.width(), command.image.height())
            : command.viewport;
        if (destination.width() <= 0 || destination.height() <= 0)
            return true;

        sampler = samplerFor(gfx::Wrap::Clamp, gfx::Filter::Bilinear);
        if (!sampler)
            return false;
        texture = createTexture(command.image, run);
        if (!texture)
            return false;

        const QPointF topLeft = toClip(QPointF(destination.x(), destination.y()));
        const QPointF bottomRight = toClip(
            QPointF(destination.x() + destination.width(),
                    destination.y() + destination.height()));
        vertices.push_back(makeVertex2D(topLeft, QColor(Qt::white), QPointF(0.0, 0.0)));
        vertices.push_back(makeVertex2D(QPointF(bottomRight.x(), topLeft.y()),
                                        QColor(Qt::white), QPointF(1.0, 0.0)));
        vertices.push_back(makeVertex2D(bottomRight, QColor(Qt::white),
                                        QPointF(1.0, 1.0)));
        vertices.push_back(makeVertex2D(topLeft, QColor(Qt::white), QPointF(0.0, 0.0)));
        vertices.push_back(makeVertex2D(bottomRight, QColor(Qt::white),
                                        QPointF(1.0, 1.0)));
        vertices.push_back(makeVertex2D(QPointF(topLeft.x(), bottomRight.y()),
                                        QColor(Qt::white), QPointF(0.0, 1.0)));
        break;
    }

    default:
        return true;
    }

    if (vertices.empty() || clip.isEmpty())
        return true;

    PERIODICITY_RHI_LOG("rhi appendDraw: vertices=%d clipEmpty=%d",
                        static_cast<int>(vertices.size()), clip.isEmpty() ? 1 : 0);

    const bool hasTexture = texture != nullptr;
    // The unlit fragment stage always declares the sampler at binding 1, so an
    // unlit pipeline always needs the two-binding layout — the 1x1 white dummy
    // stands in when the draw has no texture of its own.
    const bool texturedLayout = unlit;
    const gfx::PrimitiveTopology topology = command.kind == gfx::CommandKind::DrawMesh3D
        ? command.mesh3d.topology()
        : (command.kind == gfx::CommandKind::DrawMesh2D
               ? command.mesh2d.topology()
               : gfx::PrimitiveTopology::Triangles);

    QRhiGraphicsPipeline* pipeline =
        pipelineFor(shaderName, topology, command.pipeline, is3D, texturedLayout);
    if (!pipeline)
        return false;

    GpuDraw draw;
    draw.pipeline = pipeline;
    draw.bindings = unlit
        ? (hasTexture ? bindingsFor(texture, sampler, run) : dummyTexturedBindings)
        : plainBindings;
    if (!draw.bindings)
        return false;

    if (run.draws.size() >= static_cast<std::size_t>(uniformSlotCapacity)) {
        appendFailure(QStringLiteral("the command buffer has more than %1 GPU draws")
                          .arg(uniformSlotCapacity));
        return false;
    }

    UniformData uniform;
    uniform.setMatrix(kUniformClip, rhi->clipSpaceCorrMatrix());
    // 2D and image vertices are already in clip space, so their transform is the
    // identity; Mesh3D uses the command's mvp.
    uniform.setMatrix(kUniformMvp,
                      is3D ? command.uniforms.matrix(QStringLiteral("mvp"), QMatrix4x4())
                           : QMatrix4x4());
    uniform.setColour(kUniformColour,
                      command.uniforms.colour(QStringLiteral("colour"), QColor(Qt::white)));
    uniform.setColour(kUniformBaseColour,
                      command.uniforms.colour(QStringLiteral("baseColour"),
                                              QColor(Qt::white)));
    uniform.setVector4(kUniformTargetSize, static_cast<float>(targetPixels.width()),
                       static_cast<float>(targetPixels.height()), 0.0F, 0.0F);

    QVector3D light = command.uniforms.vector(QStringLiteral("lightDirection"),
                                              QVector3D(0.0F, 0.0F, 1.0F));
    if (light.lengthSquared() <= 0.0F)
        light = QVector3D(0.0F, 0.0F, 1.0F);
    else
        light.normalize();
    uniform.setVector4(kUniformLightDirection, light.x(), light.y(), light.z(), 0.0F);

    const qreal ambient = qBound(0.0, command.uniforms.real(QStringLiteral("ambient"), 0.2),
                                 1.0);
    uniform.setVector4(kUniformParams, static_cast<float>(ambient),
                       hasTexture ? 1.0F : 0.0F, is3D ? 1.0F : 0.0F, 0.0F);

    draw.uniformSlot = static_cast<quint32>(run.draws.size());
    draw.firstVertex = static_cast<quint32>(run.vertices.size());
    draw.vertexCount = static_cast<quint32>(vertices.size());
    draw.viewport = drawViewport;
    draw.scissor = drawScissor;

    run.vertices.insert(run.vertices.end(), vertices.begin(), vertices.end());
    run.uniforms.push_back(uniform);
    run.draws.push_back(draw);

    PERIODICITY_RHI_LOG(
        "rhi appendDraw: RECORDED slot=%u firstVertex=%u count=%u pipeline=%p bindings=%p "
        "plain=%p v0=(%f,%f,%f) colour=(%f,%f,%f,%f) params=(%f,%f,%f,%f)",
        draw.uniformSlot, draw.firstVertex, draw.vertexCount,
        static_cast<void*>(pipeline), static_cast<void*>(draw.bindings),
        static_cast<void*>(plainBindings), static_cast<double>(vertices.front().x),
        static_cast<double>(vertices.front().y), static_cast<double>(vertices.front().z),
        static_cast<double>(vertices.front().r), static_cast<double>(vertices.front().g),
        static_cast<double>(vertices.front().b), static_cast<double>(vertices.front().a),
        static_cast<double>(uniform.words[48]), static_cast<double>(uniform.words[49]),
        static_cast<double>(uniform.words[50]), static_cast<double>(uniform.words[51]));

    if (imageDraw)
        stats.pixelsWritten += clip.width() * clip.height();

    return true;
}

bool RhiDevice::Impl::executeRun(QImage& image, Run& run, const QSize& targetPixels)
{
    if (!rhi || !renderTarget || run.draws.empty())
        return true;

    const quint32 vertexBytes =
        static_cast<quint32>(run.vertices.size() * kVertexStride);
    if (!ensureVertexCapacity(vertexBytes))
        return false;

    if (!colourTexture || targetSize != targetPixels) {
        appendFailure(QStringLiteral("the offscreen target is not sized for this frame"));
        return false;
    }

    PERIODICITY_RHI_LOG(
        "rhi executeRun: target=%dx%d draws=%d vertexBytes=%u vertexStride=%d "
        "uniformStride=%d api=%s",
        targetPixels.width(), targetPixels.height(),
        static_cast<int>(run.draws.size()), vertexBytes, kVertexStride,
        uniformStride, qPrintable(api));

    QRhiCommandBuffer* commandBuffer = nullptr;
    const QRhi::FrameOpResult begin = rhi->beginOffscreenFrame(&commandBuffer);
    if (begin != QRhi::FrameOpSuccess || !commandBuffer) {
        appendFailure(QStringLiteral("QRhi::beginOffscreenFrame() failed (%1)")
                          .arg(static_cast<int>(begin)));
        return false;
    }

    // The colour attachment starts as the caller's image. It is uploaded here,
    // before the pass, so PreserveColorContents loads exactly those pixels.
    const QImage upload = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if (upload.isNull()) {
        appendFailure(QStringLiteral("could not convert the target image for upload"));
        rhi->endOffscreenFrame();
        return false;
    }

    QRhiResourceUpdateBatch* uploads = rhi->nextResourceUpdateBatch();
    {
        QRhiTextureSubresourceUploadDescription subresource(
            upload.constBits(), static_cast<quint32>(upload.sizeInBytes()));
        subresource.setDataStride(static_cast<quint32>(upload.bytesPerLine()));
        QRhiTextureUploadDescription description(
            QRhiTextureUploadEntry(0, 0, subresource));
        uploads->uploadTexture(colourTexture, description);
    }

    if (dummyTextureNeedsUpload) {
        QRhiTextureSubresourceUploadDescription subresource(
            dummyImage.constBits(), static_cast<quint32>(dummyImage.sizeInBytes()));
        subresource.setDataStride(static_cast<quint32>(dummyImage.bytesPerLine()));
        uploads->uploadTexture(dummyTexture,
                               QRhiTextureUploadDescription(
                                   QRhiTextureUploadEntry(0, 0, subresource)));
        dummyTextureNeedsUpload = false;
    }

    uploads->updateDynamicBuffer(vertexBuffer, 0, vertexBytes, run.vertices.data());
    for (std::size_t slot = 0; slot < run.uniforms.size(); ++slot) {
        uploads->updateDynamicBuffer(
            uniformBuffer, static_cast<quint32>(slot) * static_cast<quint32>(uniformStride),
            kUniformBlockSize, run.uniforms[slot].words.data());
    }

    for (const TextureUpload& texture : run.textures) {
        QRhiTextureSubresourceUploadDescription subresource(
            texture.image.constBits(),
            static_cast<quint32>(texture.image.sizeInBytes()));
        subresource.setDataStride(static_cast<quint32>(texture.image.bytesPerLine()));
        uploads->uploadTexture(texture.texture,
                               QRhiTextureUploadDescription(
                                   QRhiTextureUploadEntry(0, 0, subresource)));
    }

    QRhiReadbackResult readback;
    readback.completed = [] {};
    QRhiResourceUpdateBatch* readbackBatch = rhi->nextResourceUpdateBatch();
    readbackBatch->readBackTexture(QRhiReadbackDescription(colourTexture), &readback);

    commandBuffer->resourceUpdate(uploads);
    commandBuffer->beginPass(renderTarget, QColor(Qt::transparent),
                             QRhiDepthStencilClearValue(1.0F, 0));

    for (const GpuDraw& draw : run.draws) {
#if PERIODICITY_RHI_TRACE
        static const float zeroWords[64] = {};
        const float* uniformWords = draw.uniformSlot < run.uniforms.size()
            ? run.uniforms[draw.uniformSlot].words.data()
            : zeroWords;
        const RhiVertex emptyVertex;
        const RhiVertex& firstVertex = draw.firstVertex < run.vertices.size()
            ? run.vertices[draw.firstVertex]
            : emptyVertex;

        PERIODICITY_RHI_LOG(
            "rhi draw: count=%u first=%u v0=(%f,%f,%f) vp=(%d,%d %dx%d) "
            "sc=(%d,%d %dx%d) pipeline=%p bindings=%p vb=%p target=(%f,%f) "
            "params=(%f,%f,%f,%f) uClipDiag=(%f,%f,%f,%f) uMvpDiag=(%f,%f,%f,%f)",
            draw.vertexCount, draw.firstVertex, static_cast<double>(firstVertex.x),
            static_cast<double>(firstVertex.y), static_cast<double>(firstVertex.z),
            draw.viewport.x(), draw.viewport.y(), draw.viewport.width(),
            draw.viewport.height(), draw.scissor.x(), draw.scissor.y(),
            draw.scissor.width(), draw.scissor.height(),
            static_cast<void*>(draw.pipeline), static_cast<void*>(draw.bindings),
            static_cast<void*>(vertexBuffer), static_cast<double>(uniformWords[40]),
            static_cast<double>(uniformWords[41]), static_cast<double>(uniformWords[48]),
            static_cast<double>(uniformWords[49]), static_cast<double>(uniformWords[50]),
            static_cast<double>(uniformWords[51]), static_cast<double>(uniformWords[0]),
            static_cast<double>(uniformWords[5]), static_cast<double>(uniformWords[10]),
            static_cast<double>(uniformWords[15]), static_cast<double>(uniformWords[16]),
            static_cast<double>(uniformWords[21]), static_cast<double>(uniformWords[26]),
            static_cast<double>(uniformWords[31]));
#endif

        const QRhiCommandBuffer::DynamicOffset offset(
            0, draw.uniformSlot * static_cast<quint32>(uniformStride));
        const QRhiCommandBuffer::VertexInput input(
            vertexBuffer, draw.firstVertex * static_cast<quint32>(kVertexStride));

        commandBuffer->setGraphicsPipeline(draw.pipeline);
        commandBuffer->setShaderResources(draw.bindings, 1, &offset);
        commandBuffer->setVertexInput(0, 1, &input);
        commandBuffer->setViewport(QRhiViewport(
            static_cast<float>(draw.viewport.x()),
            static_cast<float>(draw.viewport.y()),
            static_cast<float>(draw.viewport.width()),
            static_cast<float>(draw.viewport.height())));
        commandBuffer->setScissor(QRhiScissor(draw.scissor.x(), draw.scissor.y(),
                                              draw.scissor.width(),
                                              draw.scissor.height()));
        commandBuffer->draw(draw.vertexCount);
    }

    commandBuffer->endPass(readbackBatch);

    const QRhi::FrameOpResult end = rhi->endOffscreenFrame();
    if (end != QRhi::FrameOpSuccess) {
        appendFailure(QStringLiteral("QRhi::endOffscreenFrame() failed (%1)")
                          .arg(static_cast<int>(end)));
        return false;
    }

    // Read-backs complete during the frame's execution; finish() is what makes
    // the data available before submit() returns.
    const QRhi::FrameOpResult finished = rhi->finish();
    if (finished != QRhi::FrameOpSuccess) {
        appendFailure(QStringLiteral("QRhi::finish() failed (%1)")
                          .arg(static_cast<int>(finished)));
        return false;
    }

#if PERIODICITY_RHI_TRACE
    if (!readback.data.isEmpty() && !readback.pixelSize.isEmpty()) {
        const uchar* pixels = reinterpret_cast<const uchar*>(readback.data.constData());
        const int width = readback.pixelSize.width();
        const int height = readback.pixelSize.height();
        const int centre = ((height / 2) * width + width / 2) * 4;
        if (readback.data.size() >= centre + 4) {
            PERIODICITY_RHI_LOG(
                "rhi readback: format=%d %dx%d first=(%u,%u,%u,%u) centre=(%u,%u,%u,%u)",
                static_cast<int>(readback.format), width, height,
                static_cast<unsigned>(pixels[0]), static_cast<unsigned>(pixels[1]),
                static_cast<unsigned>(pixels[2]), static_cast<unsigned>(pixels[3]),
                static_cast<unsigned>(pixels[centre]),
                static_cast<unsigned>(pixels[centre + 1]),
                static_cast<unsigned>(pixels[centre + 2]),
                static_cast<unsigned>(pixels[centre + 3]));
        }
    }
#endif

    const QImage result = imageFromReadback(readback, rhi->isYUpInFramebuffer());
    if (result.isNull()) {
        appendFailure(QStringLiteral("the RHI read-back produced no usable image"));
        return false;
    }

    const QImage converted = result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (converted.isNull()) {
        appendFailure(QStringLiteral("could not convert the read-back image"));
        return false;
    }

    image = converted;
    return true;
}

// ---------------------------------------------------------------------------
// RhiDevice public interface
// ---------------------------------------------------------------------------

RhiDevice::RhiDevice()
    : impl_(std::make_unique<Impl>())
{
    initialize();
}

RhiDevice::~RhiDevice()
{
    if (impl_)
        impl_->releaseAll();
}

bool RhiDevice::initialize()
{
    impl_->releaseAll();
    impl_->lastError.clear();
    impl_->shaderLog.clear();

    periodicityInitRenderShaderResources();

    if (!impl_->loadShaders()) {
        impl_->lastError = impl_->shaderLog.isEmpty()
            ? QStringLiteral("the RHI shaders could not be loaded")
            : impl_->shaderLog;
        return false;
    }

    if (!impl_->createRhi()) {
        impl_->lastError = impl_->backendFailure;
        return false;
    }

    if (!impl_->createSharedResources()) {
        impl_->releaseAll();
        return false;
    }

    // Validate one pipeline against a tiny offscreen target. This is what turns
    // "the shader data is unusable" or "this backend rejects the pipeline" into
    // a construction failure with a diagnostic, instead of a first-frame
    // surprise.
    if (!impl_->ensureResources(QSize(1, 1))) {
        impl_->releaseAll();
        return false;
    }

    gfx::Pipeline validation;
    validation.shader = gfx::ShaderLibrary::solid();
    validation.blend = gfx::BlendMode::None;
    validation.depth.test = false;
    validation.depth.write = false;
    validation.raster.cullBack = false;

    if (impl_->pipelineFor(QStringLiteral("solid"), gfx::PrimitiveTopology::Triangles,
                           validation, false, false) == nullptr) {
        if (impl_->lastError.isEmpty())
            impl_->lastError = impl_->shaderLog.isEmpty()
                ? QStringLiteral("the RHI device could not build its first pipeline")
                : impl_->shaderLog;
        impl_->releaseAll();
        return false;
    }

    impl_->initialized = true;
    impl_->available = true;
    return true;
}

bool RhiDevice::initializeWithSolidShaderData(const QByteArray& vertexShader,
                                              const QByteArray& fragmentShader)
{
    impl_->customSolid = true;
    impl_->customSolidVertex = vertexShader;
    impl_->customSolidFragment = fragmentShader;
    impl_->solidVertexOverride = QShader();
    impl_->solidFragmentOverride = QShader();
    return initialize();
}

bool RhiDevice::isInitialized() const
{
    return impl_->initialized;
}

QString RhiDevice::shaderLog() const
{
    return impl_->shaderLog;
}

QString RhiDevice::apiName() const
{
    return impl_->api;
}

gfx::RasterStats RhiDevice::submit(RenderTarget& target,
                                   const gfx::CommandBuffer& commands)
{
    Impl& device = *impl_;

    // The software device is the reference for invalid input too, so both
    // devices produce the same diagnostic and the same untouched target.
    if (!target.isValid()) {
        const gfx::RasterStats stats = device.software->submit(target, commands);
        device.lastError = device.software->lastError();
        return stats;
    }

    const QSize pixelSize = target.pixelSize();
    const qint64 pixels = static_cast<qint64>(pixelSize.width())
        * static_cast<qint64>(pixelSize.height());
    const gfx::RasterCaps caps;
    if (pixels > caps.maxTargetPixels || primitiveCount(commands) > caps.maxTriangles) {
        const gfx::RasterStats stats = device.software->submit(target, commands);
        device.lastError = device.software->lastError();
        return stats;
    }

    if (commands.isEmpty())
        return {};

    if (!device.available) {
        const gfx::RasterStats stats = device.software->submit(target, commands);
        if (device.lastError.isEmpty())
            device.lastError = device.software->lastError();
        return stats;
    }

    const auto fallback = [&device, &target, &commands]() {
        const gfx::RasterStats stats = device.software->submit(target, commands);
        if (device.lastError.isEmpty())
            device.lastError = device.software->lastError();
        return stats;
    };

    if (!device.ensureResources(pixelSize))
        return fallback();

    const QImage::Format targetFormat = target.image().format();
    QImage working = target.image().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (working.isNull()) {
        device.appendFailure(QStringLiteral("could not convert the target for the RHI device"));
        return fallback();
    }

    const gfx::CommandBuffer scaled = scaledCommands(commands, target.devicePixelRatio());
    const QRect targetRect(QPoint(0, 0), pixelSize);
    QRect scissor = targetRect;
    gfx::RasterStats stats;

    Run run;
    bool frameOk = true;

    const auto flush = [&]() -> bool {
        if (run.draws.empty())
            return true;
        const bool ok = device.executeRun(working, run, pixelSize);
        device.releaseRun(run);
        return ok;
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

        case gfx::CommandKind::Clear: {
            if (!flush()) {
                frameOk = false;
                break;
            }
            const QRect clip = clipFor(command, targetRect, scissor);
            clearImage(working, clip, command.colour);
            continue;
        }

        case gfx::CommandKind::DrawText:
        case gfx::CommandKind::DrawPath: {
            if (!flush()) {
                frameOk = false;
                break;
            }
            const QRect viewport = viewportFor(command, targetRect);
            const QRect clip = clipFor(command, targetRect, scissor);
            if (command.kind == gfx::CommandKind::DrawText)
                paintText(working, command, viewport, clip);
            else
                paintPath(working, command, viewport, clip);
            continue;
        }

        default:
            break;
        }

        const QRect viewport = viewportFor(command, targetRect);
        const QRect clip = clipFor(command, targetRect, scissor);
        PERIODICITY_RHI_LOG("rhi submit: kind=%d clip=(%d,%d %dx%d) empty=%d",
                            static_cast<int>(command.kind), clip.x(), clip.y(),
                            clip.width(), clip.height(), clip.isEmpty() ? 1 : 0);
        if (clip.isEmpty())
            continue;

        if (command.kind == gfx::CommandKind::DrawMesh2D
            || command.kind == gfx::CommandKind::DrawMesh3D) {
            countTriangles(command, viewport, stats);
        }

        if (!device.appendDraw(run, command, viewport, clip, pixelSize, stats)) {
            frameOk = false;
            break;
        }
    }

    if (frameOk && !flush())
        frameOk = false;

    if (!frameOk) {
        device.releaseRun(run);
        device.appendFailure(QStringLiteral("the frame fell back to the software device"));
        return fallback();
    }

    if (working.format() != targetFormat) {
        const QImage converted = working.convertToFormat(targetFormat);
        if (!converted.isNull())
            working = converted;
    }
    target.imageRef() = working;
    return stats;
}

DeviceCapabilities RhiDevice::capabilities() const
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

QString RhiDevice::name() const
{
    return QStringLiteral("rhi");
}

bool RhiDevice::isAvailable() const
{
    return impl_->available;
}

QString RhiDevice::lastError() const
{
    return impl_->lastError;
}

} // namespace md::render

#else // PERIODICITY_HAS_RHI

namespace md::render {

namespace {

QString noRhiReason()
{
    return QStringLiteral(
        "rhi/qrhi.h was not found at build time (PERIODICITY_HAS_RHI=0), so the "
        "RHI device is not compiled in; using the software device");
}

} // namespace

struct RhiDevice::Impl
{
    SoftwareDevice software;
    QString lastError = noRhiReason();
    QString shaderLog;
};

RhiDevice::RhiDevice()
    : impl_(std::make_unique<Impl>())
{
}

RhiDevice::~RhiDevice() = default;

bool RhiDevice::initialize()
{
    return false;
}

bool RhiDevice::initializeWithSolidShaderData(const QByteArray&, const QByteArray&)
{
    impl_->shaderLog = noRhiReason();
    return false;
}

bool RhiDevice::isInitialized() const
{
    return false;
}

QString RhiDevice::shaderLog() const
{
    return impl_->shaderLog;
}

QString RhiDevice::apiName() const
{
    return QString();
}

gfx::RasterStats RhiDevice::submit(RenderTarget& target,
                                   const gfx::CommandBuffer& commands)
{
    return impl_->software.submit(target, commands);
}

DeviceCapabilities RhiDevice::capabilities() const
{
    DeviceCapabilities capabilities;
    capabilities.backendName = name();
    return capabilities;
}

QString RhiDevice::name() const
{
    return QStringLiteral("rhi");
}

bool RhiDevice::isAvailable() const
{
    return false;
}

QString RhiDevice::lastError() const
{
    return impl_->lastError;
}

} // namespace md::render

#endif // PERIODICITY_HAS_RHI
