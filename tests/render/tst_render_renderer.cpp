// render layer tests: the frame state machine.
//
// Renderer is small, but the two rules that matter are exactly the ones a
// Canvas will depend on: a second begin() must not silently replace the target
// of an open frame, and end() without begin() must be harmless. The rest of the
// file checks that a refusal from the device reaches the caller.

#include <md/gfx/command_buffer.h>
#include <md/gfx/mesh.h>
#include <md/gfx/pipeline.h>
#include <md/gfx/shader.h>
#include <md/render/device_factory.h>
#include <md/render/render_target.h>
#include <md/render/renderer.h>
#include <md/render/software_device.h>

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QTest>

using namespace md::gfx;
using namespace md::render;

namespace {

CommandBuffer redQuad(qreal width, qreal height)
{
    Pipeline pipeline;
    pipeline.shader = ShaderLibrary::vertexColour();
    pipeline.blend = BlendMode::None;
    pipeline.raster.antialias = false;

    DrawCommand command;
    command.kind = CommandKind::DrawMesh2D;
    command.pipeline = pipeline;
    command.mesh2d = Mesh2D::rect(QRectF(0.0, 0.0, width, height), QColor(Qt::red));

    CommandBuffer buffer;
    buffer.add(command);
    return buffer;
}

std::shared_ptr<RenderDevice> softwareDevice()
{
    return DeviceFactory::create(Backend::Software);
}

} // namespace

class TestRenderRenderer : public QObject
{
    Q_OBJECT

private slots:
    void frameLifecycle();
    void doubleBeginIsReported();
    void endWithoutBeginIsANoOp();
    void submitWithoutBeginFails();
    void beginWithInvalidTargetFails();
    void beginWithoutDeviceFails();
    void deviceSwapDuringFrameIsRefused();
    void deviceRefusalIsPropagated();
};

void TestRenderRenderer::frameLifecycle()
{
    Renderer renderer(softwareDevice());
    QVERIFY(renderer.device() != nullptr);
    QVERIFY(!renderer.isActive());
    QVERIFY(renderer.target() == nullptr);
    QVERIFY(renderer.lastError().isEmpty());

    RenderTarget target(QSizeF(4.0, 4.0));
    QVERIFY(target.isValid());
    target.clear(QColor(Qt::black));

    QVERIFY(renderer.begin(target));
    QVERIFY(renderer.isActive());
    QCOMPARE(renderer.target(), &target);

    const RasterStats stats = renderer.submit(redQuad(4.0, 4.0));
    QCOMPARE(stats.trianglesDrawn, 2);
    QCOMPARE(stats.pixelsWritten, 16);
    QCOMPARE(target.image().pixelColor(2, 2), QColor(Qt::red));

    renderer.end();
    QVERIFY(!renderer.isActive());
    QVERIFY(renderer.target() == nullptr);
}

void TestRenderRenderer::doubleBeginIsReported()
{
    Renderer renderer(softwareDevice());

    RenderTarget first(QSizeF(4.0, 4.0));
    RenderTarget second(QSizeF(8.0, 8.0));
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());

    QVERIFY(renderer.begin(first));
    QVERIFY(!renderer.begin(second));
    QVERIFY2(!renderer.lastError().isEmpty(), "the second begin() must be reported");
    QCOMPARE(renderer.target(), &first);
    QVERIFY(renderer.isActive());

    // The open frame is still usable after the refused begin().
    QCOMPARE(renderer.submit(redQuad(4.0, 4.0)).pixelsWritten, 16);

    renderer.end();
    QVERIFY(renderer.begin(second));
    QCOMPARE(renderer.target(), &second);
    renderer.end();
}

void TestRenderRenderer::endWithoutBeginIsANoOp()
{
    Renderer renderer(softwareDevice());

    renderer.end();
    QVERIFY(!renderer.isActive());
    QVERIFY(renderer.lastError().isEmpty());

    RenderTarget target(QSizeF(2.0, 2.0));
    QVERIFY(renderer.begin(target));
    renderer.end();
    renderer.end();
    QVERIFY(!renderer.isActive());
    QVERIFY(renderer.lastError().isEmpty());
}

void TestRenderRenderer::submitWithoutBeginFails()
{
    Renderer renderer(softwareDevice());

    const RasterStats stats = renderer.submit(redQuad(4.0, 4.0));
    QCOMPARE(stats.trianglesDrawn, 0);
    QCOMPARE(stats.pixelsWritten, 0);
    QVERIFY(!renderer.lastError().isEmpty());
}

void TestRenderRenderer::beginWithInvalidTargetFails()
{
    Renderer renderer(softwareDevice());

    RenderTarget invalid;
    QVERIFY(!invalid.isValid());

    QVERIFY(!renderer.begin(invalid));
    QVERIFY(!renderer.isActive());
    QVERIFY(!renderer.lastError().isEmpty());
}

void TestRenderRenderer::beginWithoutDeviceFails()
{
    Renderer renderer;

    RenderTarget target(QSizeF(4.0, 4.0));
    QVERIFY(target.isValid());

    QVERIFY(!renderer.begin(target));
    QVERIFY(!renderer.isActive());
    QVERIFY(!renderer.lastError().isEmpty());

    const RasterStats stats = renderer.submit(redQuad(4.0, 4.0));
    QCOMPARE(stats.pixelsWritten, 0);
}

void TestRenderRenderer::deviceSwapDuringFrameIsRefused()
{
    Renderer renderer(softwareDevice());
    const std::shared_ptr<RenderDevice> original = renderer.device();

    RenderTarget target(QSizeF(4.0, 4.0));
    QVERIFY(renderer.begin(target));

    renderer.setDevice(softwareDevice());
    QVERIFY(!renderer.lastError().isEmpty());
    QCOMPARE(renderer.device(), original);
    QVERIFY(renderer.isActive());

    renderer.end();
    renderer.setDevice(softwareDevice());
    QVERIFY(renderer.lastError().isEmpty());
    QVERIFY(renderer.device() != original);
}

void TestRenderRenderer::deviceRefusalIsPropagated()
{
    Renderer renderer(softwareDevice());

    RenderTarget target(QSizeF(8.0, 8.0));
    target.clear(QColor(Qt::black));
    QVERIFY(renderer.begin(target));

    // More primitives than the gfx cap: the device refuses the whole buffer.
    const Mesh2D grid = Mesh2D::grid(QRectF(0.0, 0.0, 8.0, 8.0), 150, 100,
                                     QColor(Qt::red));
    QVERIFY(grid.isValid());

    Pipeline pipeline;
    pipeline.shader = ShaderLibrary::vertexColour();
    pipeline.raster.antialias = false;

    DrawCommand command;
    command.kind = CommandKind::DrawMesh2D;
    command.pipeline = pipeline;
    command.mesh2d = grid;

    CommandBuffer oversized;
    oversized.add(command);
    oversized.add(command);
    oversized.add(command);

    const RasterStats stats = renderer.submit(oversized);
    QCOMPARE(stats.trianglesDrawn, 0);
    QCOMPARE(stats.pixelsWritten, 0);
    QVERIFY(!renderer.lastError().isEmpty());
    QCOMPARE(target.image().pixelColor(4, 4), QColor(Qt::black));

    renderer.end();
}

QTEST_MAIN(TestRenderRenderer)

#include "tst_render_renderer.moc"
