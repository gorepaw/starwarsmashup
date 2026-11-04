# Game simulation library. Pure C++: no Godot headers (see docs/ARCHITECTURE.md).
add_library(swm_core STATIC
  src/build_info.cpp
  src/client.cpp
  src/heightfield.cpp
  src/movement.cpp
  src/net/link.cpp
  src/net/protocol.cpp
  src/net/udp.cpp
  src/server.cpp)
target_include_directories(swm_core PUBLIC include)
target_compile_definitions(swm_core PRIVATE SWM_VERSION="${PROJECT_VERSION}")
if(WIN32)
  target_link_libraries(swm_core PUBLIC ws2_32)
endif()
swm_target_warnings(swm_core)

foreach(test core_test movement_test net_test)
  add_executable(${test} tests/${test}.cpp)
  target_link_libraries(${test} PRIVATE swm_core)
  swm_target_warnings(${test})
  add_test(NAME ${test} COMMAND ${test})
endforeach()
