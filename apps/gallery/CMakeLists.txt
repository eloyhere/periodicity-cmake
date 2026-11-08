qt_add_executable(periodicity_gallery main.cpp)

target_link_libraries(periodicity_gallery PRIVATE periodicity::components)

target_compile_features(periodicity_gallery PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(periodicity_gallery PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
else()
  target_compile_options(periodicity_gallery PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(periodicity_gallery PROPERTIES
  WIN32_EXECUTABLE OFF
  AUTOMOC ON)
