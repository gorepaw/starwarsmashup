# Game simulation library. Pure C++: no Godot headers (see docs/ARCHITECTURE.md).
add_library(swm_core STATIC
  src/build_info.cpp)
target_include_directories(swm_core PUBLIC include)
target_compile_definitions(swm_core PRIVATE SWM_VERSION="${PROJECT_VERSION}")
swm_target_warnings(swm_core)

add_executable(core_test tests/core_test.cpp)
target_link_libraries(core_test PRIVATE swm_core)
swm_target_warnings(core_test)
add_test(NAME core_test COMMAND core_test)
