// render layer tests: RHI/software parity, the failure paths, and resize.
//
// The RHI device is optional in two ways: the build may not have the shipped
// rhi/ headers (PERIODICITY_HAS_RHI=0), and the machine may have no QRhi
// backend that can create a device (a headless CI box). Both are supported
// states, so initTestCase() QSKIPs cleanly instead of failing.
//
// When a device is available, the same CommandBuffer is submitted to a
// SoftwareDevice and to RhiDevice. The comparison is per command kind and uses
// the tolerance documented in md/render/rhi_device.h:
//
//   kind         max channel delta   max differing pixels
//   -----------  ------------------  --------------------
//   Clear        1                   0 %
//   SetScissor   1                   0 %
//   DrawMesh2D   4                   3 %
//   DrawMesh3D   4                   3 %
//   DrawImage    4                   3 %
//   DrawPath     8                   8 %
//   DrawText     32                  15 %
//
// The largest differences are on antialiased edges (the software 2D path
// 4x4-supersamples, the GPU has one sample per pixel) and on text/path fringes
// (the bridge paints on a premultiplied read-back while the software target is
// straight ARGB). Clear and SetScissor are exact, which is why their table
// allows zero differing pixels.

#include <md/gfx/command_buffer.h>
#include <md/gfx/mesh.h>
#include <md/gfx/pipeline.h>
#include <md/gfx/shader.h>
#include <md/render/device_factory.h>
#include <md/render/rhi_device.h>
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
#include <QSizeF>
#include <QString>
#include <QTest>

#include <cmath>
#include <memory>

using namespace md::gfx;
using namespace md::render;

namespace {

struct Tolerance
{
    int maxChannelDelta = 0;
    int maxDifferingPercent = 0;
};

// Returns false and fills `why` when the images differ more than `tolerance`.
bool compareWithin(const QImage& software, const QImage& rhi,
                   const Tolerance& tolerance, QString* why)
{
    if (software.size() != rhi.size()) {
        *why = QStringLiteral("size mismatch: software %1x%2, RHI %3x%4")
                   .arg(software.width()).arg(software.height())
                   .arg(rhi.width()).arg(rhi.height());
        return false;
    }

    int differing = 0;
    int worst = 0;
    QPoint worstPoint;
    for (int y = 0; y < software.height(); ++y) {
        for (int x = 0; x < software.width(); ++x) {
            const QColor a = software.pixelColor(x, y);
            const QColor b = rhi.pixelColor(x, y);
            const int delta = qMax(qMax(qAbs(a.red() - b.red()),
                                       qAbs(a.green() - b.green())),
                                   qMax(qAbs(a.blue() - b.blue()),
                                        qAbs(a.alpha() - b.alpha())));
            if (delta > worst) {
                worst = delta;
                worstPoint = QPoint(x, y);
            }
            if (delta > tolerance.maxChannelDelta)
                ++differing;
        }
    }

    const int total = software.width() * software.height();
    const int allowed = (total * tolerance.maxDifferingPercent) / 100;
    if (differing > allowed) {
        *why = QStringLiteral("software and RHI differ at %1 of %2 pixels "
                              "(allowed %3); worst channel delta %4 at (%5,%6)")
                   .arg(differing).arg(total).arg(allowed).arg(worst)
                   .arg(worstPoint.x()).arg(worstPoint.y());
        return false;
    }
    return true;
}

Pipeline colourPipeline(BlendMode blend = BlendMode::None, bool antialias = false)
{
    Pipeline pipeline;
    pipeline.shader = ShaderLibrary::vertexColour();
    pipeline.blend = blend;
    pipeline.raster.antialias = antialias;
    return pipeline;
}

Pipeline solidPipeline(BlendMode blend = BlendMode::None)
{
    Pipeline pipeline;
    pipeline.shader = ShaderLibrary::solid();
    pipeline.blend = blend;
    pipeline.raster.antialias = false;
    pipeline.raster.cullBack = false;
    pipeline.depth.test = true;
    pipeline.depth.write = true;
    return pipeline;
}

DrawCommand mesh2DCommand(const Mesh2D& mesh, const Pipeline& pipeline,
                          const QRect& viewport = QRect())
{
    DrawCommand command;
    command.kind = CommandKind::DrawMesh2D;
    command.pipeline = pipeline;
    command.mesh2d = mesh;
    command.viewport = viewport;
    return command;
}

DrawCommand clearCommand(const QColor& colour, const QRect& scissor = QRect())
{
    DrawCommand command;
    command.kind = CommandKind::Clear;
    command.colour = colour;
    command.scissor = scissor;
    return command;
}

DrawCommand scissorCommand(const QRect& scissor)
{
    DrawCommand command;
    command.kind = CommandKind::SetScissor;
    command.scissor = scissor;
    return command;
}

CommandBuffer mesh3DDepthBuffer()
{
    // Two overlapping quads. The red one is farther (z = 0.2), the green one is
    // nearer (z = 0.0) and is submitted second, so both the software z-buffer
    // and QRhi's depth test must leave green in the overlap.
    const auto quad = [](float z, const QColor& colour) {
        QVector<Vertex3D> vertices;
        const auto vertex = [&](float x, float y) {
            Vertex3D v;
            v.position = QVector3D(x, y, z);
            v.colour = colour;
            v.normal = QVector3D(0.0F, 0.0F, 1.0F);
            return v;
        };
        vertices = {vertex(-0.8F, -0.8F), vertex(0.8F, -0.8F),
                    vertex(0.8F, 0.8F), vertex(-0.8F, 0.8F)};
        return Mesh3D(vertices, {0, 1, 2, 0, 2, 3});
    };

    CommandBuffer buffer;
    DrawCommand redCommand;
    redCommand.kind = CommandKind::DrawMesh3D;
    redCommand.pipeline = solidPipeline();
    redCommand.mesh3d = quad(0.2F, QColor(Qt::red));
    redCommand.uniforms.set(QStringLiteral("colour"), QColor(Qt::red));
    buffer.add(redCommand);

    DrawCommand greenCommand = redCommand;
    greenCommand.mesh3d = quad(0.0F, QColor(Qt::green));
    greenCommand.uniforms.set(QStringLiteral("colour"), QColor(Qt::green));
    buffer.add(greenCommand);
    return buffer;
}

CommandBuffer imageBuffer()
{
    QImage image(8, 8, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixelColor(x, y,
                                ((x / 2 + y / 2) % 2 == 0) ? QColor(Qt::red)
                                                           : QColor(Qt::blue));
        }
    }

    DrawCommand command;
    command.kind = CommandKind::DrawImage;
    command.image = image;
    command.viewport = QRect(8, 8, 48, 48);
    command.pipeline.blend = BlendMode::None;

    CommandBuffer buffer;
    buffer.add(command);
    return buffer;
}

CommandBuffer pathBuffer()
{
    QPainterPath path;
    path.addRect(QRectF(8.0, 8.0, 16.0, 16.0));

    DrawCommand command;
    command.kind = CommandKind::DrawPath;
    command.path = path;
    command.pen = QPen(Qt::NoPen);
    command.brush = QBrush(QColor(Qt::red));

    CommandBuffer buffer;
    buffer.add(command);
    return buffer;
}

CommandBuffer textBuffer()
{
    QFont font;
    font.setPixelSize(20);

    DrawCommand command;
    command.kind = CommandKind::DrawText;
    command.text = QStringLiteral("Parity");
    command.textPosition = QPointF(8.0, 30.0);
    command.font = font;
    command.colour = QColor(Qt::black);

    CommandBuffer buffer;
    buffer.add(command);
    return buffer;
}

CommandBuffer redRect(const QRectF& rectangle, const QColor& colour)
{
    DrawCommand command;
    command.kind = CommandKind::DrawMesh2D;
    command.pipeline = colourPipeline();
    command.mesh2d = Mesh2D::rect(rectangle, colour);

    CommandBuffer buffer;
    buffer.add(command);
    return buffer;
}

int countNonBackground(const QImage& image, const QColor& background)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) != background)
                ++count;
        }
    }
    return count;
}

} // namespace

class TestRenderRhi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void backendNameIsStable();
    void factoryRequestIsRunnable();
    void clearAndScissorMatch();
    void mesh2DMatch();
    void mesh3DDepthMatches();
    void imageMatch();
    void pathMatch();
    void textMatch();

    void brokenPipelineFallsBack();
    void resizedTargetReallocates();

private:
    std::unique_ptr<RhiDevice> device_;
    QString unavailableReason_;
};

void TestRenderRhi::initTestCase()
{
#if !defined(PERIODICITY_HAS_RHI) || !PERIODICITY_HAS_RHI
    unavailableReason_ = QStringLiteral(
        "PERIODICITY_HAS_RHI=0: the shipped rhi/ headers were not found at "
        "configure time, so the RHI device is not compiled in");
    QSKIP(qPrintable(unavailableReason_));
#endif

    device_ = std::make_unique<RhiDevice>();
    if (!device_->isAvailable()) {
        unavailableReason_ = device_->lastError();
        if (unavailableReason_.isEmpty())
            unavailableReason_ = QStringLiteral("RhiDevice reported unavailable without a reason");
        device_.reset();
        QSKIP(qPrintable(unavailableReason_));
    }

    QCOMPARE(device_->name(), QStringLiteral("rhi"));
    QCOMPARE(device_->capabilities().backendName, QStringLiteral("rhi"));
    QVERIFY2(!device_->apiName().isEmpty(),
             "an available RHI device must name the backend it created");
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));
}

void TestRenderRhi::backendNameIsStable()
{
    // Independent of the machine: the factory's identifier for the backend is
    // part of the public API and must not depend on probing.
    QCOMPARE(DeviceFactory::backendName(Backend::Rhi), QStringLiteral("rhi"));
    QCOMPARE(backendName(Backend::Rhi), QStringLiteral("rhi"));
    QCOMPARE(DeviceFactory::preferredBackend(), Backend::Software);
}

void TestRenderRhi::factoryRequestIsRunnable()
{
    if (!device_)
        QSKIP(qPrintable(unavailableReason_));

    const std::shared_ptr<RenderDevice> device = DeviceFactory::create(Backend::Rhi);
    QVERIFY(device != nullptr);
    QVERIFY(device->isAvailable());
    QCOMPARE(device->name(), QStringLiteral("rhi"));
    QCOMPARE(device->capabilities().backendName, device->name());
}

void TestRenderRhi::clearAndScissorMatch()
{
    if (!device_)
        QSKIP(qPrintable(unavailableReason_));

    CommandBuffer buffer;
    buffer.add(clearCommand(QColor(10, 20, 30, 255)));
    buffer.add(scissorCommand(QRect(8, 8, 24, 24)));
    buffer.add(clearCommand(QColor(200, 40, 90, 255)));

    RenderTarget softwareTarget(QSizeF(64.0, 48.0));
    RenderTarget rhiTarget(QSizeF(64.0, 48.0));
    QVERIFY(softwareTarget.isValid());
    QVERIFY(rhiTarget.isValid());
    softwareTarget.clear(QColor(Qt::magenta));
    rhiTarget.clear(QColor(Qt::magenta));

    SoftwareDevice software;
    const RasterStats softwareStats = software.submit(softwareTarget, buffer);
    const RasterStats rhiStats = device_->submit(rhiTarget, buffer);
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));

    QCOMPARE(rhiStats.pixelsWritten, softwareStats.pixelsWritten);

    QString why;
    QVERIFY2(compareWithin(softwareTarget.image(), rhiTarget.image(), {1, 0}, &why),
             qPrintable(why));

    // The scissored clear must not leak outside its rectangle.
    QCOMPARE(rhiTarget.image().pixelColor(4, 4), QColor(10, 20, 30, 255));
    QCOMPARE(rhiTarget.image().pixelColor(16, 16), QColor(200, 40, 90, 255));
}

void TestRenderRhi::mesh2DMatch()
{
    if (!device_)
        QSKIP(qPrintable(unavailableReason_));

    Pipeline pipeline = colourPipeline();
    QVERIFY(pipeline.isValid());

    CommandBuffer buffer;
    buffer.add(mesh2DCommand(Mesh2D::rect(QRectF(0.0, 0.0, 24.0, 48.0),
                                          QColor(Qt::red)), pipeline,
                             QRect(8, 8, 48, 32)));
    buffer.add(mesh2DCommand(Mesh2D::rect(QRectF(24.0, 0.0, 24.0, 48.0),
                                          QColor(Qt::green)), pipeline,
                             QRect(8, 8, 48, 32)));
    buffer.add(mesh2DCommand(Mesh2D::rect(QRectF(12.0, 12.0, 16.0, 16.0),
                                          QColor(0, 0, 255, 128)), pipeline,
                             QRect(8, 8, 48, 32)));

    RenderTarget softwareTarget(QSizeF(64.0, 48.0));
    RenderTarget rhiTarget(QSizeF(64.0, 48.0));
    softwareTarget.clear(QColor(Qt::black));
    rhiTarget.clear(QColor(Qt::black));

    SoftwareDevice software;
    const RasterStats softwareStats = software.submit(softwareTarget, buffer);
    const RasterStats rhiStats = device_->submit(rhiTarget, buffer);
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));
    QCOMPARE(rhiStats.trianglesDrawn, softwareStats.trianglesDrawn);

    QString why;
    QVERIFY2(compareWithin(softwareTarget.image(), rhiTarget.image(), {4, 3}, &why),
             qPrintable(why));
    QCOMPARE(rhiTarget.image().pixelColor(12, 24), QColor(Qt::red));
    QCOMPARE(rhiTarget.image().pixelColor(44, 24), QColor(Qt::green));
}

void TestRenderRhi::mesh3DDepthMatches()
{
    if (!device_)
        QSKIP(qPrintable(unavailableReason_));

    const CommandBuffer buffer = mesh3DDepthBuffer();
    RenderTarget softwareTarget(QSizeF(64.0, 64.0));
    RenderTarget rhiTarget(QSizeF(64.0, 64.0));
    softwareTarget.clear(QColor(Qt::black));
    rhiTarget.clear(QColor(Qt::black));

    SoftwareDevice software;
    const RasterStats softwareStats = software.submit(softwareTarget, buffer);
    const RasterStats rhiStats = device_->submit(rhiTarget, buffer);
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));
    QCOMPARE(rhiStats.trianglesDrawn, softwareStats.trianglesDrawn);

    QString why;
    QVERIFY2(compareWithin(softwareTarget.image(), rhiTarget.image(), {4, 3}, &why),
             qPrintable(why));
    QCOMPARE(rhiTarget.image().pixelColor(32, 32), QColor(Qt::green));
}

void TestRenderRhi::imageMatch()
{
    if (!device_)
        QSKIP(qPrintable(unavailableReason_));

    const CommandBuffer buffer = imageBuffer();
    RenderTarget softwareTarget(QSizeF(64.0, 64.0));
    RenderTarget rhiTarget(QSizeF(64.0, 64.0));
    softwareTarget.clear(QColor(Qt::white));
    rhiTarget.clear(QColor(Qt::white));

    SoftwareDevice software;
    software.submit(softwareTarget, buffer);
    const RasterStats rhiStats = device_->submit(rhiTarget, buffer);
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));
    QVERIFY(rhiStats.pixelsWritten > 0);

    QString why;
    QVERIFY2(compareWithin(softwareTarget.image(), rhiTarget.image(), {4, 3}, &why),
             qPrintable(why));
}

void TestRenderRhi::pathMatch()
{
    if (!device_)
        QSKIP(qPrintable(unavailableReason_));

    RenderTargetFormat format;
    format.devicePixelRatio = 2.0;
    const CommandBuffer buffer = pathBuffer();

    RenderTarget softwareTarget(QSizeF(32.0, 32.0), format);
    RenderTarget rhiTarget(QSizeF(32.0, 32.0), format);
    softwareTarget.clear(QColor(Qt::white));
    rhiTarget.clear(QColor(Qt::white));

    SoftwareDevice software;
    software.submit(softwareTarget, buffer);
    device_->submit(rhiTarget, buffer);
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));

    QString why;
    QVERIFY2(compareWithin(softwareTarget.image(), rhiTarget.image(), {8, 8}, &why),
             qPrintable(why));
    QCOMPARE(rhiTarget.image().size(), QSize(64, 64));
    QCOMPARE(rhiTarget.image().pixelColor(32, 32), QColor(Qt::red));
}

void TestRenderRhi::textMatch()
{
    if (!device_)
        QSKIP(qPrintable(unavailableReason_));

    const CommandBuffer buffer = textBuffer();
    RenderTarget softwareTarget(QSizeF(160.0, 48.0));
    RenderTarget rhiTarget(QSizeF(160.0, 48.0));
    softwareTarget.clear(QColor(Qt::white));
    rhiTarget.clear(QColor(Qt::white));

    SoftwareDevice software;
    software.submit(softwareTarget, buffer);
    device_->submit(rhiTarget, buffer);
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));

    QVERIFY(countNonBackground(rhiTarget.image(), QColor(Qt::white)) > 0);
    QString why;
    QVERIFY2(compareWithin(softwareTarget.image(), rhiTarget.image(), {32, 15}, &why),
             qPrintable(why));
}

void TestRenderRhi::brokenPipelineFallsBack()
{
    RhiDevice device;
    if (!device.isAvailable())
        QSKIP(qPrintable(device.lastError()));

    // Two opaque garbage blobs instead of .qsb data: QShader::fromSerialized
    // rejects them, which is reported before any GPU resource is created.
    const QByteArray brokenVertex("this is not a serialized QShader");
    const QByteArray brokenFragment("this is not a serialized QShader either");
    QVERIFY(!device.initializeWithSolidShaderData(brokenVertex, brokenFragment));
    QVERIFY2(!device.shaderLog().isEmpty(),
             "a rejected shader must leave the diagnostic in shaderLog()");
    QVERIFY2(!device.lastError().isEmpty(),
             "a rejected shader must set lastError()");
    QVERIFY(!device.isAvailable());
    QVERIFY(!device.isInitialized());

    RenderTarget target(QSizeF(8.0, 8.0));
    QVERIFY(target.isValid());
    target.clear(QColor(Qt::black));

    const RasterStats stats =
        device.submit(target, redRect(QRectF(0.0, 0.0, 8.0, 8.0), QColor(Qt::red)));
    QCOMPARE(stats.pixelsWritten, 64);
    QCOMPARE(target.image().pixelColor(4, 4), QColor(Qt::red));
    QVERIFY2(!device.lastError().isEmpty(),
             "the software fallback must leave the pipeline diagnostic in place");
}

void TestRenderRhi::resizedTargetReallocates()
{
    if (!device_)
        QSKIP(qPrintable(unavailableReason_));

    const CommandBuffer buffer = redRect(QRectF(0.0, 0.0, 8.0, 8.0), QColor(Qt::red));

    RenderTarget target(QSizeF(16.0, 16.0));
    QVERIFY(target.isValid());
    target.clear(QColor(Qt::black));
    QCOMPARE(device_->submit(target, buffer).trianglesDrawn, 2);
    QCOMPARE(target.image().pixelColor(4, 4), QColor(Qt::red));
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));

    // Grow: the colour/depth attachments and the pipelines are rebuilt for the
    // new pixel size, and the frame still lands in the new image.
    QVERIFY(target.resize(QSizeF(32.0, 32.0)));
    QCOMPARE(target.pixelSize(), QSize(32, 32));
    target.clear(QColor(Qt::blue));
    QCOMPARE(device_->submit(target, buffer).trianglesDrawn, 2);
    QCOMPARE(target.image().pixelColor(4, 4), QColor(Qt::red));
    QCOMPARE(target.image().pixelColor(20, 20), QColor(Qt::blue));
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));

    // Shrink again: the previous, larger attachments must not be reused.
    QVERIFY(target.resize(QSizeF(8.0, 8.0)));
    QCOMPARE(target.pixelSize(), QSize(8, 8));
    target.clear(QColor(Qt::black));
    QCOMPARE(device_->submit(target, buffer).trianglesDrawn, 2);
    QCOMPARE(target.image().size(), QSize(8, 8));
    QCOMPARE(target.image().pixelColor(4, 4), QColor(Qt::red));
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));
}

QTEST_MAIN(TestRenderRhi)

#include "tst_render_rhi.moc"
