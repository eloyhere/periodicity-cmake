# Structure test suites.
#
# Every tests/tst_<topic>.cpp becomes one Qt Test executable linked against
# the module. The glob is evaluated at configure time with CONFIGURE_DEPENDS,
# so adding a suite does not require editing this file.

file(GLOB _structure_tests CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/tst_*.cpp")

# Qt's runtime libraries live beside the Qt that CMake found; the suites are
# headless (no widgets, no window), so they run with the offscreen platform.
get_filename_component(_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
set(_qt_bin "${_qt_prefix}/bin")
set(_qt_plugins "${_qt_prefix}/plugins")

foreach(_src IN LISTS _structure_tests)
  get_filename_component(_name "${_src}" NAME_WE)

  add_executable(${_name} "${_src}")
  target_link_libraries(${_name} PRIVATE periodicity::structure Qt6::Test)
  target_compile_features(${_name} PRIVATE cxx_std_20)
  set_target_properties(${_name} PROPERTIES AUTOMOC ON)

  add_test(NAME ${_name} COMMAND $<TARGET_FILE:${_name}>)
  set_tests_properties(${_name} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${_qt_bin};QT_PLUGIN_PATH=${_qt_plugins}"
    TIMEOUT 120)
endforeach()
