cmake_minimum_required(VERSION 3.25)
project(WinBot VERSION 0.2.0 LANGUAGES CXX C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── MSVC flags ────────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_definitions(
        UNICODE
        _UNICODE
        NOMINMAX
        _WIN32_WINNT=0x0A00  # Windows 10+
    )
endif()

# ── FetchContent ──────────────────────────────────────────────────────────────
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# nlohmann/json — config.json + JSON tool call parsing + CDP messaging
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# stb — single-file public domain libraries (stb_image_write for PNG encoding)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
# stb is header-only; we just need the source directory in include path
FetchContent_MakeAvailable(stb)

# SQLite3 — persistent memory store (amalgamation, no extra deps)
FetchContent_Declare(
    sqlite_amalgamation
    URL      https://www.sqlite.org/2025/sqlite-amalgamation-3490100.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite_amalgamation)

add_library(sqlite3 STATIC
    ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c
)
target_include_directories(sqlite3 PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
)

# Lua 5.4 — Embedded scripting engine
FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.4.6.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lua)
file(GLOB LUA_SOURCES "${lua_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM LUA_SOURCES "${lua_SOURCE_DIR}/src/luac.c" "${lua_SOURCE_DIR}/src/lua.c")
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC "${lua_SOURCE_DIR}/src")

# LuaBridge3 — C++ bindings for Lua
FetchContent_Declare(
    luabridge
    GIT_REPOSITORY https://github.com/kunitoki/LuaBridge3.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(luabridge)

# ── Source files ──────────────────────────────────────────────────────────────
file(GLOB_RECURSE WINBOT_SOURCES
    "src/*.cpp"
)

add_executable(WinBot ${WINBOT_SOURCES})

target_include_directories(WinBot PRIVATE
    src
    third_party
    ${stb_SOURCE_DIR}
    ${sqlite_amalgamation_SOURCE_DIR}
    ${luabridge_SOURCE_DIR}/Source
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(WinBot PRIVATE
    nlohmann_json::nlohmann_json
    sqlite3
    lua
    # Windows SDK
    UIAutomationCore
    ole32
    oleaut32
    gdi32
    user32
    winhttp
    winmm
    shlwapi
    dwmapi
)

# SAPI (Windows Speech API) — Voice I/O
target_link_libraries(WinBot PRIVATE sapi)

# ── Per-target compile options ────────────────────────────────────────────────
target_compile_options(WinBot PRIVATE
    /std:c++latest
    /EHsc
    /W4
    /MP
    /utf-8
    /permissive-
    /Zc:__cplusplus
)
target_compile_features(WinBot PRIVATE cxx_std_23)

# ── Output directory ──────────────────────────────────────────────────────────
set_target_properties(WinBot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

message(STATUS "WinBot UI Tool Server configured: C++23/latest, MSVC ${MSVC_VERSION}")
message(STATUS "  Dependencies: nlohmann/json, SQLite3, Windows SDK (no llama.cpp)")
