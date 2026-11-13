# Periodicity — the Qt binding, in one place.
#
# Included exactly once, from the top-level CMakeLists.txt, before any module
# directory is added. It answers the only questions the rest of the build has
# about Qt:
#
#   * which Qt is required (6.8.3), and which modules are required vs optional;
#   * which optional modules were actually found, as PERIODICITY_HAS_* compile
#     definitions computed from targets — never hard-coded;
#   * where the shipped RHI headers live, and whether a target may use them.
#
# Qt 6.8.3 is pinned by a user decision (13 November 2026) and will not move.
# That is what makes two things acceptable here that would not be for a floating
# minimum:
#
#   1. Svg, SvgWidgets and ShaderTools are REQUIRED, not optional. A machine
#      without them now fails to configure instead of silently shipping a
#      degraded build.
#   2. The RHI headers (rhi/qrhi.h, rhi/qshader.h) live in QtGui's *versioned*
#      private include directory, which the Qt6::Gui target does not export.
#      Depending on a directory inside the pinned installation is acceptable
#      because the layout cannot change under us; the helper below still checks
#      for the header at configure time and falls back to PERIODICITY_HAS_RHI=0
#      rather than producing a compile error on a differently packaged Qt.

include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# Required modules
# ---------------------------------------------------------------------------
# The library proper. Core/Gui/Widgets are the baseline every layer links; Svg
# and SvgWidgets back the icon pipeline; ShaderTools is the build-time half of
# the RHI device (qsb compiles the shaders under shaders/).
find_package(Qt6 6.8.3 REQUIRED COMPONENTS
  Core Gui Widgets Svg SvgWidgets ShaderTools)

# Tests and the umbrella targets also need Test and Network, and components use
# OpenGLWidgets when the installation has it. Kept in this file so the Qt
# dependency surface is declared once; OpenGLWidgets stays optional so a
# headless Qt still configures.
find_package(Qt6 6.8.3 REQUIRED COMPONENTS Test Network
  OPTIONAL_COMPONENTS OpenGLWidgets)

# ---------------------------------------------------------------------------
# Optional modules
# ---------------------------------------------------------------------------
# Quick, QuickWidgets and Quick3D are a separate call on purpose: a machine
# without any of them still configures, and the PERIODICITY_HAS_QUICK3D
# definition below tells the code what it got.
find_package(Qt6 6.8.3 QUIET OPTIONAL_COMPONENTS
  Quick QuickWidgets Quick3D)

# LinguistTools stays optional exactly as before: without it the catalogs are
# simply not embedded.
find_package(Qt6 6.8.3 QUIET OPTIONAL_COMPONENTS LinguistTools)

# Standard Qt project settings (AUTOMOC defaults, QT_VERSION_MAJOR, ...). This
# must run after project(), which the including file does.
qt_standard_project_setup(REQUIRES 6.8)

# ---------------------------------------------------------------------------
# What was actually found
# ---------------------------------------------------------------------------
# Every definition is derived from an imported target. A missing module reports
# 0 and the code compiles its fallback, so the answer never drifts from the
# installation. add_compile_definitions() applies to every target declared
# after this point, which is all of them: this file is included before the
# first add_subdirectory().
set(PERIODICITY_HAS_SHADERTOOLS 0)
if(TARGET Qt6::ShaderTools)
  set(PERIODICITY_HAS_SHADERTOOLS 1)
endif()

set(PERIODICITY_HAS_SVG 0)
if(TARGET Qt6::Svg AND TARGET Qt6::SvgWidgets)
  set(PERIODICITY_HAS_SVG 1)
endif()

# Quick3D is the marker: it cannot be present without Quick, and QuickWidgets is
# reported separately for the layers that embed a Quick scene in a widget.
set(PERIODICITY_HAS_QUICK3D 0)
if(TARGET Qt6::Quick AND TARGET Qt6::Quick3D)
  set(PERIODICITY_HAS_QUICK3D 1)
endif()

add_compile_definitions(
  PERIODICITY_HAS_SHADERTOOLS=${PERIODICITY_HAS_SHADERTOOLS}
  PERIODICITY_HAS_SVG=${PERIODICITY_HAS_SVG}
  PERIODICITY_HAS_QUICK3D=${PERIODICITY_HAS_QUICK3D})

message(STATUS
  "Periodicity: Qt ${Qt6_VERSION} at ${Qt6_DIR}; "
  "ShaderTools=${PERIODICITY_HAS_SHADERTOOLS} "
  "Svg=${PERIODICITY_HAS_SVG} "
  "Quick3D=${PERIODICITY_HAS_QUICK3D}")

# ---------------------------------------------------------------------------
# RHI headers
# ---------------------------------------------------------------------------
# rhi/qrhi.h and rhi/qshader.h ship inside
#
#   <prefix>/include/QtGui/<version>/QtGui/rhi/
#
# and Qt6::Gui does not add .../QtGui/<version>/QtGui to the include path. The
# directory is derived from Qt6_DIR rather than a hard-coded prefix so a moved
# Qt installation still resolves, and it is validated by looking for the header
# itself — Qt6Gui_VERSION is the package version CMake recorded.
get_filename_component(_periodicity_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)

set(PERIODICITY_RHI_INCLUDE_DIR
  "${_periodicity_qt_prefix}/include/QtGui/${Qt6Gui_VERSION}/QtGui")

if(NOT EXISTS "${PERIODICITY_RHI_INCLUDE_DIR}/rhi/qrhi.h")
  # Fall back to the one versioned directory that actually contains the header,
  # in case the package version and the directory version ever disagree.
  file(GLOB _periodicity_rhi_candidates
    "${_periodicity_qt_prefix}/include/QtGui/*/QtGui/rhi/qrhi.h")
  if(_periodicity_rhi_candidates)
    list(GET _periodicity_rhi_candidates 0 _periodicity_rhi_header)
    get_filename_component(PERIODICITY_RHI_INCLUDE_DIR
      "${_periodicity_rhi_header}" DIRECTORY)   # .../QtGui/rhi -> .../QtGui
    get_filename_component(PERIODICITY_RHI_INCLUDE_DIR
      "${PERIODICITY_RHI_INCLUDE_DIR}" DIRECTORY)
  endif()
  unset(_periodicity_rhi_candidates)
  unset(_periodicity_rhi_header)
endif()
unset(_periodicity_qt_prefix)

set(PERIODICITY_HAS_RHI 0)
if(EXISTS "${PERIODICITY_RHI_INCLUDE_DIR}/rhi/qrhi.h"
   AND TARGET Qt6::Gui
   AND TARGET Qt6::ShaderTools)
  # qsb must be able to compile the shaders under shaders/ at build time. The
  # macro is defined by Qt6ShaderToolsTools, which find_package(ShaderTools)
  # pulls in as its tool dependency. If a future packaging drops it, the device
  # is simply not compiled in rather than breaking the build.
  if(COMMAND qt6_add_shaders)
    set(PERIODICITY_HAS_RHI 1)
  else()
    message(STATUS
      "Periodicity: rhi/qrhi.h is present but qt6_add_shaders is not defined; "
      "PERIODICITY_HAS_RHI=0")
  endif()
else()
  message(STATUS
    "Periodicity: the RHI headers or their Qt modules are missing under "
    "'${PERIODICITY_RHI_INCLUDE_DIR}'; PERIODICITY_HAS_RHI=0")
endif()

if(PERIODICITY_HAS_RHI)
  message(STATUS
    "Periodicity: RHI headers at ${PERIODICITY_RHI_INCLUDE_DIR}; "
    "qt6_add_shaders available; PERIODICITY_HAS_RHI=1")
endif()

# Adds the RHI include directory and PERIODICITY_HAS_RHI to a target.
#
# The definition is PUBLIC because the RhiDevice header exists in both builds
# and callers (the tests, the factory) must be able to tell the two apart
# without duplicating the configure-time check. The include directory stays
# PRIVATE: no public header in render/ includes <rhi/...>, so consumers never
# need the directory.
function(periodicity_enable_rhi target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR
      "periodicity_enable_rhi(${target}): no such target")
  endif()

  if(PERIODICITY_HAS_RHI)
    target_include_directories(${target} PRIVATE "${PERIODICITY_RHI_INCLUDE_DIR}")
    target_link_libraries(${target} PRIVATE Qt6::Gui)
    target_compile_definitions(${target} PUBLIC PERIODICITY_HAS_RHI=1)
  else()
    target_compile_definitions(${target} PUBLIC PERIODICITY_HAS_RHI=0)
  endif()
endfunction()
