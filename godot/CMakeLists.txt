# GDExtension: thin binding layer that presents core/ to Godot.
add_library(swbfo_godot SHARED
  src/register_types.cpp
  src/swbfo_info.cpp
  src/swbfo_net.cpp
  src/swbfo_store.cpp
  src/swbfo_terrain.cpp)
target_link_libraries(swbfo_godot PRIVATE swbfo_core swbfo_sqlite_store godot::cpp)
swbfo_target_warnings(swbfo_godot)

# Output straight into the Godot project, named the way game/swbfo.gdextension
# expects: swbfo.<platform>.<target>.double.<arch>.dll
get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)
set(SWBFO_GAME_BIN "${PROJECT_SOURCE_DIR}/game/bin")
set_target_properties(swbfo_godot PROPERTIES
  OUTPUT_NAME "swbfo${GODOTCPP_SUFFIX}"
  RUNTIME_OUTPUT_DIRECTORY "$<1:${SWBFO_GAME_BIN}>"
  LIBRARY_OUTPUT_DIRECTORY "$<1:${SWBFO_GAME_BIN}>"
  PDB_OUTPUT_DIRECTORY "$<1:${SWBFO_GAME_BIN}>")
