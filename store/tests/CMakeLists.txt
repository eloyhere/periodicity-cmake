# Store test suites (headless: Qt Core only).
#
# Every tests/tst_<topic>.cpp becomes one Qt Test executable linked against
# the module. The glob is evaluated at configure time with CONFIGURE_DEPENDS,
# so adding a suite does not require editing this file.

file(GLOB _store_tests CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/tst_*.cpp")

get_filename_component(_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
set(_qt_bin "${_qt_prefix}/bin")
set(_qt_plugins "${_qt_prefix}/plugins")

foreach(_src IN LISTS _store_tests)
  get_filename_component(_name "${_src}" NAME_WE)

  add_executable(${_name} "${_src}")
  target_link_libraries(${_name} PRIVATE periodicity::store Qt6::Test)

  add_test(NAME ${_name} COMMAND ${_name})
  set_tests_properties(${_name} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${_qt_bin};QT_PLUGIN_PATH=${_qt_plugins}"
    LABELS "store")
endforeach()
