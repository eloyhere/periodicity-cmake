// render layer tests: device selection and the fallback path.
//
// The software device is the one every caller can depend on, so the factory
// must always return it for Backend::Software. Backend::OpenGL is the interesting
// case in phase 2: there is no GL device yet, and the test asserts that the
// request degrades to software with a non-empty reason and a device that still
// draws. preferredBackend() must be stable, because callers cache it.

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
    void openGLFallsBackToSoftwareWithAReason();
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

void TestRenderFactory::openGLFallsBackToSoftwareWithAReason()
{
    const std::shared_ptr<RenderDevice> device = DeviceFactory::create(Backend::OpenGL);
    QVERIFY(device != nullptr);
    QVERIFY(device->isAvailable());
    QCOMPARE(device->name(), QStringLiteral("software"));
    QVERIFY2(!device->lastError().isEmpty(),
             "the fallback must explain why the OpenGL device was not used");
    QVERIFY(device->capabilities().shaders);

    // The test process runs with QT_QPA_PLATFORM=offscreen (see
    // tests/CMakeLists.txt), so the probe must have found the platform lacking
    // a GL surface. A build without Qt6::OpenGLWidgets has its own reason, so
    // only assert the platform detail when the platform is offscreen and the
    // reason names a platform at all.
    if (QGuiApplication::platformName().compare(QStringLiteral("offscreen"),
                                                Qt::CaseInsensitive)
        == 0) {
        const QString reason = device->lastError();
        const bool namesThePlatform =
            reason.contains(QStringLiteral("offscreen"), Qt::CaseInsensitive);
        const bool namesTheBuild =
            reason.contains(QStringLiteral("build time"), Qt::CaseInsensitive);
        const bool namesNoGui = reason.contains(QStringLiteral("QGuiApplication"),
                                                Qt::CaseInsensitive);
        QVERIFY2(namesThePlatform || namesTheBuild || namesNoGui,
                 qPrintable(QStringLiteral("imprecise fallback reason: %1").arg(reason)));
    }

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
