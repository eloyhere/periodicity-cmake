// Periodicity — render layer: one frame over one device.
//
// A Renderer is the small state machine a Canvas needs: it pairs a device with
// the target of the current frame, so the caller does not have to pass both on
// every submit. It owns neither the device nor the target.
//
//   begin(target)  opens the frame; fails if one is already open, if the target
//                  is invalid, or if the device is missing/unavailable
//   submit(cmd)    executes into the open target; fails with zero statistics
//                  when no frame is open
//   end()          closes the frame; a no-op when none is open
//
// Every failure is reported through lastError(); nothing throws. begin()
// clears the previous diagnostic, submit() copies the device's diagnostic when
// the device refuses, and end() is silent because closing twice is harmless.

#pragma once

#include <md/render/render_device.h>
#include <md/render/render_target.h>

#include <QString>

#include <memory>

namespace md::render {

class Renderer
{
public:
    explicit Renderer(std::shared_ptr<RenderDevice> device = {});

    // Replaces the device. Refused with a diagnostic while a frame is open, so
    // a device is never swapped underneath a target.
    void setDevice(std::shared_ptr<RenderDevice> device);
    std::shared_ptr<RenderDevice> device() const { return device_; }

    bool begin(RenderTarget& target);

    gfx::RasterStats submit(const gfx::CommandBuffer& commands);

    void end();

    bool isActive() const { return target_ != nullptr; }

    // The target of the open frame, null when no frame is open.
    RenderTarget* target() const { return target_; }

    QString lastError() const { return lastError_; }

private:
    std::shared_ptr<RenderDevice> device_;
    RenderTarget* target_ = nullptr;
    QString lastError_;
};

} // namespace md::render
