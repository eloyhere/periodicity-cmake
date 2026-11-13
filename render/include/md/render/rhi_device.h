// Periodicity — render layer: the optional RHI device.
//
// RhiDevice is the same RenderDevice as SoftwareDevice and GlDevice, but it
// renders through Qt's rendering hardware interface (QRhi) instead of the CPU
// rasterizer or a hand-written OpenGL path. It owns no window, no swap chain
// and no native surface: every frame is an offscreen QRhi frame whose colour
// attachment is a QRhiTexture, read back into the target image before submit()
// returns. The device is a UI-thread object, as docs/rendering-architecture.md
// requires.
//
// Availability and backend selection
// ----------------------------------
// The implementation is compiled only when PERIODICITY_HAS_RHI is 1. The header
// exists in both builds; without the macro (or when no backend can be created)
// isAvailable() is false and lastError() names the exact reason, and
// DeviceFactory hands back a SoftwareDevice carrying that reason.
//
// The backend is chosen from what QRhi can actually create, in this order:
//
//   Windows   D3D11  (the normal case on the pinned MSVC 2022 Qt)
//   any       Vulkan (only when the Qt build and its headers provide it)
//   any       OpenGL (QRhi's GLES2 backend over an offscreen surface)
//
// The chosen API is reported by apiName(); name() stays the stable backend id
// "rhi" so capabilities().backendName matches the factory's Backend::Rhi.
//
// Shaders
// -------
// The four gfx built-ins (solid, vertexColour, lambert, unlit) and the textured
// copy used by DrawImage are GLSL sources under shaders/, compiled to .qsb at
// build time by qt6_add_shaders() (Qt 6.8's name; defined in
// Qt6ShaderToolsTools/Qt6ShaderToolsMacros.cmake) and loaded from the
// :/shaders resource prefix. Nothing here compiles GLSL at run time and nothing
// here constructs a QOpenGLShaderProgram: the same qsb files carry SPIR-V,
// HLSL and GLSL, so the pinned Qt can serve every backend without a rebuild.
//
// Coordinate and dpr contract
// ---------------------------
// Identical to SoftwareDevice: commands arrive in the target's *logical*
// coordinates and submit() applies the target's device pixel ratio before
// rasterizing. Text and paths are delegated to the shared TextPainter on the
// read-back image, exactly as GlDevice does, so glyph shaping, font fallback,
// sub-pixel positioning and path filling stay with Qt and match the software
// device.
//
// Parity with SoftwareDevice, and the tolerance the parity test uses
// (channel deltas in 0..255 units, against an opaque target):
//
//   Command kind | max channel delta | max differing pixels | why
//   -------------|-------------------|----------------------|----------------
//   Clear        | 1                 | 0 %                  | 8-bit colour
//                |                   |                      | round-tripping
//                |                   |                      | through the
//                |                   |                      | premultiplied
//                |                   |                      | attachment.
//   SetScissor   | 1                 | 0 %                  | The same integer
//                |                   |                      | rectangle.
//   DrawMesh2D   | 4                 | 3 %                  | One sample per
//                |                   |                      | pixel here; the
//                |                   |                      | software path
//                |                   |                      | 4x4 supersamples
//                |                   |                      | AA edges.
//   DrawMesh3D   | 4                 | 3 %                  | Same edge rule;
//                |                   |                      | clipping and
//                |                   |                      | depth rounding are
//                |                   |                      | backend work.
//   DrawImage    | 4                 | 3 %                  | Bilinear
//                |                   |                      | filtering rounds
//                |                   |                      | to 8 bits in a
//                |                   |                      | different order.
//   DrawPath     | 8                 | 8 %                  | QPainter fills on
//                |                   |                      | the read-back
//                |                   |                      | image; straight
//                |                   |                      | ARGB software
//                |                   |                      | versus the
//                |                   |                      | premultiplied
//                |                   |                      | attachment.
//   DrawText     | 32                | 15 %                 | Sub-pixel glyph
//                |                   |                      | coverage is
//                |                   |                      | rasterized on
//                |                   |                      | different pixel
//                |                   |                      | representations.
//   Lines/points | 8                 | 8 %                  | Line and point
//                |                   |                      | rasterization is
//                |                   |                      | not gfx's DDA
//                |                   |                      | walk.
//
// Clear, SetScissor and the fully covered interior of a 2D rectangle are exact.
// Translucent targets are outside the table, as they are for GlDevice.
//
// Two consequences of QRhi's API that the table does not hide:
//
//   * Clear is executed on the CPU, on the target image, because QRhi exposes
//     no scissored clear across backends. That is what keeps it pixel-exact;
//     the cost is that the GPU work before and after a Clear is split into two
//     passes, with a read-back between them. A Clear also restarts the depth
//     buffer (the next pass clears it in full) rather than only inside its
//     scissor rectangle.
//   * pixelsWritten for shaded geometry is 0: QRhi has no cross-backend
//     occlusion query. DrawImage reports its covered area analytically, which
//     is exact for a single blit; Clear is not a fragment draw and therefore
//     contributes nothing, exactly as SoftwareDevice counts it. trianglesDrawn
//     is exact and is the number the parity test compares for geometry.
//
// Failure contract
// ----------------
// Any failure — no RHI backend, shader load, pipeline creation, texture
// allocation, frame begin, or read-back — leaves lastError() set and returns
// SoftwareDevice's result for that frame, with the target produced by the
// software path. Resources are owned by the implementation: a failed frame
// destroys the temporary texture and bindings it created and leaves no
// half-bound pipeline. The device is not re-probed after a failure; the factory
// has already chosen Software as the default and a caller that wants GPU work
// asks for Backend::Rhi explicitly.

#pragma once

#include <md/render/render_device.h>

#include <QByteArray>
#include <QString>

#include <memory>

namespace md::render {

class RhiDevice final : public RenderDevice
{
public:
    RhiDevice();
    ~RhiDevice() override;

    RhiDevice(const RhiDevice&) = delete;
    RhiDevice& operator=(const RhiDevice&) = delete;

    // Creates the QRhi backend, loads the .qsb programs, allocates the shared
    // buffers and validates one pipeline against a 1x1 offscreen target.
    // Idempotent: a successful call leaves the existing device untouched.
    // Returns false and records lastError() on failure.
    bool initialize();

    // Diagnostic entry point: rebuilds the device with caller-supplied serialized
    // QShader data for the "solid" program. The vertex input layout, the other
    // three gfx programs and the factory-visible behaviour stay as they are. A
    // rejected QShader or a failed pipeline build is recorded in shaderLog() and
    // lastError(), the device becomes unavailable, and a later submit() still
    // produces the software result. Used by the parity test to exercise the
    // broken-pipeline path without a production back door.
    bool initializeWithSolidShaderData(const QByteArray& vertexShader,
                                       const QByteArray& fragmentShader);

    bool isInitialized() const;

    // The diagnostic of the most recent .qsb load or pipeline build; empty when
    // the last build produced no diagnostic.
    QString shaderLog() const;

    // The QRhi backend that was created: "D3D11", "Vulkan", "OpenGL", or empty
    // when no backend could be created.
    QString apiName() const;

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
