# The child process the gallery and the IPC tests spawn.
#
# A console tool, not a widget: it reads length-prefixed frames from stdin,
# answers PING with PONG, and (with --ring) streams TICK frames into a shared
# memory ring so the parent can measure cross-process throughput.
add_executable(periodicity_ipc_echo
  src/main.cpp)

target_link_libraries(periodicity_ipc_echo PRIVATE
  periodicity::pool
  Qt6::Core)

target_compile_features(periodicity_ipc_echo PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(periodicity_ipc_echo PRIVATE
    /W4 /permissive- /utf-8 /Zc:__cplusplus)
else()
  target_compile_options(periodicity_ipc_echo PRIVATE
    -Wall -Wextra -Wpedantic)
endif()
