#include <gtest/gtest.h>
#include "services/LuaRuntime.h"
#include "platform/uia/UIAutomationScanner.h"
#include "platform/browser/BrowserAutomation.h"
#include "services/SiteProfileRegistry.h"
#include <filesystem>

namespace {

class LuaRuntimeTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_tempDir = std::filesystem::temp_directory_path() / "winbot_lua_test";
        std::filesystem::create_directories(m_tempDir);
        m_profiles = std::make_unique<SiteProfileRegistry>(m_tempDir);
        m_uia = std::make_unique<UIAutomationScanner>();
        m_browser = std::make_unique<BrowserAutomation>(9222, "");
        m_lua = std::make_unique<LuaRuntime>(*m_browser, *m_uia, *m_profiles);
    }

    void TearDown() override {
        m_lua.reset();
        m_browser.reset();
        m_uia.reset();
        m_profiles.reset();
        std::error_code ec;
        std::filesystem::remove_all(m_tempDir, ec);
    }

    std::filesystem::path m_tempDir;
    std::unique_ptr<SiteProfileRegistry> m_profiles;
    std::unique_ptr<UIAutomationScanner> m_uia;
    std::unique_ptr<BrowserAutomation> m_browser;
    std::unique_ptr<LuaRuntime> m_lua;
};

TEST_F(LuaRuntimeTest, ExecStringValidScriptReturnsResult) {
    auto result = m_lua->execString("return 'hello from lua'");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "hello from lua");
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

TEST_F(LuaRuntimeTest, ExecFileNonExistent) {
    auto result = m_lua->execFile("C:/NonExistentPath/ghost_script.lua");
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().find("Script file not found") != std::string::npos);
}

TEST_F(LuaRuntimeTest, WinBotNamespaceFunctions) {
    auto result = m_lua->execString("winbot.sleep(5); winbot.log('test log from unit test'); return 'done'");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "done");
}

} // namespace
