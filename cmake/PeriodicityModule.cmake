# Helper for declaring a Periodicity module target.
#
# Every module follows the same shape:
#   <module>/CMakeLists.txt
#   <module>/include/md/<module>/*.h    (public, installed)
#   <module>/src/*.cpp                  (private)
#
# Sources are globbed with CONFIGURE_DEPENDS so that adding a file does not
# require editing this file. The target name is always periodicity_<module>,
# with the alias periodicity::<module>.
#
# Every module is also installed: the library goes to ${CMAKE_INSTALL_LIBDIR},
# its public headers keep their md/<module>/ layout under
# ${CMAKE_INSTALL_INCLUDEDIR}, and the target joins the PeriodicityTargets
# export set that the top-level CMakeLists.txt installs as
# lib/cmake/Periodicity/PeriodicityTargets.cmake (namespace periodicity::).
# The top-level file includes GNUInstallDirs once, before any module directory
# is added, so the CMAKE_INSTALL_* variables are already set when the helper
# runs; nothing here includes them per module.

include_guard(GLOBAL)

# Installs one module: the library target, its public headers, and its
# membership in the PeriodicityTargets export set.
#
# Kept apart from periodicity_add_module() so the module declaration stays
# readable. install() records the rules in the calling directory, so this is
# called from periodicity_add_module(), which every module's CMakeLists.txt
# invokes from its own directory (CMAKE_CURRENT_SOURCE_DIR is the module dir).
function(periodicity_install_module name)
  install(TARGETS periodicity_${name}
    EXPORT PeriodicityTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

  # include/ contains only headers and mirrors the installed layout already
  # (include/md/<module>/...), so installing the directory tree preserves the
  # #include <md/...> spelling a consumer writes against the install prefix.
  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
      PATTERN "*.h"
      PATTERN "*.hpp")
endfunction()

function(periodicity_add_module name)
  cmake_parse_arguments(ARG "" "" "DEPENDS" ${ARGN})

  file(GLOB_RECURSE _headers CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
  file(GLOB_RECURSE _sources CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

  add_library(periodicity_${name} STATIC ${_headers} ${_sources})
  add_library(periodicity::${name} ALIAS periodicity_${name})

  target_include_directories(periodicity_${name}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

  # install(EXPORT) cannot rewrite an ALIAS, so a dependency is linked through
  # the real target name whenever the caller passes the periodicity:: alias.
  # The exported INTERFACE_LINK_LIBRARIES then names periodicity_<module>,
  # which the export set renames into the periodicity:: namespace.
  set(_dependencies)
  foreach(_dependency IN LISTS ARG_DEPENDS)
    if(_dependency MATCHES "^periodicity::(.+)$")
      list(APPEND _dependencies "periodicity_${CMAKE_MATCH_1}")
    else()
      list(APPEND _dependencies "${_dependency}")
    endif()
  endforeach()

  target_link_libraries(periodicity_${name} PUBLIC ${_dependencies})

  target_compile_features(periodicity_${name} PUBLIC cxx_std_20)
  set_target_properties(periodicity_${name} PROPERTIES
    # The imported name is periodicity::<module>, not periodicity_<module>.
    EXPORT_NAME ${name}
    AUTOMOC ON
    AUTOUIC OFF
    AUTORCC OFF)

  if(MSVC)
    target_compile_options(periodicity_${name} PRIVATE
      /W4 /permissive- /utf-8 /Zc:__cplusplus)
  else()
    target_compile_options(periodicity_${name} PRIVATE
      -Wall -Wextra -Wpedantic)
  endif()

  periodicity_install_module(${name})
endfunction()

# Makes an executable runnable straight from a developer build tree on Windows.
#
# A build tree does not put Qt's DLLs beside the executable, and Visual Studio
# starts a run with the IDE's environment rather than the shell's. Two things
# fix that together:
#
#   * windeployqt copies the Qt runtime (and the platform plugin) next to the
#     executable after every build, so F5, Ctrl+F5, Explorer and a plain shell
#     all find it;
#   * VS_DEBUGGER_ENVIRONMENT points the debugger at the same Qt, which covers
#     the first run, before the post-build step has produced the DLLs.
#
# Call it once per executable target: periodicity_deploy_qt_runtime(<target>).
function(periodicity_deploy_qt_runtime target)
  if(NOT WIN32)
    return()
  endif()

  get_target_property(_qmake_location Qt6::qmake IMPORTED_LOCATION)
  if(NOT _qmake_location)
    return()
  endif()

  get_filename_component(_qt_bin "${_qmake_location}" DIRECTORY)
  set(_windeployqt "${_qt_bin}/windeployqt.exe")

  if(EXISTS "${_windeployqt}")
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${_windeployqt}"
              --no-translations
              --no-compiler-runtime
              --no-system-d3d-compiler
              --no-opengl-sw
              "$<TARGET_FILE:${target}>"
      COMMENT "Deploying the Qt runtime beside $<TARGET_FILE_NAME:${target}>"
      VERBATIM)
  endif()

  set_target_properties(${target} PROPERTIES
    VS_DEBUGGER_ENVIRONMENT "PATH=${_qt_bin};%PATH%"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${target}>")
endfunction()
