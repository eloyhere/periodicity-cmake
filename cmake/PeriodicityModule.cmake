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
