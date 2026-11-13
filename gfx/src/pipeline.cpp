// Periodicity — gfx layer: pipeline state construction.
//
// The pipeline is a plain value; all that is left here is copying the four
// fields and letting the shader answer isValid(). Keeping the constructor out
// of the header means the state layout can grow in phase 2 without every caller
// recompiling against the member order.

#include <md/gfx/pipeline.h>

#include <utility>

namespace md::gfx {

Pipeline::Pipeline(Shader shader, BlendMode blend, DepthState depth, RasterState raster)
    : shader(std::move(shader))
    , blend(blend)
    , depth(depth)
    , raster(raster)
{
}

} // namespace md::gfx
