# Periodicity gallery — a visual test bench for the public component API.
#
# The gallery is assembled from public targets only (framework.md §12). It is
# deliberately small: every page is a hand-written composition of component
# instances, so a rendering or interaction defect shows up without a test
# harness in the way.
qt_add_executable(periodicity_gallery
  src/main.cpp
  src/pool_demo.cpp
  src/pool_demo.h)

target_link_libraries(periodicity_gallery PRIVATE
  periodicity::components
  periodicity::pool
  Qt6::Widgets)

target_compile_features(periodicity_gallery PRIVATE cxx_std_20)
set_target_properties(periodicity_gallery PROPERTIES
  AUTOMOC ON
  WIN32_EXECUTABLE OFF)

# Same treatment as the demo: the gallery should run from the build tree and
# from Visual Studio without Qt's bin directory on PATH.
periodicity_deploy_qt_runtime(periodicity_gallery)

if(MSVC)
  target_compile_options(periodicity_gallery PRIVATE
    /W4 /permissive- /utf-8 /Zc:__cplusplus)
else()
  target_compile_options(periodicity_gallery PRIVATE
    -Wall -Wextra -Wpedantic)
endif()
