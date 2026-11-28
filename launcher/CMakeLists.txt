# The launcher (Windows): updates from R2, install discovery and extraction,
# and starting the game (docs/DISTRIBUTION.md).

# Update manifests, signatures, downloads.
add_library(swm_update STATIC update.cpp)
target_include_directories(swm_update PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(swm_update
  PUBLIC swm_extract_lib
  PRIVATE swm::monocypher nlohmann_json::nlohmann_json winhttp bcrypt)
swm_target_warnings(swm_update)

add_executable(launcher WIN32 main.cpp app.cpp launcher.rc)
target_link_libraries(launcher PRIVATE swm_update swm_extract_lib swm::imgui ole32 shell32)
if(MSVC)
  target_link_options(launcher PRIVATE /MANIFEST:NO)  # launcher.rc carries launcher.manifest
endif()
swm_target_warnings(launcher)

# Release signing (publisher side).
add_executable(swm_sign sign_main.cpp)
target_link_libraries(swm_sign PRIVATE swm_update)
swm_target_warnings(swm_sign)

add_executable(update_test tests/update_test.cpp)
target_link_libraries(update_test PRIVATE swm_update)
swm_target_warnings(update_test)
add_test(NAME update_test COMMAND update_test)
