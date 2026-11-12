// Periodicity — capability detection for the optional GPU rendering path.
//
// Detection is deliberately conservative. A headless QPA plugin can be built
// with OpenGL support in Qt and still have no window system to attach a GL
// surface to, so the platform name is checked before a context is created.
// After a successful probe the answer is cached: creating a QOpenGLContext
// initialises a driver and is not something a paint-time query should repeat.

#include <md/view/render_backend.h>

#include <QGuiApplication>
#include <QOpenGLContext>
#include <QString>
#include <QtGlobal>

namespace md {
namespace {

// QPA platforms without a usable window/GL surface. The offscreen plugin is
// what ctest uses; minimal and vnc are the other headless plugins shipped with
// Qt. linuxfb has no GL integration.
[[nodiscard]] bool isHeadlessPlatform(const QString& name)
{
    return name.compare(QStringLiteral("offscreen"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("minimal"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("vnc"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("linuxfb"), Qt::CaseInsensitive) == 0;
}

} // namespace

bool isBackendAvailable(RenderBackend backend)
{
    if (backend == RenderBackend::Software)
        return true;

#if !MD_HAS_OPENGL_WIDGETS
    return false;
#else
    // A context needs a QGuiApplication and a platform integration. Answering
    // false before they exist is correct; the query is repeated later.
    if (QGuiApplication::instance() == nullptr)
        return false;

    if (isHeadlessPlatform(QGuiApplication::platformName()))
        return false;

    static const bool available = [] {
        QOpenGLContext context;
        return context.create() && context.isValid();
    }();

    return available;
#endif
}

RenderBackend preferredBackend()
{
    return isBackendAvailable(RenderBackend::OpenGL) ? RenderBackend::OpenGL
                                                     : RenderBackend::Software;
}

const char* backendName(RenderBackend backend) noexcept
{
    switch (backend) {
    case RenderBackend::Software:
        return "Software";
    case RenderBackend::OpenGL:
        return "OpenGL";
    }
    return "Unknown";
}

} // namespace md
