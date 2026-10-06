#include "Common.h"
#include "core/McpServer.h"
#include "core/ToolRegistry.h"
#include "core/ToolServer.h"
#include "platform/browser/BrowserAutomation.h"
#include "platform/uia/UIADebugger.h"
#include "platform/uia/UIAutomationScanner.h"
#include "security/AuditLog.h"
#include "security/KillSwitch.h"
#include "security/PermissionSystem.h"
#include "services/LuaRuntime.h"
#include "services/LuaToolLoader.h"
#include "services/SiteProfileRegistry.h"
#include "tools/BuiltinTools.h"


#include <filesystem>
#include <fstream>
#include <span>


// ── Load config.json ─────────────────────────────────────────────────────────
static json loadConfig(const std::filesystem::path &path) {
  if (!std::filesystem::exists(path)) {
    WINBOT_INFO("config.json not found at '{}' — creating defaults.",
                path.string());
    json def = {{"kill_hotkey", "Ctrl+Alt+X"},
                {"action_delay_ms", 200},
                {"browser_cdp_port", 9222},
                {"browser_exe", ""},
                {"lua_tools_dir", "data/tools"},
                {"mcp_servers", json::array()},
                {"scheduled_tasks", json::array()},
                {"permission",
                 {{"allowed_paths", {"%USERPROFILE%", "%APPDATA%", "%TEMP%"}},
                  {"blocked_paths",
                   {"C:\\Windows\\System32", "C:\\Windows\\SysWOW64",
                    "%USERPROFILE%\\.ssh", "%USERPROFILE%\\.aws"}},
                  {"blocked_processes",
                   {"winlogon.exe", "csrss.exe", "smss.exe", "lsass.exe",
                    "services.exe"}},
                  {"dangerous_cmd_patterns",
                   {"rm -rf", "format ", "del /f /s /q", "reg delete",
                    "diskpart", "bcdedit", "shutdown", "Stop-Computer"}},
                  {"confirm_shell_commands", true},
                  {"confirm_file_delete", true},
                  {"confirm_process_kill", true}}},
                {"disabled_tools", json::array()}};
    std::ofstream{path} << def.dump(2);
    return def;
  }
  std::ifstream file{path};
  try {
    return json::parse(file);
  } catch (const json::exception &e) {
    // A corrupt config must not take the whole server down via
    // std::terminate. Warn loudly and continue with built-in defaults.
    WINBOT_ERROR("config.json is invalid ({}). Continuing with defaults.",
                 e.what());
    return json::object();
  }
}

// ── Build PermissionSystem::Config from json
// ──────────────────────────────────
static PermissionSystem::Config buildPermConfig(const json &cfg) {
  PermissionSystem::Config perm;
  const json &p = cfg.value("permission", json::object());
  for (const auto &v : p.value("allowed_paths", json::array())) {
    perm.allowedPaths.push_back(v);
  }
  for (const auto &v : p.value("blocked_paths", json::array())) {
    perm.blockedPaths.push_back(v);
  }
  for (const auto &v : p.value("blocked_processes", json::array())) {
    perm.blockedProcesses.push_back(v);
  }
  for (const auto &v : p.value("dangerous_cmd_patterns", json::array())) {
    perm.dangerousCmdPatterns.push_back(v);
  }
  perm.confirmShellCommands = p.value("confirm_shell_commands", true);
  perm.confirmFileDelete = p.value("confirm_file_delete", true);
  perm.confirmProcessKill = p.value("confirm_process_kill", true);

  // Parse and expand disabled_tools
  for (const auto &v : cfg.value("disabled_tools", json::array())) {
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

// Returns the directory containing the WinBot executable
static std::filesystem::path getExecutableDir() {
  std::vector<wchar_t> buffer(MAX_PATH);
  DWORD len = ::GetModuleFileNameW(nullptr, buffer.data(),
                                   static_cast<DWORD>(buffer.size()));
  while (len == buffer.size()) {
    buffer.resize(buffer.size() * 2);
    len = ::GetModuleFileNameW(nullptr, buffer.data(),
                               static_cast<DWORD>(buffer.size()));
  }
  if (len > 0) {
    return std::filesystem::path(buffer.data()).parent_path();
  }
  return std::filesystem::current_path();
}

// Resolves a relative application path (e.g. config.json, data/tools,
// data/site_profiles)
static std::filesystem::path resolveAppPath(const std::filesystem::path &exeDir,
                                            const std::string &subPath) {
  std::filesystem::path p(subPath);
  if (p.is_absolute() && std::filesystem::exists(p)) {
    return p;
  }
  // 1. Relative to executable directory (e.g. build/bin/data/tools)
  if (std::filesystem::exists(exeDir / subPath)) {
    return exeDir / subPath;
  }
  // 2. WINBOT_ROOT environment variable (if set)
#ifdef _MSC_VER
  char *rawEnvBuf = nullptr;
  size_t envSz = 0;
  if (_dupenv_s(&rawEnvBuf, &envSz, "WINBOT_ROOT") == 0 && rawEnvBuf != nullptr) {
    std::unique_ptr<char, decltype(&std::free)> envBuf(rawEnvBuf, &std::free);
    std::filesystem::path root(envBuf.get());
    if (!root.empty() && std::filesystem::exists(root / subPath)) {
      return root / subPath;
    }
  }
#else
  if (const char *envRoot = std::getenv("WINBOT_ROOT")) {
    if (*envRoot != '\0' &&
        std::filesystem::exists(std::filesystem::path(envRoot) / subPath)) {
      return std::filesystem::path(envRoot) / subPath;
    }
  }
#endif
  // 3. Current working directory
  if (std::filesystem::exists(std::filesystem::current_path() / subPath)) {
    return std::filesystem::current_path() / subPath;
  }
  // 4. Traverse parent directories from exeDir (e.g. build/bin -> project root)
  std::filesystem::path parent = exeDir;
  for (int i = 0; i < 3; ++i) {
    parent = parent.parent_path();
    if (std::filesystem::exists(parent / subPath)) {
      return parent / subPath;
    }
  }
  return exeDir / subPath;
}

// ── Entry point
// ───────────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
  try {
    const std::span<char* const> args(argv, argv != nullptr ? static_cast<size_t>(argc) : 0);

    // ── High DPI Awareness ────────────────────────────────────────────────────
    // Without this, GetWindowRect and BitBlt will fail to return correct
    // coordinates or content on systems with display scaling (e.g. 150%).
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);

    // ── Check for --mcp mode ──────────────────────────────────────────────────
    // When running as an MCP subprocess, stdout must be clean JSON only.
    if (!args.empty()) {
      for (const auto* arg : args.subspan(1)) {
        if (std::string_view(arg) == "--mcp") {
          g_mcpMode = true;
          break;
        }
      }
    }

    if (!g_mcpMode) {
      (void)std::fputs("\n"
                 "__        __ _       ____        _\n"
                 "\\ \\      / /(_)_ __ | __ )  ___ | |_\n"
                 " \\ \\ /\\ / / | | '_ \\|  _ \\ / _ \\| __|\n"
                 "  \\ V  V /  | | | | | |_) | (_) | |_\n"
                 "   \\_/\\_/   |_|_| |_|____/ \\___/ \\__|\n"
                 "  Windows UI Automation Tool Server\n"
                 "  "
                 "\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500"
                 "\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500"
                 "\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500"
                 "\u2500\u2500\u2500\u2500\n"
                 "  Protocol: newline-delimited JSON over stdin/stdout\n"
                 "  Send  {\"id\":1,\"tool\":\"list_tools\",\"args\":{}} to "
                 "discover all tools.\n"
                 "\n",
                 stdout);
    }

    // ── Config ────────────────────────────────────────────────────────────────
    auto exeDir = getExecutableDir();
    auto configPath = resolveAppPath(exeDir, "config.json");
    auto cfg = loadConfig(configPath);
    auto dataDir = resolveAppPath(exeDir, "data");

    // ── Kill-switch ───────────────────────────────────────────────────────────
    std::string killHotkey = cfg.value("kill_hotkey", "Ctrl+Alt+X");
    KillSwitch::install(killHotkey);

    // ── Audit log ─────────────────────────────────────────────────────────────
    AuditLog auditLog{dataDir / "audit.log"};

    auto permCfg = buildPermConfig(cfg);
    permCfg.allowedPaths.push_back(exeDir.string());
    if (std::filesystem::exists(dataDir)) {
      permCfg.allowedPaths.push_back(dataDir.string());
    }

    auto siteProfilesDir = resolveAppPath(
        exeDir, cfg.value("site_profiles_dir", "data/site_profiles"));
    permCfg.allowedPaths.push_back(siteProfilesDir.string());

    auto luaToolsDir =
        resolveAppPath(exeDir, cfg.value("lua_tools_dir", "data/tools"));
    permCfg.allowedPaths.push_back(luaToolsDir.string());

    // In MCP mode, disable interactive prompts — they would deadlock since
    // stdin is controlled by the MCP bridge, not a human operator.
    if (g_mcpMode) {
      permCfg.confirmShellCommands = false;
      permCfg.confirmFileDelete = false;
      permCfg.confirmProcessKill = false;
    }
    PermissionSystem perms{permCfg};

    // ── UIA debug mode (developer helper, bypasses agent) ────────────────────
    if (args.size() > 1 && std::string_view(args.subspan(1).front()) == "--debug-uia") {
      WINBOT_INFO("Running in UI Automation Debug Mode");
      UIAutomationScanner uia;
      UIADebugger debugger{uia};
      debugger.run();
      KillSwitch::uninstall();
      return 0;
    }

    // ── Core services ─────────────────────────────────────────────────────────
    UIAutomationScanner uia;
    uia.setHumanMovement(cfg.value("human_movement", false));
    SiteProfileRegistry siteProfiles{siteProfilesDir};

    BrowserAutomation browser{cfg.value("browser_cdp_port", 9222),
                              cfg.value("browser_exe", "")};

    LuaRuntime luaRuntime{browser, uia, siteProfiles, &perms};

    // ── Tool registry + dynamic Lua tools + built-in tools registration ───────
    ToolRegistry tools;

    LuaToolLoader luaToolLoader{luaToolsDir, luaRuntime, tools};
    size_t loadedLuaCount = luaToolLoader.loadAll();
    WINBOT_INFO("Loaded {} custom Lua tool(s) from {}", loadedLuaCount,
                luaToolLoader.toolsDir().string());

    BuiltinTools::registerAll(tools, {.uia = uia,
                                      .browser = browser,
                                      .perms = perms,
                                      .siteProfiles = siteProfiles,
                                      .luaRuntime = luaRuntime,
                                      .luaToolLoader = &luaToolLoader});

    if (g_mcpMode) {
      // ── MCP mode: full JSON-RPC 2.0 / Model Context Protocol server ──────
      McpServer mcpServer{
          McpServer::Config{.serverName = "WinBot",
                            .serverVersion = "0.2.0",
                            .actionDelayMs = cfg.value("action_delay_ms", 200)},
          tools};
      auditLog.note("=== WinBot MCP session started ===");
      mcpServer.run();
    } else {
      // ── Legacy mode: custom WinBot JSON protocol ─────────────────────────
      ToolServer server{
          ToolServer::Config{.actionDelayMs = cfg.value("action_delay_ms", 200)},
          tools, auditLog};
      auditLog.note("=== WinBot session started ===");
      server.run(); // blocks until stdin closes or kill-switch fires
    }
    auditLog.note("=== WinBot session ended ===");

    KillSwitch::uninstall();
    WINBOT_INFO("Goodbye.");
    return 0;
  } catch (const std::exception &ex) {
    (void)std::fputs("[ERROR ] Fatal error in main: ", stderr);
    (void)std::fputs(ex.what(), stderr);
    (void)std::fputs("\n", stderr);
    return 1;
  } catch (...) {
    (void)std::fputs("[ERROR ] Unknown fatal error in main.\n", stderr);
    return 1;
  }
}
