// Periodicity — render layer: the reference device.
//
// SoftwareDevice executes a CommandBuffer through gfx::rasterize(). It is the
// device every caller can rely on: it needs no window system, no context and no
// driver, and its output is byte-identical on every platform, which is what
// makes the pixel assertions in tests/render possible.
//
// It is created with the full documented capability set. The gfx caps are
// reported through capabilities() as well as enforced at submit time, so a
// caller can size its geometry and its target before drawing instead of being
// refused afterwards.
//
// The device owns no target. submit() takes one by reference and writes into
// it; a target outlives the call and may be reused for the next frame.

#pragma once

#include <md/render/render_device.h>

namespace md::render {

class SoftwareDevice final : public RenderDevice
{
public:
    SoftwareDevice();

    // Rasterizes `commands` into `target`, applying the target's device pixel
    // ratio. Refusals (invalid target, more primitives than the gfx cap, a
    // target above the pixel cap) return zero statistics, leave the target
    // untouched and record the reason in lastError().
    gfx::RasterStats submit(RenderTarget& target,
                            const gfx::CommandBuffer& commands) override;

    DeviceCapabilities capabilities() const override;
    QString name() const override;
    bool isAvailable() const override;
    QString lastError() const override;

    // Records why the factory chose this device instead of a GL one. lastError()
    // keeps reporting it until a later refusal replaces it.
    void setFallbackReason(const QString& reason);

private:
    gfx::RasterCaps caps_;
    QString lastError_;
};

} // namespace md::render
