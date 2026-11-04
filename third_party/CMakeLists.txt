include(FetchContent)

set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zlib
  GIT_REPOSITORY https://github.com/madler/zlib.git
  GIT_TAG v1.3.1
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(zlib)

# zlib 1.3.1 doesn't attach its include directories to its targets.
target_include_directories(zlibstatic INTERFACE ${zlib_BINARY_DIR} ${zlib_SOURCE_DIR})
add_library(swm::zlib ALIAS zlibstatic)

if(SWM_BUILD_GODOT)
  # Must match the engine build: Godot 4.7, double precision (see docs/ARCHITECTURE.md).
  # godot-cpp's bundled API files come from single-precision engines, so we use
  # one dumped from our own editor build: godot --dump-extension-api
  set(GODOTCPP_CUSTOM_API_FILE "${CMAKE_CURRENT_SOURCE_DIR}/godot-api/extension_api-4.7.2-double.json"
      CACHE FILEPATH "" FORCE)
  set(GODOTCPP_PRECISION "double" CACHE STRING "" FORCE)
  set(GODOTCPP_TARGET "template_debug" CACHE STRING "")
  set(GODOTCPP_USE_HOT_RELOAD ON CACHE BOOL "")
  # core/ uses the standard library with exceptions enabled; godot-cpp's
  # default would define _HAS_EXCEPTIONS=0 for everything that links it.
  set(GODOTCPP_DISABLE_EXCEPTIONS OFF CACHE BOOL "" FORCE)
  # Keep our /W4 warnings to our own code.
  set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "" FORCE)

  FetchContent_Declare(godot-cpp
    GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
    GIT_TAG 10.0.0-stable
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(godot-cpp)
endif()

# SQLite, the amalgamation, for the persistence store (core/persistence).
FetchContent_Declare(sqlite
  URL https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip
  URL_HASH SHA3_256=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(sqlite)
add_library(swm_sqlite3 STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
target_include_directories(swm_sqlite3 SYSTEM PUBLIC ${sqlite_SOURCE_DIR})
# Recommended options (sqlite.org/compile.html): no extension loading, no
# double-quoted string literals, foreign keys on.
target_compile_definitions(swm_sqlite3 PRIVATE
  SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_DEPRECATED SQLITE_DEFAULT_FOREIGN_KEYS=1)
add_library(swm::sqlite3 ALIAS swm_sqlite3)
