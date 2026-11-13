// Periodicity — render layer: the optional OpenGL device.
//
// GlDevice is the same RenderDevice as SoftwareDevice, but it renders into an
// offscreen QOpenGLFramebufferObject instead of the CPU rasterizer. It owns its
// own QOpenGLContext and QOffscreenSurface: there is no QOpenGLWidget, no native
// child window and no window-system dependency. The context is made current only
// for the duration of initialize() and submit(); the device is still a UI-thread
// object, as docs/rendering-architecture.md requires.
//
// Availability
// -------------
// The implementation is compiled only when PERIODICITY_HAS_OPENGL is 1. The
// header exists in both builds. Without the macro (or when the probe fails at
// run time) isAvailable() is false and lastError() names the exact reason, in
// the same spirit as Context3D's software fallback. DeviceFactory never returns
// an unavailable GlDevice: create(Backend::OpenGL) hands back a SoftwareDevice
// carrying that reason instead.
//
// Shaders
// -------
// The programs are built at run time from GLSL strings in gl_device.cpp. There
// is no qsb file, no Qt6::ShaderTools dependency and no generated shader step.
// The four gfx built-ins (solid, vertexColour, lambert, unlit) have one program
// each, plus an internal textured-copy program used for target upload/read-back.
//
// Parity with SoftwareDevice
// --------------------------
// Both devices receive the same gfx::CommandBuffer in logical target
// coordinates and apply the target's device pixel ratio. Text and paths are
// delegated to the shared TextPainter on the read-back image, because that is
// the only way to keep glyph shaping, font fallback, sub-pixel positioning and
// path filling identical. The image is uploaded again before the next GL
// command, so command order is preserved.
//
// What GL cannot reproduce exactly, and the tolerance the parity test uses
// (channel deltas are in 0..255 units, against an opaque target):
//
//   Command kind | max channel delta | max differing pixels | why
//   -------------|-------------------|----------------------|----------------
//   Clear        | 1                 | 0 %                  | 8-bit colour
//                |                   |                      | round-tripping.
//   SetScissor   | 1                 | 0 %                  | The same integer
//                |                   |                      | rectangle.
//   DrawMesh2D   | 4                 | 3 %                  | GL has one sample
//                |                   |                      | per pixel; the
//                |                   |                      | software path 4x4
//                |                   |                      | supersamples AA
//                |                   |                      | edges. Shared-edge
//                |                   |                      | ownership can also
//                |                   |                      | differ by one
//                |                   |                      | pixel.
//   DrawMesh3D   | 4                 | 3 %                  | Same edge rule;
//                |                   |                      | near-plane clipping
//                |                   |                      | is done by GL,
//                |                   |                      | while gfx rejects a
//                |                   |                      | primitive whose
//                |                   |                      | vertex is behind the
//                |                   |                      | eye.
//   DrawImage    | 4                 | 3 %                  | Texture filtering
//                |                   |                      | rounds to 8 bits in
//                |                   |                      | a different order.
//   DrawPath     | 8                 | 8 %                  | QPainter fills on
//                |                   |                      | the read-back image;
//                |                   |                      | the software target
//                |                   |                      | is straight ARGB and
//                |                   |                      | the GL read-back is
//                |                   |                      | premultiplied, so AA
//                |                   |                      | fringes can differ.
//   DrawText     | 32                | 15 %                 | Sub-pixel glyph
//                |                   |                      | coverage is rendered
//                |                   |                      | on different pixel
//                |                   |                      | representations.
//   Lines/points | 8                 | 8 %                  | GL's line/point
//                |                   |                      | rasterisation is not
//                |                   |                      | gfx's DDA walk.
//
// The test compares whole images and counts a pixel as differing only when a
// channel exceeds the per-kind delta; translucent targets are outside this
// table because gfx blends straight ARGB and premultiplies at the end, while GL
// blends premultiplied.
//
// Failure contract
// ----------------
// Any context/FBO/shader/upload failure leaves lastError() set and returns
// SoftwareDevice's result for that frame, with the target produced by the
// software path. Programs are always released and FBOs are owned by unique_ptr,
// so no half-bound state survives. A lost context is re-initialised once; if
// that recovery fails (or is needed a second time) the device degrades to
// software permanently and says why.

#pragma once

#include <md/render/render_device.h>

#include <QString>

#include <memory>

namespace md::render {

class GlDevice final : public RenderDevice
{
public:
    GlDevice();
    ~GlDevice() override;

    GlDevice(const GlDevice&) = delete;
    GlDevice& operator=(const GlDevice&) = delete;

    // Establishes the offscreen context, the FBO, the vertex buffer and the
    // built-in programs. Idempotent: a successful call leaves the existing
    // context untouched. Returns false and records lastError() on failure.
    bool initialize();

    // Diagnostic entry point: rebuilds the device with a caller-supplied solid
    // program. The built-in vertex layout and the other three gfx programs stay
    // as they are. A compile or link failure is recorded in shaderLog() and
    // lastError(), the device becomes unavailable, and a later submit() still
    // produces the software result. Used by the parity test to exercise the
    // broken-shader path without a production back door.
    bool initializeWithShaderSources(const QString& vertexSource,
                                     const QString& fragmentSource);

    bool isInitialized() const;

    // The compiler/linker log of the most recent program build; empty when the
    // last build produced no diagnostic.
    QString shaderLog() const;

    // Simulates a driver context loss between frames. The next submit() makes
    // one recovery attempt (and permanently degrades if it fails). It never
    // throws and never leaves a context current.
    void simulateContextLoss();

    gfx::RasterStats submit(RenderTarget& target,
                            const gfx::CommandBuffer& commands) override;

    DeviceCapabilities capabilities() const override;
    QString name() const override;
    bool isAvailable() const override;
    QString lastError() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace md::render
