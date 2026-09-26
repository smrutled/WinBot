#include "BuiltinTools.h"

#include "UIAutomationScanner.h"
#include "ScreenCapture.h"
#include "BrowserAutomation.h"
#include "../PermissionSystem.h"
#include "../SiteProfileRegistry.h"
#include "../LuaRuntime.h"

#include "BrowserTools.h"
#include "FileTools.h"
#include "InputTools.h"
#include "ShellTools.h"
#include "WindowTools.h"

#include <format>
#include <string>

// ── ListToolsTool ────────────────────────────────────────────────────────────
ToolResult ListToolsTool::execute(const json&) {
    return ok(m_registry.buildToolsSchema().dump());
}

// ── UiScanTool ───────────────────────────────────────────────────────────────
ToolResult UiScanTool::execute(const json&) {
    auto treeResult = m_uia.scanFocusedWindow();
    if (treeResult) {
        int n = UIAutomationScanner::countInteractive(*treeResult);
        if (n > 0) {
            return ok("## UI Tree (focused window):\n" +
                      UIAutomationScanner::serialize(*treeResult));
        }
        WINBOT_INFO("UiScanTool: UIA tree sparse ({} elements) — using screenshot fallback", n);
    }

    auto screen = ScreenCapture::captureDesktop();
    if (screen) {
        return ok(std::format("## Screenshot taken: {}x{} px", screen->width, screen->height));
    }

    return ok("(observation failed)");
}

// ── UiScanWindowTool ─────────────────────────────────────────────────────────
json UiScanWindowTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"title", {{"type", "string"}, {"description", "Window title substring (case-insensitive)"}}}
        }},
        {"required", {"title"}}
    };
}

ToolResult UiScanWindowTool::execute(const json& args) {
    std::string title = args.value("title", "");
    auto tree = m_uia.scanWindow(title);
    if (!tree) return err(tree.error());
    return ok(UIAutomationScanner::serialize(*tree));
}

// ── ScreenshotDesktopTool ────────────────────────────────────────────────────
ToolResult ScreenshotDesktopTool::execute(const json&) {
    auto r = ScreenCapture::captureDesktop();
    if (!r) return err(r.error());
    json result = {
        {"width", r->width},
        {"height", r->height},
        {"format", "png"},
        {"data", ScreenCapture::toBase64(r->pngBytes)}
    };
    return ok(result.dump());
}

// ── ScreenshotWindowTool ─────────────────────────────────────────────────────
json ScreenshotWindowTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"title", {
                {"type", "string"},
                {"description", "Window title substring (case-insensitive)"}
            }},
            {"bring_to_front", {
                {"type", "boolean"},
                {"description", "Optional: bring the window to the foreground before capturing "
                                "(default: false; captures in background without occlusions and "
                                "falls back to foreground if offscreen capture is unsupported)"}
            }}
        }},
        {"required", {"title"}}
    };
}

ToolResult ScreenshotWindowTool::execute(const json& args) {
    std::string title = args.value("title", "");
    bool bringToFront = args.value("bring_to_front", false);

    auto tree = m_uia.scanWindow(title);
    if (!tree) return err(tree.error());

    HWND hwnd = tree->ownerHwnd;
    if (!hwnd) {
        return err(std::format("Found UIA element for '{}' but no HWND", title));
    }

    auto r = ScreenCapture::captureWindow(hwnd, bringToFront);
    if (!r) return err(r.error());

    json result = {
        {"width", r->width},
        {"height", r->height},
        {"format", "png"},
        {"data", ScreenCapture::toBase64(r->pngBytes)}
    };
    return ok(result.dump());
}

// ── ScreenshotElementTool ────────────────────────────────────────────────────
json ScreenshotElementTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"left",   {{"type", "integer"}, {"description", "Left edge in screen pixels"}}},
            {"top",    {{"type", "integer"}, {"description", "Top edge in screen pixels"}}},
            {"right",  {{"type", "integer"}, {"description", "Right edge in screen pixels"}}},
            {"bottom", {{"type", "integer"}, {"description", "Bottom edge in screen pixels"}}}
        }},
        {"required", {"left", "top", "right", "bottom"}}
    };
}

ToolResult ScreenshotElementTool::execute(const json& args) {
    RECT region{
        args.value("left", 0),
        args.value("top", 0),
        args.value("right", 0),
        args.value("bottom", 0)
    };
    if (region.right <= region.left || region.bottom <= region.top) {
        return err("Invalid region: right must be > left and bottom must be > top");
    }

    auto r = ScreenCapture::captureRegion(region);
    if (!r) return err(r.error());

    json result = {
        {"width", r->width},
        {"height", r->height},
        {"format", "png"},
        {"data", ScreenCapture::toBase64(r->pngBytes)}
    };
    return ok(result.dump());
}

// ── RunCommandTool ───────────────────────────────────────────────────────────
json RunCommandTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"cmd", {{"type", "string"}, {"description", "Command to execute"}}},
            {"shell", {{"type", "string"}, {"description", "Shell to use: cmd|powershell|pwsh"}, {"default", "cmd"}}},
            {"timeout_ms", {{"type", "integer"}, {"default", 30000}}}
        }},
        {"required", {"cmd"}}
    };
}

ToolResult RunCommandTool::execute(const json& args) {
    auto cmd = args.value("cmd", "");
    auto shell = args.value("shell", "cmd");
    auto check = m_perms.checkShellCommand(cmd);
    if (!check) return check;
    return tools::runCommand(cmd, shell, args.value("timeout_ms", 30000));
}

// ── ReadFileTool ─────────────────────────────────────────────────────────────
json ReadFileTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {{"path", {{"type", "string"}}}}},
        {"required", {"path"}}
    };
}

ToolResult ReadFileTool::execute(const json& args) {
    auto path = args.value("path", "");
    auto check = m_perms.checkPath(path);
    if (!check) return check;
    return tools::readFile(path);
}

// ── WriteFileTool ────────────────────────────────────────────────────────────
json WriteFileTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}}},
            {"content", {{"type", "string"}}}
        }},
        {"required", {"path", "content"}}
    };
}

ToolResult WriteFileTool::execute(const json& args) {
    auto path = args.value("path", "");
    auto check = m_perms.checkPath(path);
    if (!check) return check;
    return tools::writeFile(path, args.value("content", ""));
}

// ── AppendFileTool ───────────────────────────────────────────────────────────
json AppendFileTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}}},
            {"content", {{"type", "string"}}}
        }},
        {"required", {"path", "content"}}
    };
}

ToolResult AppendFileTool::execute(const json& args) {
    auto path = args.value("path", "");
    auto check = m_perms.checkPath(path);
    if (!check) return check;
    return tools::appendFile(path, args.value("content", ""));
}

// ── DeleteFileTool ───────────────────────────────────────────────────────────
json DeleteFileTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {{"path", {{"type", "string"}}}}},
        {"required", {"path"}}
    };
}

ToolResult DeleteFileTool::execute(const json& args) {
    auto path = args.value("path", "");
    auto check = m_perms.checkPath(path);
    if (!check) return check;

    auto confirm = PermissionSystem::promptUser(
        std::format("Delete file '{}'?", path));
    if (!confirm) return confirm;

    return tools::deleteFile(path);
}

// ── KillProcessTool ──────────────────────────────────────────────────────────
json KillProcessTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {{"name_or_pid", {{"type", "string"}}}}},
        {"required", {"name_or_pid"}}
    };
}

ToolResult KillProcessTool::execute(const json& args) {
    auto nameOrPid = args.value("name_or_pid", "");
    auto check = m_perms.checkProcess(nameOrPid);
    if (!check) return check;
    return tools::killProcess(nameOrPid);
}

// ── Browser Tools ────────────────────────────────────────────────────────────
json BrowserNavigateTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {{"url", {{"type", "string"}}}}},
        {"required", {"url"}}
    };
}

ToolResult BrowserNavigateTool::execute(const json& args) {
    return tools::browserNavigate(m_browser, args.value("url", ""));
}

json BrowserClickTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {{"selector", {{"type", "string"}}}}},
        {"required", {"selector"}}
    };
}

ToolResult BrowserClickTool::execute(const json& args) {
    return tools::browserClick(m_browser, args.value("selector", ""));
}

json BrowserTypeTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"selector", {{"type", "string"}}},
            {"text", {{"type", "string"}}}
        }},
        {"required", {"selector", "text"}}
    };
}

ToolResult BrowserTypeTool::execute(const json& args) {
    return tools::browserType(m_browser, args.value("selector", ""), args.value("text", ""));
}

ToolResult BrowserGetDomTool::execute(const json&) {
    return tools::browserGetDom(m_browser);
}

ToolResult BrowserGetPageTextTool::execute(const json&) {
    return tools::browserGetPageText(m_browser);
}

json BrowserReadPageTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {{"url", {{"type", "string"}}}}},
        {"required", {"url"}}
    };
}

ToolResult BrowserReadPageTool::execute(const json& args) {
    std::string url = args.value("url", "");
    auto navRes = tools::browserNavigate(m_browser, url);
    if (!navRes) return navRes;

    ::Sleep(2000); // Wait for content to settle

    const SiteProfile* profile = m_siteProfiles.match(url);
    if (profile) {
        std::string js = "(() => { let out = {};\n";
        for (const auto& item : profile->items) {
            js += std::format("try {{\n  let els = document.querySelectorAll('{}');\n", item.selector);
            js += std::format("  if (els.length > 0) {{\n");
            std::string extJs;
            if (item.extract == "text") extJs = "e.innerText";
            else if (item.extract == "html") extJs = "e.innerHTML";
            else extJs = std::format("e.getAttribute('{}')", item.extract);

            if (item.isArray) {
                js += std::format("    out['{}'] = Array.from(els).map(e => {}).filter(x => x);\n", item.name, extJs);
            } else {
                js += std::format("    out['{}'] = (e => {}) (els[0]);\n", item.name, extJs);
            }
            js += "  }\n} catch(e){}\n";
        }
        js += "return JSON.stringify(out);\n})()";
        return tools::browserEval(m_browser, js);
    }

    return tools::browserGetPageText(m_browser);
}

json BrowserSaveProfileTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"name", {{"type", "string"}, {"description", "File name (e.g. 'old_reddit')"}}},
            {"profile", {{"type", "object"}, {"description", "JSON object with 'url_match' and 'extract_items' array"}}}
        }},
        {"required", {"name", "profile"}}
    };
}

ToolResult BrowserSaveProfileTool::execute(const json& args) {
    return m_siteProfiles.saveProfile(args.value("name", ""), args.value("profile", json::object()));
}

json BrowserEvalTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {{"js", {{"type", "string"}}}}},
        {"required", {"js"}}
    };
}

ToolResult BrowserEvalTool::execute(const json& args) {
    return tools::browserEval(m_browser, args.value("js", ""));
}

// ── Lua Scripting Tools ──────────────────────────────────────────────────────
json LuaExecTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"code", {{"type", "string"}, {"description", "Raw Lua source code to execute"}}}
        }},
        {"required", {"code"}}
    };
}

ToolResult LuaExecTool::execute(const json& args) {
    return m_luaRuntime.execString(args.value("code", ""));
}

json LuaRunTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Absolute path to the .lua file"}}}
        }},
        {"required", {"path"}}
    };
}

ToolResult LuaRunTool::execute(const json& args) {
    return m_luaRuntime.execFile(args.value("path", ""));
}

// ── Registration Factory ─────────────────────────────────────────────────────
void BuiltinTools::registerAll(ToolRegistry& registry, const BuiltinToolDependencies& deps) {
    auto maybeRegister = [&](std::unique_ptr<ITool> tool) {
        if (deps.perms.isToolEnabled(tool->name())) {
            registry.registerTool(std::move(tool));
        }
    };

    // ── Discovery ────────────────────────────────────────────────────────────
    maybeRegister(std::make_unique<ListToolsTool>(registry));

    // ── Perception ───────────────────────────────────────────────────────────
    maybeRegister(std::make_unique<UiScanTool>(deps.uia));
    maybeRegister(std::make_unique<UiScanWindowTool>(deps.uia));
    maybeRegister(std::make_unique<ScreenshotDesktopTool>());
    maybeRegister(std::make_unique<ScreenshotWindowTool>(deps.uia));
    maybeRegister(std::make_unique<ScreenshotElementTool>());

    // ── Windows & Mouse & Keyboard (Stateless / Direct) ──────────────────────
    maybeRegister(std::make_unique<LambdaTool>(
        "get_window_list",
        "List all visible windows with title and HWND",
        json{{"type", "object"}, {"properties", json::object()}},
        [](const json&) { return tools::getWindowList(); }
    ));

    maybeRegister(std::make_unique<LambdaTool>(
        "get_cursor_position",
        "Get current mouse cursor position",
        json{{"type", "object"}, {"properties", json::object()}},
        [](const json&) { return tools::getCursorPosition(); }
    ));

    maybeRegister(std::make_unique<LambdaTool>(
        "input_human_move",
        "Move the mouse to (x, y) using a human-like path (cubic Bézier curve with ease-in/out and jitter)",
        json{
            {"type", "object"},
            {"properties", {
                {"x", {{"type", "integer"}, {"description", "Screen X coordinate"}}},
                {"y", {{"type", "integer"}, {"description", "Screen Y coordinate"}}}
            }},
            {"required", {"x", "y"}}
        },
        [](const json& a) { return tools::humanMouseMove(a.value("x", 0), a.value("y", 0)); }
    ));

    maybeRegister(std::make_unique<LambdaTool>(
        "click",
        "Click the mouse at (x, y)",
        json{
            {"type", "object"},
            {"properties", {
                {"x", {{"type", "integer"}, {"description", "Screen X coordinate"}}},
                {"y", {{"type", "integer"}, {"description", "Screen Y coordinate"}}},
                {"button", {{"type", "string"}, {"description", "left|right|middle"}, {"default", "left"}}}
            }},
            {"required", {"x", "y"}}
        },
        [](const json& a) { return tools::click(a.value("x", 0), a.value("y", 0), a.value("button", "left")); }
    ));

    maybeRegister(std::make_unique<LambdaTool>(
        "double_click",
        "Double-click at (x, y)",
        json{
            {"type", "object"},
            {"properties", {
                {"x", {{"type", "integer"}}},
                {"y", {{"type", "integer"}}}
            }},
            {"required", {"x", "y"}}
        },
        [](const json& a) { return tools::doubleClick(a.value("x", 0), a.value("y", 0)); }
    ));

    maybeRegister(std::make_unique<LambdaTool>(
        "drag",
        "Click-drag from (x1,y1) to (x2,y2)",
        json{
            {"type", "object"},
            {"properties", {
                {"x1", {{"type", "integer"}}},
                {"y1", {{"type", "integer"}}},
                {"x2", {{"type", "integer"}}},
                {"y2", {{"type", "integer"}}}
            }},
            {"required", {"x1", "y1", "x2", "y2"}}
        },
        [](const json& a) {
            return tools::drag(a.value("x1", 0), a.value("y1", 0), a.value("x2", 0), a.value("y2", 0));
        }
    ));

    maybeRegister(std::make_unique<LambdaTool>(
        "scroll",
        "Scroll the mouse wheel at (x, y)",
        json{
            {"type", "object"},
            {"properties", {
                {"x", {{"type", "integer"}}},
                {"y", {{"type", "integer"}}},
                {"delta", {{"type", "integer"}, {"description", "Positive=up, negative=down"}}}
            }},
            {"required", {"x", "y", "delta"}}
        },
        [](const json& a) { return tools::scroll(a.value("x", 0), a.value("y", 0), a.value("delta", -3)); }
    ));

    maybeRegister(std::make_unique<LambdaTool>(
        "type",
        "Type text using the keyboard",
        json{
            {"type", "object"},
            {"properties", {{"text", {{"type", "string"}, {"description", "Text to type"}}}}},
            {"required", {"text"}}
        },
        [](const json& a) { return tools::typeText(a.value("text", "")); }
    ));

    maybeRegister(std::make_unique<LambdaTool>(
        "key",
        "Press a key or key combination (e.g. Ctrl+C, Enter, F5)",
        json{
            {"type", "object"},
            {"properties", {{"combo", {{"type", "string"}, {"description", "e.g. Ctrl+C, Enter, F5"}}}}},
            {"required", {"combo"}}
        },
        [](const json& a) { return tools::keyPress(a.value("combo", "Enter")); }
    ));

    maybeRegister(std::make_unique<LambdaTool>(
        "focus_window",
        "Bring a window to the foreground by title substring",
        json{
            {"type", "object"},
            {"properties", {{"title", {{"type", "string"}, {"description", "Window title substring"}}}}},
            {"required", {"title"}}
        },
        [](const json& a) { return tools::focusWindow(a.value("title", "")); }
    ));

    maybeRegister(std::make_unique<LambdaTool>(
        "close_window",
        "Close a window by title substring",
        json{
            {"type", "object"},
            {"properties", {{"title", {{"type", "string"}}}}},
            {"required", {"title"}}
        },
        [](const json& a) { return tools::closeWindow(a.value("title", "")); }
    ));

    // ── Shell & Files ────────────────────────────────────────────────────────
    maybeRegister(std::make_unique<RunCommandTool>(deps.perms));
    maybeRegister(std::make_unique<ReadFileTool>(deps.perms));
    maybeRegister(std::make_unique<WriteFileTool>(deps.perms));
    maybeRegister(std::make_unique<AppendFileTool>(deps.perms));

    maybeRegister(std::make_unique<LambdaTool>(
        "list_directory",
        "List files in a directory",
        json{
            {"type", "object"},
            {"properties", {{"path", {{"type", "string"}}}}},
            {"required", {"path"}}
        },
        [](const json& a) { return tools::listDirectory(a.value("path", ".")); }
    ));

    maybeRegister(std::make_unique<DeleteFileTool>(deps.perms));

    maybeRegister(std::make_unique<LambdaTool>(
        "copy_file",
        "Copy a file from src to dst",
        json{
            {"type", "object"},
            {"properties", {
                {"src", {{"type", "string"}}},
                {"dst", {{"type", "string"}}}
            }},
            {"required", {"src", "dst"}}
        },
        [](const json& a) { return tools::copyFile(a.value("src", ""), a.value("dst", "")); }
    ));

    // ── System ───────────────────────────────────────────────────────────────
    maybeRegister(std::make_unique<LambdaTool>(
        "get_clipboard",
        "Read current clipboard text",
        json{{"type", "object"}, {"properties", json::object()}},
        [](const json&) { return tools::getClipboard(); }
    ));

    maybeRegister(std::make_unique<LambdaTool>(
        "set_clipboard",
        "Write text to the clipboard",
        json{
            {"type", "object"},
            {"properties", {{"text", {{"type", "string"}}}}},
            {"required", {"text"}}
        },
        [](const json& a) { return tools::setClipboard(a.value("text", "")); }
    ));

    maybeRegister(std::make_unique<LambdaTool>(
        "get_processes",
        "List running processes (name + PID)",
        json{{"type", "object"}, {"properties", json::object()}},
        [](const json&) { return tools::getProcesses(); }
    ));

    maybeRegister(std::make_unique<KillProcessTool>(deps.perms));

    maybeRegister(std::make_unique<LambdaTool>(
        "get_system_info",
        "Get CPU, RAM, and disk usage info",
        json{{"type", "object"}, {"properties", json::object()}},
        [](const json&) { return tools::getSystemInfo(); }
    ));

    // ── Web ──────────────────────────────────────────────────────────────────
    maybeRegister(std::make_unique<LambdaTool>(
        "http_get",
        "Make an HTTP GET request and return the body",
        json{
            {"type", "object"},
            {"properties", {
                {"url", {{"type", "string"}}},
                {"headers", {{"type", "string"}, {"default", ""}}}
            }},
            {"required", {"url"}}
        },
        [](const json& a) { return tools::httpGet(a.value("url", ""), a.value("headers", "")); }
    ));

    maybeRegister(std::make_unique<LambdaTool>(
        "search_web",
        "Search DuckDuckGo and return the top result URLs + snippets",
        json{
            {"type", "object"},
            {"properties", {{"query", {{"type", "string"}}}}},
            {"required", {"query"}}
        },
        [](const json& a) { return tools::searchWeb(a.value("query", "")); }
    ));

    // ── Browser (CDP) ────────────────────────────────────────────────────────
    maybeRegister(std::make_unique<BrowserNavigateTool>(deps.browser));
    maybeRegister(std::make_unique<BrowserClickTool>(deps.browser));
    maybeRegister(std::make_unique<BrowserTypeTool>(deps.browser));
    maybeRegister(std::make_unique<BrowserGetDomTool>(deps.browser));
    maybeRegister(std::make_unique<BrowserGetPageTextTool>(deps.browser));
    maybeRegister(std::make_unique<BrowserReadPageTool>(deps.browser, deps.siteProfiles));
    maybeRegister(std::make_unique<BrowserSaveProfileTool>(deps.siteProfiles));
    maybeRegister(std::make_unique<BrowserEvalTool>(deps.browser));

    // ── Lua Scripting ────────────────────────────────────────────────────────
    maybeRegister(std::make_unique<LuaExecTool>(deps.luaRuntime));
    maybeRegister(std::make_unique<LuaRunTool>(deps.luaRuntime));
}
