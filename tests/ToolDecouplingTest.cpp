#include <gtest/gtest.h>
#include "Common.h"
#include "ITool.h"
#include "ToolRegistry.h"
#include "ToolServer.h"
#include "McpServer.h"
#include "PermissionSystem.h"
#include "tools/UIAutomationScanner.h"
#include "tools/BrowserAutomation.h"
#include "SiteProfileRegistry.h"
#include "LuaRuntime.h"
#include "tools/BuiltinTools.h"
#include <filesystem>

// ── Test custom tool inheriting from ITool ───────────────────────────────────
class CustomEchoTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "custom_echo"; }
    [[nodiscard]] std::string description() const override { return "Echoes back the message"; }
    [[nodiscard]] json parametersSchema() const override {
        return {
            {"type", "object"},
            {"properties", {{"msg", {{"type", "string"}}}}},
            {"required", {"msg"}}
        };
    }
    [[nodiscard]] ToolResult execute(const json& args) override {
        std::string msg = args.value("msg", "");
        return ok("echo: " + msg);
    }
};

TEST(ToolDecouplingTest, CustomToolInheritingFromITool) {
    ToolRegistry registry;
    registry.registerTool(std::make_unique<CustomEchoTool>());

    EXPECT_TRUE(registry.hasTool("custom_echo"));

    json call = {
        {"tool", "custom_echo"},
        {"args", {{"msg", "hello world"}}}
    };
    auto result = registry.dispatch(call);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "echo: hello world");
}

TEST(ToolDecouplingTest, LambdaToolInheritingFromITool) {
    ToolRegistry registry;
    registry.registerTool(std::make_unique<LambdaTool>(
        "test_add",
        "Adds two numbers",
        json{{"type", "object"}},
        [](const json& a) -> ToolResult {
            int x = a.value("x", 0);
            int y = a.value("y", 0);
            return ok(std::to_string(x + y));
        }
    ));

    EXPECT_TRUE(registry.hasTool("test_add"));
    auto result = registry.dispatch(json{{"tool", "test_add"}, {"args", {{"x", 10}, {"y", 32}}}});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "42");
}

TEST(ToolDecouplingTest, BuiltinToolsRegisterAllPopulatesRegistry) {
    ToolRegistry registry;
    PermissionSystem::Config permCfg;
    PermissionSystem perms{permCfg};

    UIAutomationScanner uia;
    BrowserAutomation browser{9222, ""};
    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "winbot_test_profiles";
    SiteProfileRegistry siteProfiles{tempDir};
    LuaRuntime luaRuntime{browser, uia, siteProfiles};

    BuiltinTools::registerAll(registry, {
        .uia          = uia,
        .browser      = browser,
        .perms        = perms,
        .siteProfiles = siteProfiles,
        .luaRuntime   = luaRuntime
    });

    // Check essential tools from each subsystem
    EXPECT_TRUE(registry.hasTool("list_tools"));
    EXPECT_TRUE(registry.hasTool("ui_scan"));
    EXPECT_TRUE(registry.hasTool("ui_scan_window"));
    EXPECT_TRUE(registry.hasTool("screenshot"));
    EXPECT_TRUE(registry.hasTool("screenshot_window"));
    EXPECT_TRUE(registry.hasTool("screenshot_element"));
    EXPECT_TRUE(registry.hasTool("click"));
    EXPECT_TRUE(registry.hasTool("run_command"));
    EXPECT_TRUE(registry.hasTool("read_file"));
    EXPECT_TRUE(registry.hasTool("browser_navigate"));
    EXPECT_TRUE(registry.hasTool("browser_eval"));
    EXPECT_TRUE(registry.hasTool("lua_exec"));

    // Verify list_tools schema returns all tools
    auto listResult = registry.dispatch(json{{"tool", "list_tools"}, {"args", json::object()}});
    ASSERT_TRUE(listResult.has_value());
    json parsedList = json::parse(*listResult);
    EXPECT_TRUE(parsedList.is_array());
    EXPECT_GT(parsedList.size(), 20u);

    std::filesystem::remove_all(tempDir);
}

TEST(ToolDecouplingTest, BuiltinToolsHonorsDisabledTools) {
    ToolRegistry registry;
    PermissionSystem::Config permCfg;
    permCfg.disabledTools.insert("browser_navigate");
    permCfg.disabledTools.insert("run_command");
    PermissionSystem perms{permCfg};

    UIAutomationScanner uia;
    BrowserAutomation browser{9222, ""};
    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "winbot_test_profiles_dis";
    SiteProfileRegistry siteProfiles{tempDir};
    LuaRuntime luaRuntime{browser, uia, siteProfiles};

    BuiltinTools::registerAll(registry, {
        .uia          = uia,
        .browser      = browser,
        .perms        = perms,
        .siteProfiles = siteProfiles,
        .luaRuntime   = luaRuntime
    });

    EXPECT_FALSE(registry.hasTool("browser_navigate"));
    EXPECT_FALSE(registry.hasTool("run_command"));
    // Other tools should still be enabled
    EXPECT_TRUE(registry.hasTool("screenshot"));
    EXPECT_TRUE(registry.hasTool("click"));

    std::filesystem::remove_all(tempDir);
}

TEST(ToolDecouplingTest, ToolServerRequiresZeroSubsystemDependencies) {
    // ToolServer can be constructed with just Config, ToolRegistry, and AuditLog
    ToolRegistry registry;
    registry.registerTool(std::make_unique<CustomEchoTool>());

    std::filesystem::path logPath = std::filesystem::temp_directory_path() / "test_audit.log";
    AuditLog audit{logPath};

    ToolServer server(ToolServer::Config{.actionDelayMs = 0}, registry, audit);
    // ToolServer successfully instantiated without UIAutomationScanner, ScreenCapture, or BrowserAutomation!
    EXPECT_TRUE(registry.hasTool("custom_echo"));

    std::filesystem::remove(logPath);
}

TEST(ToolDecouplingTest, ReadFileToolBlocksProtectedPaths) {
    PermissionSystem::Config permCfg;
    permCfg.blockedPaths = {"C:\\Windows\\System32"};
    PermissionSystem perms{permCfg};

    ReadFileTool tool{perms};
    EXPECT_EQ(tool.name(), "read_file");

    auto result = tool.execute(json{{"path", "C:\\Windows\\System32\\drivers\\etc\\hosts"}});
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().contains("Access denied"));
}

TEST(ToolDecouplingTest, KillProcessToolBlocksProtectedProcesses) {
    PermissionSystem::Config permCfg;
    permCfg.blockedProcesses = {"csrss.exe", "winlogon.exe"};
    permCfg.confirmProcessKill = false;
    PermissionSystem perms{permCfg};

    KillProcessTool tool{perms};
    EXPECT_EQ(tool.name(), "kill_process");

    auto result = tool.execute(json{{"name_or_pid", "csrss.exe"}});
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().contains("Cannot kill protected process"));
}
