// render layer tests: OpenGL/software parity and the GL failure paths.
//
// The GL device is optional: the QPA plugin used by ctest is "offscreen" on
// most machines and cannot create a context. The test therefore skips cleanly
// when GlDevice::isAvailable() is false; unavailability is a supported state,
// not a failure.
//
// When a context is available, the same CommandBuffer is submitted to a
// SoftwareDevice and a GlDevice. The comparison is per command kind and uses
// the tolerance documented in md/render/gl_device.h:
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
// supersamples, GL does not) and on text/path fringes (the bridge paints on a
// premultiplied read-back while the software device paints straight ARGB).
// Everything else is an 8-bit filtering/rounding difference.

#include <md/gfx/command_buffer.h>
#include <md/gfx/mesh.h>
#include <md/gfx/pipeline.h>
#include <md/gfx/shader.h>
#include <md/render/gl_device.h>
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
bool compareWithin(const QImage& software, const QImage& gl, const Tolerance& tolerance,
                   QString* why)
{
    if (software.size() != gl.size()) {
        *why = QStringLiteral("size mismatch: software %1x%2, GL %3x%4")
                   .arg(software.width()).arg(software.height())
                   .arg(gl.width()).arg(gl.height());
        return false;
    }

    int differing = 0;
    int worst = 0;
    QPoint worstPoint;
    for (int y = 0; y < software.height(); ++y) {
        for (int x = 0; x < software.width(); ++x) {
            const QColor a = software.pixelColor(x, y);
            const QColor b = gl.pixelColor(x, y);
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
        *why = QStringLiteral("software and GL differ at %1 of %2 pixels "
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
    // and GL's depth test must leave green in the overlap.
    Mesh3D red;
    Mesh3D green;

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

    red = quad(0.2F, QColor(Qt::red));
    green = quad(0.0F, QColor(Qt::green));

    CommandBuffer buffer;
    DrawCommand redCommand;
    redCommand.kind = CommandKind::DrawMesh3D;
    redCommand.pipeline = solidPipeline();
    redCommand.mesh3d = red;
    redCommand.uniforms.set(QStringLiteral("colour"), QColor(Qt::red));
    buffer.add(redCommand);

    DrawCommand greenCommand = redCommand;
    greenCommand.pipeline = solidPipeline();
    greenCommand.mesh3d = green;
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

class TestRenderGlParity : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void clearAndScissorMatch();
    void mesh2DMatch();
    void mesh3DDepthMatches();
    void imageMatch();
    void pathMatch();
    void textMatch();

    void brokenShaderFallsBack();
    void droppedContextRecovers();

private:
    std::unique_ptr<GlDevice> device_;
    QString unavailableReason_;
};

void TestRenderGlParity::initTestCase()
{
    device_ = std::make_unique<GlDevice>();
    if (!device_->isAvailable()) {
        unavailableReason_ = device_->lastError();
        if (unavailableReason_.isEmpty())
            unavailableReason_ = QStringLiteral("GlDevice reported unavailable without a reason");
        device_.reset();
    }
}

void TestRenderGlParity::clearAndScissorMatch()
{
    if (!device_)
        QSKIP(qPrintable(unavailableReason_));

    CommandBuffer buffer;
    buffer.add(clearCommand(QColor(10, 20, 30, 255)));
    buffer.add(scissorCommand(QRect(8, 8, 24, 24)));
    buffer.add(clearCommand(QColor(200, 40, 90, 255)));

    RenderTarget softwareTarget(QSizeF(64.0, 48.0));
    RenderTarget glTarget(QSizeF(64.0, 48.0));
    QVERIFY(softwareTarget.isValid());
    QVERIFY(glTarget.isValid());
    softwareTarget.clear(QColor(Qt::magenta));
    glTarget.clear(QColor(Qt::magenta));

    SoftwareDevice software;
    const RasterStats softwareStats = software.submit(softwareTarget, buffer);
    const RasterStats glStats = device_->submit(glTarget, buffer);
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));

    QCOMPARE(glStats.trianglesDrawn, softwareStats.trianglesDrawn);
    QCOMPARE(glStats.pixelsWritten, softwareStats.pixelsWritten);

    QString why;
    QVERIFY2(compareWithin(softwareTarget.image(), glTarget.image(), {1, 0}, &why),
             qPrintable(why));

    // The scissored clear must not leak outside its rectangle.
    QCOMPARE(glTarget.image().pixelColor(4, 4), QColor(10, 20, 30, 255));
    QCOMPARE(glTarget.image().pixelColor(16, 16), QColor(200, 40, 90, 255));
}

void TestRenderGlParity::mesh2DMatch()
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
    RenderTarget glTarget(QSizeF(64.0, 48.0));
    softwareTarget.clear(QColor(Qt::black));
    glTarget.clear(QColor(Qt::black));

    SoftwareDevice software;
    const RasterStats softwareStats = software.submit(softwareTarget, buffer);
    const RasterStats glStats = device_->submit(glTarget, buffer);
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));
    QCOMPARE(glStats.trianglesDrawn, softwareStats.trianglesDrawn);

    QString why;
    QVERIFY2(compareWithin(softwareTarget.image(), glTarget.image(), {4, 3}, &why),
             qPrintable(why));
    QCOMPARE(glTarget.image().pixelColor(12, 24), QColor(Qt::red));
    QCOMPARE(glTarget.image().pixelColor(44, 24), QColor(Qt::green));
}

void TestRenderGlParity::mesh3DDepthMatches()
{
    if (!device_)
        QSKIP(qPrintable(unavailableReason_));

    const CommandBuffer buffer = mesh3DDepthBuffer();
    RenderTarget softwareTarget(QSizeF(64.0, 64.0));
    RenderTarget glTarget(QSizeF(64.0, 64.0));
    softwareTarget.clear(QColor(Qt::black));
    glTarget.clear(QColor(Qt::black));

    SoftwareDevice software;
    const RasterStats softwareStats = software.submit(softwareTarget, buffer);
    const RasterStats glStats = device_->submit(glTarget, buffer);
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));
    QCOMPARE(glStats.trianglesDrawn, softwareStats.trianglesDrawn);

    QString why;
    QVERIFY2(compareWithin(softwareTarget.image(), glTarget.image(), {4, 3}, &why),
             qPrintable(why));
    QCOMPARE(glTarget.image().pixelColor(32, 32), QColor(Qt::green));
}

void TestRenderGlParity::imageMatch()
{
    if (!device_)
        QSKIP(qPrintable(unavailableReason_));

    const CommandBuffer buffer = imageBuffer();
    RenderTarget softwareTarget(QSizeF(64.0, 64.0));
    RenderTarget glTarget(QSizeF(64.0, 64.0));
    softwareTarget.clear(QColor(Qt::white));
    glTarget.clear(QColor(Qt::white));

    SoftwareDevice software;
    software.submit(softwareTarget, buffer);
    const RasterStats glStats = device_->submit(glTarget, buffer);
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));
    QVERIFY(glStats.pixelsWritten > 0);

    QString why;
    QVERIFY2(compareWithin(softwareTarget.image(), glTarget.image(), {4, 3}, &why),
             qPrintable(why));
}

void TestRenderGlParity::pathMatch()
{
    if (!device_)
        QSKIP(qPrintable(unavailableReason_));

    RenderTargetFormat format;
    format.devicePixelRatio = 2.0;
    const CommandBuffer buffer = pathBuffer();

    RenderTarget softwareTarget(QSizeF(32.0, 32.0), format);
    RenderTarget glTarget(QSizeF(32.0, 32.0), format);
    softwareTarget.clear(QColor(Qt::white));
    glTarget.clear(QColor(Qt::white));

    SoftwareDevice software;
    software.submit(softwareTarget, buffer);
    device_->submit(glTarget, buffer);
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));

    QString why;
    QVERIFY2(compareWithin(softwareTarget.image(), glTarget.image(), {8, 8}, &why),
             qPrintable(why));
    QCOMPARE(glTarget.image().pixelColor(32, 32), QColor(Qt::red));
}

void TestRenderGlParity::textMatch()
{
    if (!device_)
        QSKIP(qPrintable(unavailableReason_));

    const CommandBuffer buffer = textBuffer();
    RenderTarget softwareTarget(QSizeF(160.0, 48.0));
    RenderTarget glTarget(QSizeF(160.0, 48.0));
    softwareTarget.clear(QColor(Qt::white));
    glTarget.clear(QColor(Qt::white));

    SoftwareDevice software;
    software.submit(softwareTarget, buffer);
    device_->submit(glTarget, buffer);
    QVERIFY2(device_->lastError().isEmpty(), qPrintable(device_->lastError()));

    QVERIFY(countNonBackground(glTarget.image(), QColor(Qt::white)) > 0);
    QString why;
    QVERIFY2(compareWithin(softwareTarget.image(), glTarget.image(), {32, 15}, &why),
             qPrintable(why));
}

void TestRenderGlParity::brokenShaderFallsBack()
{
    GlDevice device;
    if (!device.isAvailable())
        QSKIP(qPrintable(device.lastError()));

    const QString broken = QStringLiteral("#version 330 core\n"
                                          "this is not a shader\n");
    QVERIFY(!device.initializeWithShaderSources(broken, broken));
    QVERIFY2(!device.shaderLog().isEmpty(), "a failed compile must keep the driver log");
    QVERIFY2(!device.lastError().isEmpty(), "a failed compile must set lastError()");
    QVERIFY(!device.isAvailable());

    RenderTarget target(QSizeF(8.0, 8.0));
    target.clear(QColor(Qt::black));

    CommandBuffer buffer;
    DrawCommand command;
    command.kind = CommandKind::DrawMesh2D;
    command.pipeline = colourPipeline();
    command.mesh2d = Mesh2D::rect(QRectF(0.0, 0.0, 8.0, 8.0), QColor(Qt::red));
    buffer.add(command);

    const RasterStats stats = device.submit(target, buffer);
    QCOMPARE(stats.pixelsWritten, 64);
    QCOMPARE(target.image().pixelColor(4, 4), QColor(Qt::red));
    QVERIFY2(!device.lastError().isEmpty(),
             "the software fallback must leave the compile diagnostic in place");
}

void TestRenderGlParity::droppedContextRecovers()
{
    GlDevice device;
    if (!device.isAvailable())
        QSKIP(qPrintable(device.lastError()));

    CommandBuffer buffer;
    DrawCommand command;
    command.kind = CommandKind::DrawMesh2D;
    command.pipeline = colourPipeline();
    command.mesh2d = Mesh2D::rect(QRectF(0.0, 0.0, 8.0, 8.0), QColor(Qt::red));
    buffer.add(command);

    RenderTarget first(QSizeF(8.0, 8.0));
    first.clear(QColor(Qt::black));
    QCOMPARE(device.submit(first, buffer).pixelsWritten, 64);
    QCOMPARE(first.image().pixelColor(4, 4), QColor(Qt::red));

    device.simulateContextLoss();

    RenderTarget second(QSizeF(8.0, 8.0));
    second.clear(QColor(Qt::black));
    const RasterStats recovered = device.submit(second, buffer);
    QCOMPARE(recovered.pixelsWritten, 64);
    QCOMPARE(second.image().pixelColor(4, 4), QColor(Qt::red));

    // Either the context was re-initialised (available, with the recovery
    // diagnostic) or the device degraded permanently to software. Both paths
    // must still draw and must explain themselves.
    QVERIFY2(!device.lastError().isEmpty(), "a dropped context must be diagnosed");
    if (device.isAvailable()) {
        RenderTarget third(QSizeF(8.0, 8.0));
        third.clear(QColor(Qt::black));
        QCOMPARE(device.submit(third, buffer).pixelsWritten, 64);
        QCOMPARE(third.image().pixelColor(4, 4), QColor(Qt::red));
    }
}

QTEST_MAIN(TestRenderGlParity)

#include "tst_render_gl_parity.moc"
