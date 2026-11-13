# render — the device layer between gfx and the drawing contexts.
#
# Qt Gui and Qt Core only, exactly like gfx: the module has to build and run
# headlessly, so it never links Qt6::Widgets. The QPainter text/path bridge lives
# here (TextPainter), but it paints onto QImage surfaces and needs no window
# system.
#
# OpenGL is optional. Qt6::OpenGL supplies QOpenGLContext's companion classes
# (QOpenGLShaderProgram, QOpenGLFramebufferObject, buffers and VAOs);
# Qt6::OpenGLWidgets stays out of this module because GlDevice owns an offscreen
# context and surface, never a widget. When the target is absent the device
# reports the build-time reason through isAvailable()/lastError().
periodicity_add_module(render
  DEPENDS periodicity::gfx Qt6::Gui Qt6::Core)

find_package(Qt6 6.8 QUIET OPTIONAL_COMPONENTS OpenGL)

if(TARGET Qt6::OpenGL)
  target_link_libraries(periodicity_render PRIVATE Qt6::OpenGL)
  target_compile_definitions(periodicity_render PRIVATE
    PERIODICITY_HAS_OPENGL=1)
else()
  target_compile_definitions(periodicity_render PRIVATE
    PERIODICITY_HAS_OPENGL=0)
endif()
