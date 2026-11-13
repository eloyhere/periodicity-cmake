// Periodicity — render layer: which device to use, and why not the other one.
//
// The factory owns the process-wide capability probe. It answers once and
// caches the answer, because probing a GL context can be expensive and its
// result cannot change under a running process unless the GUI application is
// recreated (which resetProbe() covers). The preferred backend is therefore
// stable for the lifetime of the process; create() is the only thing a caller
// needs to call per device.
//
// Two GPU devices are opt-in:
//
//   Backend::OpenGL  GlDevice, when PERIODICITY_HAS_OPENGL and the offscreen
//                    context probe succeeds.
//   Backend::Rhi     RhiDevice over QRhi (D3D11 first on Windows), when
//                    PERIODICITY_HAS_RHI and a backend can be created.
//
// The factory still defaults to Software for both: a GPU device is a
// capability, not the process default. A request that cannot be satisfied
// returns a SoftwareDevice carrying the precise reason in lastError().
//
// create() never returns null and never returns an unavailable device.

#pragma once

#include <md/render/render_device.h>

#include <QString>

#include <memory>

namespace md::render {

enum class Backend
{
    Software,
    OpenGL,
    Rhi,
};

class DeviceFactory
{
public:
    // A ready-to-use device. Backend::OpenGL returns a GlDevice when its probe
    // succeeds, Backend::Rhi a RhiDevice when its backend can be created, and a
    // SoftwareDevice carrying the reason otherwise.
    static std::shared_ptr<RenderDevice> create(Backend backend);

    // The backend the probe selected. Stable for the process; Software today,
    // because the GPU devices are opt-in.
    static Backend preferredBackend();

    // Stable backend identifiers: "software", "opengl" and "rhi". The
    // no-argument overload names the preferred backend.
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
