cmake_minimum_required(VERSION 3.25)
project(WinBot VERSION 0.2.0 LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Force standard levels globally so CMake's try_compile tests use appropriate flags
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Global Compiler & Static Analysis Configuration ──────────────────────────
option(WINBOT_ENABLE_WERROR "Treat warnings as errors" ON)
option(WINBOT_ENABLE_STATIC_ANALYSIS "Enable compiler static code analysis" ON)
option(WINBOT_ENABLE_CLANG_TIDY "Run clang-tidy during compilation and treat warnings as errors" ON)

if(WINBOT_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(WINBOT_CLANG_TIDY_COMMAND "${CLANG_TIDY_EXE};--warnings-as-errors=*;--extra-arg=/EHsc")
        message(STATUS "clang-tidy enabled with warnings-as-errors: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "WINBOT_ENABLE_CLANG_TIDY is ON, but clang-tidy executable was not found")
    endif()
endif()

add_library(winbot_strict_warnings INTERFACE)

if(MSVC)
    target_compile_definitions(winbot_strict_warnings INTERFACE
        UNICODE
        _UNICODE
        NOMINMAX
        _WIN32_WINNT=0x0A00 # Target Windows 10/11 APIs
    )
    
    if(WINBOT_ENABLE_STATIC_ANALYSIS)
        set(DEFAULT_RULESET_PATH "$ENV{VSINSTALLDIR}/Team Tools/Static Analysis Tools/Rule Sets/AllRules.ruleset")
        if(NOT EXISTS "${DEFAULT_RULESET_PATH}")
            set(DEFAULT_RULESET_PATH "C:/Program Files/Microsoft Visual Studio/18/Community/Team Tools/Static Analysis Tools/Rule Sets/AllRules.ruleset")
        endif()
        set(WINBOT_MSVC_RULESET "${DEFAULT_RULESET_PATH}" CACHE FILEPATH "Path to MSVC Code Analysis RuleSet")
    endif()

    # Global MSVC strict flags and standard conformance
    target_compile_options(winbot_strict_warnings INTERFACE
        /MP
        /utf-8
        /permissive-
        /Zc:__cplusplus
        /Zc:strictStrings
        /Zc:throwingNew
        /Zc:inline
        /Zc:enumTypes
        /Zc:templateScope
        /EHsc
        /W4
        /sdl # Microsoft Security Development Lifecycle (SDL) checks (elevates security warnings to errors)
        /Zc:preprocessor # Standard ISO C/C++ conforming preprocessor
        /Zc:lambda # Standard-conforming lambda processor
        # Suppress warnings originating from external/Windows SDK headers
        /external:anglebrackets
        /external:W0
        # Elevated strict warnings (Microsoft recommended off-by-default warnings)
        /w14242 # conversion from 'type1' to 'type2', possible loss of data
        /w14254 # conversion from 'type1' to 'type2', possible loss of data
        /w14263 # member function does not override any base class virtual member function
        /w14265 # class has virtual functions, but destructor is not virtual
        /w14287 # unsigned/negative constant mismatch
        /w14296 # expression is always true/false
        /w14311 # pointer truncation from 'type1' to 'type2'
        /w14545 # expression before comma evaluates to a function which is missing an argument list
        /w14546 # function call before comma missing argument list
        /w14547 # operator before comma has no effect
        /w14549 # operator before comma has no effect
        /w14555 # expression has no effect
        /w14571 # catch(...) semantics changed since Visual C++ 7.1; SEH not caught
        /w14619 # pragma warning: there is no warning number
        /w14640 # thread-unsafe static member initialization
        /w14774 # format string expected in argument is not a string literal
        /w14777 # format specifier type mismatch
        /w14826 # conversion from 'type1' to 'type2' is sign-extended
        /w14905 # wide string literal cast to LPSTR
        /w14906 # string literal cast to LPWSTR
        /w14928 # illegal copy-initialization
        /w15038 # data member initialization order mismatch
        /w15042 # function declaration at block scope cannot have storage class or type specifier
        /w15219 # implicit conversion from integral type to floating-point type
        /w15246 # initialization of a subobject should be wrapped in braces
        /w15262 # implicit fall-through in switch statement
        /w15264 # 'const' variable is not used
        # Critical structural bugs promoted directly to errors
        /we4715 # not all control paths return a value
        /we4150 # deletion of pointer to incomplete type
        /we4456 # declaration hides previous local declaration
        /we4457 # declaration hides function parameter
        /we4458 # declaration hides class member
        $<$<BOOL:${WINBOT_ENABLE_WERROR}>:/WX>
        $<$<BOOL:${WINBOT_ENABLE_STATIC_ANALYSIS}>:/analyze>
        $<$<BOOL:${WINBOT_ENABLE_STATIC_ANALYSIS}>:/analyze:autolog->
        $<$<BOOL:${WINBOT_ENABLE_STATIC_ANALYSIS}>:/analyze:external->
        $<$<BOOL:${WINBOT_ENABLE_STATIC_ANALYSIS}>:/analyze:ruleset${WINBOT_MSVC_RULESET}>
        /wd6326 # Potential comparison of a constant with another constant (triggers on GTest EXPECT/ASSERT macro expansions)
    )

elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(winbot_strict_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        $<$<BOOL:${WINBOT_ENABLE_WERROR}>:-Werror>
    )
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

# ── Source groupings ──────────────────────────────────────────────────────────
set(CORE_SOURCES
    src/core/ToolRegistry.cpp
    src/core/ToolServer.cpp
    src/core/McpServer.cpp
)

set(SECURITY_SOURCES
    src/security/AuditLog.cpp
    src/security/KillSwitch.cpp
    src/security/PermissionSystem.cpp
)

set(PLATFORM_SOURCES
    src/platform/uia/UIAutomationScanner.cpp
    src/platform/uia/UIHandle.cpp
    src/platform/uia/UIADebugger.cpp
    src/platform/screen/ScreenCapture.cpp
    src/platform/browser/BrowserAutomation.cpp
    src/platform/win/WindowUtils.cpp
)

set(SERVICES_SOURCES
    src/services/LuaRuntime.cpp
    src/services/LuaToolLoader.cpp
    src/services/Scheduler.cpp
    src/services/SiteProfileRegistry.cpp
)

set(TOOL_SOURCES
    src/tools/BrowserTools.cpp
    src/tools/BuiltinTools.cpp
    src/tools/FileTools.cpp
    src/tools/InputTools.cpp
    src/tools/LuaScriptTool.cpp
    src/tools/ScreenTools.cpp
    src/tools/ShellTools.cpp
    src/tools/WindowTools.cpp
)

add_library(WinBot_Core STATIC
    ${CORE_SOURCES}
    ${SECURITY_SOURCES}
    ${PLATFORM_SOURCES}
    ${SERVICES_SOURCES}
    ${TOOL_SOURCES}
)

target_include_directories(WinBot_Core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_include_directories(WinBot_Core SYSTEM PUBLIC
    third_party
    ${stb_SOURCE_DIR}
    ${luabridge_SOURCE_DIR}/Source
)

target_link_libraries(WinBot_Core PUBLIC
    winbot_strict_warnings
    nlohmann_json::nlohmann_json
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

if(WINBOT_CLANG_TIDY_COMMAND)
    set_target_properties(WinBot_Core PROPERTIES CXX_CLANG_TIDY "${WINBOT_CLANG_TIDY_COMMAND}")
endif()

# ── WinBot Executable ─────────────────────────────────────────────────────────
add_executable(WinBot src/main.cpp)
target_link_libraries(WinBot PRIVATE WinBot_Core)

if(MSVC)
    target_link_options(WinBot PRIVATE
        "/WHOLEARCHIVE:WinBot_Core"
    )
endif()

set_target_properties(WinBot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

if(WINBOT_CLANG_TIDY_COMMAND)
    set_target_properties(WinBot PROPERTIES CXX_CLANG_TIDY "${WINBOT_CLANG_TIDY_COMMAND}")
endif()

# Deploy data directory (tools, site profiles) alongside the binary
add_custom_command(TARGET WinBot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/data"
        "$<TARGET_FILE_DIR:WinBot>/data"
)

if(WIN32)
    add_custom_command(TARGET WinBot PRE_LINK
        COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/kill_winbot.cmake"
        COMMENT "Terminating any running WinBot processes..."
    )
endif()

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
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_link_libraries(WinBot_Tests PRIVATE
        WinBot_Core
        GTest::gtest
        GTest::gtest_main
    )

    target_compile_definitions(WinBot_Tests PRIVATE
        WINBOT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )

    if(MSVC)
        target_compile_options(WinBot_Tests PRIVATE
            /W4
            /std:c++latest
        )
        target_link_options(WinBot_Tests PRIVATE
            "/WHOLEARCHIVE:WinBot_Core"
        )
    endif()

    set_target_properties(WinBot_Tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    )

    if(WINBOT_CLANG_TIDY_COMMAND)
        set_target_properties(WinBot_Tests PROPERTIES CXX_CLANG_TIDY "${WINBOT_CLANG_TIDY_COMMAND}")
    endif()

    add_custom_command(TARGET WinBot_Tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data"
            "$<TARGET_FILE_DIR:WinBot_Tests>/data"
    )

    include(GoogleTest)
    gtest_discover_tests(WinBot_Tests
        DISCOVERY_TIMEOUT 60
        PROPERTIES ENVIRONMENT "WINBOT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

message(STATUS "WinBot UI Tool Server configured: C++23/latest, MSVC ${MSVC_VERSION}")
