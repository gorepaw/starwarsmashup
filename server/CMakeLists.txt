# Standalone game server: core/ in its own process, without Godot.
add_executable(swbfo_server main.cpp)
target_link_libraries(swbfo_server PRIVATE swbfo_core swbfo_sqlite_store)
if(WIN32)
  target_link_libraries(swbfo_server PRIVATE winmm)
endif()
swbfo_target_warnings(swbfo_server)

# Joins a server as a player for a few seconds and reports what it saw.
add_executable(swbfo_probe probe.cpp)
target_link_libraries(swbfo_probe PRIVATE swbfo_core)
swbfo_target_warnings(swbfo_probe)
