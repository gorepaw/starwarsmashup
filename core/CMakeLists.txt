# Game simulation library. Pure C++: no Godot headers (see docs/ARCHITECTURE.md).
add_library(swbfo_core STATIC
  src/bot.cpp
  src/bot_roster.cpp
  src/build_info.cpp
  src/client.cpp
  src/combat.cpp
  src/conquest.cpp
  src/crafting.cpp
  src/galaxy.cpp
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
  src/server_tow.cpp
  src/server_quests.cpp
  src/server_specialists.cpp
  src/server_perks.cpp
  src/server_vehicles.cpp
  src/specialist.cpp
  src/species.cpp
  src/telemetry.cpp
  src/vehicle.cpp
  src/wildlife.cpp
  src/world.cpp)
target_include_directories(swbfo_core PUBLIC include)
target_compile_definitions(swbfo_core PRIVATE SWBFO_VERSION="${PROJECT_VERSION}")
if(WIN32)
  target_link_libraries(swbfo_core PUBLIC ws2_32)
endif()
swbfo_target_warnings(swbfo_core)

# SQLite implementation of the persistence interfaces, kept out of swbfo_core.
add_library(swbfo_sqlite_store STATIC src/sqlite_store.cpp)
target_link_libraries(swbfo_sqlite_store PUBLIC swbfo_core PRIVATE swbfo::sqlite3)
swbfo_target_warnings(swbfo_sqlite_store)

foreach(test core_test movement_test nav_test net_test persistence_test combat_test match_test hub_test species_test items_test wildlife_test loot_test quest_test vehicle_test space_test profession_test crafting_test services_test modes_test specialist_test galaxy_test)
  add_executable(${test} tests/${test}.cpp)
  target_link_libraries(${test} PRIVATE swbfo_core swbfo_sqlite_store)
  swbfo_target_warnings(${test})
  add_test(NAME ${test} COMMAND ${test})
endforeach()
# Also opens a database directly, to fake one from a newer build.
target_link_libraries(persistence_test PRIVATE swbfo::sqlite3)
