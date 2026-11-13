// render layer tests: device selection and the fallback path.
//
// The software device is the one every caller can depend on, so the factory
// must always return it for Backend::Software. Backend::OpenGL is opt-in: it
// returns a working GlDevice when the probe succeeds and a SoftwareDevice with
// the reason when it does not. preferredBackend() stays Software in both cases,
// because an OpenGL context is a capability, not the process default.

#include <md/gfx/command_buffer.h>
#include <md/gfx/mesh.h>
#include <md/gfx/pipeline.h>
#include <md/gfx/shader.h>
#include <md/render/device_factory.h>
#include <md/render/render_target.h>
#include <md/render/software_device.h>

#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QTest>

using namespace md::gfx;
using namespace md::render;

namespace {

CommandBuffer redQuad()
{
    Pipeline pipeline;
    pipeline.shader = ShaderLibrary::vertexColour();
    pipeline.blend = BlendMode::None;
    pipeline.raster.antialias = false;

    DrawCommand command;
    command.kind = CommandKind::DrawMesh2D;
    command.pipeline = pipeline;
    command.mesh2d = Mesh2D::rect(QRectF(0.0, 0.0, 4.0, 4.0), QColor(Qt::red));

    CommandBuffer buffer;
    buffer.add(command);
    return buffer;
}

} // namespace

class TestRenderFactory : public QObject
{
    Q_OBJECT

private slots:
    void softwareIsAlwaysAvailable();
    void openGLRequestReturnsARunnableDevice();
    void fallbackDeviceStillDraws();
    void preferredBackendIsStable();
    void backendNamesAreStable();
    void probeIsCachedUntilReset();
};

void TestRenderFactory::softwareIsAlwaysAvailable()
{
    const std::shared_ptr<RenderDevice> device = DeviceFactory::create(Backend::Software);
    QVERIFY(device != nullptr);
    QVERIFY(device->isAvailable());
    QCOMPARE(device->name(), QStringLiteral("software"));
    QVERIFY(device->lastError().isEmpty());

    const DeviceCapabilities capabilities = device->capabilities();
    QVERIFY(capabilities.text);
    QVERIFY(capabilities.paths);
    QVERIFY(capabilities.images);
    QVERIFY(capabilities.depthBuffer);
    QVERIFY(capabilities.shaders);
    QCOMPARE(capabilities.backendName, QStringLiteral("software"));
    QVERIFY(capabilities.maxTriangles > 0);
    QVERIFY(capabilities.maxTargetPixels > 0);

    // The free-function form is the same factory.
    const std::shared_ptr<RenderDevice> alias = create(Backend::Software);
    QVERIFY(alias != nullptr);
    QVERIFY(alias->isAvailable());
}

void TestRenderFactory::openGLRequestReturnsARunnableDevice()
{
    const std::shared_ptr<RenderDevice> device = DeviceFactory::create(Backend::OpenGL);
    QVERIFY(device != nullptr);
    QVERIFY(device->isAvailable());

    // The request succeeds as a GlDevice, or degrades to the software device
    // with the probe's explanation in lastError(). Those are the only two
    // documented outcomes; an unavailable device is never returned.
    const QString backend = device->name();
    QVERIFY2(backend == QStringLiteral("opengl") || backend == QStringLiteral("software"),
             qPrintable(QStringLiteral("unexpected backend name: %1").arg(backend)));
    QCOMPARE(device->capabilities().backendName, backend);
    if (backend == QStringLiteral("software"))
        QVERIFY2(!device->lastError().isEmpty(),
                 "the software fallback must explain why OpenGL was not used");
    else
        QVERIFY2(device->lastError().isEmpty(),
                 qPrintable(device->lastError()));

    QCOMPARE(DeviceFactory::preferredBackend(), Backend::Software);
}

void TestRenderFactory::fallbackDeviceStillDraws()
{
    const std::shared_ptr<RenderDevice> device = DeviceFactory::create(Backend::OpenGL);

    RenderTarget target(QSizeF(4.0, 4.0));
    QVERIFY(target.isValid());
    target.clear(QColor(Qt::black));

    const RasterStats stats = device->submit(target, redQuad());
    QCOMPARE(stats.pixelsWritten, 16);
    QCOMPARE(target.image().pixelColor(2, 2), QColor(Qt::red));
}

void TestRenderFactory::preferredBackendIsStable()
{
    const Backend first = DeviceFactory::preferredBackend();
    QCOMPARE(first, Backend::Software);

    for (int i = 0; i < 8; ++i) {
        QCOMPARE(DeviceFactory::preferredBackend(), first);
        QCOMPARE(preferredBackend(), first);
    }

    QVERIFY(!DeviceFactory::probeReason().isEmpty());
    QCOMPARE(DeviceFactory::probeReason(), DeviceFactory::probeReason());
}

void TestRenderFactory::backendNamesAreStable()
{
    QCOMPARE(DeviceFactory::backendName(Backend::Software), QStringLiteral("software"));
    QCOMPARE(DeviceFactory::backendName(Backend::OpenGL), QStringLiteral("opengl"));
    QCOMPARE(DeviceFactory::backendName(), QStringLiteral("software"));
    QCOMPARE(backendName(Backend::Software), QStringLiteral("software"));
    QCOMPARE(backendName(), QStringLiteral("software"));

    // A device's own name matches its capabilities' backend name.
    const std::shared_ptr<RenderDevice> device = DeviceFactory::create(Backend::Software);
    QCOMPARE(device->name(), device->capabilities().backendName);
}

void TestRenderFactory::probeIsCachedUntilReset()
{
    const QString before = DeviceFactory::probeReason();
    QVERIFY(!before.isEmpty());

    // The cached probe answers without probing again: the reason is identical.
    QCOMPARE(DeviceFactory::probeReason(), before);

    // resetProbe() makes the next query probe again; the answer under the same
    // platform is still the software device with a reason.
    DeviceFactory::resetProbe();
    QCOMPARE(DeviceFactory::preferredBackend(), Backend::Software);
    QVERIFY(!DeviceFactory::probeReason().isEmpty());
}

QTEST_MAIN(TestRenderFactory)

#include "tst_render_factory.moc"
