#include "tools/BuiltinTools.h"

#include "platform/uia/UIAutomationScanner.h"
#include "platform/uia/UIADebugger.h"
#include "platform/screen/ScreenCapture.h"
#include "services/LuaRuntime.h"
#include "services/LuaToolLoader.h"
#include "tools/InputTools.h"

#include <format>
#include <sstream>
#include <string>

// ── ListToolsTool ────────────────────────────────────────────────────────────
ToolResult ListToolsTool::execute(const json& /*args*/) {
    if (m_registry == nullptr) {
        return ok("[]");
    }
    return ok(m_registry->buildToolsSchema().dump());
}

// ── UiScanTool ───────────────────────────────────────────────────────────────
ToolResult UiScanTool::execute(const json& /*args*/) {
    if (m_uia != nullptr) {
        auto treeResult = m_uia->scanFocusedWindow();
        if (treeResult) {
            int n = UIAutomationScanner::countInteractive(*treeResult);
            if (n > 0) {
                return ok("## UI Tree (focused window):\n" +
                          UIAutomationScanner::serialize(*treeResult));
            }
            WINBOT_INFO("UiScanTool: UIA tree sparse ({} elements) — using screenshot fallback", n);
        }
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
    if (m_uia == nullptr) {
        return err("UIAutomationScanner not available");
    }
    std::string title = args.value("title", "");
    auto tree = m_uia->scanWindow(title);
    if (!tree) {
        return err(tree.error());
    }
    return ok(UIAutomationScanner::serialize(*tree));
}

// ── DebugUiaTool ─────────────────────────────────────────────────────────────
DebugUiaTool::DebugUiaTool(UIAutomationScanner& uia)
    : m_uia(&uia), m_debugger(std::make_unique<UIADebugger>(uia)) {}

DebugUiaTool::DebugUiaTool(UIAutomationScanner* uia)
    : m_uia(uia), m_debugger((uia != nullptr) ? std::make_unique<UIADebugger>(*uia) : nullptr) {}

DebugUiaTool::~DebugUiaTool() = default;

json DebugUiaTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"cmd", {
                {"type", "string"},
                {"description", "UIADebugger script commands to execute (separated by semicolons or newlines). Also accepts 'script', 'command', or 'code'."}
            }}
        }},
        {"required", {"cmd"}}
    };
}

ToolResult DebugUiaTool::execute(const json& args) {
    if (!m_debugger) {
        if (m_uia != nullptr) {
            m_debugger = std::make_unique<UIADebugger>(*m_uia);
        } else {
            return err("UIAutomationScanner not available");
        }
    }

    std::string script;
    if (args.contains("cmd") && args.at("cmd").is_string()) {
        script = args.at("cmd").get<std::string>();
    } else if (args.contains("script") && args.at("script").is_string()) {
        script = args.at("script").get<std::string>();
    } else if (args.contains("command") && args.at("command").is_string()) {
        script = args.at("command").get<std::string>();
    } else if (args.contains("code") && args.at("code").is_string()) {
        script = args.at("code").get<std::string>();
    } else {
        return err("Missing 'cmd' parameter");
    }

    std::string outputCapture;
    m_debugger->setOutputCapture(&outputCapture);
    struct CaptureGuard {
        UIADebugger* dbg;
        explicit CaptureGuard(UIADebugger* d) : dbg(d) {}
        ~CaptureGuard() {
            if (dbg != nullptr) {
                dbg->setOutputCapture(nullptr);
            }
        }
        CaptureGuard(const CaptureGuard&) = delete;
        CaptureGuard& operator=(const CaptureGuard&) = delete;
        CaptureGuard(CaptureGuard&&) = delete;
        CaptureGuard& operator=(CaptureGuard&&) = delete;
    } guard(m_debugger.get());

    std::istringstream stream(script);
    std::string line;
    while (std::getline(stream, line)) {
        auto s = line.find_first_not_of(" \t\r\n");
        if (s == std::string::npos) {
            continue;
        }
        auto e = line.find_last_not_of(" \t\r\n");
        std::string trimmed = line.substr(s, e - s + 1);
        if (trimmed.empty() || trimmed.starts_with("#") || trimmed.starts_with("//")) {
            continue;
        }

        if (!m_debugger->execute(trimmed)) {
            break;
        }
    }

    if (outputCapture.empty()) {
        return ok("(success, no output)");
    }

    while (!outputCapture.empty() && (outputCapture.back() == '\n' || outputCapture.back() == '\r')) {
        outputCapture.pop_back();
    }

    return ok(outputCapture);
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
    if (m_luaRuntime == nullptr) {
        return err("LuaRuntime not available");
    }
    return m_luaRuntime->execString(args.value("code", ""));
}

// ── LuaRunTool ───────────────────────────────────────────────────────────────
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
    if (m_luaRuntime == nullptr) {
        return err("LuaRuntime not available");
    }
    return m_luaRuntime->execFile(args.value("path", ""));
}

ToolResult ReloadLuaToolsTool::execute(const json& /*args*/) {
    if (m_loader == nullptr) {
        return err("LuaToolLoader is not configured");
    }
    size_t count = m_loader->reload();
    auto tools = m_loader->loadedToolNames();
    std::string list;
    for (size_t i = 0; i < tools.size(); ++i) {
        if (i > 0) {
            list += ", ";
        }
        list += tools.at(i);
    }
    return ok(std::format("Reloaded {} Lua tool(s): [{}]", count, list));
}

// ── Self-Registration ────────────────────────────────────────────────────────
REGISTER_TOOL_WITH_DEPS(ListToolsTool, [](const ToolDependencies& d) {
    return std::make_unique<ListToolsTool>(d.registry);
});
REGISTER_TOOL(PingTool);
REGISTER_TOOL(EchoTool);
REGISTER_TOOL(VersionTool);
REGISTER_TOOL_WITH_DEPS(UiScanTool, [](const ToolDependencies& d) {
    return std::make_unique<UiScanTool>(d.uia);
});
REGISTER_TOOL_WITH_DEPS(UiScanWindowTool, [](const ToolDependencies& d) {
    return std::make_unique<UiScanWindowTool>(d.uia);
});
REGISTER_TOOL_WITH_DEPS(DebugUiaTool, [](const ToolDependencies& d) {
    return std::make_unique<DebugUiaTool>(d.uia);
});
REGISTER_TOOL_WITH_DEPS(LuaExecTool, [](const ToolDependencies& d) {
    return std::make_unique<LuaExecTool>(d.luaRuntime);
});
REGISTER_TOOL_WITH_DEPS(LuaRunTool, [](const ToolDependencies& d) {
    return std::make_unique<LuaRunTool>(d.luaRuntime);
});
REGISTER_TOOL_WITH_DEPS(ReloadLuaToolsTool, [](const ToolDependencies& d) {
    return std::make_unique<ReloadLuaToolsTool>(d.luaToolLoader);
});

void initBuiltinTools() {}

static void initAllTools() {
    initFileTools();
    initWindowTools();
    initInputTools();
    initScreenTools();
    initShellTools();
    initBrowserTools();
    initBuiltinTools();
}

// ── Registration Factory ─────────────────────────────────────────────────────
void BuiltinTools::registerAll(ToolRegistry& registry, const BuiltinToolDependencies& deps) {
    initAllTools();

    ToolDependencies td{
        .uia           = &deps.uia,
        .browser       = &deps.browser,
        .perms         = &deps.perms,
        .siteProfiles  = &deps.siteProfiles,
        .luaRuntime    = &deps.luaRuntime,
        .luaToolLoader = deps.luaToolLoader,
        .registry      = &registry
    };

    registry.registerSelfRegisteredTools(td);
}
