#include <md/render/device_factory.h>

#include <md/render/gl_device.h>
#include <md/render/software_device.h>

#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>

#if defined(PERIODICITY_HAS_OPENGL) && PERIODICITY_HAS_OPENGL
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

// The explanation the preferred (software) backend carries. It names the real
// condition rather than "GL failed", and it is the factory's documented answer
// even when a GL context is available, because OpenGL is opt-in.
QString softwarePreferenceReason()
{
#if !defined(PERIODICITY_HAS_OPENGL) || !PERIODICITY_HAS_OPENGL
    return QStringLiteral("Qt6::OpenGL was not found at build time, so the OpenGL "
                          "device is not compiled in; using the software device");
#else
    if (QGuiApplication::instance() == nullptr) {
        return QStringLiteral("no QGuiApplication instance is alive, so no GL context "
                              "can be created; using the software device");
    }

    QOpenGLContext context;
    if (!context.create()) {
        return QStringLiteral("QOpenGLContext::create() failed; using the software device");
    }

    return QStringLiteral("a GL context can be created, but the software device remains "
                          "the default; OpenGL is opt-in through create(Backend::OpenGL)");
#endif
}

// Called with probeMutex() held. Returns the cached state, probing on first use.
const ProbeState& ensureProbe()
{
    ProbeState& state = probeState();
    if (!state.answered) {
        state.reason = softwarePreferenceReason();
        // OpenGL is a capability, not the default. The preference stays stable
        // for the process, exactly as the header promises.
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

    auto gl = std::make_shared<GlDevice>();
    if (gl->isAvailable())
        return gl;

    auto software = std::make_shared<SoftwareDevice>();
    software->setFallbackReason(gl->lastError());
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
