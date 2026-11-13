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

# RHI — the GPU device on Qt's rendering hardware interface (QRhi).
#
# Qt 6.8.3 is pinned by a user decision, so the rhi/ headers that ship inside
# QtGui's versioned include directory are a supported dependency even though
# Qt6::Gui does not export them. cmake/qt_binding.cmake locates the directory,
# checks that rhi/qrhi.h really exists and defines PERIODICITY_HAS_RHI for the
# module (PUBLIC: the header exists in both builds, so callers can tell which
# one they got). When the header is absent the module compiles the unavailable
# stub and nothing here changes.
periodicity_enable_rhi(periodicity_render)

if(PERIODICITY_HAS_RHI)
  # Qt6::ShaderTools is a build-time dependency: qsb compiles shaders/*.vert and
  # shaders/*.frag into .qsb resources. No runtime GLSL compilation happens in
  # this module. It is linked (rather than only looked up) so the dependency is
  # visible in the target and in the installed package config.
  target_link_libraries(periodicity_render PRIVATE Qt6::ShaderTools)

  # qt6_add_shaders() is the Qt 6.8 name; it is defined by
  # Qt6ShaderToolsTools/Qt6ShaderToolsMacros.cmake, which
  # find_package(Qt6 ... COMPONENTS ShaderTools) brings in as its tool
  # dependency. The versionless alias qt_add_shaders() exists too, but the
  # versioned spelling states the pinned binding. Output: :/shaders/<name>.qsb.
  set(_periodicity_shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/../shaders")
  qt6_add_shaders(periodicity_render "render_shaders"
    PREFIX "/shaders"
    BASE "${_periodicity_shader_dir}"
    FILES
      "${_periodicity_shader_dir}/rhi.vert"
      "${_periodicity_shader_dir}/rhi_solid.frag"
      "${_periodicity_shader_dir}/rhi_vertex_colour.frag"
      "${_periodicity_shader_dir}/rhi_lambert.frag"
      "${_periodicity_shader_dir}/rhi_unlit.frag")
  unset(_periodicity_shader_dir)

  # qt6_add_shaders() does not compile into periodicity_render directly: it
  # creates a companion library, periodicity_render_resources_1, and links it
  # through the module's interface. A target that embeds resources therefore
  # carries that companion as an INTERFACE dependency, and install(EXPORT) then
  # refuses to generate until the companion is in the same export set:
  #   "includes target periodicity_render which requires target
  #    periodicity_render_resources_1 that is not in any export set"
  # Installing it here, in the same directory that created it, keeps the rule
  # next to the thing it depends on instead of guessing in the module helper
  # (which runs before this file gets a chance to create the target).
  foreach(_periodicity_resource_index RANGE 0 15)
    set(_periodicity_resource_target
        "periodicity_render_resources_${_periodicity_resource_index}")
    if(TARGET ${_periodicity_resource_target})
      install(TARGETS ${_periodicity_resource_target}
        EXPORT PeriodicityTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    unset(_periodicity_resource_target)
  endforeach()
  unset(_periodicity_resource_index)
endif()
