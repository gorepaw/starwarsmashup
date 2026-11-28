# SWG asset library: TRE archives, hashing, manifests.
add_library(swm_swg STATIC
  swg/appearance.cpp
  swg/appearance_collision.cpp
  swg/building_export.cpp
  swg/character_export.cpp
  swg/city_export.cpp
  swg/datatable.cpp
  swg/gltf.cpp
  swg/gltf_assets.cpp
  swg/iff.cpp
  swg/image.cpp
  swg/keyframe_animation.cpp
  swg/manifest.cpp
  swg/object_template.cpp
  swg/portal_layout.cpp
  swg/sha256.cpp
  swg/skeleton.cpp
  swg/skinned_mesh.cpp
  swg/texture_renderer.cpp
  swg/particle_effect.cpp
  swg/particle_export.cpp
  swg/static_mesh.cpp
  swg/terrain.cpp
  swg/tre_archive.cpp
  swg/tre_library.cpp
  swg/tre_scan.cpp
  swg/world_snapshot.cpp)
target_include_directories(swm_swg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(swm_swg PUBLIC swm_core PRIVATE swm::zlib)
swm_target_warnings(swm_swg)

add_executable(swgtool swgtool/main.cpp)
target_link_libraries(swgtool PRIVATE swm_swg)
swm_target_warnings(swgtool)

foreach(test blend_shape_test city_export_test iff_test keyframe_animation_test image_test manifest_test portal_layout_test sha256_test skeleton_test terrain_test tre_archive_test)
  add_executable(${test} swg/tests/${test}.cpp)
  target_link_libraries(${test} PRIVATE swm_swg swm::zlib)
  swm_target_warnings(${test})
  add_test(NAME ${test} COMMAND ${test})
endforeach()

# Battlefront II asset library: munged ucfb files.
add_library(swm_bf2 STATIC
  bf2/animation.cpp
  bf2/effect.cpp
  bf2/hud.cpp
  bf2/level.cpp
  bf2/model.cpp
  bf2/model_export.cpp
  bf2/odf.cpp
  bf2/sound.cpp
  bf2/ucfb.cpp)
target_include_directories(swm_bf2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(swm_bf2 PUBLIC swm_swg)  # glTF and PNG writing, DXT decoding
swm_target_warnings(swm_bf2)

# Battlefront animation -> Galaxies skeleton.
add_library(swm_retarget STATIC
  retarget/retarget.cpp)
target_link_libraries(swm_retarget PUBLIC swm_swg swm_bf2)
swm_target_warnings(swm_retarget)
target_link_libraries(swgtool PRIVATE swm_retarget)

add_executable(bf2tool bf2tool/main.cpp)
target_link_libraries(bf2tool PRIVATE swm_bf2)
swm_target_warnings(bf2tool)

foreach(test animation_test effect_test hud_test level_test model_test sound_test ucfb_test)
  add_executable(${test} bf2/tests/${test}.cpp)
  target_link_libraries(${test} PRIVATE swm_bf2)
  swm_target_warnings(${test})
  add_test(NAME ${test} COMMAND ${test})
endforeach()

# The extractor (Windows): finds installs, reads the discs' cabinets, runs the
# recipe; the launcher's core, and swm_extract over it (docs/DISTRIBUTION.md).
if(WIN32)
  add_library(swm_extract_lib STATIC
    extract/cabs.cpp
    extract/discovery.cpp
    extract/process.cpp
    extract/recipe.cpp)
  target_include_directories(swm_extract_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(swm_extract_lib PUBLIC nlohmann_json::nlohmann_json PRIVATE swm::unshield advapi32)
  target_compile_definitions(swm_extract_lib PUBLIC UNICODE _UNICODE)
  swm_target_warnings(swm_extract_lib)

  add_executable(swm_extract extract/main.cpp)
  target_link_libraries(swm_extract PRIVATE swm_extract_lib shell32)
  swm_target_warnings(swm_extract)

  add_executable(extract_test extract/tests/extract_test.cpp)
  target_link_libraries(extract_test PRIVATE swm_extract_lib shell32)
  swm_target_warnings(extract_test)
  add_test(NAME extract_test COMMAND extract_test)
endif()
