# Standalone game server: core/ in its own process, without Godot.
add_executable(swm_server main.cpp)
target_link_libraries(swm_server PRIVATE swm_core)
if(WIN32)
  target_link_libraries(swm_server PRIVATE winmm)
endif()
swm_target_warnings(swm_server)
