// Periodicity — render layer: which device to use, and why not the other one.
//
// The factory owns the process-wide capability probe. It answers once and
// caches the answer, because probing a GL context can be expensive and its
// result cannot change under a running process unless the GUI application is
// recreated (which resetProbe() covers). The preferred backend is therefore
// stable for the lifetime of the process; create() is the only thing a caller
// needs to call per device.
//
// Phase 2 of docs/rendering-architecture.md ships the software device only. The
// OpenGL probe still runs, and its reason is the diagnostic the software
// fallback carries: "no Qt6::OpenGLWidgets at build time", "no QGuiApplication",
// "the platform has no GL surface" or "QOpenGLContext::create() failed". That
// is what the phase 3 GL device will replace, behind the same interface.
//
// create() never returns null and never returns an unavailable device: a failed
// OpenGL request degrades to SoftwareDevice with the reason in lastError().

#pragma once

#include <md/render/render_device.h>

#include <QString>

#include <memory>

namespace md::render {

enum class Backend
{
    Software,
    OpenGL,
};

class DeviceFactory
{
public:
    // A ready-to-use device. Backend::OpenGL returns the software device with
    // the probe reason recorded while the GL device is not built.
    static std::shared_ptr<RenderDevice> create(Backend backend);

    // The backend the probe selected. Stable for the process; Software today.
    static Backend preferredBackend();

    // Stable backend identifiers: "software" and "opengl". The no-argument
    // overload names the preferred backend.
    static QString backendName(Backend backend);
    static QString backendName();

    // The probe's explanation of why the preferred backend is the one it is.
    // Runs the probe on first use and returns the cached text afterwards.
    static QString probeReason();

    // Clears the cached probe so the next query probes again. For tests and for
    // a host that tears down and recreates its QGuiApplication.
    static void resetProbe();
};

// Free-function forms for call sites that read better without the class name.
inline std::shared_ptr<RenderDevice> create(Backend backend)
{
    return DeviceFactory::create(backend);
}

inline Backend preferredBackend()
{
    return DeviceFactory::preferredBackend();
}

inline QString backendName(Backend backend)
{
    return DeviceFactory::backendName(backend);
}

inline QString backendName()
{
    return DeviceFactory::backendName();
}

} // namespace md::render
