#ifndef WINBOT_TOOLS_BUILTINTOOLS_H
#define WINBOT_TOOLS_BUILTINTOOLS_H

#include "Common.h"
#include "core/ITool.h"
#include "core/ToolRegistry.h"

// Include all modular tool endpoints
#include "tools/BrowserTools.h"
#include "tools/FileTools.h"
#include "tools/InputTools.h"
#include "tools/ScreenTools.h"
#include "tools/ShellTools.h"
#include "tools/WindowTools.h"

#include <functional>
#include <memory>
#include <string>

// Forward declarations of subsystem dependencies
class UIAutomationScanner;
class UIADebugger;
class BrowserAutomation;
class PermissionSystem;
class SiteProfileRegistry;
class LuaRuntime;


// ── Discovery Tools ──────────────────────────────────────────────────────────
class ListToolsTool : public ITool {
public:
    explicit ListToolsTool(const ToolRegistry& registry) : m_registry(&registry) {}
    explicit ListToolsTool(const ToolRegistry* registry = nullptr) : m_registry(registry) {}

    [[nodiscard]] std::string name() const override { return "list_tools"; }
    [[nodiscard]] std::string description() const override {
        return "Return the full JSON schema of all available tools";
    }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    const ToolRegistry* m_registry{nullptr};
};

// ── Built-in Basic Utilities ─────────────────────────────────────────────────
class PingTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "ping"; }
    [[nodiscard]] std::string description() const override { return "Check if server is responsive"; }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json&) override { return ok("pong"); }
};

class EchoTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "echo"; }
    [[nodiscard]] std::string description() const override { return "Echo back input text"; }
    [[nodiscard]] json parametersSchema() const override {
        return {
            {"type", "object"},
            {"properties", {{"text", {{"type", "string"}}}}},
            {"required", {"text"}}
        };
    }
    [[nodiscard]] ToolResult execute(const json& args) override {
        return ok(args.value("text", ""));
    }
};

class VersionTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "version"; }
    [[nodiscard]] std::string description() const override { return "Get WinBot version information"; }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json&) override {
        return ok(json{
            {"version", "0.2.0"},
            {"build", "msvc-ninja"}
        }.dump());
    }
};

// ── Perception Tools (UIA) ───────────────────────────────────────────────────
class UiScanTool : public ITool {
public:
    explicit UiScanTool(UIAutomationScanner& uia) : m_uia(&uia) {}
    explicit UiScanTool(UIAutomationScanner* uia = nullptr) : m_uia(uia) {}

    [[nodiscard]] std::string name() const override { return "ui_scan"; }
    [[nodiscard]] std::string description() const override {
        return "Scan the UI Automation tree of the focused window. "
               "Each element shows its bounds as (left,top,right,bottom) in screen pixels — "
               "pass those to screenshot_element to capture any element.";
    }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    UIAutomationScanner* m_uia{nullptr};
};

class UiScanWindowTool : public ITool {
public:
    explicit UiScanWindowTool(UIAutomationScanner& uia) : m_uia(&uia) {}
    explicit UiScanWindowTool(UIAutomationScanner* uia = nullptr) : m_uia(uia) {}

    [[nodiscard]] std::string name() const override { return "ui_scan_window"; }
    [[nodiscard]] std::string description() const override {
        return "Scan the UI Automation tree of a specific window (case-insensitive title substring match). "
               "Each element shows its bounds as (left,top,right,bottom) in screen pixels — "
               "pass those to screenshot_element to capture any element.";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    UIAutomationScanner* m_uia{nullptr};
};

// ── UI Automation Debugger Scripting Tool ────────────────────────────────────
class DebugUiaTool : public ITool {
public:
    explicit DebugUiaTool(UIAutomationScanner& uia);
    explicit DebugUiaTool(UIAutomationScanner* uia = nullptr);
    ~DebugUiaTool() override;

    [[nodiscard]] std::string name() const override { return "debug_uia"; }
    [[nodiscard]] std::string description() const override {
        return "Execute UI Automation commands using the UIADebugger script syntax. "
               "Supports selecting windows and controls, clicking via UIA Invoke pattern (without moving mouse), "
               "typing, key combinations, inspecting text/values, variables, and dot-chained pipelines. "
               "Commands are separated by semicolons or newlines.\n"
               "Syntax examples:\n"
               "  Select(\"Calculator\", 3000).Click(\"Button\", \"One\")\n"
               "  $calc = Select(\"Calculator\", 3000) ; $calc.Click(\"Button\", \"One\") ; $calc.Click(\"Button\", \"Plus\")\n"
               "  $calc.Text(\"CalculatorResults\")\n"
               "  Scan(\"Calculator\")\n"
               "  Clear(All)";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    UIAutomationScanner* m_uia{nullptr};
    std::unique_ptr<UIADebugger> m_debugger;
};

// ── Lua Scripting Tools ──────────────────────────────────────────────────────
class LuaExecTool : public ITool {
public:
    explicit LuaExecTool(LuaRuntime& luaRuntime) : m_luaRuntime(&luaRuntime) {}
    explicit LuaExecTool(LuaRuntime* luaRuntime = nullptr) : m_luaRuntime(luaRuntime) {}

    [[nodiscard]] std::string name() const override { return "lua_exec"; }
    [[nodiscard]] std::string description() const override {
        return "Execute an inline Lua 5.4 script. The 'winbot' table is available globally "
               "for automation (navigate, click, evalJs, log, sleep, typeText, pressKey, readPage).";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    LuaRuntime* m_luaRuntime{nullptr};
};

class LuaRunTool : public ITool {
public:
    explicit LuaRunTool(LuaRuntime& luaRuntime) : m_luaRuntime(&luaRuntime) {}
    explicit LuaRunTool(LuaRuntime* luaRuntime = nullptr) : m_luaRuntime(luaRuntime) {}

    [[nodiscard]] std::string name() const override { return "lua_run"; }
    [[nodiscard]] std::string description() const override {
        return "Execute a Lua script file from disk.";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    LuaRuntime* m_luaRuntime{nullptr};
};

// ── Registration Dependency Container & Factory ──────────────────────────────
struct BuiltinToolDependencies {
    UIAutomationScanner& uia;
    BrowserAutomation&   browser;
    PermissionSystem&    perms;
    SiteProfileRegistry& siteProfiles;
    LuaRuntime&          luaRuntime;
};

namespace BuiltinTools {

    // Registers all tools into registry, checking deps.perms.isToolEnabled(name)
    void registerAll(ToolRegistry& registry, const BuiltinToolDependencies& deps);

} // namespace BuiltinTools

// Explicit initialization anchor
void initBuiltinTools();

#endif // WINBOT_TOOLS_BUILTINTOOLS_H
