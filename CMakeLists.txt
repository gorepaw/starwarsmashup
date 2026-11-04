cmake_minimum_required(VERSION 3.28)
project(starwarsmashup VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static MSVC runtime everywhere, matching godot-cpp's default. Set before any
# target is created so every library agrees.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "MSVC runtime library")

option(SWM_BUILD_GODOT "Build the GDExtension (fetches godot-cpp)" ON)

function(swm_target_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

enable_testing()

add_subdirectory(third_party)
add_subdirectory(core)
add_subdirectory(server)
add_subdirectory(tools)
if(SWM_BUILD_GODOT)
  add_subdirectory(godot)
endif()
