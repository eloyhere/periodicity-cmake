# Periodicity — a Material Design 3 component library for Qt Widgets.
# Architecture: see framework.md. One CMake target per architectural layer.
cmake_minimum_required(VERSION 3.21)

project(Periodicity VERSION 0.1.0 LANGUAGES CXX)

# Install layout (CMAKE_INSTALL_LIBDIR, CMAKE_INSTALL_INCLUDEDIR, ...) and the
# helpers that generate PeriodicityConfig.cmake / PeriodicityConfigVersion.cmake.
# Included once here, not per module: cmake/PeriodicityModule.cmake records each
# module's install() rules and reads these variables when a module is declared.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# OpenGLWidgets is optional: Context3D always has its software projection path,
# and compiles the QOpenGLWidget backend only when Qt provides the module.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Test Network
  OPTIONAL_COMPONENTS OpenGLWidgets)
qt_standard_project_setup(REQUIRES 6.8)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PeriodicityModule)

enable_testing()

# Layers in dependency order (framework.md §4).
add_subdirectory(core)
add_subdirectory(structure)
add_subdirectory(store)
add_subdirectory(pool)
add_subdirectory(schedule)
add_subdirectory(tokens)
add_subdirectory(theme)
add_subdirectory(event)
add_subdirectory(drawable)
add_subdirectory(layer)
add_subdirectory(animation)
add_subdirectory(ripple)
add_subdirectory(view)
add_subdirectory(layout)
add_subdirectory(components)

add_subdirectory(tests)
add_subdirectory(demo)
add_subdirectory(apps)

# Visual Studio: make the demo the solution's startup project, so pressing F5 in
# a freshly generated Periodicity.sln builds and runs it without first hunting
# for it in Solution Explorer. Ignored by the Ninja presets.
if(CMAKE_GENERATOR MATCHES "^Visual Studio")
  set_property(DIRECTORY "${CMAKE_SOURCE_DIR}"
    PROPERTY VS_STARTUP_PROJECT periodicity_demo)
endif()

# ---------------------------------------------------------------------------
# Installation: find_package(Periodicity)
#
# Each module contributes, from cmake/PeriodicityModule.cmake,
#
#   install(TARGETS periodicity_<module> EXPORT PeriodicityTargets ...)
#   install(DIRECTORY <module>/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
#
# here the export set becomes an importable targets file and the package config
# and version files are generated beside it, so an installed tree is consumed
# with find_package(Periodicity) and periodicity::<module> targets:
#
#   <prefix>/lib/cmake/Periodicity/PeriodicityConfig.cmake
#   <prefix>/lib/cmake/Periodicity/PeriodicityConfigVersion.cmake
#   <prefix>/lib/cmake/Periodicity/PeriodicityTargets.cmake
#
# Tests, the demo and the gallery are development-only and are not installed.
set(PERIODICITY_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Periodicity")

install(EXPORT PeriodicityTargets
  DESTINATION "${PERIODICITY_INSTALL_CMAKEDIR}"
  NAMESPACE periodicity::
  FILE PeriodicityTargets.cmake)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PeriodicityConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/PeriodicityConfig.cmake"
  INSTALL_DESTINATION "${PERIODICITY_INSTALL_CMAKEDIR}")

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/PeriodicityConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/PeriodicityConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/PeriodicityConfigVersion.cmake"
  DESTINATION "${PERIODICITY_INSTALL_CMAKEDIR}")
