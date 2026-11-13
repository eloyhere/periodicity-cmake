# render — the device layer between gfx and the drawing contexts.
#
# Qt Gui and Qt Core only, exactly like gfx: the module has to build and run
# headlessly, so it never links Qt6::Widgets, directly or through a dependency.
# The QPainter text/path bridge lives here (TextPainter), but it paints onto
# QImage surfaces and needs no window system.
#
# The OpenGL device is phase 3 of docs/rendering-architecture.md. The factory
# still probes for it and has to be able to say precisely why it is unavailable,
# so record whether the widget-side GL module was part of this build. Nothing in
# render links Qt6::OpenGLWidgets: the probe only needs QtGui's QOpenGLContext.
periodicity_add_module(render
  DEPENDS periodicity::gfx Qt6::Gui Qt6::Core)

if(TARGET Qt6::OpenGLWidgets)
  target_compile_definitions(periodicity_render PRIVATE
    PERIODICITY_RENDER_HAS_OPENGL_WIDGETS=1)
else()
  target_compile_definitions(periodicity_render PRIVATE
    PERIODICITY_RENDER_HAS_OPENGL_WIDGETS=0)
endif()
