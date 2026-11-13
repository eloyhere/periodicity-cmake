# gfx — backend-neutral draw data and the software rasterizer.
#
# Qt Gui and Qt Core only: no Widgets, no window system, no OpenGL. The layer
# has to build and run headlessly, which is what lets the pixel assertions run
# under ctest on every platform. In particular this module never links
# Qt6::Widgets, directly or through a dependency; periodicity::core is Core +
# Gui as well.
periodicity_add_module(gfx
  DEPENDS periodicity::core Qt6::Gui Qt6::Core)
