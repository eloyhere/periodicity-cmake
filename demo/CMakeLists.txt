# Periodicity demo — an Element-Plus-style documentation window.
qt_add_executable(periodicity_demo
  src/main.cpp
  src/demo_ui.h)

target_link_libraries(periodicity_demo PRIVATE
  periodicity::theme
  periodicity::animation
  periodicity::event
  periodicity::ripple
  periodicity::schedule
  periodicity::drawable
  periodicity::layer
  Qt6::Widgets)

target_compile_features(periodicity_demo PRIVATE cxx_std_20)
set_target_properties(periodicity_demo PROPERTIES AUTOMOC ON WIN32_EXECUTABLE OFF)

if(MSVC)
  target_compile_options(periodicity_demo PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
else()
  target_compile_options(periodicity_demo PRIVATE -Wall -Wextra -Wpedantic)
endif()
