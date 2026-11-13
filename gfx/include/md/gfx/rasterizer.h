// Periodicity — gfx layer: the software rasterizer.
//
// rasterize() executes a CommandBuffer into a QImage. It owns no window, no GL
// context and no state between calls, and it is deterministic: the same command
// buffer and target produce the same pixels on every platform. That property is
// what the pixel assertions in tests/gfx rely on.
//
// Implementation contract:
//
//   - Mesh2D is filled with barycentric coverage and a top-left rule, so two
//     triangles sharing an edge never both write the boundary pixel. Boundary
//     pixels are 4x4 supersampled when RasterState::antialias is set; interior
//     pixels cost one sample.
//   - Mesh3D uses the pipeline's vertex stage, a real z-buffer (smaller depth is
//     nearer, [0, 1] after the viewport transform), back-face culling and
//     perspective-correct attribute interpolation (1/w per vertex).
//   - BlendMode::None/Alpha/Additive/Multiply are applied per fragment.
//   - The command viewport and the scissor state clip every primitive; both are
//     intersected with the target and never trusted as-is.
//   - Text and paths are drawn through QPainter on the target. This is the
//     temporary bridge described in docs/rendering-architecture.md; phase 2
//     moves it to render/TextPainter and render/PathRasterizer.
//
// Bounds: a command buffer whose primitives exceed RasterCaps::maxTriangles, or
// a target larger than RasterCaps::maxTargetPixels, is refused as a whole with a
// qWarning and no partial draw. An invalid mesh is skipped (it has already been
// diagnosed by Mesh2D/Mesh3D); it does not poison the rest of the buffer.
//
// Storage: one ARGB32 scratch image the size of the target, plus a float depth
// buffer allocated on the first Mesh3D command. Nothing else allocates per draw
// call. The result is written back into `target` (which becomes ARGB32).

#pragma once

#include <md/gfx/command_buffer.h>

#include <QImage>

namespace md::gfx {

struct RasterCaps
{
    // Primitives (triangles, line segments, points) across the whole buffer.
    int maxTriangles = 65536;

    // Target width x height. 8 388 608 is 4096 x 2048; the depth buffer for
    // that target is 32 MiB and is only allocated when a 3D command appears.
    int maxTargetPixels = 8388608;
};

struct RasterStats
{
    // Triangles that passed culling and reached the fill stage.
    int trianglesDrawn = 0;

    // Fragments that passed the depth and scissor tests and were written.
    int pixelsWritten = 0;
};

// Executes `commands` into `target`. `target` is left untouched when the caps
// reject the submission or the target is null/empty. Returns the statistics;
// the caller can compare pixelsWritten with zero to tell "drew nothing" from
// "refused".
RasterStats rasterize(QImage& target, const CommandBuffer& commands,
                      const RasterCaps& caps = RasterCaps{});

} // namespace md::gfx
