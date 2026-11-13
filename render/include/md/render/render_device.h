// Periodicity — render layer: the device interface.
//
// A RenderDevice executes a gfx::CommandBuffer into a RenderTarget. It owns no
// target, no window and no drawing state between calls: the caller keeps the
// target alive, submits, and reads the pixels back. Everything above render/
// talks to this interface only, which is what lets the software and OpenGL
// implementations be swapped without the drawing contexts knowing.
//
// Coordinate and lifecycle contract:
//
//   - submit() takes commands in the target's *logical* coordinates and applies
//     the target's device pixel ratio while rasterizing. A command that fills a
//     10 x 10 logical rectangle covers 20 x 20 physical pixels at ratio 2.
//   - A device is created, used and destroyed on the UI thread. Command buffers
//     are pure data and may be built anywhere.
//   - submit() never throws. Invalid input is refused with a zero RasterStats,
//     a diagnostic in lastError(), and the target exactly as it was passed in.
//   - lastError() reports the most recent diagnostic, empty when there is none.
//     It is not cleared by a later successful submit: the factory uses it to
//     keep a fallback explanation visible.

#pragma once

#include <md/gfx/command_buffer.h>
#include <md/gfx/rasterizer.h>
#include <md/render/render_target.h>

#include <QString>

namespace md::render {

struct DeviceCapabilities
{
    bool text = false;
    bool paths = false;
    bool images = false;
    bool depthBuffer = false;
    bool shaders = false;

    // The same caps the device enforces, so a caller can size its geometry or
    // its target before submitting rather than being refused afterwards.
    int maxTriangles = 0;
    int maxTargetPixels = 0;

    // The stable name of the backend that runs the device ("software" today).
    QString backendName;
};

class RenderDevice
{
public:
    virtual ~RenderDevice() = default;

    // Executes `commands` into `target`. Returns zero statistics and leaves the
    // target untouched when the device refuses the submission (invalid target,
    // a command buffer above the caps, or an unavailable device).
    virtual gfx::RasterStats submit(RenderTarget& target,
                                    const gfx::CommandBuffer& commands) = 0;

    virtual DeviceCapabilities capabilities() const = 0;

    // The device's own name, matching capabilities().backendName.
    virtual QString name() const = 0;

    // False for a device whose probe failed; a caller must then fall back.
    virtual bool isAvailable() const = 0;

    // The most recent diagnostic; empty when the device has none to report.
    virtual QString lastError() const = 0;
};

} // namespace md::render
