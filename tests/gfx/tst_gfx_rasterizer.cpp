// gfx layer tests: the software rasterizer's pixels.
//
// These are deliberately pixel assertions rather than statistics checks: the
// contract is that the same command buffer produces the same image everywhere,
// so interpolation, depth ordering, culling, texture sampling and the bounds
// refusal are all read back from the target. Blend modes are checked on a
// known background as well, because that is where off-by-alpha bugs hide.

#include <md/gfx/command_buffer.h>
#include <md/gfx/mesh.h>
#include <md/gfx/rasterizer.h>
#include <md/gfx/shader.h>
#include <md/gfx/texture.h>

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QTest>
#include <QVector3D>

#include <cmath>
#include <utility>

using namespace md::gfx;

namespace {

QImage targetImage(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    return image;
}

Pipeline colourPipeline()
{
    Pipeline pipeline;
    pipeline.shader = ShaderLibrary::vertexColour();
    pipeline.blend = BlendMode::None;
    pipeline.raster.antialias = false;
    return pipeline;
}

Pipeline solidPipeline()
{
    Pipeline pipeline;
    pipeline.shader = ShaderLibrary::solid();
    pipeline.blend = BlendMode::None;
    pipeline.raster.antialias = false;
    pipeline.raster.cullBack = false;
    return pipeline;
}

Mesh2D triangle2D(const QVector<Vertex2D>& vertices)
{
    return Mesh2D(vertices, {0, 1, 2});
}

Mesh3D triangle3D(float z, const QColor& colour)
{
    QVector<Vertex3D> vertices;
    vertices.append({QVector3D(-1.0F, -1.0F, z), QVector3D(0.0F, 0.0F, 1.0F),
                     colour, QPointF()});
    vertices.append({QVector3D(1.0F, -1.0F, z), QVector3D(0.0F, 0.0F, 1.0F),
                     colour, QPointF()});
    vertices.append({QVector3D(0.0F, 1.0F, z), QVector3D(0.0F, 0.0F, 1.0F),
                     colour, QPointF()});
    return Mesh3D(std::move(vertices), {0, 1, 2});
}

DrawCommand meshCommand(const Mesh2D& mesh, const Pipeline& pipeline)
{
    DrawCommand command;
    command.kind = CommandKind::DrawMesh2D;
    command.pipeline = pipeline;
    command.mesh2d = mesh;
    return command;
}

DrawCommand meshCommand(const Mesh3D& mesh, const Pipeline& pipeline)
{
    DrawCommand command;
    command.kind = CommandKind::DrawMesh3D;
    command.pipeline = pipeline;
    command.mesh3d = mesh;
    return command;
}

bool allTransparent(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y).alpha() != 0)
                return false;
        }
    }
    return true;
}

} // namespace

class TestGfxRasterizer : public QObject
{
    Q_OBJECT

private slots:
    void triangleInterpolatesVertexColours();
    void degenerateTriangleWritesNothing();
    void zBufferKeepsTheNearTriangle();
    void cullingDropsBackFacingTriangle();
    void texturedQuadSamplesExpectedColours();
    void scissorLimitsTheFill();
    void blendModesCombineAsDocumented();
    void targetOverCapIsRefused();
    void primitiveOverCapIsRefused();
};

void TestGfxRasterizer::triangleInterpolatesVertexColours()
{
    QImage target = targetImage(32, 32);

    QVector<Vertex2D> vertices;
    vertices.append({QPointF(-8.0, -8.0), QColor(Qt::red), QPointF()});
    vertices.append({QPointF(40.0, -8.0), QColor(Qt::red), QPointF()});
    vertices.append({QPointF(-8.0, 40.0), QColor(Qt::blue), QPointF()});

    CommandBuffer buffer;
    buffer.add(meshCommand(triangle2D(vertices), colourPipeline()));

    const RasterStats stats = rasterize(target, buffer);
    QCOMPARE(stats.trianglesDrawn, 1);
    QVERIFY(stats.pixelsWritten > 0);

    // Near the red edge the red channel dominates; on the diagonal the two
    // vertex colours meet near the middle.
    const QColor redEnd = target.pixelColor(2, 2);
    QCOMPARE(redEnd.alpha(), 255);
    QVERIFY(redEnd.red() > 150);
    QVERIFY(redEnd.blue() < 100);

    const QColor middle = target.pixelColor(15, 15);
    QCOMPARE(middle.alpha(), 255);
    QVERIFY(std::abs(middle.red() - middle.blue()) <= 6);
    QVERIFY(middle.red() > 100 && middle.red() < 155);
}

void TestGfxRasterizer::degenerateTriangleWritesNothing()
{
    QImage target = targetImage(32, 32);

    QVector<Vertex2D> vertices;
    vertices.append({QPointF(0.0, 10.0), QColor(Qt::red), QPointF()});
    vertices.append({QPointF(10.0, 10.0), QColor(Qt::red), QPointF()});
    vertices.append({QPointF(20.0, 10.0), QColor(Qt::red), QPointF()});

    CommandBuffer buffer;
    buffer.add(meshCommand(triangle2D(vertices), colourPipeline()));

    const RasterStats stats = rasterize(target, buffer);
    QCOMPARE(stats.trianglesDrawn, 0);
    QCOMPARE(stats.pixelsWritten, 0);
    QVERIFY(allTransparent(target));
}

void TestGfxRasterizer::zBufferKeepsTheNearTriangle()
{
    QImage target = targetImage(32, 32);

    Pipeline pipeline = solidPipeline();
    pipeline.depth.test = true;
    pipeline.depth.write = true;

    DrawCommand far = meshCommand(triangle3D(0.5F, QColor(Qt::blue)), pipeline);
    far.uniforms.set(QStringLiteral("colour"), QColor(Qt::blue));

    DrawCommand near = meshCommand(triangle3D(-0.5F, QColor(Qt::red)), pipeline);
    near.uniforms.set(QStringLiteral("colour"), QColor(Qt::red));

    // Far first, near second: only a real depth test can keep the near colour.
    CommandBuffer buffer;
    buffer.add(far);
    buffer.add(near);

    const RasterStats stats = rasterize(target, buffer);
    QCOMPARE(stats.trianglesDrawn, 2);
    QVERIFY(stats.pixelsWritten > 0);
    QCOMPARE(target.pixelColor(16, 16), QColor(Qt::red));

    // The same buffer with only the far triangle shows that the depth test,
    // not the draw order, made the difference.
    QImage farOnly = targetImage(32, 32);
    CommandBuffer farBuffer;
    farBuffer.add(far);
    rasterize(farOnly, farBuffer);
    QCOMPARE(farOnly.pixelColor(16, 16), QColor(Qt::blue));
}

void TestGfxRasterizer::cullingDropsBackFacingTriangle()
{
    QVector<Vertex3D> backVertices;
    backVertices.append({QVector3D(-1.0F, -1.0F, 0.0F), QVector3D(0.0F, 0.0F, 1.0F),
                         QColor(Qt::red), QPointF()});
    backVertices.append({QVector3D(0.0F, 1.0F, 0.0F), QVector3D(0.0F, 0.0F, 1.0F),
                         QColor(Qt::red), QPointF()});
    backVertices.append({QVector3D(1.0F, -1.0F, 0.0F), QVector3D(0.0F, 0.0F, 1.0F),
                         QColor(Qt::red), QPointF()});

    Pipeline pipeline = solidPipeline();
    pipeline.raster.cullBack = true;

    DrawCommand back = meshCommand(Mesh3D(backVertices, {0, 1, 2}), pipeline);
    back.uniforms.set(QStringLiteral("colour"), QColor(Qt::red));

    QImage culled = targetImage(32, 32);
    CommandBuffer culledBuffer;
    culledBuffer.add(back);
    const RasterStats culledStats = rasterize(culled, culledBuffer);
    QCOMPARE(culledStats.trianglesDrawn, 0);
    QCOMPARE(culledStats.pixelsWritten, 0);
    QVERIFY(allTransparent(culled));

    // Reversing the winding makes it front-facing and it draws.
    QVector<Vertex3D> frontVertices;
    frontVertices.append(backVertices.at(0));
    frontVertices.append(backVertices.at(2));
    frontVertices.append(backVertices.at(1));

    DrawCommand front = meshCommand(Mesh3D(frontVertices, {0, 1, 2}), pipeline);
    front.uniforms.set(QStringLiteral("colour"), QColor(Qt::red));

    QImage drawn = targetImage(32, 32);
    CommandBuffer drawnBuffer;
    drawnBuffer.add(front);
    const RasterStats drawnStats = rasterize(drawn, drawnBuffer);
    QCOMPARE(drawnStats.trianglesDrawn, 1);
    QCOMPARE(drawn.pixelColor(16, 20), QColor(Qt::red));
}

void TestGfxRasterizer::texturedQuadSamplesExpectedColours()
{
    QImage pixels(2, 2, QImage::Format_ARGB32);
    pixels.setPixelColor(0, 0, QColor(Qt::red));
    pixels.setPixelColor(1, 0, QColor(Qt::green));
    pixels.setPixelColor(0, 1, QColor(Qt::blue));
    pixels.setPixelColor(1, 1, QColor(Qt::white));

    const Texture texture(pixels, Wrap::Clamp, Filter::Nearest);
    QVERIFY(texture.isValid());

    Pipeline pipeline;
    pipeline.shader = ShaderLibrary::unlit();
    pipeline.blend = BlendMode::None;
    pipeline.raster.antialias = false;

    const Mesh2D quad = Mesh2D::rect(QRectF(0.0, 0.0, 4.0, 4.0));
    QVERIFY(quad.isValid());

    DrawCommand command = meshCommand(quad, pipeline);
    command.uniforms.set(QStringLiteral("texture"), texture);

    QImage target = targetImage(4, 4);
    CommandBuffer buffer;
    buffer.add(command);

    const RasterStats stats = rasterize(target, buffer);
    QVERIFY(stats.pixelsWritten > 0);

    QCOMPARE(target.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(target.pixelColor(3, 0), QColor(Qt::green));
    QCOMPARE(target.pixelColor(0, 3), QColor(Qt::blue));
    QCOMPARE(target.pixelColor(3, 3), QColor(Qt::white));
}

void TestGfxRasterizer::scissorLimitsTheFill()
{
    QImage target = targetImage(16, 16);

    const Mesh2D quad = Mesh2D::rect(QRectF(0.0, 0.0, 16.0, 16.0), QColor(Qt::red));
    DrawCommand command = meshCommand(quad, colourPipeline());
    command.scissor = QRect(0, 0, 8, 8);

    CommandBuffer buffer;
    buffer.add(command);
    QVERIFY(rasterize(target, buffer).pixelsWritten > 0);

    QCOMPARE(target.pixelColor(2, 2), QColor(Qt::red));
    QCOMPARE(target.pixelColor(12, 12).alpha(), 0);
}

void TestGfxRasterizer::blendModesCombineAsDocumented()
{
    const Mesh2D quad = Mesh2D::rect(QRectF(0.0, 0.0, 8.0, 8.0));

    const auto drawMode = [&](BlendMode mode, const QColor& background) {
        QImage target = targetImage(8, 8);
        target.fill(background);

        Pipeline pipeline = solidPipeline();
        pipeline.blend = mode;

        DrawCommand command = meshCommand(quad, pipeline);
        command.uniforms.set(QStringLiteral("colour"), QColor(Qt::red));

        CommandBuffer buffer;
        buffer.add(command);
        rasterize(target, buffer);
        return target.pixelColor(4, 4);
    };

    QCOMPARE(drawMode(BlendMode::None, QColor(Qt::darkGray)), QColor(Qt::red));
    QCOMPARE(drawMode(BlendMode::Alpha, QColor(Qt::black)), QColor(Qt::red));
    QCOMPARE(drawMode(BlendMode::Additive, QColor(Qt::black)), QColor(Qt::red));
    QCOMPARE(drawMode(BlendMode::Multiply, QColor(Qt::white)), QColor(Qt::red));
}

void TestGfxRasterizer::targetOverCapIsRefused()
{
    QImage target = targetImage(8, 8);
    target.fill(QColor(10, 20, 30, 255));

    QVector<Vertex2D> vertices;
    vertices.append({QPointF(0.0, 0.0), QColor(Qt::red), QPointF()});
    vertices.append({QPointF(8.0, 0.0), QColor(Qt::red), QPointF()});
    vertices.append({QPointF(0.0, 8.0), QColor(Qt::red), QPointF()});

    CommandBuffer buffer;
    buffer.add(meshCommand(triangle2D(vertices), colourPipeline()));

    RasterCaps caps;
    caps.maxTargetPixels = 16; // the target is 64 pixels
    const RasterStats stats = rasterize(target, buffer, caps);

    QCOMPARE(stats.trianglesDrawn, 0);
    QCOMPARE(stats.pixelsWritten, 0);
    QCOMPARE(target.pixelColor(0, 0), QColor(10, 20, 30, 255));
    QCOMPARE(target.pixelColor(7, 7), QColor(10, 20, 30, 255));
}

void TestGfxRasterizer::primitiveOverCapIsRefused()
{
    QImage target = targetImage(8, 8);
    target.fill(QColor(10, 20, 30, 255));

    QVector<Vertex2D> vertices;
    vertices.append({QPointF(0.0, 0.0), QColor(Qt::red), QPointF()});
    vertices.append({QPointF(8.0, 0.0), QColor(Qt::red), QPointF()});
    vertices.append({QPointF(0.0, 8.0), QColor(Qt::red), QPointF()});

    CommandBuffer buffer;
    buffer.add(meshCommand(triangle2D(vertices), colourPipeline()));

    RasterCaps caps;
    caps.maxTriangles = 0;
    const RasterStats stats = rasterize(target, buffer, caps);

    QCOMPARE(stats.pixelsWritten, 0);
    QCOMPARE(target.pixelColor(0, 0), QColor(10, 20, 30, 255));
}

QTEST_GUILESS_MAIN(TestGfxRasterizer)

#include "tst_gfx_rasterizer.moc"
