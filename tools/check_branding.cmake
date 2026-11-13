# Periodicity — forbidden-branding gate.
#
# The library does not name other vendors' products anywhere in its sources,
# comments or documentation: the design baseline is described by what it is
# ("the reference design system", "the 24 dp icon grid"), never by who ships it.
# This script is the enforcement half of that rule; wire it into ctest so a
# string that slips back in fails the build.
#
# Usage:  cmake -DSOURCE_DIR=<repo> -P tools/check_branding.cmake
#
# Exit code 0 when clean, 1 with a file:line report otherwise.

if(NOT DEFINED SOURCE_DIR)
  get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

# Case-sensitive on purpose: the ordinary English verb "materialising" must not
# trip the gate, while "Material" as a product name must. The design-system
# shorthand is deliberately NOT in this list: the same three characters appear
# as an SVG path command ("M3 9v6h4…") in icon data, which is not an attribution.
set(_forbidden "Android|Google|Material")

# Only text that ships or documents the library. Build output, the VCS store and
# binary assets are out of scope.
set(_globs
  "core/*.h" "core/*.cpp"
  "mathematic/include/md/mathematic/*.h" "mathematic/src/*.cpp"
  "tests/mathematic/*.cpp"
  "structure/*.h" "structure/*.cpp"
  "store/*.h" "store/*.cpp"
  "pool/*.h" "pool/*.cpp"
  "schedule/*.h" "schedule/*.cpp"
  "tokens/*.h" "tokens/*.cpp"
  "theme/*.h" "theme/*.cpp"
  "event/*.h" "event/*.cpp"
  "drawable/*.h" "drawable/*.cpp"
  "layer/*.h" "layer/*.cpp"
  "animation/*.h" "animation/*.cpp"
  "ripple/*.h" "ripple/*.cpp"
  "view/*.h" "view/*.cpp"
  "layout/*.h" "layout/*.cpp"
  "components/*.h" "components/*.cpp"
  "gfx/*.h" "gfx/*.cpp"
  "render/*.h" "render/*.cpp"
  "tests/*.cpp"
  "apps/*.cpp"
  "demo/*.cpp"
  "CMakeLists.txt"
  "framework.md"
  "README.md"
  "README.zh-CN.md"
  "docs/*.md"
  "translations/*.ts")

file(GLOB_RECURSE _files
  LIST_DIRECTORIES false
  RELATIVE "${SOURCE_DIR}"
  ${_globs})

set(_violations "")
foreach(_file IN LISTS _files)
  file(STRINGS "${SOURCE_DIR}/${_file}" _lines)
  set(_number 0)
  foreach(_line IN LISTS _lines)
    math(EXPR _number "${_number} + 1")
    if(_line MATCHES "${_forbidden}")
      string(APPEND _violations "${_file}:${_number}: ${_line}\n")
    endif()
  endforeach()
endforeach()

if(_violations)
  message(FATAL_ERROR
    "Forbidden branding found (Android / Google / Material / M3):\n"
    "${_violations}"
    "\nDescribe the baseline by what it is, not by whose product it is.")
endif()

message(STATUS "Branding check passed (${_files} files).")
