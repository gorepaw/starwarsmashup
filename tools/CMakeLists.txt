# SWG asset library: TRE archives, hashing, manifests.
add_library(swm_swg STATIC
  swg/manifest.cpp
  swg/sha256.cpp
  swg/tre_archive.cpp)
target_include_directories(swm_swg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(swm_swg PRIVATE swm::zlib)
swm_target_warnings(swm_swg)

add_executable(swgtool swgtool/main.cpp)
target_link_libraries(swgtool PRIVATE swm_swg)
swm_target_warnings(swgtool)

foreach(test sha256_test tre_archive_test)
  add_executable(${test} swg/tests/${test}.cpp)
  target_link_libraries(${test} PRIVATE swm_swg swm::zlib)
  swm_target_warnings(${test})
  add_test(NAME ${test} COMMAND ${test})
endforeach()
