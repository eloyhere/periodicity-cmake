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

include_guard(GLOBAL)

function(periodicity_add_module name)
  cmake_parse_arguments(ARG "" "" "DEPENDS" ${ARGN})

  file(GLOB_RECURSE _headers CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
  file(GLOB_RECURSE _sources CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

  add_library(periodicity_${name} STATIC ${_headers} ${_sources})
  add_library(periodicity::${name} ALIAS periodicity_${name})

  target_include_directories(periodicity_${name}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  target_link_libraries(periodicity_${name} PUBLIC ${ARG_DEPENDS})

  target_compile_features(periodicity_${name} PUBLIC cxx_std_20)
  set_target_properties(periodicity_${name} PROPERTIES
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
