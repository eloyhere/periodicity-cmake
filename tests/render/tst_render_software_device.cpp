// render layer tests: the software device.
//
// These are pixel assertions, in the same spirit as tests/gfx: the device is
// deterministic, so the test can state exactly which pixels a command buffer
// must produce. What is new here, compared with gfx, is the device contract:
// commands arrive in the target's logical coordinates and the device applies
// the device pixel ratio, text and paths keep working, and a refused submission
// leaves the target exactly as it was.

#include <md/gfx/command_buffer.h>
#include <md/gfx/mesh.h>
#include <md/gfx/pipeline.h>
#include <md/gfx/rasterizer.h>
#include <md/gfx/shader.h>
#include <md/render/render_target.h>
#include <md/render/software_device.h>

#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QTest>

using namespace md::gfx;
using namespace md::render;

namespace {

Pipeline colourPipeline()
{
    Pipeline pipeline;
    pipeline.shader = ShaderLibrary::vertexColour();
    pipeline.blend = BlendMode::None;
    pipeline.raster.antialias = false;
    return pipeline;
}

DrawCommand meshCommand(const Mesh2D& mesh)
{
    DrawCommand command;
    command.kind = CommandKind::DrawMesh2D;
    command.pipeline = colourPipeline();
    command.mesh2d = mesh;
    return command;
}

// A two-colour split: the left logical half red, the right half green.
CommandBuffer twoColourMesh(qreal width, qreal height)
{
    CommandBuffer buffer;
    buffer.add(meshCommand(Mesh2D::rect(QRectF(0.0, 0.0, width / 2.0, height),
                                        QColor(Qt::red))));
    buffer.add(meshCommand(Mesh2D::rect(QRectF(width / 2.0, 0.0, width / 2.0, height),
                                        QColor(Qt::green))));
    return buffer;
}

int countPixelsDifferentFrom(const QImage& image, const QRect& rect,
                             const QColor& background)
{
    const QRect bounded = rect.intersected(image.rect());
    int count = 0;
    for (int y = bounded.top(); y <= bounded.bottom(); ++y) {
        for (int x = bounded.left(); x <= bounded.right(); ++x) {
            if (image.pixelColor(x, y) != background)
                ++count;
        }
    }
    return count;
}

} // namespace

class TestRenderSoftwareDevice : public QObject
{
    Q_OBJECT

private slots:
    void capabilitiesDescribeTheDevice();
    void twoColourMeshLandsExpectedPixels();
    void devicePixelRatioIsAppliedToGeometry();
    void textCommandProducesPixelsInsideItsRect();
    void pathCommandFillsItsInterior();
    void oversizedCommandBufferIsRefusedWithoutTouchingTheTarget();
    void invalidTargetIsRefused();
    void emptyBufferDrawsNothing();
};

void TestRenderSoftwareDevice::capabilitiesDescribeTheDevice()
{
    SoftwareDevice device;

    QVERIFY(device.isAvailable());
    QCOMPARE(device.name(), QStringLiteral("software"));

    const DeviceCapabilities capabilities = device.capabilities();
    QVERIFY(capabilities.text);
    QVERIFY(capabilities.paths);
    QVERIFY(capabilities.images);
    QVERIFY(capabilities.depthBuffer);
    QVERIFY(capabilities.shaders);
    QCOMPARE(capabilities.backendName, QStringLiteral("software"));

    const RasterCaps gfxCaps;
    QCOMPARE(capabilities.maxTriangles, gfxCaps.maxTriangles);
    QCOMPARE(capabilities.maxTargetPixels, gfxCaps.maxTargetPixels);
}

void TestRenderSoftwareDevice::twoColourMeshLandsExpectedPixels()
{
    SoftwareDevice device;

    RenderTarget target(QSizeF(8.0, 8.0));
    QVERIFY(target.isValid());
    QCOMPARE(target.pixelSize(), QSize(8, 8));
    target.clear(QColor(Qt::black));

    const RasterStats stats = device.submit(target, twoColourMesh(8.0, 8.0));

    QCOMPARE(stats.trianglesDrawn, 4);
    QCOMPARE(stats.pixelsWritten, 64);
    QCOMPARE(target.image().pixelColor(1, 4), QColor(Qt::red));
    QCOMPARE(target.image().pixelColor(6, 4), QColor(Qt::green));
    QCOMPARE(target.image().pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(target.image().pixelColor(7, 7), QColor(Qt::green));
    QVERIFY(!target.image().isNull());
    QCOMPARE(target.image().format(), QImage::Format_ARGB32_Premultiplied);
}

void TestRenderSoftwareDevice::devicePixelRatioIsAppliedToGeometry()
{
    SoftwareDevice device;

    RenderTargetFormat format;
    format.devicePixelRatio = 2.0;
    RenderTarget target(QSizeF(8.0, 8.0), format);
    QVERIFY(target.isValid());
    QCOMPARE(target.pixelSize(), QSize(16, 16));
    target.clear(QColor(Qt::black));

    // The buffer still describes 8 x 8 logical units, so each half is 4 x 8
    // logical units and must cover 8 x 16 physical pixels.
    const RasterStats stats = device.submit(target, twoColourMesh(8.0, 8.0));

    QCOMPARE(stats.trianglesDrawn, 4);
    QCOMPARE(stats.pixelsWritten, 256);
    QCOMPARE(target.image().pixelColor(1, 1), QColor(Qt::red));
    QCOMPARE(target.image().pixelColor(6, 12), QColor(Qt::red));
    QCOMPARE(target.image().pixelColor(13, 13), QColor(Qt::green));
    QCOMPARE(target.image().pixelColor(9, 8), QColor(Qt::green));

    // The left half ends at logical x = 4, i.e. pixel 8.
    QCOMPARE(target.image().pixelColor(7, 8), QColor(Qt::red));
    QCOMPARE(target.image().pixelColor(8, 8), QColor(Qt::green));
}

void TestRenderSoftwareDevice::textCommandProducesPixelsInsideItsRect()
{
    SoftwareDevice device;

    RenderTarget target(QSizeF(120.0, 40.0));
    QVERIFY(target.isValid());
    target.clear(QColor(Qt::white));

    QFont font;
    font.setPixelSize(18);

    DrawCommand command;
    command.kind = CommandKind::DrawText;
    command.text = QStringLiteral("Render");
    command.textPosition = QPointF(8.0, 26.0);
    command.font = font;
    command.colour = QColor(Qt::black);

    CommandBuffer buffer;
    buffer.add(command);

    const RasterStats stats = device.submit(target, buffer);
    QCOMPARE(stats.trianglesDrawn, 0);
    QCOMPARE(stats.pixelsWritten, 0);

    // The baseline is at y = 26 and the font is 18 px tall, so the glyphs sit
    // inside this rectangle; the right margin must stay background.
    const int glyphPixels =
        countPixelsDifferentFrom(target.image(), QRect(8, 8, 80, 20), QColor(Qt::white));
    QVERIFY2(glyphPixels > 0, "the text command produced no pixels");
    QCOMPARE(target.image().pixelColor(118, 2), QColor(Qt::white));
    QCOMPARE(target.image().pixelColor(118, 38), QColor(Qt::white));
}

void TestRenderSoftwareDevice::pathCommandFillsItsInterior()
{
    SoftwareDevice device;

    // Ratio 2: a path given in logical coordinates must be scaled, or the
    // centre of the surface would stay background.
    RenderTargetFormat format;
    format.devicePixelRatio = 2.0;
    RenderTarget target(QSizeF(32.0, 32.0), format);
    QVERIFY(target.isValid());
    QCOMPARE(target.pixelSize(), QSize(64, 64));
    target.clear(QColor(Qt::white));

    QPainterPath path;
    path.addRect(QRectF(8.0, 8.0, 16.0, 16.0));

    DrawCommand command;
    command.kind = CommandKind::DrawPath;
    command.path = path;
    command.pen = QPen(Qt::NoPen);
    command.brush = QBrush(QColor(Qt::red));

    CommandBuffer buffer;
    buffer.add(command);
    device.submit(target, buffer);

    // Logical (16, 16) is physical (32, 32) and lies inside the rectangle.
    QCOMPARE(target.image().pixelColor(32, 32), QColor(Qt::red));
    QCOMPARE(target.image().pixelColor(18, 18), QColor(Qt::red));

    // Logical (4, 4) is outside the rectangle in both coordinate systems.
    QCOMPARE(target.image().pixelColor(8, 8), QColor(Qt::white));
    QCOMPARE(target.image().pixelColor(0, 0), QColor(Qt::white));
}

void TestRenderSoftwareDevice::oversizedCommandBufferIsRefusedWithoutTouchingTheTarget()
{
    SoftwareDevice device;

    RenderTarget target(QSizeF(8.0, 8.0));
    QVERIFY(target.isValid());
    const QColor background(10, 20, 30, 255);
    target.clear(background);
    const QImage before = target.readBack();

    // 150 x 100 cells is 30 000 triangles: valid on its own, over the 65 536
    // cap once three of them share a buffer.
    const Mesh2D grid = Mesh2D::grid(QRectF(0.0, 0.0, 8.0, 8.0), 150, 100,
                                     QColor(Qt::red));
    QVERIFY(grid.isValid());

    CommandBuffer oversized;
    oversized.add(meshCommand(grid));
    oversized.add(meshCommand(grid));
    oversized.add(meshCommand(grid));

    const RasterStats refused = device.submit(target, oversized);

    QCOMPARE(refused.trianglesDrawn, 0);
    QCOMPARE(refused.pixelsWritten, 0);
    QVERIFY2(!device.lastError().isEmpty(), "a refusal must be reported");
    QCOMPARE(target.readBack(), before);
    QCOMPARE(target.image().pixelColor(0, 0), background);

    // The refusal must not poison the device: the next frame still draws.
    CommandBuffer small;
    small.add(meshCommand(Mesh2D::rect(QRectF(0.0, 0.0, 4.0, 4.0), QColor(Qt::red))));
    const RasterStats recovered = device.submit(target, small);
    QCOMPARE(recovered.trianglesDrawn, 2);
    QCOMPARE(recovered.pixelsWritten, 16);
    QCOMPARE(target.image().pixelColor(1, 1), QColor(Qt::red));
}

void TestRenderSoftwareDevice::invalidTargetIsRefused()
{
    SoftwareDevice device;

    RenderTarget target;
    QVERIFY(!target.isValid());

    const RasterStats stats = device.submit(target, CommandBuffer());
    QCOMPARE(stats.trianglesDrawn, 0);
    QCOMPARE(stats.pixelsWritten, 0);
    QVERIFY(!device.lastError().isEmpty());
}

void TestRenderSoftwareDevice::emptyBufferDrawsNothing()
{
    SoftwareDevice device;

    RenderTarget target(QSizeF(4.0, 4.0));
    target.clear(QColor(Qt::black));

    const RasterStats stats = device.submit(target, CommandBuffer());
    QCOMPARE(stats.trianglesDrawn, 0);
    QCOMPARE(stats.pixelsWritten, 0);
    QCOMPARE(target.image().pixelColor(2, 2), QColor(Qt::black));
}

QTEST_MAIN(TestRenderSoftwareDevice)

#include "tst_render_software_device.moc"
