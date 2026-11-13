#include <md/render/device_factory.h>

#include <md/render/software_device.h>

#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>

#if QT_CONFIG(opengl)
#include <QOpenGLContext>
#include <QSurfaceFormat>
#endif

#include <utility>

namespace md::render {

namespace {

struct ProbeState
{
    bool answered = false;
    Backend preferred = Backend::Software;
    QString reason;
};

QMutex& probeMutex()
{
    static QMutex mutex;
    return mutex;
}

ProbeState& probeState()
{
    static ProbeState state;
    return state;
}

// The precise reason the OpenGL device cannot run here, in the order the
// conditions can be checked cheaply. The text is shown by the fallback device's
// lastError(), so it names the actual platform rather than "GL failed".
QString openGLUnavailableReason()
{
#if !PERIODICITY_RENDER_HAS_OPENGL_WIDGETS
    return QStringLiteral("Qt6::OpenGLWidgets was not found at build time, so the "
                          "OpenGL device is not compiled in; using the software device");
#else
    if (QGuiApplication::instance() == nullptr) {
        return QStringLiteral("no QGuiApplication instance is alive, so no GL context "
                              "can be created; using the software device");
    }

    const QString platformName = QGuiApplication::platformName();
    if (platformName.isEmpty() || platformName == QStringLiteral("offscreen")
        || platformName == QStringLiteral("minimal")
        || platformName == QStringLiteral("minimalegl")) {
        return QStringLiteral("platform '%1' has no GL surface; using the software device")
            .arg(platformName.isEmpty() ? QStringLiteral("unknown") : platformName);
    }

#if QT_CONFIG(opengl)
    QOpenGLContext context;
    if (!context.create()) {
        return QStringLiteral("QOpenGLContext::create() failed; using the software device");
    }
#else
    return QStringLiteral("Qt was built without OpenGL support; using the software device");
#endif

    // A context exists, but phase 2 does not ship the device that would use it.
    return QStringLiteral("a GL context can be created, but the OpenGL device is phase 3 "
                          "of docs/rendering-architecture.md; using the software device");
#endif
}

// Called with probeMutex() held. Returns the cached state, probing on first use.
const ProbeState& ensureProbe()
{
    ProbeState& state = probeState();
    if (!state.answered) {
        state.reason = openGLUnavailableReason();
        // Phase 2 has no GL device, so Software is the only answer that can run.
        state.preferred = Backend::Software;
        state.answered = true;
    }
    return state;
}

} // namespace

std::shared_ptr<RenderDevice> DeviceFactory::create(Backend backend)
{
    if (backend == Backend::Software)
        return std::make_shared<SoftwareDevice>();

    auto software = std::make_shared<SoftwareDevice>();
    software->setFallbackReason(probeReason());
    return software;
}

Backend DeviceFactory::preferredBackend()
{
    QMutexLocker<QMutex> locker(&probeMutex());
    return ensureProbe().preferred;
}

QString DeviceFactory::backendName(Backend backend)
{
    switch (backend) {
    case Backend::Software:
        return QStringLiteral("software");
    case Backend::OpenGL:
        return QStringLiteral("opengl");
    }
    return QStringLiteral("unknown");
}

QString DeviceFactory::backendName()
{
    return backendName(preferredBackend());
}

QString DeviceFactory::probeReason()
{
    QMutexLocker<QMutex> locker(&probeMutex());
    return ensureProbe().reason;
}

void DeviceFactory::resetProbe()
{
    QMutexLocker<QMutex> locker(&probeMutex());
    probeState() = ProbeState{};
}

} // namespace md::render
