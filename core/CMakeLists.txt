# Game simulation library. Pure C++: no Godot headers (see docs/ARCHITECTURE.md).
add_library(swm_core STATIC
  src/bot.cpp
  src/build_info.cpp
  src/client.cpp
  src/combat.cpp
  src/conquest.cpp
  src/crafting.cpp
  src/heightfield.cpp
  src/hub.cpp
  src/ini.cpp
  src/items.cpp
  src/loadout.cpp
  src/loot.cpp
  src/movement.cpp
  src/nav.cpp
  src/net/link.cpp
  src/net/protocol.cpp
  src/net/udp.cpp
  src/persistence.cpp
  src/profession.cpp
  src/quest.cpp
  src/server.cpp
  src/server_crafting.cpp
  src/server_professions.cpp
  src/server_services.cpp
  src/server_quests.cpp
  src/server_vehicles.cpp
  src/species.cpp
  src/vehicle.cpp
  src/wildlife.cpp
  src/world.cpp)
target_include_directories(swm_core PUBLIC include)
target_compile_definitions(swm_core PRIVATE SWM_VERSION="${PROJECT_VERSION}")
if(WIN32)
  target_link_libraries(swm_core PUBLIC ws2_32)
endif()
swm_target_warnings(swm_core)

# SQLite implementation of the persistence interfaces, kept out of swm_core.
add_library(swm_sqlite_store STATIC src/sqlite_store.cpp)
target_link_libraries(swm_sqlite_store PUBLIC swm_core PRIVATE swm::sqlite3)
swm_target_warnings(swm_sqlite_store)

foreach(test core_test movement_test nav_test net_test persistence_test combat_test match_test hub_test species_test items_test wildlife_test loot_test quest_test vehicle_test profession_test crafting_test services_test)
  add_executable(${test} tests/${test}.cpp)
  target_link_libraries(${test} PRIVATE swm_core swm_sqlite_store)
  swm_target_warnings(${test})
  add_test(NAME ${test} COMMAND ${test})
endforeach()
# Also opens a database directly, to fake one from a newer build.
target_link_libraries(persistence_test PRIVATE swm::sqlite3)
