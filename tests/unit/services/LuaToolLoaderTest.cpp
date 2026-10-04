#include "services/LuaToolLoader.h"
#include "TestHelpers.h"
#include "core/ToolRegistry.h"
#include "platform/uia/UIAutomationScanner.h"
#include "security/PermissionSystem.h"
#include "services/LuaRuntime.h"
#include "services/SiteProfileRegistry.h"
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

class LuaToolLoaderTest : public ::testing::Test {
protected:
  void SetUp() override {
    m_tempDir =
        std::filesystem::temp_directory_path() /
        ("winbot_loader_test_" +
         std::to_string(
             std::chrono::system_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(m_tempDir);

    m_profiles = std::make_unique<SiteProfileRegistry>(m_tempDir);
    m_uia = std::make_unique<UIAutomationScanner>();

    PermissionSystem::Config permCfg;
    permCfg.allowedPaths = {m_tempDir.string()};
    permCfg.confirmShellCommands = false;
    m_perms = std::make_unique<PermissionSystem>(permCfg);

    m_lua = std::make_unique<LuaRuntime>(m_uia.get(), nullptr, m_profiles.get(),
                                         m_perms.get());
    m_registry = std::make_unique<ToolRegistry>();
    m_loader = std::make_unique<LuaToolLoader>(m_tempDir, *m_lua, *m_registry);
  }

  void TearDown() override {
    m_loader.reset();
    m_registry.reset();
    m_lua.reset();
    m_perms.reset();
    m_uia.reset();
    m_profiles.reset();

    std::error_code ec;
    std::filesystem::remove_all(m_tempDir, ec);
  }

  std::filesystem::path m_tempDir; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
  std::unique_ptr<SiteProfileRegistry> m_profiles; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
  std::unique_ptr<UIAutomationScanner> m_uia; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
  std::unique_ptr<PermissionSystem> m_perms; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
  std::unique_ptr<LuaRuntime> m_lua; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
  std::unique_ptr<ToolRegistry> m_registry; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
  std::unique_ptr<LuaToolLoader> m_loader; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
};

// ── Test 1: Load Single Tool Script ──────────────────────────────────────────
TEST_F(LuaToolLoaderTest, LoadSingleToolScript) {
  auto scriptPath = m_tempDir / "greet.lua";
  {
    std::ofstream ofs(scriptPath);
    ofs << R"(
            local tool = {
                name = "greet_user",
                description = "Greets a user by name",
                parameters = {
                    type = "object",
                    properties = {
                        name = { type = "string" }
                    },
                    required = {"name"}
                }
            }
            function tool.execute(args)
                return "Hello, " .. (args.name or "stranger") .. "!"
            end
            return tool
        )";
  }

  size_t count = m_loader->loadAll();
  EXPECT_EQ(count, 1);
  EXPECT_TRUE(m_registry->hasTool("greet_user"));

  auto res = m_registry->dispatch(
      {{"tool", "greet_user"}, {"args", {{"name", "World"}}}});

  ASSERT_TRUE(res.has_value()) << res.error();
  EXPECT_EQ(*res, "Hello, World!");
}

// ── Test 2: Load Multi-Tool Script in Single File
// ─────────────────────────────
TEST_F(LuaToolLoaderTest, LoadMultiToolScript) {
  auto scriptPath = m_tempDir / "calc_suite.lua";
  {
    std::ofstream ofs(scriptPath);
    ofs << R"(
            local tools = {}
            tools[1] = {
                name = "calc_add",
                description = "Adds two numbers",
                parameters = {
                    type = "object",
                    properties = {
                        a = { type = "number" },
                        b = { type = "number" }
                    },
                    required = {"a", "b"}
                },
                execute = function(args)
                    return tostring(args.a + args.b)
                end
            }
            tools[2] = {
                name = "calc_multiply",
                description = "Multiplies two numbers",
                parameters = {
                    type = "object",
                    properties = {
                        x = { type = "number" },
                        y = { type = "number" }
                    },
                    required = {"x", "y"}
                },
                execute = function(args)
                    return tostring(args.x * args.y)
                end
            }
            return tools
        )";
  }

  size_t count = m_loader->loadAll();
  EXPECT_EQ(count, 2);
  EXPECT_TRUE(m_registry->hasTool("calc_add"));
  EXPECT_TRUE(m_registry->hasTool("calc_multiply"));

  // Call tool 1
  auto addRes = m_registry->dispatch(
      {{"tool", "calc_add"}, {"args", {{"a", 15}, {"b", 25}}}});
  ASSERT_TRUE(addRes.has_value()) << addRes.error();
  EXPECT_EQ(*addRes, "40");

  // Call tool 2
  auto mulRes = m_registry->dispatch(
      {{"tool", "calc_multiply"}, {"args", {{"x", 6}, {"y", 7}}}});
  ASSERT_TRUE(mulRes.has_value()) << mulRes.error();
  EXPECT_EQ(*mulRes, "42");
}

// ── Test 3: Table Return Value Serialized to JSON
// ─────────────────────────────
TEST_F(LuaToolLoaderTest, TableReturnValueFormatsAsJson) {
  auto scriptPath = m_tempDir / "stats.lua";
  {
    std::ofstream ofs(scriptPath);
    ofs << R"(
            local tool = {
                name = "get_stats",
                description = "Returns statistics object",
                execute = function(args)
                    return {
                        status = "ok",
                        code = 200,
                        items = {"alpha", "beta"}
                    }
                end
            }
            return tool
        )";
  }

  m_loader->loadAll();
  EXPECT_TRUE(m_registry->hasTool("get_stats"));

  auto res =
      m_registry->dispatch({{"tool", "get_stats"}, {"args", json::object()}});
  ASSERT_TRUE(res.has_value()) << res.error();

  json parsed = json::parse(*res);
  EXPECT_EQ(parsed.at("status"), "ok");
  EXPECT_EQ(parsed.at("code"), 200);
  EXPECT_EQ(parsed.at("items").size(), 2);
  EXPECT_EQ(parsed.at("items").at(0), "alpha");
}

// ── Test 4: Dynamic Hot-Reload (Add, Modify, Remove)
// ──────────────────────────
TEST_F(LuaToolLoaderTest, HotReloadDynamicUpdates) {
  // 1. Initial tool
  auto pathA = m_tempDir / "tool_a.lua";
  {
    std::ofstream ofs(pathA);
    ofs << R"(
            return {
                name = "tool_a",
                description = "Version 1",
                execute = function(args) return "v1" end
            }
        )";
  }
  m_loader->loadAll();
  EXPECT_TRUE(m_registry->hasTool("tool_a"));
  auto res1 = m_registry->dispatch({{"tool", "tool_a"}});
  EXPECT_EQ(*res1, "v1");

  // 2. Add second tool without server recompile
  auto pathB = m_tempDir / "tool_b.lua";
  {
    std::ofstream ofs(pathB);
    ofs << R"(
            return {
                name = "tool_b",
                description = "New dynamic tool",
                execute = function(args) return "hello from tool_b" end
            }
        )";
  }

  EXPECT_TRUE(m_loader->hasChanges());
  size_t count = m_loader->reload();
  EXPECT_EQ(count, 2);
  EXPECT_TRUE(m_registry->hasTool("tool_b"));

  auto resB = m_registry->dispatch({{"tool", "tool_b"}});
  EXPECT_EQ(*resB, "hello from tool_b");

  // 3. Modify tool_a
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  {
    std::ofstream ofs(pathA);
    ofs << R"(
            return {
                name = "tool_a",
                description = "Version 2",
                execute = function(args) return "v2_updated" end
            }
        )";
  }
  m_loader->reload();
  auto res2 = m_registry->dispatch({{"tool", "tool_a"}});
  EXPECT_EQ(*res2, "v2_updated");

  // 4. Remove tool_b file
  std::filesystem::remove(pathB);
  m_loader->reload();
  EXPECT_FALSE(m_registry->hasTool("tool_b"));
  EXPECT_TRUE(m_registry->hasTool("tool_a"));
}

// ── Test 5: Broken Script Resilience
// ──────────────────────────────────────────
TEST_F(LuaToolLoaderTest, BrokenScriptIgnoredWithoutCrashing) {
  auto brokenPath = m_tempDir / "broken.lua";
  {
    std::ofstream ofs(brokenPath);
    ofs << "function incomplete_syntax( }}} invalid code !!!";
  }

  auto validPath = m_tempDir / "valid.lua";
  {
    std::ofstream ofs(validPath);
    ofs << R"(
            return {
                name = "valid_tool",
                execute = function(args) return "works" end
            }
        )";
  }

  size_t count = m_loader->loadAll();
  EXPECT_EQ(count, 1);
  EXPECT_TRUE(m_registry->hasTool("valid_tool"));
  EXPECT_FALSE(m_registry->hasTool("broken"));
}

// ── Test 6: Verify data/tools/calculator.lua Suite ───────────────────────────
TEST_F(LuaToolLoaderTest, CalculatorLuaSuiteLoadsAndRegisters) {
  auto sourceCalcPath = getProjectRoot() / "data/tools/calculator.lua";
  ASSERT_TRUE(std::filesystem::exists(sourceCalcPath))
      << "Could not find calculator.lua at: " << sourceCalcPath
      << " (set WINBOT_ROOT environment variable to override)";

  auto testCalcPath = m_tempDir / "calculator.lua";
  std::filesystem::copy_file(sourceCalcPath, testCalcPath);

  size_t count = m_loader->loadAll();
  EXPECT_EQ(count, 4);

  EXPECT_TRUE(m_registry->hasTool("calc_calculate"));
  EXPECT_TRUE(m_registry->hasTool("calc_press"));
  EXPECT_TRUE(m_registry->hasTool("calc_get_display"));
  EXPECT_TRUE(m_registry->hasTool("calc_clear"));

  json schema = m_registry->buildToolsSchema();
  bool foundCalc = false;
  for (const auto &t : schema) {
    if (t.value("name", "") == "calc_calculate") {
      foundCalc = true;
      EXPECT_TRUE(t.at("schema").at("properties").contains("expression"));
    }
  }
  EXPECT_TRUE(foundCalc);

  // Verify each tool in the suite is registered with proper schema metadata
  bool foundCalculate = false;
  bool foundPress = false;
  bool foundGetDisplay = false;
  bool foundClear = false;

  for (const auto &t : schema) {
    auto tName = t.value("name", "");
    if (tName == "calc_calculate") {
      foundCalculate = true;
      EXPECT_TRUE(t.at("schema").at("properties").contains("expression"));
      EXPECT_TRUE(t.at("schema").at("properties").contains("clear_first"));
    } else if (tName == "calc_press") {
      foundPress = true;
      EXPECT_TRUE(t.at("schema").at("properties").contains("button"));
    } else if (tName == "calc_get_display") {
      foundGetDisplay = true;
    } else if (tName == "calc_clear") {
      foundClear = true;
    }
  }
  EXPECT_TRUE(foundCalculate);
  EXPECT_TRUE(foundPress);
  EXPECT_TRUE(foundGetDisplay);
  EXPECT_TRUE(foundClear);
}

} // namespace
