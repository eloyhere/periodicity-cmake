# Periodicity — a Material Design 3 component library for Qt Widgets.
# Architecture: see framework.md. One CMake target per architectural layer.
cmake_minimum_required(VERSION 3.21)

project(Periodicity VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Test Network)
qt_standard_project_setup(REQUIRES 6.8)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PeriodicityModule)

enable_testing()

# Layers in dependency order (framework.md §4).
add_subdirectory(core)
add_subdirectory(store)
add_subdirectory(structure)
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
