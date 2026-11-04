# GDExtension: thin binding layer that presents core/ to Godot.
add_library(swm_godot SHARED
  src/register_types.cpp
  src/swm_info.cpp
  src/swm_net.cpp)
target_link_libraries(swm_godot PRIVATE swm_core swm_sqlite_store godot::cpp)
swm_target_warnings(swm_godot)

# Output straight into the Godot project, named the way game/swm.gdextension
# expects: swm.<platform>.<target>.double.<arch>.dll
get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)
set(SWM_GAME_BIN "${PROJECT_SOURCE_DIR}/game/bin")
set_target_properties(swm_godot PROPERTIES
  OUTPUT_NAME "swm${GODOTCPP_SUFFIX}"
  RUNTIME_OUTPUT_DIRECTORY "$<1:${SWM_GAME_BIN}>"
  LIBRARY_OUTPUT_DIRECTORY "$<1:${SWM_GAME_BIN}>"
  PDB_OUTPUT_DIRECTORY "$<1:${SWM_GAME_BIN}>")
