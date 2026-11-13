// Periodicity — gfx layer: the immutable pipeline state.
//
// A Pipeline is a value: one shader plus the blend, depth and raster state a
// draw uses. The rasterizer reads it and never writes it back. The default
// Pipeline has no shader and is therefore invalid; a draw command carrying an
// invalid pipeline is refused rather than drawn with guessed state.
//
// Winding and culling are fixed here rather than per command: a triangle is
// front-facing when its vertices are counter-clockwise in clip space. The
// rasterizer projects that to screen space, which flips the handedness, so a
// front face has a negative signed screen area. cullBack is ignored for Mesh2D,
// whose triangles are normalised to a consistent winding instead.

#pragma once

#include <md/gfx/shader.h>

namespace md::gfx {

enum class BlendMode
{
    None,
    Alpha,
    Additive,
    Multiply,
};

struct DepthState
{
    bool test = true;
    bool write = true;
};

struct RasterState
{
    bool cullBack = true;
    bool antialias = true;
};

class Pipeline
{
public:
    Pipeline() = default;
    Pipeline(Shader shader, BlendMode blend = BlendMode::None,
             DepthState depth = DepthState{}, RasterState raster = RasterState{});

    bool isValid() const { return shader.isValid(); }

    Shader shader;
    BlendMode blend = BlendMode::None;
    DepthState depth;
    RasterState raster;
};

} // namespace md::gfx
