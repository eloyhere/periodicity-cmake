# L6 — public components.
#
# Qt Network is linked for Avatar's url loader; every other component stays on
# Widgets. Nothing else in the layer performs I/O. schedule is linked for the
# Scheduler panel, which reads the task queue headlessly.
periodicity_add_module(components
  DEPENDS periodicity::layout periodicity::structure periodicity::store
          periodicity::schedule Qt6::Widgets Qt6::Network)

# Context3D has two backends: the software projection path is always compiled
# (it runs under ctest, which has no GL context), and the QOpenGLWidget backend
# is compiled only when Qt ships the module. PERIODICITY_HAS_OPENGL tells the
# header which one exists, so callers can query it at compile time.
if(TARGET Qt6::OpenGLWidgets)
  target_link_libraries(periodicity_components PRIVATE Qt6::OpenGLWidgets)
  target_compile_definitions(periodicity_components PRIVATE PERIODICITY_HAS_OPENGL=1)
else()
  target_compile_definitions(periodicity_components PRIVATE PERIODICITY_HAS_OPENGL=0)
endif()
