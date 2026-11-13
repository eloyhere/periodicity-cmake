// Periodicity — gfx layer: the software rasterizer.
//
// This is the only place in gfx that writes pixels. Everything it needs comes
// from the command buffer; nothing is cached between calls, so two identical
// submissions produce identical images.
//
// The fill rules are chosen so the result is reproducible:
//
//   - coverage uses barycentric edge functions normalised by the signed area,
//     with a top-left rule. A shared edge is therefore included by exactly one
//     of the two triangles that own it, whatever the floating point rounding.
//   - Mesh2D triangles are normalised to positive area and never culled.
//   - Mesh3D triangles are front-facing when their clip-space winding is
//     counter-clockwise, which is a negative screen-space area after the Y flip;
//     cullBack drops the rest before any fragment work.
//   - attributes use perspective-correct weights (1/w) for Mesh3D and plain
//     barycentric weights for Mesh2D, whose w is always 1.
//   - boundary pixels of an antialiased 2D triangle are 4x4 supersampled; the
//     coverage multiplies the source alpha, so an edge blends instead of
//     stair-stepping. Interior pixels take one sample.
//
// Text and paths are the documented temporary bridge: they are painted with
// QPainter straight onto the target. Phase 2 moves that code into
// render/TextPainter and render/PathRasterizer; nothing else here depends on it.

#include <md/gfx/rasterizer.h>

#include <md/gfx/shader.h>

#include <QDebug>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace md::gfx {

namespace {

bool isFinite(qreal value)
{
    return std::isfinite(static_cast<double>(value));
}

bool isFinite(const QPointF& point)
{
    return isFinite(point.x()) && isFinite(point.y());
}

qint64 primitiveCount(const Mesh2D& mesh)
{
    if (!mesh.isValid())
        return 0;

    const qint64 indices = mesh.indices().size();
    switch (mesh.topology()) {
    case PrimitiveTopology::Triangles:
        return indices / 3;
    case PrimitiveTopology::Lines:
        return indices / 2;
    case PrimitiveTopology::Points:
        return indices;
    }
    return 0;
}

qint64 primitiveCount(const Mesh3D& mesh)
{
    if (!mesh.isValid())
        return 0;

    const qint64 indices = mesh.indices().size();
    switch (mesh.topology()) {
    case PrimitiveTopology::Triangles:
        return indices / 3;
    case PrimitiveTopology::Lines:
        return indices / 2;
    case PrimitiveTopology::Points:
        return indices;
    }
    return 0;
}

// A vertex after the vertex stage: screen position, depth in [0, 1], the
// reciprocal clip w for perspective correction, and the interpolants.
struct ScreenVertex
{
    QPointF screen;
    qreal depth = 0.0;
    qreal invW = 1.0;
    QColor colour = QColor(Qt::white);
    QPointF uv;
    QVector3D normal;
    QVector3D world;
};

// Edge function of the directed edge a -> b at p. Positive on one side.
qreal edge(const QPointF& a, const QPointF& b, const QPointF& p)
{
    return (b.x() - a.x()) * (p.y() - a.y())
        - (b.y() - a.y()) * (p.x() - a.x());
}

qreal edgeLength(const QPointF& a, const QPointF& b)
{
    return std::hypot(b.x() - a.x(), b.y() - a.y());
}

// Inclusion rule for a sample exactly on an edge. Chosen so that a shared edge
// is included by exactly one of the two triangles that share it: horizontal
// edges going right are top edges, non-horizontal edges going up are left
// edges, in the triangle's positive-area winding.
bool isTopLeft(const QPointF& a, const QPointF& b)
{
    if (a.y() == b.y())
        return b.x() > a.x();
    return b.y() < a.y();
}

bool coversEdge(qreal value, bool topLeft)
{
    return value > 0.0 || (value == 0.0 && topLeft);
}

struct Interpolation
{
    qreal w0 = 0.0;
    qreal w1 = 0.0;
    qreal w2 = 0.0;
};

// Perspective-correct weights: divide the barycentrics by w, then renormalise.
// For 2D every w is 1 and this collapses to the plain barycentrics.
Interpolation interpolateWeights(const ScreenVertex& v0, const ScreenVertex& v1,
                                 const ScreenVertex& v2, qreal b0, qreal b1, qreal b2,
                                 bool perspective)
{
    if (!perspective)
        return {b0, b1, b2};

    const qreal w0 = b0 * v0.invW;
    const qreal w1 = b1 * v1.invW;
    const qreal w2 = b2 * v2.invW;
    const qreal sum = w0 + w1 + w2;
    if (!(sum > 0.0) || !isFinite(sum))
        return {b0, b1, b2};
    return {w0 / sum, w1 / sum, w2 / sum};
}

QColor interpolateColour(const ScreenVertex& v0, const ScreenVertex& v1,
                         const ScreenVertex& v2, const Interpolation& w)
{
    const auto mix = [&](float a, float b, float c) {
        return static_cast<float>(w.w0 * static_cast<qreal>(a)
                                  + w.w1 * static_cast<qreal>(b)
                                  + w.w2 * static_cast<qreal>(c));
    };

    return QColor::fromRgbF(mix(v0.colour.redF(), v1.colour.redF(), v2.colour.redF()),
                            mix(v0.colour.greenF(), v1.colour.greenF(), v2.colour.greenF()),
                            mix(v0.colour.blueF(), v1.colour.blueF(), v2.colour.blueF()),
                            mix(v0.colour.alphaF(), v1.colour.alphaF(), v2.colour.alphaF()));
}

QPointF interpolatePoint(const QPointF& a, const QPointF& b, const QPointF& c,
                         const Interpolation& w)
{
    return QPointF(w.w0 * a.x() + w.w1 * b.x() + w.w2 * c.x(),
                   w.w0 * a.y() + w.w1 * b.y() + w.w2 * c.y());
}

QVector3D interpolateVector(const QVector3D& a, const QVector3D& b, const QVector3D& c,
                            const Interpolation& w)
{
    const qreal x = w.w0 * a.x() + w.w1 * b.x() + w.w2 * c.x();
    const qreal y = w.w0 * a.y() + w.w1 * b.y() + w.w2 * c.y();
    const qreal z = w.w0 * a.z() + w.w1 * b.z() + w.w2 * c.z();
    return QVector3D(static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(z));
}

int toByte(qreal value)
{
    return static_cast<int>(std::lround(qBound(0.0, value, 1.0) * 255.0));
}

// Blend one shaded fragment onto the destination. `coverage` is 1 except on an
// antialiased boundary pixel, where it scales the source alpha. BlendMode::None
// overwrites fully covered pixels and falls back to source-over on a partial
// one, because replacing part of a pixel has no other meaningful definition.
QRgb blendPixel(QRgb destination, const QColor& source, BlendMode mode, qreal coverage)
{
    const qreal sa = qBound(0.0, static_cast<qreal>(source.alphaF()), 1.0) * coverage;
    const qreal sr = qBound(0.0, static_cast<qreal>(source.redF()), 1.0);
    const qreal sg = qBound(0.0, static_cast<qreal>(source.greenF()), 1.0);
    const qreal sb = qBound(0.0, static_cast<qreal>(source.blueF()), 1.0);

    const qreal da = static_cast<qreal>(qAlpha(destination)) / 255.0;
    const qreal dr = static_cast<qreal>(qRed(destination)) / 255.0;
    const qreal dg = static_cast<qreal>(qGreen(destination)) / 255.0;
    const qreal db = static_cast<qreal>(qBlue(destination)) / 255.0;

    qreal outAlpha = 0.0;
    qreal outRed = 0.0;
    qreal outGreen = 0.0;
    qreal outBlue = 0.0;

    switch (mode) {
    case BlendMode::None:
        if (coverage >= 1.0)
            return qRgba(toByte(sr), toByte(sg), toByte(sb), toByte(sa));
        [[fallthrough]];

    case BlendMode::Alpha: {
        outAlpha = sa + da * (1.0 - sa);
        if (outAlpha > 0.0) {
            outRed = (sr * sa + dr * da * (1.0 - sa)) / outAlpha;
            outGreen = (sg * sa + dg * da * (1.0 - sa)) / outAlpha;
            outBlue = (sb * sa + db * da * (1.0 - sa)) / outAlpha;
        }
        break;
    }

    case BlendMode::Additive:
        outAlpha = qMin(1.0, da + sa);
        outRed = qMin(1.0, dr + sr * sa);
        outGreen = qMin(1.0, dg + sg * sa);
        outBlue = qMin(1.0, db + sb * sa);
        break;

    case BlendMode::Multiply: {
        // Blend function dst * src, then source-over. On a transparent target
        // this reduces to the product with the source alpha; on an opaque one
        // it is a lerp between the destination and the product.
        outAlpha = sa + da * (1.0 - sa);
        if (outAlpha > 0.0) {
            outRed = (dr * sr * sa + dr * da * (1.0 - sa)) / outAlpha;
            outGreen = (dg * sg * sa + dg * da * (1.0 - sa)) / outAlpha;
            outBlue = (db * sb * sa + db * da * (1.0 - sa)) / outAlpha;
        }
        break;
    }
    }

    return qRgba(toByte(outRed), toByte(outGreen), toByte(outBlue), toByte(outAlpha));
}

class SoftwareRasterizer
{
public:
    SoftwareRasterizer(QImage& surface, RasterStats& stats)
        : surface_(surface)
        , stats_(stats)
        , target_(0, 0, surface.width(), surface.height())
    {
    }

    void execute(const CommandBuffer& buffer);

private:
    void clear(const DrawCommand& command, const QRect& scissor);
    void drawImage(const DrawCommand& command, const QRect& scissor);
    void drawMesh2D(const DrawCommand& command, const QRect& scissor);
    void drawMesh3D(const DrawCommand& command, const QRect& scissor);
    void drawText(const DrawCommand& command, const QRect& scissor);
    void drawPath(const DrawCommand& command, const QRect& scissor);

    QRect viewportFor(const DrawCommand& command) const;
    QRect clipFor(const DrawCommand& command, const QRect& scissor) const;

    void draw2DTopology(const Mesh2D& mesh, const Pipeline& pipeline,
                        const Uniforms& uniforms, const QRect& viewport,
                        const QRect& clip);
    void draw3DTopology(const Mesh3D& mesh, const Pipeline& pipeline,
                        const Uniforms& uniforms, const QRect& viewport,
                        const QRect& clip);

    bool project(const Vertex3D& vertex, const Shader& shader,
                 const Uniforms& uniforms, const QRect& viewport,
                 ScreenVertex& out) const;

    void triangle(ScreenVertex v0, ScreenVertex v1, ScreenVertex v2,
                  const Pipeline& pipeline, const Uniforms& uniforms, bool is3D,
                  const QRect& clip);
    void line(const ScreenVertex& v0, const ScreenVertex& v1,
              const Pipeline& pipeline, const Uniforms& uniforms, bool is3D,
              const QRect& clip);
    void point(const ScreenVertex& vertex, const Pipeline& pipeline,
               const Uniforms& uniforms, bool is3D, const QRect& clip);

    bool shade(QRgb* scanline, int x, int y, const FragmentInput& input,
               qreal coverage, const Pipeline& pipeline, const Uniforms& uniforms,
               bool is3D);

    void ensureDepth();

    QImage& surface_;
    RasterStats& stats_;
    QRect target_;
    QVector<float> depth_;
};

void SoftwareRasterizer::execute(const CommandBuffer& buffer)
{
    QRect scissor = target_;

    for (const DrawCommand& command : buffer.commands()) {
        switch (command.kind) {
        case CommandKind::SetScissor:
            scissor = command.scissor.isEmpty()
                          ? target_
                          : command.scissor.intersected(target_);
            break;
        case CommandKind::Clear:
            clear(command, scissor);
            break;
        case CommandKind::DrawMesh2D:
            drawMesh2D(command, scissor);
            break;
        case CommandKind::DrawMesh3D:
            drawMesh3D(command, scissor);
            break;
        case CommandKind::DrawText:
            drawText(command, scissor);
            break;
        case CommandKind::DrawPath:
            drawPath(command, scissor);
            break;
        case CommandKind::DrawImage:
            drawImage(command, scissor);
            break;
        }
    }
}

QRect SoftwareRasterizer::viewportFor(const DrawCommand& command) const
{
    if (command.viewport.isEmpty())
        return target_;
    return command.viewport.intersected(target_);
}

QRect SoftwareRasterizer::clipFor(const DrawCommand& command, const QRect& scissor) const
{
    QRect clip = viewportFor(command).intersected(scissor);
    // A command may narrow the state scissor further; the two intersect and
    // neither can widen the other.
    if (!command.scissor.isEmpty())
        clip = clip.intersected(command.scissor);
    return clip;
}

void SoftwareRasterizer::clear(const DrawCommand& command, const QRect& scissor)
{
    const QRect area = clipFor(command, scissor);
    if (area.isEmpty())
        return;

    const QColor colour = command.colour.isValid() ? command.colour : QColor(Qt::white);
    const QRgb value = qRgba(colour.red(), colour.green(), colour.blue(), colour.alpha());

    for (int y = area.top(); y <= area.bottom(); ++y) {
        QRgb* scanline = reinterpret_cast<QRgb*>(surface_.scanLine(y));
        std::fill(scanline + area.left(), scanline + area.right() + 1, value);
    }

    if (!depth_.isEmpty()) {
        const float infinity = std::numeric_limits<float>::infinity();
        const int width = surface_.width();
        for (int y = area.top(); y <= area.bottom(); ++y) {
            std::fill(depth_.begin() + y * width + area.left(),
                      depth_.begin() + y * width + area.right() + 1, infinity);
        }
    }
}

void SoftwareRasterizer::drawMesh2D(const DrawCommand& command, const QRect& scissor)
{
    if (!command.pipeline.isValid()) {
        qWarning() << "gfx::rasterize: skipped Mesh2D with an invalid pipeline";
        return;
    }
    if (!command.mesh2d.isValid()) {
        qWarning() << "gfx::rasterize: skipped invalid Mesh2D";
        return;
    }

    const QRect clip = clipFor(command, scissor);
    if (clip.isEmpty())
        return;

    draw2DTopology(command.mesh2d, command.pipeline, command.uniforms,
                   viewportFor(command), clip);
}

void SoftwareRasterizer::draw3DTopology(const Mesh3D& mesh, const Pipeline& pipeline,
                                        const Uniforms& uniforms, const QRect& viewport,
                                        const QRect& clip)
{
    const auto screenVertex = [&](quint32 index, ScreenVertex& out) {
        return project(mesh.vertices().at(static_cast<qsizetype>(index)),
                       pipeline.shader, uniforms, viewport, out);
    };

    switch (mesh.topology()) {
    case PrimitiveTopology::Triangles:
        for (qsizetype i = 0; i + 2 < mesh.indices().size(); i += 3) {
            ScreenVertex v0;
            ScreenVertex v1;
            ScreenVertex v2;
            if (!screenVertex(mesh.indices().at(i), v0)
                || !screenVertex(mesh.indices().at(i + 1), v1)
                || !screenVertex(mesh.indices().at(i + 2), v2)) {
                continue;
            }
            triangle(v0, v1, v2, pipeline, uniforms, true, clip);
        }
        break;

    case PrimitiveTopology::Lines:
        for (qsizetype i = 0; i + 1 < mesh.indices().size(); i += 2) {
            ScreenVertex v0;
            ScreenVertex v1;
            if (!screenVertex(mesh.indices().at(i), v0)
                || !screenVertex(mesh.indices().at(i + 1), v1)) {
                continue;
            }
            line(v0, v1, pipeline, uniforms, true, clip);
        }
        break;

    case PrimitiveTopology::Points:
        for (qsizetype i = 0; i < mesh.indices().size(); ++i) {
            ScreenVertex v;
            if (screenVertex(mesh.indices().at(i), v))
                point(v, pipeline, uniforms, true, clip);
        }
        break;
    }
}

void SoftwareRasterizer::drawMesh3D(const DrawCommand& command, const QRect& scissor)
{
    if (!command.pipeline.isValid() || !command.pipeline.shader.vertexStage()) {
        qWarning() << "gfx::rasterize: skipped Mesh3D with an invalid pipeline";
        return;
    }
    if (!command.mesh3d.isValid()) {
        qWarning() << "gfx::rasterize: skipped invalid Mesh3D";
        return;
    }

    const QRect clip = clipFor(command, scissor);
    if (clip.isEmpty())
        return;

    draw3DTopology(command.mesh3d, command.pipeline, command.uniforms,
                   viewportFor(command), clip);
}

void SoftwareRasterizer::draw2DTopology(const Mesh2D& mesh, const Pipeline& pipeline,
                                        const Uniforms& uniforms, const QRect& viewport,
                                        const QRect& clip)
{
    const auto screenVertex = [&](quint32 index) {
        const Vertex2D& vertex = mesh.vertices().at(static_cast<qsizetype>(index));
        ScreenVertex out;
        out.screen = QPointF(viewport.x() + vertex.position.x(),
                             viewport.y() + vertex.position.y());
        out.depth = 0.0;
        out.invW = 1.0;
        out.colour = vertex.colour.isValid() ? vertex.colour : QColor(Qt::white);
        out.uv = vertex.uv;
        out.normal = QVector3D(0.0F, 0.0F, 1.0F);
        out.world = QVector3D(static_cast<float>(vertex.position.x()),
                              static_cast<float>(vertex.position.y()), 0.0F);
        return out;
    };

    switch (mesh.topology()) {
    case PrimitiveTopology::Triangles:
        for (qsizetype i = 0; i + 2 < mesh.indices().size(); i += 3) {
            triangle(screenVertex(mesh.indices().at(i)),
                     screenVertex(mesh.indices().at(i + 1)),
                     screenVertex(mesh.indices().at(i + 2)), pipeline, uniforms,
                     false, clip);
        }
        break;

    case PrimitiveTopology::Lines:
        for (qsizetype i = 0; i + 1 < mesh.indices().size(); i += 2) {
            line(screenVertex(mesh.indices().at(i)),
                 screenVertex(mesh.indices().at(i + 1)), pipeline, uniforms, false,
                 clip);
        }
        break;

    case PrimitiveTopology::Points:
        for (qsizetype i = 0; i < mesh.indices().size(); ++i) {
            point(screenVertex(mesh.indices().at(i)), pipeline, uniforms, false,
                  clip);
        }
        break;
    }
}

bool SoftwareRasterizer::project(const Vertex3D& vertex, const Shader& shader,
                                 const Uniforms& uniforms, const QRect& viewport,
                                 ScreenVertex& out) const
{
    if (!shader.vertexStage())
        return false;

    const QVector4D clip = shader.vertexStage()(vertex, uniforms);
    if (!isFinite(clip.x()) || !isFinite(clip.y()) || !isFinite(clip.z())
        || !isFinite(clip.w()) || clip.w() <= 0.0F) {
        // A vertex at or behind the eye has no finite projection. Phase 1
        // rejects the primitive rather than clipping it against the near plane.
        return false;
    }

    const qreal inverseW = 1.0 / static_cast<qreal>(clip.w());
    const qreal ndcX = static_cast<qreal>(clip.x()) * inverseW;
    const qreal ndcY = static_cast<qreal>(clip.y()) * inverseW;
    const qreal ndcZ = static_cast<qreal>(clip.z()) * inverseW;

    out.screen = QPointF(viewport.x() + (ndcX * 0.5 + 0.5) * viewport.width(),
                         viewport.y() + (0.5 - ndcY * 0.5) * viewport.height());
    out.depth = qBound(0.0, ndcZ * 0.5 + 0.5, 1.0);
    out.invW = inverseW;
    out.colour = vertex.colour.isValid() ? vertex.colour : QColor(Qt::white);
    out.uv = vertex.uv;
    out.normal = vertex.normal.isNull() ? QVector3D(0.0F, 0.0F, 1.0F)
                                        : vertex.normal;
    out.world = vertex.position;

    return isFinite(out.screen);
}

void SoftwareRasterizer::triangle(ScreenVertex v0, ScreenVertex v1, ScreenVertex v2,
                                  const Pipeline& pipeline, const Uniforms& uniforms,
                                  bool is3D, const QRect& clip)
{
    qreal area = edge(v0.screen, v1.screen, v2.screen);
    if (!isFinite(area) || area == 0.0)
        return;

    if (is3D && pipeline.raster.cullBack && area >= 0.0)
        return;

    // Rasterise with a positive area so every inside test is >= 0 and the
    // top-left rule has a single orientation to reason about.
    if (area < 0.0) {
        std::swap(v1, v2);
        area = -area;
    }

    ++stats_.trianglesDrawn;

    const qreal minimumX = qMin(v0.screen.x(), qMin(v1.screen.x(), v2.screen.x()));
    const qreal maximumX = qMax(v0.screen.x(), qMax(v1.screen.x(), v2.screen.x()));
    const qreal minimumY = qMin(v0.screen.y(), qMin(v1.screen.y(), v2.screen.y()));
    const qreal maximumY = qMax(v0.screen.y(), qMax(v1.screen.y(), v2.screen.y()));

    const qreal left = qMax<qreal>(clip.left(), std::floor(minimumX));
    const qreal right = qMin<qreal>(clip.right(), std::ceil(maximumX) - 1.0);
    const qreal top = qMax<qreal>(clip.top(), std::floor(minimumY));
    const qreal bottom = qMin<qreal>(clip.bottom(), std::ceil(maximumY) - 1.0);
    if (!(left <= right) || !(top <= bottom))
        return;

    const int firstX = static_cast<int>(left);
    const int lastX = static_cast<int>(right);
    const int firstY = static_cast<int>(top);
    const int lastY = static_cast<int>(bottom);

    const bool topLeft0 = isTopLeft(v1.screen, v2.screen);
    const bool topLeft1 = isTopLeft(v2.screen, v0.screen);
    const bool topLeft2 = isTopLeft(v0.screen, v1.screen);

    const qreal length0 = edgeLength(v1.screen, v2.screen);
    const qreal length1 = edgeLength(v2.screen, v0.screen);
    const qreal length2 = edgeLength(v0.screen, v1.screen);

    for (int y = firstY; y <= lastY; ++y) {
        QRgb* scanline = reinterpret_cast<QRgb*>(surface_.scanLine(y));

        for (int x = firstX; x <= lastX; ++x) {
            const QPointF centre(x + 0.5, y + 0.5);

            const qreal e0 = edge(v1.screen, v2.screen, centre);
            const qreal e1 = edge(v2.screen, v0.screen, centre);
            const qreal e2 = edge(v0.screen, v1.screen, centre);

            if (!coversEdge(e0, topLeft0) || !coversEdge(e1, topLeft1)
                || !coversEdge(e2, topLeft2)) {
                continue;
            }

            qreal coverage = 1.0;
            if (!is3D && pipeline.raster.antialias) {
                // Perpendicular distance to the closest edge, in pixels. Only
                // pixels that can straddle an edge are supersampled.
                const qreal distance0 = length0 > 0.0 ? e0 / length0 : 1.0;
                const qreal distance1 = length1 > 0.0 ? e1 / length1 : 1.0;
                const qreal distance2 = length2 > 0.0 ? e2 / length2 : 1.0;

                if (qMin(distance0, qMin(distance1, distance2)) < 1.0) {
                    int covered = 0;
                    for (int sampleY = 0; sampleY < 4; ++sampleY) {
                        for (int sampleX = 0; sampleX < 4; ++sampleX) {
                            const QPointF sample(
                                x + (static_cast<qreal>(sampleX) + 0.5) * 0.25,
                                y + (static_cast<qreal>(sampleY) + 0.5) * 0.25);

                            if (edge(v1.screen, v2.screen, sample) >= 0.0
                                && edge(v2.screen, v0.screen, sample) >= 0.0
                                && edge(v0.screen, v1.screen, sample) >= 0.0) {
                                ++covered;
                            }
                        }
                    }
                    coverage = static_cast<qreal>(covered) / 16.0;
                    if (coverage <= 0.0)
                        continue;
                }
            }

            const Interpolation weights = interpolateWeights(
                v0, v1, v2, e0 / area, e1 / area, e2 / area, is3D);

            FragmentInput input;
            input.colour = interpolateColour(v0, v1, v2, weights);
            input.uv = interpolatePoint(v0.uv, v1.uv, v2.uv, weights);
            input.normal = interpolateVector(v0.normal, v1.normal, v2.normal, weights);
            if (input.normal.lengthSquared() > 0.0F)
                input.normal.normalize();
            input.worldPosition = interpolateVector(v0.world, v1.world, v2.world, weights);
            input.depth = (e0 / area) * v0.depth + (e1 / area) * v1.depth
                + (e2 / area) * v2.depth;

            shade(scanline, x, y, input, coverage, pipeline, uniforms, is3D);
        }
    }
}

void SoftwareRasterizer::line(const ScreenVertex& v0, const ScreenVertex& v1,
                              const Pipeline& pipeline, const Uniforms& uniforms,
                              bool is3D, const QRect& clip)
{
    const qreal dx = v1.screen.x() - v0.screen.x();
    const qreal dy = v1.screen.y() - v0.screen.y();
    const qreal length = std::max(std::abs(dx), std::abs(dy));

    // Bound the walk by the clip size: a line whose endpoints are far outside
    // the target must not turn into a huge loop.
    const qreal limit = static_cast<qreal>(clip.width() + clip.height() + 2);
    const int steps = static_cast<int>(qMin(std::ceil(length), limit));
    if (steps <= 0) {
        point(v0, pipeline, uniforms, is3D, clip);
        return;
    }

    for (int step = 0; step <= steps; ++step) {
        const qreal t = static_cast<qreal>(step) / static_cast<qreal>(steps);
        const QPointF position(v0.screen.x() + dx * t, v0.screen.y() + dy * t);

        const int x = static_cast<int>(std::floor(position.x()));
        const int y = static_cast<int>(std::floor(position.y()));
        if (!clip.contains(x, y))
            continue;

        qreal w0 = 1.0 - t;
        qreal w1 = t;
        if (is3D) {
            const qreal scaled0 = w0 * v0.invW;
            const qreal scaled1 = w1 * v1.invW;
            const qreal sum = scaled0 + scaled1;
            if (sum > 0.0 && isFinite(sum)) {
                w0 = scaled0 / sum;
                w1 = scaled1 / sum;
            }
        }

        FragmentInput input;
        input.colour = QColor::fromRgbF(
            static_cast<float>(w0 * v0.colour.redF() + w1 * v1.colour.redF()),
            static_cast<float>(w0 * v0.colour.greenF() + w1 * v1.colour.greenF()),
            static_cast<float>(w0 * v0.colour.blueF() + w1 * v1.colour.blueF()),
            static_cast<float>(w0 * v0.colour.alphaF() + w1 * v1.colour.alphaF()));
        input.uv = QPointF(w0 * v0.uv.x() + w1 * v1.uv.x(),
                           w0 * v0.uv.y() + w1 * v1.uv.y());
        input.normal = interpolateVector(v0.normal, v1.normal, QVector3D(), Interpolation{w0, w1, 0.0});
        if (input.normal.lengthSquared() > 0.0F)
            input.normal.normalize();
        input.worldPosition = interpolateVector(v0.world, v1.world, QVector3D(), Interpolation{w0, w1, 0.0});
        input.depth = w0 * v0.depth + w1 * v1.depth;

        QRgb* scanline = reinterpret_cast<QRgb*>(surface_.scanLine(y));
        shade(scanline, x, y, input, 1.0, pipeline, uniforms, is3D);
    }
}

void SoftwareRasterizer::point(const ScreenVertex& vertex, const Pipeline& pipeline,
                               const Uniforms& uniforms, bool is3D,
                               const QRect& clip)
{
    const int x = static_cast<int>(std::floor(vertex.screen.x()));
    const int y = static_cast<int>(std::floor(vertex.screen.y()));
    if (!clip.contains(x, y))
        return;

    FragmentInput input;
    input.colour = vertex.colour;
    input.uv = vertex.uv;
    input.normal = vertex.normal;
    input.worldPosition = vertex.world;
    input.depth = vertex.depth;

    QRgb* scanline = reinterpret_cast<QRgb*>(surface_.scanLine(y));
    shade(scanline, x, y, input, 1.0, pipeline, uniforms, is3D);
}

bool SoftwareRasterizer::shade(QRgb* scanline, int x, int y, const FragmentInput& input,
                               qreal coverage, const Pipeline& pipeline,
                               const Uniforms& uniforms, bool is3D)
{
    if (is3D) {
        ensureDepth();
        const int index = y * surface_.width() + x;
        if (pipeline.depth.test && input.depth >= depth_.at(index))
            return false;
        if (pipeline.depth.write)
            depth_[index] = static_cast<float>(input.depth);
    }

    QColor colour = pipeline.shader.fragmentStage()
                        ? pipeline.shader.fragmentStage()(input, uniforms)
                        : input.colour;
    if (!colour.isValid())
        colour = input.colour;

    scanline[x] = blendPixel(scanline[x], colour, pipeline.blend, coverage);
    ++stats_.pixelsWritten;
    return true;
}

void SoftwareRasterizer::ensureDepth()
{
    if (!depth_.isEmpty())
        return;

    depth_.resize(static_cast<qsizetype>(target_.width()) * target_.height());
    std::fill(depth_.begin(), depth_.end(),
              std::numeric_limits<float>::infinity());
}

void SoftwareRasterizer::drawText(const DrawCommand& command, const QRect& scissor)
{
    const QRect viewport = viewportFor(command);
    const QRect clip = clipFor(command, scissor);
    if (clip.isEmpty() || command.text.isEmpty())
        return;

    // Temporary bridge: glyph shaping and font fallback stay with QPainter
    // until phase 2 gives both devices a shared TextPainter.
    QPainter painter(&surface_);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setClipRect(clip);
    painter.setFont(command.font);
    painter.setPen(command.colour.isValid() ? command.colour : QColor(Qt::white));
    painter.drawText(QPointF(viewport.x() + command.textPosition.x(),
                             viewport.y() + command.textPosition.y()),
                     command.text);
}

void SoftwareRasterizer::drawPath(const DrawCommand& command, const QRect& scissor)
{
    const QRect viewport = viewportFor(command);
    const QRect clip = clipFor(command, scissor);
    if (clip.isEmpty() || command.path.isEmpty())
        return;

    // Temporary bridge: path filling is QPainter's job until phase 2 gives both
    // devices a shared PathRasterizer.
    QPainter painter(&surface_);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(clip);
    painter.translate(viewport.topLeft());
    painter.setPen(command.pen);
    painter.setBrush(command.brush);
    painter.drawPath(command.path);
}

void SoftwareRasterizer::drawImage(const DrawCommand& command, const QRect& scissor)
{
    if (command.image.isNull()) {
        qWarning() << "gfx::rasterize: skipped DrawImage with a null image";
        return;
    }

    const QRect destination = command.viewport.isEmpty()
                                  ? QRect(0, 0, command.image.width(), command.image.height())
                                  : command.viewport;
    if (destination.width() <= 0 || destination.height() <= 0)
        return;

    QRect clip = destination.intersected(scissor).intersected(target_);
    if (!command.scissor.isEmpty())
        clip = clip.intersected(command.scissor);
    if (clip.isEmpty())
        return;

    // A direct blit rather than a quad: it needs no per-draw vertex allocation,
    // samples through the same Texture rules a textured quad would, and still
    // respects the scissor and the pipeline's blend mode.
    const Texture texture(command.image, Wrap::Clamp, Filter::Bilinear);
    if (!texture.isValid())
        return;

    const qreal inverseWidth = 1.0 / static_cast<qreal>(destination.width());
    const qreal inverseHeight = 1.0 / static_cast<qreal>(destination.height());

    for (int y = clip.top(); y <= clip.bottom(); ++y) {
        QRgb* scanline = reinterpret_cast<QRgb*>(surface_.scanLine(y));
        const qreal v = (static_cast<qreal>(y - destination.y()) + 0.5) * inverseHeight;

        for (int x = clip.left(); x <= clip.right(); ++x) {
            const qreal u = (static_cast<qreal>(x - destination.x()) + 0.5) * inverseWidth;
            const QColor colour = texture.sample(QPointF(u, v), 0.0);
            scanline[x] = blendPixel(scanline[x], colour, command.pipeline.blend, 1.0);
            ++stats_.pixelsWritten;
        }
    }
}

} // namespace

RasterStats rasterize(QImage& target, const CommandBuffer& commands,
                      const RasterCaps& caps)
{
    RasterStats stats;

    if (target.isNull() || target.width() <= 0 || target.height() <= 0) {
        qWarning() << "gfx::rasterize: refused a null or empty target";
        return stats;
    }

    const qint64 pixels = static_cast<qint64>(target.width())
        * static_cast<qint64>(target.height());
    if (pixels > static_cast<qint64>(caps.maxTargetPixels)) {
        qWarning() << "gfx::rasterize: refused target of" << target.width() << "x"
                   << target.height() << "pixels (cap" << caps.maxTargetPixels << ")";
        return stats;
    }

    qint64 primitives = 0;
    for (const DrawCommand& command : commands.commands()) {
        switch (command.kind) {
        case CommandKind::DrawMesh2D:
            primitives += primitiveCount(command.mesh2d);
            break;
        case CommandKind::DrawMesh3D:
            primitives += primitiveCount(command.mesh3d);
            break;
        case CommandKind::DrawImage:
            primitives += 2;
            break;
        case CommandKind::DrawText:
        case CommandKind::DrawPath:
        case CommandKind::SetScissor:
        case CommandKind::Clear:
            break;
        }

        if (primitives > static_cast<qint64>(caps.maxTriangles)) {
            // Refused as a whole: no command has run yet, so the target still
            // holds exactly what the caller passed in.
            qWarning() << "gfx::rasterize: refused" << primitives
                       << "primitives (cap" << caps.maxTriangles << ")";
            return stats;
        }
    }

    // The one documented scratch buffer: ARGB32 so blending is byte-exact, then
    // written back to the caller's image (which therefore becomes ARGB32).
    QImage surface = target.convertToFormat(QImage::Format_ARGB32);
    if (surface.isNull()) {
        qWarning() << "gfx::rasterize: could not create the ARGB32 scratch image";
        return stats;
    }

    SoftwareRasterizer rasterizer(surface, stats);
    rasterizer.execute(commands);

    target = surface;
    return stats;
}

} // namespace md::gfx
