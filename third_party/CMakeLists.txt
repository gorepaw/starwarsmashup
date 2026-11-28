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
add_library(swbfo::zlib ALIAS zlibstatic)

if(SWBFO_BUILD_GODOT)
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
add_library(swbfo_sqlite3 STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
target_include_directories(swbfo_sqlite3 SYSTEM PUBLIC ${sqlite_SOURCE_DIR})
# Recommended options (sqlite.org/compile.html): no extension loading, no
# double-quoted string literals, foreign keys on.
target_compile_definitions(swbfo_sqlite3 PRIVATE
  SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_DEPRECATED SQLITE_DEFAULT_FOREIGN_KEYS=1)
add_library(swbfo::sqlite3 ALIAS swbfo_sqlite3)

# nlohmann/json (MIT, header-only): the extractor's recipe and the launcher's
# update manifests (docs/DISTRIBUTION.md).
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(nlohmann_json)

# unshield 1.6.2 (MIT): reads the Total Experience discs' InstallShield
# cabinets, the version the reference manifest was made with. Only its library,
# built here (its own MD5, no OpenSSL); SOURCE_SUBDIR keeps its CMake out.
FetchContent_Declare(unshield
  URL https://github.com/twogood/unshield/archive/refs/tags/1.6.2.tar.gz
  URL_HASH SHA256=a937ef596ad94d16e7ed2c8553ad7be305798dcdcfd65ae60210b1e54ab51a2f
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR do-not-add
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(unshield)
set(SWBFO_UNSHIELD_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/unshield_config")
file(WRITE "${SWBFO_UNSHIELD_CONFIG}/lib/unshield_config.h" [=[
#define HAVE_INTTYPES_H 1
#define HAVE_MEMORY_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define PACKAGE "unshield"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_NAME "unshield"
#define PACKAGE_STRING "unshield 1.6.2"
#define PACKAGE_TARNAME "unshield"
#define PACKAGE_URL ""
#define PACKAGE_VERSION "1.6.2"
#define SIZE_FORMAT "zi"
#define USE_OUR_OWN_MD5 1
#define VERSION "1.6.2"
]=])
set(SWBFO_UNSHIELD_LIB "${unshield_SOURCE_DIR}/lib")
add_library(swbfo_unshield STATIC
  ${SWBFO_UNSHIELD_LIB}/component.c ${SWBFO_UNSHIELD_LIB}/converter.c ${SWBFO_UNSHIELD_LIB}/directory.c
  ${SWBFO_UNSHIELD_LIB}/file.c ${SWBFO_UNSHIELD_LIB}/file_group.c ${SWBFO_UNSHIELD_LIB}/helper.c
  ${SWBFO_UNSHIELD_LIB}/libunshield.c ${SWBFO_UNSHIELD_LIB}/log.c ${SWBFO_UNSHIELD_LIB}/md5/md5c.c)
target_include_directories(swbfo_unshield SYSTEM PUBLIC ${SWBFO_UNSHIELD_LIB})
target_include_directories(swbfo_unshield PRIVATE ${SWBFO_UNSHIELD_CONFIG} ${unshield_SOURCE_DIR})
if(MSVC)
  target_include_directories(swbfo_unshield PRIVATE ${unshield_SOURCE_DIR}/win32_msvc)
  target_compile_options(swbfo_unshield PRIVATE /W0)
  target_compile_definitions(swbfo_unshield PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
endif()
# PROTOTYPES: its RSA md5 code otherwise declares functions with empty
# parameter lists, which C23 (GCC 15's default) reads as taking none.
target_compile_definitions(swbfo_unshield PRIVATE HAVE_CONFIG_H PROTOTYPES=1)
target_link_libraries(swbfo_unshield PUBLIC swbfo::zlib)
add_library(swbfo::unshield ALIAS swbfo_unshield)

# Monocypher 4.0.2 (CC0 / BSD-2): Ed25519 signatures on update manifests.
FetchContent_Declare(monocypher
  URL https://monocypher.org/download/monocypher-4.0.2.tar.gz
  URL_HASH SHA256=38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR do-not-add
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(monocypher)
add_library(swbfo_monocypher STATIC
  ${monocypher_SOURCE_DIR}/src/monocypher.c ${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c)
target_include_directories(swbfo_monocypher SYSTEM PUBLIC ${monocypher_SOURCE_DIR}/src ${monocypher_SOURCE_DIR}/src/optional)
add_library(swbfo::monocypher ALIAS swbfo_monocypher)

# Dear ImGui 1.91.5 (MIT): the launcher's UI, on Win32 + Direct3D 11.
if(WIN32)
  FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.5.tar.gz
    URL_HASH SHA256=2aa2d169c569368439e5d5667e0796d09ca5cc6432965ce082e516937d7db254
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR do-not-add
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(imgui)
  add_library(swbfo_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
  target_include_directories(swbfo_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(swbfo_imgui PUBLIC d3d11 d3dcompiler dxgi)
  add_library(swbfo::imgui ALIAS swbfo_imgui)
endif()
