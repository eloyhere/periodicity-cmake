# Periodicity — a design-baseline component library for Qt Widgets.
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
add_subdirectory(mathematic)
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
# gfx is the software drawing substrate: data types plus the rasterizer that
# render/ (phase 2) will drive. It sits above layout and below components, and
# links Qt Gui/Core only, so it stays usable without a window system.
add_subdirectory(gfx)
# render is the device layer of phase 2: RenderTarget, RenderDevice,
# SoftwareDevice, the QPainter text bridge, the factory and the frame helper.
# It sits between gfx and components, links Qt Gui/Core only, and depends on
# gfx plus drawable/layer. Components does not depend on it yet, so phase 3 can
# move Context2D over without a public-header change today.
add_subdirectory(render)
add_subdirectory(components)

# Phase 1 of docs/rendering-architecture.md: components may use gfx (and, in
# phase 3, Context2D will emit its CommandBuffers), but no components header is
# changed yet. Linking here keeps the module graph correct without editing the
# components module declaration.
target_link_libraries(periodicity_components PUBLIC periodicity::gfx)

# Translations. The .ts sources are compiled to .qm and embedded under
# :/i18n, which is exactly where md::i18n::install() looks them up.
#
# The catalogs go through a private OBJECT library rather than straight into
# periodicity_core: the module helper installs every module into the
# PeriodicityTargets export set, and install(EXPORT) rejects an exported target
# whose link dependencies are not exported too — which is what a resource target
# attached to periodicity_core would be. Merging the objects keeps the archive
# self-contained and the export set clean.
#
# LinguistTools stays optional: without it, md::i18n finds no catalog and the
# source language stays in place.
find_package(Qt6 6.8 QUIET OPTIONAL_COMPONENTS LinguistTools)
if(TARGET Qt6::lrelease)
  qt_add_lrelease(periodicity_translations
    TS_FILES translations/periodicity_zh_CN.ts
    QM_FILES_OUTPUT_VARIABLE periodicity_qm_files)
  add_library(periodicity_i18n_catalogs OBJECT)
  qt_add_resources(periodicity_i18n_catalogs "i18n"
    PREFIX "/i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${periodicity_qm_files})
  target_link_libraries(periodicity_i18n_catalogs PRIVATE Qt6::Core)
  target_sources(periodicity_core PRIVATE $<TARGET_OBJECTS:periodicity_i18n_catalogs>)
  # i18n.cpp references the generated resource initialiser only when this is
  # defined; without LinguistTools no catalog exists and the call is compiled out.
  target_compile_definitions(periodicity_core PRIVATE PERIODICITY_HAS_I18N_CATALOGS)
else()
  message(STATUS "Qt LinguistTools not found: shipping without .qm catalogs")
endif()

add_subdirectory(tests)

# The render tests (tests/render/) exercise periodicity::render directly. The
# test driver links the umbrella components target, which deliberately does not
# depend on render yet, so attach render to the test executables that need it.
# This keeps the module graph honest: only tests, never components, gain the
# dependency during phase 2.
get_property(_periodicity_test_targets DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests"
  PROPERTY BUILDSYSTEM_TARGETS)
foreach(_periodicity_test_target IN LISTS _periodicity_test_targets)
  if(_periodicity_test_target MATCHES "^tst_render_")
    target_link_libraries(${_periodicity_test_target} PRIVATE periodicity::render)
  endif()
endforeach()
unset(_periodicity_test_targets)
unset(_periodicity_test_target)

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
