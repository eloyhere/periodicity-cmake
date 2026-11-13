# Test suite.
#
# Each module contributes tests under tests/<module>/tst_<topic>.cpp.
# The glob is evaluated at configure time (with CONFIGURE_DEPENDS), so a new
# test file is picked up without editing this file — which keeps the test
# directory a conflict-free space for module owners.
#
# Every test links the umbrella target, so a test may exercise any layer; the
# architectural restriction on *library* includes is enforced separately by
# tools/check_layering.cmake.

file(GLOB _test_sources CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/*/tst_*.cpp")

get_filename_component(_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
set(_qt_bin "${_qt_prefix}/bin")
set(_qt_plugins "${_qt_prefix}/plugins")

set(_test_count 0)

foreach(_src IN LISTS _test_sources)
  get_filename_component(_name "${_src}" NAME_WE)

  add_executable(${_name} "${_src}")
  target_link_libraries(${_name} PRIVATE periodicity::components periodicity::mathematic Qt6::Test)

  # tests/render/ exercises periodicity::render directly. Every other test sees
  # it only through the view module's private dependency, which is why this is a
  # directory rule rather than a link on the umbrella target.
  if(_src MATCHES "/render/")
    target_link_libraries(${_name} PRIVATE periodicity::render)
  endif()
  target_compile_features(${_name} PRIVATE cxx_std_20)
  set_target_properties(${_name} PROPERTIES AUTOMOC ON)

  add_test(NAME ${_name} COMMAND $<TARGET_FILE:${_name}>)
  set_tests_properties(${_name} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${_qt_bin};QT_PLUGIN_PATH=${_qt_plugins};TEMP=${CMAKE_CURRENT_BINARY_DIR}/tmp;TMP=${CMAKE_CURRENT_BINARY_DIR}/tmp"
    TIMEOUT 120)

  # The layer benchmarks take about 40 seconds and assert a timing ratio, so a
  # loaded machine can fail them without anything being wrong. They are
  # registered, but labelled so the correctness gate can run `ctest -LE benchmark`
  # and the benchmarks can be run deliberately (`ctest -L benchmark`) with
  # PERIODICITY_BENCH_OUT set. A timing assertion inside the default suite would
  # eventually hide a real failure behind a false one.
  if(_name MATCHES "benchmark")
    set_tests_properties(${_name} PROPERTIES LABELS benchmark TIMEOUT 900)
  endif()

  math(EXPR _test_count "${_test_count} + 1")
endforeach()

message(STATUS "Periodicity: ${_test_count} test executable(s) registered")
