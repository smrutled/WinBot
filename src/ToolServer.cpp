#include "ToolServer.h"
#include "KillSwitch.h"
#include "tools/BrowserTools.h"
#include "tools/FileTools.h"
#include "tools/InputTools.h"
#include "tools/ShellTools.h"
#include "tools/WindowTools.h"
#include <chrono>
#include <iostream>
#include <thread>

static std::string toBase64(const std::vector<uint8_t> &data) {
  static constexpr char b64[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;
  encoded.reserve(((data.size() + 2) / 3) * 4);
  for (size_t i = 0; i < data.size(); i += 3) {
    uint32_t n = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < data.size())
      n |= static_cast<uint32_t>(data[i + 1]) << 8;
    if (i + 2 < data.size())
      n |= static_cast<uint32_t>(data[i + 2]);
    encoded += b64[(n >> 18) & 0x3F];
    encoded += b64[(n >> 12) & 0x3F];
    encoded += (i + 1 < data.size()) ? b64[(n >> 6) & 0x3F] : '=';
    encoded += (i + 2 < data.size()) ? b64[n & 0x3F] : '=';
  }
  return encoded;
}

// ──────────────────────────────────────────────────────────────────────────────
ToolServer::ToolServer(Config cfg, ToolRegistry &tools,
                       UIAutomationScanner &uia, ScreenCapture &capture,
                       BrowserAutomation &browser, PermissionSystem &perms,
                       AuditLog &audit, Memory &memory,
                       SiteProfileRegistry &siteProfiles,
                       LuaRuntime &luaRuntime)
    : m_cfg(cfg), m_tools(tools), m_uia(uia), m_capture(capture),
      m_browser(browser), m_perms(perms), m_audit(audit), m_memory(memory),
      m_siteProfiles(siteProfiles), m_luaRuntime(luaRuntime) {
  registerBuiltinTools();
}

// ──────────────────────────────────────────────────────────────────────────────
void ToolServer::sendResponse(const std::string &line) {
  std::cout << line << '\n';
  std::cout.flush();
}

// ──────────────────────────────────────────────────────────────────────────────
std::string ToolServer::observe() {
  auto treeResult = m_uia.scanFocusedWindow();
  if (treeResult) {
    int n = UIAutomationScanner::countInteractive(*treeResult);
    if (n > 0)
      return "## UI Tree (focused window):\n" +
             UIAutomationScanner::serialize(*treeResult);
    WINBOT_INFO(
        "ToolServer: UIA tree sparse ({} elements) — using screenshot fallback",
        n);
  }

  auto screen = ScreenCapture::captureDesktop();
  if (screen)
    return std::format("## Screenshot taken: {}x{} px", screen->width,
                       screen->height);

  return "(observation failed)";
}

// ──────────────────────────────────────────────────────────────────────────────
void ToolServer::run() {
  WINBOT_INFO("ToolServer: ready. Listening for JSON requests on stdin.");
  WINBOT_INFO(
      "ToolServer: send {{\"id\":1,\"tool\":\"list_tools\",\"args\":{{}}}} to "
      "discover tools.");

  std::string line;
  while (!KillSwitch::isTriggered() && std::getline(std::cin, line)) {
    auto req = protocol::parseRequest(line);
    if (!req) {
      // Empty line or parse error
      if (!req.error().empty())
        WINBOT_WARN("ToolServer: {}", req.error());
      continue;
    }

    WINBOT_INFO("ToolServer: [id={}] tool='{}' args={}", req->id, req->tool,
                req->args.dump());

    auto result =
        m_tools.dispatch(json{{"tool", req->tool}, {"args", req->args}});

    std::string response;
    if (result) {
      m_audit.record({utc_timestamp(), req->tool, "tool_call",
                      json{{"tool", req->tool}, {"args", req->args}}, "ok",
                      result->substr(0, 256)});
      response = protocol::makeOk(req->id, *result);
    } else {
      m_audit.record({utc_timestamp(), req->tool, "tool_call",
                      json{{"tool", req->tool}, {"args", req->args}}, "error",
                      result.error()});
      response = protocol::makeErr(req->id, result.error());
    }

    sendResponse(response);

    // Optional inter-action delay to avoid hammering the OS
    if (m_cfg.actionDelayMs > 0)
      std::this_thread::sleep_for(
          std::chrono::milliseconds(m_cfg.actionDelayMs));
  }

  WINBOT_INFO("ToolServer: stdin closed or kill-switch fired — shutting down.");
}

// ──────────────────────────────────────────────────────────────────────────────
void ToolServer::registerBuiltinTools() {
  using namespace tools;

  auto maybeRegister = [&](ToolRegistry::ToolDef def) {
    if (m_perms.isToolEnabled(def.name)) {
      m_tools.registerTool(std::move(def));
    }
  };

  // ── Discovery ────────────────────────────────────────────────────────────
  maybeRegister({"list_tools",
                 "Return the full JSON schema of all available tools",
                 {{"type", "object"}, {"properties", json::object()}},
                 [this](const json &) -> ToolResult {
                   return ok(m_tools.buildToolsSchema().dump());
                 }});

  // ── Perception ───────────────────────────────────────────────────────────
  maybeRegister({"ui_scan",
                 "Scan the UI Automation tree of the focused window. "
                 "Each element shows its bounds as (left,top,right,bottom) in "
                 "screen pixels — "
                 "pass those to screenshot_element to capture any element.",
                 {{"type", "object"}, {"properties", json::object()}},
                 [this](const json &) -> ToolResult { return ok(observe()); }});

  maybeRegister(
      {"ui_scan_window",
       "Scan the UI Automation tree of a specific window (case-insensitive "
       "title substring match). "
       "Each element shows its bounds as (left,top,right,bottom) in screen "
       "pixels — "
       "pass those to screenshot_element to capture any element.",
       {{"type", "object"},
        {"properties",
         {{"title",
           {{"type", "string"},
            {"description", "Window title substring (case-insensitive)"}}}}},
        {"required", {"title"}}},
       [this](const json &args) -> ToolResult {
         std::string title = args.value("title", "");
         auto tree = m_uia.scanWindow(title);
         if (!tree)
           return err(tree.error());
         return ok(UIAutomationScanner::serialize(*tree));
       }});

  maybeRegister(
      {"screenshot",
       "Capture the desktop and return the image as a base64-encoded PNG",
       {{"type", "object"}, {"properties", json::object()}},
       [](const json &) -> ToolResult {
         auto r = ScreenCapture::captureDesktop();
         if (!r)
           return err(r.error());
         json result = {{"width", r->width},
                        {"height", r->height},
                        {"format", "png"},
                        {"data", toBase64(r->pngBytes)}};
         return ok(result.dump());
       }});

  maybeRegister(
      {"screenshot_window",
       "Capture a specific window by title substring (case-insensitive, "
       "partial match OK). "
       "Uses UI Automation to locate the window so casing doesn't matter.",
       {{"type", "object"},
        {"properties",
         {{"title",
           {{"type", "string"},
            {"description", "Window title substring (case-insensitive)"}}}}},
        {"required", {"title"}}},
       [this](const json &args) -> ToolResult {
         std::string title = args.value("title", "");
         // Use UIA scanWindow for fuzzy case-insensitive HWND lookup
         auto tree = m_uia.scanWindow(title);
         if (!tree)
           return err(tree.error());
         HWND hwnd = tree->ownerHwnd;
         if (!hwnd)
           return err(
               std::format("Found UIA element for '{}' but no HWND", title));
         auto r = ScreenCapture::captureWindow(hwnd);
         if (!r)
           return err(r.error());
         json result = {{"width", r->width},
                        {"height", r->height},
                        {"format", "png"},
                        {"data", toBase64(r->pngBytes)}};
         return ok(result.dump());
       }});

  maybeRegister(
      {"screenshot_element",
       "Capture a specific UI element's screen region as a PNG. "
       "Get the bounds (left,top,right,bottom) from ui_scan or ui_scan_window "
       "output "
       "and pass them here to get a tight screenshot of just that element.",
       {{"type", "object"},
        {"properties",
         {{"left",
           {{"type", "integer"},
            {"description", "Left edge in screen pixels"}}},
          {"top",
           {{"type", "integer"}, {"description", "Top edge in screen pixels"}}},
          {"right",
           {{"type", "integer"},
            {"description", "Right edge in screen pixels"}}},
          {"bottom",
           {{"type", "integer"},
            {"description", "Bottom edge in screen pixels"}}}}},
        {"required", {"left", "top", "right", "bottom"}}},
       [](const json &args) -> ToolResult {
         RECT region{args.value("left", 0), args.value("top", 0),
                     args.value("right", 0), args.value("bottom", 0)};
         if (region.right <= region.left || region.bottom <= region.top)
           return err(
               "Invalid region: right must be > left and bottom must be > top");
         auto r = ScreenCapture::captureRegion(region);
         if (!r)
           return err(r.error());
         json result = {{"width", r->width},
                        {"height", r->height},
                        {"format", "png"},
                        {"data", toBase64(r->pngBytes)}};
         return ok(result.dump());
       }});

  maybeRegister({"get_window_list",
                 "List all visible windows with title and HWND",
                 {{"type", "object"}, {"properties", json::object()}},
                 [](const json &) { return getWindowList(); }});

  maybeRegister({"get_cursor_position",
                 "Get current mouse cursor position",
                 {{"type", "object"}, {"properties", json::object()}},
                 [](const json &) { return getCursorPosition(); }});

  // ── Mouse & Keyboard ─────────────────────────────────────────────────────
  maybeRegister(
      {"input_human_move",
       "Move the mouse to (x, y) using a human-like path (cubic Bézier curve with ease-in/out and jitter)",
       {{"type", "object"},
        {"properties",
         {{"x", {{"type", "integer"}, {"description", "Screen X coordinate"}}},
          {"y", {{"type", "integer"}, {"description", "Screen Y coordinate"}}}}},
        {"required", {"x", "y"}}},
       [](const json &a) {
         return humanMouseMove(a.value("x", 0), a.value("y", 0));
       }});

  maybeRegister(
      {"click",
       "Click the mouse at (x, y)",
       {{"type", "object"},
        {"properties",
         {{"x", {{"type", "integer"}, {"description", "Screen X coordinate"}}},
          {"y", {{"type", "integer"}, {"description", "Screen Y coordinate"}}},
          {"button",
           {{"type", "string"},
            {"description", "left|right|middle"},
            {"default", "left"}}}}},
        {"required", {"x", "y"}}},
       [](const json &a) {
         return click(a.value("x", 0), a.value("y", 0),
                      a.value("button", "left"));
       }});

  maybeRegister(
      {"double_click",
       "Double-click at (x, y)",
       {{"type", "object"},
        {"properties",
         {{"x", {{"type", "integer"}}}, {"y", {{"type", "integer"}}}}},
        {"required", {"x", "y"}}},
       [](const json &a) {
         return doubleClick(a.value("x", 0), a.value("y", 0));
       }});

  maybeRegister({"drag",
                 "Click-drag from (x1,y1) to (x2,y2)",
                 {{"type", "object"},
                  {"properties",
                   {{"x1", {{"type", "integer"}}},
                    {"y1", {{"type", "integer"}}},
                    {"x2", {{"type", "integer"}}},
                    {"y2", {{"type", "integer"}}}}},
                  {"required", {"x1", "y1", "x2", "y2"}}},
                 [](const json &a) {
                   return drag(a.value("x1", 0), a.value("y1", 0),
                               a.value("x2", 0), a.value("y2", 0));
                 }});

  maybeRegister({"scroll",
                 "Scroll the mouse wheel at (x, y)",
                 {{"type", "object"},
                  {"properties",
                   {{"x", {{"type", "integer"}}},
                    {"y", {{"type", "integer"}}},
                    {"delta",
                     {{"type", "integer"},
                      {"description", "Positive=up, negative=down"}}}}},
                  {"required", {"x", "y", "delta"}}},
                 [](const json &a) {
                   return scroll(a.value("x", 0), a.value("y", 0),
                                 a.value("delta", -3));
                 }});

  maybeRegister(
      {"type",
       "Type text using the keyboard",
       {{"type", "object"},
        {"properties",
         {{"text", {{"type", "string"}, {"description", "Text to type"}}}}},
        {"required", {"text"}}},
       [](const json &a) { return typeText(a.value("text", "")); }});

  maybeRegister(
      {"key",
       "Press a key or key combination (e.g. Ctrl+C, Enter, F5)",
       {{"type", "object"},
        {"properties",
         {{"combo",
           {{"type", "string"}, {"description", "e.g. Ctrl+C, Enter, F5"}}}}},
        {"required", {"combo"}}},
       [](const json &a) { return keyPress(a.value("combo", "Enter")); }});

  maybeRegister(
      {"focus_window",
       "Bring a window to the foreground by title substring",
       {{"type", "object"},
        {"properties",
         {{"title",
           {{"type", "string"}, {"description", "Window title substring"}}}}},
        {"required", {"title"}}},
       [](const json &a) { return focusWindow(a.value("title", "")); }});

  maybeRegister(
      {"close_window",
       "Close a window by title substring",
       {{"type", "object"},
        {"properties", {{"title", {{"type", "string"}}}}},
        {"required", {"title"}}},
       [](const json &a) { return closeWindow(a.value("title", "")); }});

  // ── Shell & Files ────────────────────────────────────────────────────────
  maybeRegister(
      {"run_command",
       "Run a PowerShell/cmd command and return stdout+stderr",
       {{"type", "object"},
        {"properties",
         {{"cmd", {{"type", "string"}, {"description", "Command to execute"}}},
          {"shell",
           {{"type", "string"},
            {"description", "Shell to use: cmd|powershell|pwsh"},
            {"default", "cmd"}}},
          {"timeout_ms", {{"type", "integer"}, {"default", 30000}}}}},
        {"required", {"cmd"}}},
       [this](const json &a) -> ToolResult {
         auto cmd = a.value("cmd", "");
         auto shell = a.value("shell", "cmd");
         auto check = m_perms.checkShellCommand(cmd);
         if (!check)
           return check;
         return runCommand(cmd, shell, a.value("timeout_ms", 30000));
       }});

  maybeRegister({"read_file",
                 "Read a file's text contents",
                 {{"type", "object"},
                  {"properties", {{"path", {{"type", "string"}}}}},
                  {"required", {"path"}}},
                 [this](const json &a) -> ToolResult {
                   auto check = m_perms.checkPath(a.value("path", ""));
                   if (!check)
                     return check;
                   return readFile(a.value("path", ""));
                 }});

  maybeRegister(
      {"write_file",
       "Write content to a file (creates or overwrites)",
       {{"type", "object"},
        {"properties",
         {{"path", {{"type", "string"}}}, {"content", {{"type", "string"}}}}},
        {"required", {"path", "content"}}},
       [this](const json &a) -> ToolResult {
         auto check = m_perms.checkPath(a.value("path", ""));
         if (!check)
           return check;
         return writeFile(a.value("path", ""), a.value("content", ""));
       }});

  maybeRegister(
      {"append_file",
       "Append content to a file",
       {{"type", "object"},
        {"properties",
         {{"path", {{"type", "string"}}}, {"content", {{"type", "string"}}}}},
        {"required", {"path", "content"}}},
       [this](const json &a) -> ToolResult {
         auto check = m_perms.checkPath(a.value("path", ""));
         if (!check)
           return check;
         return appendFile(a.value("path", ""), a.value("content", ""));
       }});

  maybeRegister(
      {"list_directory",
       "List files in a directory",
       {{"type", "object"},
        {"properties", {{"path", {{"type", "string"}}}}},
        {"required", {"path"}}},
       [](const json &a) { return listDirectory(a.value("path", ".")); }});

  maybeRegister({"delete_file",
                 "Delete a file (permission-checked)",
                 {{"type", "object"},
                  {"properties", {{"path", {{"type", "string"}}}}},
                  {"required", {"path"}}},
                 [this](const json &a) -> ToolResult {
                   auto check = m_perms.checkPath(a.value("path", ""));
                   if (!check)
                     return check;
                   auto confirm = PermissionSystem::promptUser(
                       std::format("Delete file '{}'?", a.value("path", "")));
                   if (!confirm)
                     return confirm;
                   return deleteFile(a.value("path", ""));
                 }});

  maybeRegister(
      {"copy_file",
       "Copy a file from src to dst",
       {{"type", "object"},
        {"properties",
         {{"src", {{"type", "string"}}}, {"dst", {{"type", "string"}}}}},
        {"required", {"src", "dst"}}},
       [](const json &a) {
         return copyFile(a.value("src", ""), a.value("dst", ""));
       }});

  // ── System ───────────────────────────────────────────────────────────────
  maybeRegister({"get_clipboard",
                 "Read current clipboard text",
                 {{"type", "object"}, {"properties", json::object()}},
                 [](const json &) { return getClipboard(); }});

  maybeRegister(
      {"set_clipboard",
       "Write text to the clipboard",
       {{"type", "object"},
        {"properties", {{"text", {{"type", "string"}}}}},
        {"required", {"text"}}},
       [](const json &a) { return setClipboard(a.value("text", "")); }});

  maybeRegister({"get_processes",
                 "List running processes (name + PID)",
                 {{"type", "object"}, {"properties", json::object()}},
                 [](const json &) { return getProcesses(); }});

  maybeRegister({"kill_process",
                 "Kill a process by name or PID",
                 {{"type", "object"},
                  {"properties", {{"name_or_pid", {{"type", "string"}}}}},
                  {"required", {"name_or_pid"}}},
                 [this](const json &a) -> ToolResult {
                   auto check =
                       m_perms.checkProcess(a.value("name_or_pid", ""));
                   if (!check)
                     return check;
                   return killProcess(a.value("name_or_pid", ""));
                 }});

  maybeRegister({"get_system_info",
                 "Get CPU, RAM, and disk usage info",
                 {{"type", "object"}, {"properties", json::object()}},
                 [](const json &) { return getSystemInfo(); }});

  // ── Web ──────────────────────────────────────────────────────────────────
  maybeRegister({"http_get",
                 "Make an HTTP GET request and return the body",
                 {{"type", "object"},
                  {"properties",
                   {{"url", {{"type", "string"}}},
                    {"headers", {{"type", "string"}, {"default", ""}}}}},
                  {"required", {"url"}}},
                 [](const json &a) {
                   return httpGet(a.value("url", ""), a.value("headers", ""));
                 }});

  maybeRegister(
      {"search_web",
       "Search DuckDuckGo and return the top result URLs + snippets",
       {{"type", "object"},
        {"properties", {{"query", {{"type", "string"}}}}},
        {"required", {"query"}}},
       [](const json &a) { return searchWeb(a.value("query", "")); }});

  // ── Browser (Chrome DevTools Protocol) ───────────────────────────────────
  maybeRegister({"browser_navigate",
                 "Navigate the browser to a URL",
                 {{"type", "object"},
                  {"properties", {{"url", {{"type", "string"}}}}},
                  {"required", {"url"}}},
                 [this](const json &a) {
                   return browserNavigate(m_browser, a.value("url", ""));
                 }});

  maybeRegister({"browser_click",
                 "Click a DOM element by CSS selector",
                 {{"type", "object"},
                  {"properties", {{"selector", {{"type", "string"}}}}},
                  {"required", {"selector"}}},
                 [this](const json &a) {
                   return browserClick(m_browser, a.value("selector", ""));
                 }});

  maybeRegister(
      {"browser_type",
       "Type text into a browser input field",
       {{"type", "object"},
        {"properties",
         {{"selector", {{"type", "string"}}}, {"text", {{"type", "string"}}}}},
        {"required", {"selector", "text"}}},
       [this](const json &a) {
         return browserType(m_browser, a.value("selector", ""),
                            a.value("text", ""));
       }});

  maybeRegister({"browser_get_dom",
                 "Get a simplified DOM of the current page",
                 {{"type", "object"}, {"properties", json::object()}},
                 [this](const json &) { return browserGetDom(m_browser); }});

  maybeRegister({"browser_get_page_text",
                 "Get the raw readable text of the current page",
                 {{"type", "object"}, {"properties", json::object()}},
                 [this](const json &) { return browserGetPageText(m_browser); }});

  maybeRegister({"browser_read_page",
                 "Navigate to a URL and extract its content. If a site profile exists, extracts structured data. Otherwise falls back to raw page text.",
                 {{"type", "object"},
                  {"properties", {{"url", {{"type", "string"}}}}},
                  {"required", {"url"}}},
                 [this](const json &a) -> ToolResult {
                     std::string url = a.value("url", "");
                     auto navRes = browserNavigate(m_browser, url);
                     if (!navRes) return navRes;
                     
                     ::Sleep(2000); // Wait for content to settle
                     
                     const SiteProfile* profile = m_siteProfiles.match(url);
                     if (profile) {
                         // Build extraction JS
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
                         return browserEval(m_browser, js);
                     }
                     
                     // Fallback
                     return browserGetPageText(m_browser);
                 }});

  maybeRegister({"browser_save_profile",
                 "Save a JSON site profile to the registry for structured extraction in browser_read_page.",
                 {{"type", "object"},
                  {"properties", 
                   {{"name", {{"type", "string"}, {"description", "File name (e.g. 'old_reddit')"}}},
                    {"profile", {{"type", "object"}, {"description", "JSON object with 'url_match' and 'extract_items' array"}}}
                   }},
                  {"required", {"name", "profile"}}},
                 [this](const json &a) {
                     return m_siteProfiles.saveProfile(a.value("name", ""), a.value("profile", json::object()));
                 }});

  maybeRegister({"browser_eval",
                 "Execute JavaScript in the browser and return result",
                 {{"type", "object"},
                  {"properties", {{"js", {{"type", "string"}}}}},
                  {"required", {"js"}}},
                 [this](const json &a) {
                   return browserEval(m_browser, a.value("js", ""));
                 }});

  // ── Persistent Memory (SQLite) ───────────────────────────────────────────
  maybeRegister(
      {"remember",
       "Save a key-value fact to persistent memory",
       {{"type", "object"},
        {"properties",
         {{"key", {{"type", "string"}}}, {"value", {{"type", "string"}}}}},
        {"required", {"key", "value"}}},
       [this](const json &a) -> ToolResult {
         return m_memory.store(a.value("key", ""), a.value("value", ""));
       }});

  maybeRegister({"recall",
                 "Retrieve a fact from persistent memory by key",
                 {{"type", "object"},
                  {"properties", {{"key", {{"type", "string"}}}}},
                  {"required", {"key"}}},
                 [this](const json &a) {
                   return m_memory.retrieve(a.value("key", ""));
                 }});

  maybeRegister({"recall_all",
                 "List all entries stored in persistent memory",
                 {{"type", "object"}, {"properties", json::object()}},
                 [this](const json &) -> ToolResult {
                   auto entries = m_memory.listAll();
                   std::string out;
                   for (const auto &[k, v, ts] : entries)
                     out += std::format("{} = {} (updated: {})\n", k, v, ts);
                   return ok(out.empty() ? "(no memories)" : out);
                 }});

  maybeRegister(
      {"forget",
       "Delete a memory entry by key",
       {{"type", "object"},
        {"properties", {{"key", {{"type", "string"}}}}},
        {"required", {"key"}}},
       [this](const json &a) { return m_memory.remove(a.value("key", "")); }});

  // ── Lua Scripting Tools ──────────────────────────────────────────────────
  maybeRegister({"lua_exec",
                 "Execute an inline Lua 5.4 script. The 'winbot' table is available globally for automation (navigate, click, evalJs, log, sleep, typeText, pressKey, readPage).",
                 {{"type", "object"},
                  {"properties", {{"code", {{"type", "string"}, {"description", "Raw Lua source code to execute"}}}}},
                  {"required", {"code"}}},
                 [this](const json &a) { return m_luaRuntime.execString(a.value("code", "")); }});

  maybeRegister({"lua_run",
                 "Execute a Lua script file from disk.",
                 {{"type", "object"},
                  {"properties", {{"path", {{"type", "string"}, {"description", "Absolute path to the .lua file"}}}}},
                  {"required", {"path"}}},
                 [this](const json &a) { return m_luaRuntime.execFile(a.value("path", "")); }});
}
