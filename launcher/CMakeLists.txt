# The launcher (Windows): updates from R2, install discovery and extraction,
# and starting the game (docs/DISTRIBUTION.md).

# Update manifests, signatures, downloads.
add_library(swbfo_update STATIC update.cpp)
target_include_directories(swbfo_update PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(swbfo_update
  PUBLIC swbfo_extract_lib
  PRIVATE swbfo::monocypher nlohmann_json::nlohmann_json winhttp bcrypt)
swbfo_target_warnings(swbfo_update)

add_executable(launcher WIN32 main.cpp app.cpp launcher.rc)
target_link_libraries(launcher PRIVATE swbfo_update swbfo_extract_lib swbfo::imgui ole32 shell32)
if(MSVC)
  target_link_options(launcher PRIVATE /MANIFEST:NO)  # launcher.rc carries launcher.manifest
endif()
swbfo_target_warnings(launcher)

# Release signing (publisher side).
add_executable(swbfo_sign sign_main.cpp)
target_link_libraries(swbfo_sign PRIVATE swbfo_update)
swbfo_target_warnings(swbfo_sign)

add_executable(update_test tests/update_test.cpp)
target_link_libraries(update_test PRIVATE swbfo_update)
swbfo_target_warnings(update_test)
add_test(NAME update_test COMMAND update_test)
