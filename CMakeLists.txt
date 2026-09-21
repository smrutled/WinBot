cmake_minimum_required(VERSION 3.25)
project(WinBot VERSION 0.2.0 LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Force standard levels globally so CMake's try_compile tests use appropriate flags
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Global MSVC Configuration ────────────────────────────────────────────────
if(MSVC)
    add_compile_definitions(
        UNICODE
        _UNICODE
        NOMINMAX
        _WIN32_WINNT=0x0A00 # Target Windows 10/11 APIs
    )
    
    # Enable multi-processor compilation globally for all sub-targets
    add_compile_options(/MP /utf-8 /permissive- /Zc:__cplusplus /EHsc)
endif()

# ── FetchContent ──────────────────────────────────────────────────────────────
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# nlohmann/json — config.json + JSON tool call parsing + CDP messaging
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
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
FetchContent_MakeAvailable(stb)

# SQLite3 — persistent memory store (amalgamation, no extra deps)
FetchContent_Declare(
    sqlite_amalgamation
    URL            https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
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

# Lua 5.5.1 — Embedded scripting engine
FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        v5.5.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(lua)
file(GLOB LUA_SOURCES "${lua_SOURCE_DIR}/*.c")
list(REMOVE_ITEM LUA_SOURCES "${lua_SOURCE_DIR}/luac.c" "${lua_SOURCE_DIR}/lua.c" "${lua_SOURCE_DIR}/onelua.c" "${lua_SOURCE_DIR}/ltests.c")
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC "${lua_SOURCE_DIR}")

# LuaBridge3 — C++ bindings for Lua
FetchContent_Declare(
    luabridge
    GIT_REPOSITORY https://github.com/kunitoki/LuaBridge3.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(luabridge)

# ── Source files & WinBot_Core ────────────────────────────────────────────────
file(GLOB_RECURSE WINBOT_CORE_SOURCES
    "src/*.cpp"
)
list(FILTER WINBOT_CORE_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

add_library(WinBot_Core STATIC ${WINBOT_CORE_SOURCES})

target_include_directories(WinBot_Core PUBLIC
    src
    third_party
    ${stb_SOURCE_DIR}
    ${sqlite_amalgamation_SOURCE_DIR}
    ${luabridge_SOURCE_DIR}/Source
)

target_link_libraries(WinBot_Core PUBLIC
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

if(MSVC)
    target_compile_options(WinBot_Core PUBLIC
        /W4
        /std:c++latest
    )
endif()

# ── WinBot Executable ─────────────────────────────────────────────────────────
add_executable(WinBot src/main.cpp)
target_link_libraries(WinBot PRIVATE WinBot_Core)

if(MSVC)
    target_compile_options(WinBot PRIVATE
        /W4
        /std:c++latest
    )
endif()

# ── Output directory ──────────────────────────────────────────────────────────
set_target_properties(WinBot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

# ── Tests & Benchmarks ────────────────────────────────────────────────────────
option(BUILD_TESTING "Build the testing tree." ON)
if(BUILD_TESTING)
    enable_testing()

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    file(GLOB_RECURSE TEST_SOURCES
        "tests/*.cpp"
        "tests/*.h"
    )

    add_executable(WinBot_Tests ${TEST_SOURCES})
    target_include_directories(WinBot_Tests PRIVATE
        src
        tests
    )
    target_link_libraries(WinBot_Tests PRIVATE
        WinBot_Core
        GTest::gtest
        GTest::gtest_main
    )

    if(MSVC)
        target_compile_options(WinBot_Tests PRIVATE
            /W4
            /std:c++latest
        )
    endif()

    set_target_properties(WinBot_Tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    )

    include(GoogleTest)
    gtest_discover_tests(WinBot_Tests
        DISCOVERY_TIMEOUT 60
    )
endif()

message(STATUS "WinBot UI Tool Server configured: C++23/latest, MSVC ${MSVC_VERSION}")
