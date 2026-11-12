# L4 — the component kernel: measure, arrange, paint template, state machine.
periodicity_add_module(view
  DEPENDS periodicity::core periodicity::schedule periodicity::tokens
          periodicity::theme periodicity::event periodicity::ripple
          periodicity::drawable periodicity::layer periodicity::animation
          Qt6::Widgets)

# The optional GPU compositor: GpuCompositor is a QOpenGLWidget when Qt ships
# OpenGLWidgets. The definition is PUBLIC because the class shape in
# md/view/gpu_compositor.h depends on it — every consumer must see the same
# header, or the ODR would differ between the library and the caller.
if(TARGET Qt6::OpenGLWidgets)
  target_link_libraries(periodicity_view PUBLIC Qt6::OpenGLWidgets)
  if(TARGET Qt6::OpenGL)
    target_link_libraries(periodicity_view PUBLIC Qt6::OpenGL)
  endif()
  target_compile_definitions(periodicity_view PUBLIC PERIODICITY_HAS_OPENGL=1)
else()
  target_compile_definitions(periodicity_view PUBLIC PERIODICITY_HAS_OPENGL=0)
endif()
