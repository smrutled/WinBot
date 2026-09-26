#ifndef WINBOT_TOOLS_BUILTINTOOLS_H
#define WINBOT_TOOLS_BUILTINTOOLS_H

#include "../Common.h"
#include "../ITool.h"
#include "../ToolRegistry.h"
#include <functional>
#include <memory>
#include <string>

// Forward declarations of subsystem dependencies
class UIAutomationScanner;
class BrowserAutomation;
class PermissionSystem;
class SiteProfileRegistry;
class LuaRuntime;

// ── Generic Lambda Tool ──────────────────────────────────────────────────────
class LambdaTool : public ITool {
public:
    using Handler = std::function<ToolResult(const json& args)>;

    LambdaTool(std::string name, std::string description, json schema, Handler handler)
        : m_name(std::move(name)), m_description(std::move(description)),
          m_schema(std::move(schema)), m_handler(std::move(handler)) {}

    [[nodiscard]] std::string name() const override { return m_name; }
    [[nodiscard]] std::string description() const override { return m_description; }
    [[nodiscard]] json parametersSchema() const override { return m_schema; }
    [[nodiscard]] ToolResult execute(const json& args) override { return m_handler(args); }

private:
    std::string m_name;
    std::string m_description;
    json        m_schema;
    Handler     m_handler;
};

// ── Discovery Tools ──────────────────────────────────────────────────────────
class ListToolsTool : public ITool {
public:
    explicit ListToolsTool(const ToolRegistry& registry) : m_registry(registry) {}
    [[nodiscard]] std::string name() const override { return "list_tools"; }
    [[nodiscard]] std::string description() const override {
        return "Return the full JSON schema of all available tools";
    }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    const ToolRegistry& m_registry;
};

// ── Perception Tools ─────────────────────────────────────────────────────────
class UiScanTool : public ITool {
public:
    explicit UiScanTool(UIAutomationScanner& uia) : m_uia(uia) {}
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
    UIAutomationScanner& m_uia;
};

class UiScanWindowTool : public ITool {
public:
    explicit UiScanWindowTool(UIAutomationScanner& uia) : m_uia(uia) {}
    [[nodiscard]] std::string name() const override { return "ui_scan_window"; }
    [[nodiscard]] std::string description() const override {
        return "Scan the UI Automation tree of a specific window (case-insensitive title substring match). "
               "Each element shows its bounds as (left,top,right,bottom) in screen pixels — "
               "pass those to screenshot_element to capture any element.";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    UIAutomationScanner& m_uia;
};

class ScreenshotDesktopTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "screenshot"; }
    [[nodiscard]] std::string description() const override {
        return "Capture the desktop and return the image as a base64-encoded PNG";
    }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class ScreenshotWindowTool : public ITool {
public:
    explicit ScreenshotWindowTool(UIAutomationScanner& uia) : m_uia(uia) {}
    [[nodiscard]] std::string name() const override { return "screenshot_window"; }
    [[nodiscard]] std::string description() const override {
        return "Capture a specific window by title substring (case-insensitive, partial match OK). "
               "Uses UI Automation to locate the window so casing doesn't matter. "
               "Captures the target window cleanly even if occluded or in the background. "
               "Set optional 'bring_to_front' to true to bring the window to the foreground first.";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    UIAutomationScanner& m_uia;
};

class ScreenshotElementTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "screenshot_element"; }
    [[nodiscard]] std::string description() const override {
        return "Capture a specific UI element's screen region as a PNG. "
               "Get the bounds (left,top,right,bottom) from ui_scan or ui_scan_window output "
               "and pass them here to get a tight screenshot of just that element.";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

// ── Shell & File Tools ───────────────────────────────────────────────────────
class RunCommandTool : public ITool {
public:
    explicit RunCommandTool(PermissionSystem& perms) : m_perms(perms) {}
    [[nodiscard]] std::string name() const override { return "run_command"; }
    [[nodiscard]] std::string description() const override {
        return "Run a PowerShell/cmd command and return stdout+stderr";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    PermissionSystem& m_perms;
};

class ReadFileTool : public ITool {
public:
    explicit ReadFileTool(PermissionSystem& perms) : m_perms(perms) {}
    [[nodiscard]] std::string name() const override { return "read_file"; }
    [[nodiscard]] std::string description() const override {
        return "Read a file's text contents";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    PermissionSystem& m_perms;
};

class WriteFileTool : public ITool {
public:
    explicit WriteFileTool(PermissionSystem& perms) : m_perms(perms) {}
    [[nodiscard]] std::string name() const override { return "write_file"; }
    [[nodiscard]] std::string description() const override {
        return "Write content to a file (creates or overwrites)";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    PermissionSystem& m_perms;
};

class AppendFileTool : public ITool {
public:
    explicit AppendFileTool(PermissionSystem& perms) : m_perms(perms) {}
    [[nodiscard]] std::string name() const override { return "append_file"; }
    [[nodiscard]] std::string description() const override {
        return "Append content to a file";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    PermissionSystem& m_perms;
};

class DeleteFileTool : public ITool {
public:
    explicit DeleteFileTool(PermissionSystem& perms) : m_perms(perms) {}
    [[nodiscard]] std::string name() const override { return "delete_file"; }
    [[nodiscard]] std::string description() const override {
        return "Delete a file (permission-checked)";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    PermissionSystem& m_perms;
};

class KillProcessTool : public ITool {
public:
    explicit KillProcessTool(PermissionSystem& perms) : m_perms(perms) {}
    [[nodiscard]] std::string name() const override { return "kill_process"; }
    [[nodiscard]] std::string description() const override {
        return "Kill a process by name or PID";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    PermissionSystem& m_perms;
};

// ── Browser Tools ────────────────────────────────────────────────────────────
class BrowserNavigateTool : public ITool {
public:
    explicit BrowserNavigateTool(BrowserAutomation& browser) : m_browser(browser) {}
    [[nodiscard]] std::string name() const override { return "browser_navigate"; }
    [[nodiscard]] std::string description() const override {
        return "Navigate the browser to a URL";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    BrowserAutomation& m_browser;
};

class BrowserClickTool : public ITool {
public:
    explicit BrowserClickTool(BrowserAutomation& browser) : m_browser(browser) {}
    [[nodiscard]] std::string name() const override { return "browser_click"; }
    [[nodiscard]] std::string description() const override {
        return "Click a DOM element by CSS selector";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    BrowserAutomation& m_browser;
};

class BrowserTypeTool : public ITool {
public:
    explicit BrowserTypeTool(BrowserAutomation& browser) : m_browser(browser) {}
    [[nodiscard]] std::string name() const override { return "browser_type"; }
    [[nodiscard]] std::string description() const override {
        return "Type text into a browser input field";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    BrowserAutomation& m_browser;
};

class BrowserGetDomTool : public ITool {
public:
    explicit BrowserGetDomTool(BrowserAutomation& browser) : m_browser(browser) {}
    [[nodiscard]] std::string name() const override { return "browser_get_dom"; }
    [[nodiscard]] std::string description() const override {
        return "Get a simplified DOM of the current page";
    }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    BrowserAutomation& m_browser;
};

class BrowserGetPageTextTool : public ITool {
public:
    explicit BrowserGetPageTextTool(BrowserAutomation& browser) : m_browser(browser) {}
    [[nodiscard]] std::string name() const override { return "browser_get_page_text"; }
    [[nodiscard]] std::string description() const override {
        return "Get the raw readable text of the current page";
    }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    BrowserAutomation& m_browser;
};

class BrowserReadPageTool : public ITool {
public:
    BrowserReadPageTool(BrowserAutomation& browser, SiteProfileRegistry& siteProfiles)
        : m_browser(browser), m_siteProfiles(siteProfiles) {}
    [[nodiscard]] std::string name() const override { return "browser_read_page"; }
    [[nodiscard]] std::string description() const override {
        return "Navigate to a URL and extract its content. If a site profile exists, "
               "extracts structured data. Otherwise falls back to raw page text.";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    BrowserAutomation&   m_browser;
    SiteProfileRegistry& m_siteProfiles;
};

class BrowserSaveProfileTool : public ITool {
public:
    explicit BrowserSaveProfileTool(SiteProfileRegistry& siteProfiles)
        : m_siteProfiles(siteProfiles) {}
    [[nodiscard]] std::string name() const override { return "browser_save_profile"; }
    [[nodiscard]] std::string description() const override {
        return "Save a JSON site profile to the registry for structured extraction in browser_read_page.";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    SiteProfileRegistry& m_siteProfiles;
};

class BrowserEvalTool : public ITool {
public:
    explicit BrowserEvalTool(BrowserAutomation& browser) : m_browser(browser) {}
    [[nodiscard]] std::string name() const override { return "browser_eval"; }
    [[nodiscard]] std::string description() const override {
        return "Execute JavaScript in the browser and return result";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    BrowserAutomation& m_browser;
};

// ── Lua Scripting Tools ──────────────────────────────────────────────────────
class LuaExecTool : public ITool {
public:
    explicit LuaExecTool(LuaRuntime& luaRuntime) : m_luaRuntime(luaRuntime) {}
    [[nodiscard]] std::string name() const override { return "lua_exec"; }
    [[nodiscard]] std::string description() const override {
        return "Execute an inline Lua 5.4 script. The 'winbot' table is available globally "
               "for automation (navigate, click, evalJs, log, sleep, typeText, pressKey, readPage).";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    LuaRuntime& m_luaRuntime;
};

class LuaRunTool : public ITool {
public:
    explicit LuaRunTool(LuaRuntime& luaRuntime) : m_luaRuntime(luaRuntime) {}
    [[nodiscard]] std::string name() const override { return "lua_run"; }
    [[nodiscard]] std::string description() const override {
        return "Execute a Lua script file from disk.";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    LuaRuntime& m_luaRuntime;
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

    // Registers all built-in tools into registry, checking deps.perms.isToolEnabled(name)
    void registerAll(ToolRegistry& registry, const BuiltinToolDependencies& deps);

} // namespace BuiltinTools

#endif // WINBOT_TOOLS_BUILTINTOOLS_H
