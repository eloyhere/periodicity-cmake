# Layering check — enforces the dependency matrix in framework.md §13.
#
# Usage:
#   cmake -DSOURCE_DIR=<repo root> -P tools/check_layering.cmake
#
# Every public header under <module>/include/md/<module>/ is scanned for
# #include directives that name another md module. The include set must be a
# subset of the module's allowed dependencies. Tokens and core are additionally
# forbidden from including QtWidgets, because they must remain usable headlessly.

cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED SOURCE_DIR)
  get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(MODULES core mathematic structure store pool schedule tokens theme event ripple drawable layer animation view layout gfx render components)

set(ALLOWED_core "")
set(ALLOWED_mathematic "core")
set(ALLOWED_store "core;structure")
set(ALLOWED_structure "core")
set(ALLOWED_pool "core")
set(ALLOWED_schedule "core")
set(ALLOWED_tokens "core")
set(ALLOWED_theme "core;tokens;store")
set(ALLOWED_event "core")
set(ALLOWED_ripple "core;tokens;theme")
set(ALLOWED_drawable "core;tokens;theme")
set(ALLOWED_layer "core;tokens;theme;drawable;ripple")
set(ALLOWED_animation "core;tokens;theme")
set(ALLOWED_view "core;schedule;tokens;theme;event;ripple;drawable;layer;animation")
set(ALLOWED_layout "core;schedule;tokens;theme;event;ripple;drawable;layer;animation;view")
# gfx is the backend-neutral drawing layer: geometry, textures, shaders and the
# software rasterizer. It may not reach up into view, layout or components.
set(ALLOWED_gfx "core;mathematic;structure")
# render is the device layer: it drives gfx and may use the drawing-side value
# types (drawable, layer), but never view/layout/components.
set(ALLOWED_render "core;mathematic;structure;gfx;drawable;layer")
set(ALLOWED_components "core;store;structure;schedule;tokens;theme;event;ripple;drawable;layer;animation;view;layout")

set(_violations "")

foreach(_module IN LISTS MODULES)
  file(GLOB_RECURSE _headers
       "${SOURCE_DIR}/${_module}/include/*.h")

  foreach(_header IN LISTS _headers)
    file(READ "${_header}" _content)
    file(RELATIVE_PATH _rel "${SOURCE_DIR}" "${_header}")

    # Module dependencies declared by md/<module>/... includes.
    string(REGEX MATCHALL "#include[ \t]*[<\"]md/([A-Za-z0-9_]+)/" _matches "${_content}")
    foreach(_match IN LISTS _matches)
      string(REGEX REPLACE "^#include[ \t]*[<\"]md/([A-Za-z0-9_]+)/.*$" "\\1" _dep "${_match}")
      if(_dep STREQUAL _module)
        continue()
      endif()
      if(NOT _dep IN_LIST MODULES)
        list(APPEND _violations "${_rel}: unknown module '${_dep}'")
      elseif(NOT _dep IN_LIST ALLOWED_${_module})
        list(APPEND _violations "${_rel}: ${_module} must not depend on ${_dep}")
      endif()
    endforeach()

    # Headless layers must not pull in widgets.
    if(_module STREQUAL "core" OR _module STREQUAL "structure"
       OR _module STREQUAL "pool" OR _module STREQUAL "tokens"
       OR _module STREQUAL "event")
      if(_content MATCHES "#include[ \t]*[<\"]QtWidgets/" OR
         _content MATCHES "#include[ \t]*[<\"]QWidget>")
        list(APPEND _violations "${_rel}: ${_module} must remain free of QtWidgets")
      endif()
    endif()
  endforeach()
endforeach()

list(LENGTH _violations _count)

if(_count GREATER 0)
  message(STATUS "Layering violations (${_count}):")
  foreach(_v IN LISTS _violations)
    message(STATUS "  - ${_v}")
  endforeach()
  message(FATAL_ERROR "Layering check failed.")
endif()

message(STATUS "Layering check passed.")
