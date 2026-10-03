#include <gtest/gtest.h>
#include "services/LuaRuntime.h"
#include "platform/uia/UIAutomationScanner.h"
#include "platform/uia/UIHandle.h"
#include "services/SiteProfileRegistry.h"
#include "security/PermissionSystem.h"
#include "TestHelpers.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

namespace {

class LuaRuntimeTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_tempDir = std::filesystem::temp_directory_path() / "winbot_lua_test";
        std::filesystem::create_directories(m_tempDir);
        m_profiles = std::make_unique<SiteProfileRegistry>(m_tempDir);
        m_uia = std::make_unique<UIAutomationScanner>();

        PermissionSystem::Config permCfg;
        permCfg.allowedPaths = { m_tempDir.string() };
        permCfg.dangerousCmdPatterns = { "format", "rmdir /s", "del /f" };
        permCfg.confirmShellCommands = false;
        m_perms = std::make_unique<PermissionSystem>(permCfg);

        // Fast offline constructor (null browser prevents 4-second WinHTTP connection timeouts)
        m_lua = std::make_unique<LuaRuntime>(m_uia.get(), nullptr, m_profiles.get(), m_perms.get());

        m_recordedClicks.clear();
        m_recordedTypes.clear();
        m_recordedKeys.clear();

        UIHandle::setInputHandlers(
            [this](int x, int y, std::string_view btn, bool /*human*/) -> ToolResult {
                m_recordedClicks.push_back(std::format("{}:{},{}", btn, x, y));
                return ok("mock click");
            },
            [this](std::string_view text) -> ToolResult {
                m_recordedTypes.push_back(std::string(text));
                return ok("mock type");
            },
            [this](std::string_view combo) -> ToolResult {
                m_recordedKeys.push_back(std::string(combo));
                return ok("mock key");
            }
        );
        m_cinStream.str("no\nno\nno\nno\nno\nno\nno\nno\n");
        m_cinStream.clear();
        m_oldCin = std::cin.rdbuf(m_cinStream.rdbuf());
    }

    void TearDown() override {
        if (m_oldCin) std::cin.rdbuf(m_oldCin);
        UIHandle::setInputHandlers(nullptr, nullptr, nullptr);
        m_lua.reset();
        m_uia.reset();
        m_profiles.reset();
        m_perms.reset();
        std::error_code ec;
        std::filesystem::remove_all(m_tempDir, ec);
    }

    std::filesystem::path m_tempDir;
    std::unique_ptr<SiteProfileRegistry> m_profiles;
    std::unique_ptr<UIAutomationScanner> m_uia;
    std::unique_ptr<PermissionSystem> m_perms;
    std::unique_ptr<LuaRuntime> m_lua;

    std::istringstream m_cinStream;
    std::streambuf* m_oldCin{nullptr};

    std::vector<std::string> m_recordedClicks;
    std::vector<std::string> m_recordedTypes;
    std::vector<std::string> m_recordedKeys;
};

// ── Basic Script Execution & Return Value Formats ────────────────────────────

TEST_F(LuaRuntimeTest, ExecStringReturnsVariousTypes) {
    // String
    auto resStr = m_lua->execString("return 'hello from lua'");
    ASSERT_TRUE(resStr.has_value());
    EXPECT_EQ(*resStr, "hello from lua");

    // Integer
    auto resInt = m_lua->execString("return 42");
    ASSERT_TRUE(resInt.has_value());
    EXPECT_EQ(*resInt, "42");

    // Arithmetic
    auto resMath = m_lua->execString("return 10 + 20 * 2");
    ASSERT_TRUE(resMath.has_value());
    EXPECT_EQ(*resMath, "50");

    // Boolean
    auto resBoolTrue = m_lua->execString("return true");
    ASSERT_TRUE(resBoolTrue.has_value());
    EXPECT_EQ(*resBoolTrue, "true");

    auto resBoolFalse = m_lua->execString("return false");
    ASSERT_TRUE(resBoolFalse.has_value());
    EXPECT_EQ(*resBoolFalse, "false");

    // Nil
    auto resNil = m_lua->execString("return nil");
    ASSERT_TRUE(resNil.has_value());
    EXPECT_EQ(*resNil, "nil");

    // Void / statement only
    auto resVoid = m_lua->execString("local a = 100; local b = 200");
    ASSERT_TRUE(resVoid.has_value());
    EXPECT_EQ(*resVoid, "Script executed successfully.");
}

TEST_F(LuaRuntimeTest, ExecStringHandlesSyntaxErrorGracefully) {
    auto result = m_lua->execString("function incomplete_syntax( }}}");
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().empty());
}

TEST_F(LuaRuntimeTest, ExecStringHandlesRuntimeErrorGracefully) {
    auto result = m_lua->execString("error('Deliberate Lua runtime error')");
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().find("Deliberate Lua runtime error") != std::string::npos);
}

// ── Sandboxing & Security ───────────────────────────────────────────────────

TEST_F(LuaRuntimeTest, SandboxedStandardLibraries_DangerousAPIsDisabled) {
    const char* script = R"(
        assert(debug == nil, "debug must be nil")
        assert(io == nil, "io must be nil")
        assert(package.loadlib == nil, "package.loadlib must be nil")
        assert(package.cpath == nil, "package.cpath must be nil")
        assert(os.execute == nil, "os.execute must be nil")
        assert(os.exit == nil, "os.exit must be nil")
        assert(os.remove == nil, "os.remove must be nil")
        assert(os.rename == nil, "os.rename must be nil")
        assert(dofile == nil, "dofile must be nil")
        assert(loadfile == nil, "loadfile must be nil")

        -- Verify safe standard libraries remain functional
        assert(type(os.clock()) == "number", "os.clock should work")
        assert(type(os.time()) == "number", "os.time should work")
        assert(string.upper("safe") == "SAFE", "string functions should work")
        assert(math.sqrt(49) == 7, "math functions should work")
        assert(table.concat({"win", "bot"}, "") == "winbot", "table functions should work")
        return "sandbox_verified"
    )";
    auto result = m_lua->execString(script);
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(*result, "sandbox_verified");
}

TEST_F(LuaRuntimeTest, InfiniteLoopTerminatesViaTimeout) {
    m_lua->setTimeout(std::chrono::milliseconds(50));
    m_lua->setMaxInstructions(100'000);

    auto result = m_lua->execString("while true do end");
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().find("Script execution aborted") != std::string::npos);

    // Reset limits and verify runtime still functions normally
    m_lua->setTimeout(std::chrono::milliseconds(10000));
    m_lua->setMaxInstructions(5'000'000);
    auto recoverRes = m_lua->execString("return 'recovered'");
    ASSERT_TRUE(recoverRes.has_value());
    EXPECT_EQ(*recoverRes, "recovered");
}

TEST_F(LuaRuntimeTest, PermissionSystemEnforcedOnShell) {
    // 1. Safe command within permissions
    auto safeRes = m_lua->execString("return winbot.shell('echo safe_shell_output')");
    ASSERT_TRUE(safeRes.has_value()) << safeRes.error();
    EXPECT_TRUE(safeRes->find("safe_shell_output") != std::string::npos);

    // 2. Dangerous command blocked by PermissionSystem
    auto blockedRes = m_lua->execString("return winbot.shell('format D: /q')");
    EXPECT_FALSE(blockedRes.has_value());
    EXPECT_TRUE(blockedRes.error().find("Permission denied") != std::string::npos);
}

TEST_F(LuaRuntimeTest, FileReadWriteWithPermissions) {
    auto testFilePath = (m_tempDir / "script_test.txt").string();
    std::string escapedPath;
    for (char c : testFilePath) {
        if (c == '\\') escapedPath += "/";
        else escapedPath += c;
    }

    std::string script = std::format(
        "winbot.writeFile('{}', 'hello from lua file I/O')\n"
        "return winbot.readFile('{}')",
        escapedPath, escapedPath
    );

    auto result = m_lua->execString(script);
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(*result, "hello from lua file I/O");

    // Reading outside allowed path must fail
    auto blockedRes = m_lua->execString("return winbot.readFile('C:/Windows/System32/drivers/etc/hosts')");
    EXPECT_FALSE(blockedRes.has_value());
    EXPECT_TRUE(blockedRes.error().find("Permission denied") != std::string::npos);
}

// ── File Execution ──────────────────────────────────────────────────────────

TEST_F(LuaRuntimeTest, ExecFileValidAndBlocked) {
    // 1. Valid script file in allowed directory
    auto validPath = m_tempDir / "script.lua";
    {
        std::ofstream ofs(validPath);
        ofs << "local x = 50; local y = 70; return x + y";
    }

    auto validRes = m_lua->execFile(validPath);
    ASSERT_TRUE(validRes.has_value()) << validRes.error();
    EXPECT_EQ(*validRes, "120");

    // 2. Non-existent file
    auto nonExistent = m_tempDir / "does_not_exist.lua";
    auto nonExistentRes = m_lua->execFile(nonExistent);
    EXPECT_FALSE(nonExistentRes.has_value());
    EXPECT_TRUE(nonExistentRes.error().find("Script file not found") != std::string::npos);

    // 3. Blocked path outside allowed directories
    auto blockedRes = m_lua->execFile("C:/Windows/System32/some_script.lua");
    EXPECT_FALSE(blockedRes.has_value());
    EXPECT_TRUE(blockedRes.error().find("Permission denied") != std::string::npos);
}

// ── UI Automation & Method Chaining ─────────────────────────────────────────

TEST_F(LuaRuntimeTest, UIHandleMethodChaining) {
    UIElement btn = createMockElement("SubmitBtn", "Button", "btnSubmit", {10, 10, 50, 30});
    UIHandle handle(btn, m_uia.get());
    m_lua->setGlobalHandle("btn", handle);

    const char* script = R"(
        btn:click():wait(10):type("sample text"):key("Enter")
        return btn:name()
    )";

    auto result = m_lua->execString(script);
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(*result, "SubmitBtn");

    EXPECT_FALSE(m_recordedClicks.empty());
    EXPECT_EQ(m_recordedTypes.size(), 1);
    EXPECT_EQ(m_recordedTypes[0], "sample text");
    EXPECT_EQ(m_recordedKeys.size(), 1);
    EXPECT_EQ(m_recordedKeys[0], "Enter");
}

// ── The Calculator Test ─────────────────────────────────────────────────────

TEST_F(LuaRuntimeTest, CalculatorScript_ButtonAutomation) {
    UIElement calcTree = createCalculatorTree();
    UIHandle calcHandle(calcTree, m_uia.get());
    m_lua->setGlobalHandle("calc", calcHandle);

    // Script selects calculator buttons, executes clicks, and reads display
    const char* script = R"(
        calc:select("One"):click()
        calc:select("Plus"):click()
        calc:select("Two"):click()
        calc:select("Equals"):click()

        local display = calc:select("CalculatorResults")
        return display:name()
    )";

    auto result = m_lua->execString(script);
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(*result, "0"); // Initial mock display value

    // Verify all 4 clicks were invoked
    EXPECT_EQ(m_recordedClicks.size(), 4);
}

TEST_F(LuaRuntimeTest, CalculatorScript_EvaluationSimulation) {
    UIElement calcTree = createCalculatorTree();
    UIHandle calcHandle(calcTree, m_uia.get());
    m_lua->setGlobalHandle("calc", calcHandle);

    // Advanced script simulating an interactive calculation test in Lua
    const char* script = R"(
        local function press(btnName)
            local btn = calc:select(btnName)
            btn:click()
            return btn:name()
        end

        local valA = press("One")
        local op   = press("Plus")
        local valB = press("Two")
        press("Equals")

        local numberMap = { One = 1, Two = 2, Three = 3 }
        local numA = numberMap[valA]
        local numB = numberMap[valB]

        if op == "Plus" then
            return tostring(numA + numB)
        else
            return "error"
        end
    )";

    auto result = m_lua->execString(script);
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(*result, "3");
    EXPECT_EQ(m_recordedClicks.size(), 4);
}

TEST_F(LuaRuntimeTest, CalculatorScript_SafeFindMissingButton) {
    UIElement calcTree = createCalculatorTree();
    UIHandle calcHandle(calcTree, m_uia.get());
    m_lua->setGlobalHandle("calc", calcHandle);

    const char* script = R"(
        -- 'find' should return nil for non-existent buttons without throwing
        local sqrtBtn = calc:find("SquareRoot")
        if sqrtBtn == nil then
            return "not_found"
        else
            return "found"
        end
    )";

    auto result = m_lua->execString(script);
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(*result, "not_found");
}

TEST_F(LuaRuntimeTest, CalculatorScript_AccessElementProperties) {
    UIElement calcTree = createCalculatorTree();
    UIHandle calcHandle(calcTree, m_uia.get());
    m_lua->setGlobalHandle("calc", calcHandle);

    const char* script = R"(
        local plus = calc:select("Plus")
        return plus:automationId() .. "|" .. plus:controlType() .. "|" .. tostring(plus:isEnabled())
    )";

    auto result = m_lua->execString(script);
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(*result, "plusButton|Button|true");
}

} // namespace
