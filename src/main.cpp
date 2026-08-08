#include "Common.h"
#include "KillSwitch.h"
#include "AuditLog.h"
#include "PermissionSystem.h"
#include "UIAutomationScanner.h"
#include "UIADebugger.h"
#include "ScreenCapture.h"
#include "Memory.h"
#include "VoiceIO.h"
#include "ToolRegistry.h"
#include "BrowserAutomation.h"
#include "ToolServer.h"
#include "SiteProfileRegistry.h"
#include "LuaRuntime.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>

// ── Load config.json ─────────────────────────────────────────────────────────
static json loadConfig(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        WINBOT_INFO("config.json not found at '{}' — creating defaults.", path.string());
        json def = {
            {"kill_hotkey",        "Ctrl+Alt+X"},
            {"action_delay_ms",    200},
            {"voice_input",        false},
            {"voice_output",       false},
            {"wake_word",          "hey winbot"},
            {"browser_cdp_port",   9222},
            {"browser_exe",        ""},
            {"mcp_servers",        json::array()},
            {"scheduled_tasks",    json::array()},
            {"permission", {
                {"allowed_paths",          {"%USERPROFILE%", "%APPDATA%", "%TEMP%"}},
                {"blocked_paths",          {"C:\\Windows\\System32", "C:\\Windows\\SysWOW64"}},
                {"blocked_processes",      {"winlogon.exe","csrss.exe","smss.exe","lsass.exe"}},
                {"dangerous_cmd_patterns", {"rm -rf","format ","del /f /s /q","reg delete"}},
                {"confirm_shell_commands", true},
                {"confirm_file_delete",    true},
                {"confirm_process_kill",   true}
            }},
            {"disabled_tools", json::array()}
        };
        std::ofstream{ path } << def.dump(2);
        return def;
    }
    std::ifstream file{ path };
    return json::parse(file);
}

// ── Build PermissionSystem::Config from json ──────────────────────────────────
static PermissionSystem::Config buildPermConfig(const json& cfg) {
    PermissionSystem::Config perm;
    const json& p = cfg.value("permission", json::object());
    for (const auto& v : p.value("allowed_paths",          json::array())) perm.allowedPaths.push_back(v);
    for (const auto& v : p.value("blocked_paths",          json::array())) perm.blockedPaths.push_back(v);
    for (const auto& v : p.value("blocked_processes",      json::array())) perm.blockedProcesses.push_back(v);
    for (const auto& v : p.value("dangerous_cmd_patterns", json::array())) perm.dangerousCmdPatterns.push_back(v);
    perm.confirmShellCommands = p.value("confirm_shell_commands", true);
    perm.confirmFileDelete    = p.value("confirm_file_delete",    true);
    perm.confirmProcessKill   = p.value("confirm_process_kill",   true);

    // Parse and expand disabled_tools
    for (const auto& v : cfg.value("disabled_tools", json::array())) {
        std::string item = v.get<std::string>();
        if (item == "shell") {
            perm.disabledTools.insert("run_command");
        } else if (item == "files") {
            perm.disabledTools.insert("read_file");
            perm.disabledTools.insert("write_file");
            perm.disabledTools.insert("append_file");
            perm.disabledTools.insert("list_directory");
            perm.disabledTools.insert("delete_file");
            perm.disabledTools.insert("copy_file");
        } else if (item == "browser") {
            perm.disabledTools.insert("browser_navigate");
            perm.disabledTools.insert("browser_click");
            perm.disabledTools.insert("browser_type");
            perm.disabledTools.insert("browser_get_dom");
            perm.disabledTools.insert("browser_eval");
        } else if (item == "input") {
            perm.disabledTools.insert("click");
            perm.disabledTools.insert("double_click");
            perm.disabledTools.insert("drag");
            perm.disabledTools.insert("scroll");
            perm.disabledTools.insert("type");
            perm.disabledTools.insert("key");
        } else if (item == "screen") {
            perm.disabledTools.insert("screenshot");
            perm.disabledTools.insert("screenshot_window");
            perm.disabledTools.insert("screenshot_element");
        } else if (item == "windows") {
            perm.disabledTools.insert("focus_window");
            perm.disabledTools.insert("close_window");
            perm.disabledTools.insert("get_window_list");
            perm.disabledTools.insert("ui_scan");
            perm.disabledTools.insert("ui_scan_window");
        } else if (item == "process") {
            perm.disabledTools.insert("kill_process");
            perm.disabledTools.insert("get_processes");
        } else if (item == "memory") {
            perm.disabledTools.insert("remember");
            perm.disabledTools.insert("recall");
            perm.disabledTools.insert("recall_all");
            perm.disabledTools.insert("forget");
        } else if (item == "web") {
            perm.disabledTools.insert("http_get");
            perm.disabledTools.insert("search_web");
        } else if (item == "system") {
            perm.disabledTools.insert("get_system_info");
            perm.disabledTools.insert("get_cursor_position");
        } else {
            perm.disabledTools.insert(item);
        }
    }

    return perm;
}

// ── Entry point ───────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    // ── High DPI Awareness ────────────────────────────────────────────────────
    // Without this, GetWindowRect and BitBlt will fail to return correct
    // coordinates or content on systems with display scaling (e.g. 150%).
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);

    // ── Check for --mcp mode ──────────────────────────────────────────────────
    // When running as an MCP subprocess, stdout must be clean JSON only.
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--mcp") {
            g_mcpMode = true;
            break;
        }
    }

    if (!g_mcpMode) {
        std::fputs(
            "\n"
            "__        __ _       ____        _\n"
            "\\ \\      / /(_)_ __ | __ )  ___ | |_\n"
            " \\ \\ /\\ / / | | '_ \\|  _ \\ / _ \\| __|\n"
            "  \\ V  V /  | | | | | |_) | (_) | |_\n"
            "   \\_/\\_/   |_|_| |_|____/ \\___/ \\__|\n"
            "  Windows UI Automation Tool Server\n"
            "  \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\n"
            "  Protocol: newline-delimited JSON over stdin/stdout\n"
            "  Send  {\"id\":1,\"tool\":\"list_tools\",\"args\":{}} to discover all tools.\n"
            "\n",
            stdout
        );
    }

    // ── Config ────────────────────────────────────────────────────────────────
    auto exeDir = std::filesystem::current_path();
    auto cfg    = loadConfig(exeDir / "config.json");

    // ── Kill-switch ───────────────────────────────────────────────────────────
    std::string killHotkey = cfg.value("kill_hotkey", "Ctrl+Alt+X");
    KillSwitch::install(killHotkey);

    // ── Audit log ─────────────────────────────────────────────────────────────
    AuditLog auditLog{ exeDir / "data" / "audit.log" };

    // ── Permission system ─────────────────────────────────────────────────────
    auto permCfg = buildPermConfig(cfg);
    // In MCP mode, disable interactive prompts — they would deadlock since
    // stdin is controlled by the MCP bridge, not a human operator.
    if (g_mcpMode) {
        permCfg.confirmShellCommands = false;
        permCfg.confirmFileDelete    = false;
        permCfg.confirmProcessKill   = false;
    }
    PermissionSystem perms{ permCfg };

    // ── UIA debug mode (developer helper, bypasses agent) ────────────────────
    if (argc > 1 && std::string_view(argv[1]) == "--debug-uia") {
        WINBOT_INFO("Running in UI Automation Debug Mode");
        UIAutomationScanner uia;
        UIADebugger debugger{ uia };
        debugger.run();
        KillSwitch::uninstall();
        return 0;
    }

    // ── Core services ─────────────────────────────────────────────────────────
    UIAutomationScanner uia;
    uia.setHumanMovement(cfg.value("human_movement", false));
    ScreenCapture       capture;
    Memory              memory{ exeDir / "data" / "memory.db" };
    SiteProfileRegistry siteProfiles{ exeDir / cfg.value("site_profiles_dir", "data/site_profiles") };

    // In MCP mode, never enable voice — no console for audio I/O
    VoiceIO voice{{
        .enableInput  = g_mcpMode ? false : cfg.value("voice_input",  false),
        .enableOutput = g_mcpMode ? false : cfg.value("voice_output", false),
        .wakeWord     = cfg.value("wake_word",    "hey winbot")
    }};

    BrowserAutomation browser{
        cfg.value("browser_cdp_port", 9222),
        cfg.value("browser_exe",      "")
    };

    LuaRuntime luaRuntime{ browser, uia, siteProfiles };

    // ── Tool registry + server ────────────────────────────────────────────────
    ToolRegistry tools;
    ToolServer server{
        ToolServer::Config{ 
            .actionDelayMs = cfg.value("action_delay_ms", 200),
            .humanMovement = cfg.value("human_movement", false)
        },
        tools, uia, capture, browser, perms, auditLog, memory, siteProfiles, luaRuntime
    };

    // ── Optional voice listener thread ────────────────────────────────────────
    // When voice is enabled, spoken commands are forwarded as special "voice"
    // tool-call log entries so the external agent can react to them.
    std::jthread voiceThread;
    if (voice.inputEnabled()) {
        voiceThread = std::jthread([&](std::stop_token st) {
            while (!st.stop_requested() && !KillSwitch::isTriggered()) {
                std::string cmd = voice.listenForCommand();
                if (!cmd.empty()) {
                    WINBOT_INFO("Voice command received: {}", cmd);
                    auditLog.note(std::format("Voice: {}", cmd));
                    // Emit a special notification line on stdout so the agent
                    // can pick it up asynchronously.
                    std::cout << json{{"event","voice"},{"text",cmd}}.dump() << '\n';
                    std::cout.flush();
                }
            }
        });
    }

    // ── Run ───────────────────────────────────────────────────────────────────
    auditLog.note("=== WinBot session started ===");
    server.run();   // blocks until stdin closes or kill-switch fires
    auditLog.note("=== WinBot session ended ===");

    KillSwitch::uninstall();
    WINBOT_INFO("Goodbye.");
    return 0;
}
