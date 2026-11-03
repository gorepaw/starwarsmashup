# SWG asset library: TRE archives, hashing, manifests.
add_library(swm_swg STATIC
  swg/appearance.cpp
  swg/character_export.cpp
  swg/gltf.cpp
  swg/iff.cpp
  swg/image.cpp
  swg/manifest.cpp
  swg/sha256.cpp
  swg/skeleton.cpp
  swg/skinned_mesh.cpp
  swg/static_mesh.cpp
  swg/terrain.cpp
  swg/tre_archive.cpp
  swg/tre_library.cpp
  swg/tre_scan.cpp
  swg/world_snapshot.cpp)
target_include_directories(swm_swg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(swm_swg PRIVATE swm::zlib)
swm_target_warnings(swm_swg)

add_executable(swgtool swgtool/main.cpp)
target_link_libraries(swgtool PRIVATE swm_swg)
swm_target_warnings(swgtool)

foreach(test iff_test image_test manifest_test sha256_test skeleton_test terrain_test tre_archive_test)
  add_executable(${test} swg/tests/${test}.cpp)
  target_link_libraries(${test} PRIVATE swm_swg swm::zlib)
  swm_target_warnings(${test})
  add_test(NAME ${test} COMMAND ${test})
endforeach()

# Battlefront II asset library: munged ucfb files.
add_library(swm_bf2 STATIC
  bf2/animation.cpp
  bf2/ucfb.cpp)
target_include_directories(swm_bf2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
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

foreach(test animation_test ucfb_test)
  add_executable(${test} bf2/tests/${test}.cpp)
  target_link_libraries(${test} PRIVATE swm_bf2)
  swm_target_warnings(${test})
  add_test(NAME ${test} COMMAND ${test})
endforeach()
