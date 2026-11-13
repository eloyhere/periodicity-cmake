#include <md/render/device_factory.h>

#include <md/render/gl_device.h>
#include <md/render/rhi_device.h>
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
// even when a GPU device is available, because both GPU devices are opt-in.
QString softwarePreferenceReason()
{
#if !defined(PERIODICITY_HAS_OPENGL) || !PERIODICITY_HAS_OPENGL
    const QString openGlReason =
        QStringLiteral("Qt6::OpenGL was not found at build time, so the OpenGL "
                       "device is not compiled in");
#else
    QString openGlReason;
    if (QGuiApplication::instance() == nullptr) {
        openGlReason = QStringLiteral("no QGuiApplication instance is alive, so no GL "
                                      "context can be created");
    } else {
        QOpenGLContext context;
        if (!context.create())
            openGlReason = QStringLiteral("QOpenGLContext::create() failed");
        else
            openGlReason = QStringLiteral("a GL context can be created");
    }
#endif

#if !defined(PERIODICITY_HAS_RHI) || !PERIODICITY_HAS_RHI
    const QString rhiReason = QStringLiteral("the RHI device is not compiled in");
#else
    const QString rhiReason = QStringLiteral("the RHI device is compiled in");
#endif

    return QStringLiteral("%1, and %2; the software device remains the default "
                          "because GPU rendering is opt-in through "
                          "create(Backend::OpenGL) or create(Backend::Rhi)")
        .arg(openGlReason, rhiReason);
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

    if (backend == Backend::Rhi) {
        auto rhi = std::make_shared<RhiDevice>();
        if (rhi->isAvailable())
            return rhi;

        auto software = std::make_shared<SoftwareDevice>();
        software->setFallbackReason(rhi->lastError());
        return software;
    }

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
    case Backend::Rhi:
        return QStringLiteral("rhi");
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
