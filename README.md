# WinBot

A high-performance Windows UI Automation and Desktop Control Tool Server built in modern C++23. WinBot enables AI agents to perceive, interact with, and automate standard Windows desktop environments through a built-in [Model Context Protocol (MCP)](https://modelcontextprotocol.io) server over stdio or an interactive CLI.

```
┌────────────────────────┐
│  AI Agent / MCP Client │
└───────────┬────────────┘
            │  MCP JSON-RPC 2.0 over stdio (Concurrent + Cancellation)
            ▼
┌────────────────────────┐
│   WinBot.exe --mcp     │
├────────────────────────┤
│  • ThreadPool (C++23)  │ ◄── Multiplexed async tool dispatch & stdout serialization
│  • Permission System   │ ◄── Path boundaries, PID resolution, command regex
│  • Tool Registry       │ ◄── 40+ Win32, UIA, Browser & Dynamic Lua tools
│  • Dynamic Tool Loader │ ◄── Hot-reloading & MCP tools/list_changed events
│  • Global Kill Switch  │ ◄── Emergency hotkey (Ctrl+Alt+X)
│  • Lua 5.5 Sandbox     │ ◄── Safe script automation with instruction & time caps
└────────────────────────┘
```

---

## Key Features

- **Windows UI Automation (UIA)**: Hierarchically scans and queries interactive controls, elements, buttons, input fields, and windows directly from the Windows OS UI tree.
- **Multimodal Visual Perception**: Captures high-resolution PNG screenshots of the entire desktop, targeted windows (even when partially occluded), or specific element bounds.
- **Human-Like Input Control**: Simulates mouse movement along natural cubic Bézier curves (with velocity easing and micro-jitter), multi-button clicks, click-and-drag operations, wheel scrolling, text input, and complex key combinations.
- **CDP Browser Automation**: Direct Chrome/Edge browser automation over Chrome DevTools Protocol (CDP). Supports navigation, DOM retrieval, JavaScript execution, and custom *Site Profiles* for structured data scraping.
- **Concurrent MCP Execution & Cooperative Cancellation**: Fully asynchronous multiplexed request handling powered by a lightweight C++23 `ThreadPool` (`std::jthread`, `std::move_only_function`). Supports live cooperative cancellation via MCP `notifications/cancelled` (`-32800 RequestCancelled`), stdout stream locking, and Windows Job Object sub-100ms process tree teardown.
- **Dynamic Lua Script Tools & Hot Reloading**: Drop `.lua` scripts into `data/tools/` to automatically register single-tool or multi-tool suites with custom JSON schemas without recompilation. Hot-reload tools at runtime via `reload_lua_tools` with automatic MCP `notifications/tools/list_changed` broadcast to connected agents.
- **Windows Calculator Suite**: Dedicated out-of-the-box calculator automation suite (`calc_calculate`, `calc_press`, `calc_get_display`, `calc_clear`) demonstrating fluent Lua UI automation scripting.
- **Sandboxed Lua Scripting**: Embedded Lua scripting engine with a sandboxed environment (`io`, `debug`, and dangerous `os` calls stripped), execution timeouts, instruction caps, and direct bindings into `winbot` native automation APIs and fluent `UIHandle` chaining.
- **Protected Filesystem & Shell**: File manipulation (read, write, append, copy, delete, list) and shell execution (CMD, PowerShell) gated by a defense-in-depth safety and permission system.
- **Global Kill Switch**: Low-level keyboard hook monitoring an emergency abort shortcut (`Ctrl+Alt+X` by default) to terminate operations instantly.
- **Audit Logging**: JSON-formatted invocation trail recorded in `data/audit.log`.

---

## Getting Started

### Prerequisites
- **Operating System**: Windows 10 or Windows 11 (x64)
- **Toolchain**: Visual Studio 2022+ with **Desktop development with C++** and **Windows 10/11 SDK**
- **Build System**: CMake 3.25+ and Ninja (recommended)

### Building WinBot

All third-party dependencies (`nlohmann/json`, `lua`, `luabridge`, `stb`, and `googletest`) are automatically managed via CMake's `FetchContent`.

1. **Configure build files**:
   ```cmd
   cmake -S . -B build -G Ninja -DCMAKE_CXX_STANDARD=23
   ```
2. **Compile the binary**:
   ```cmd
   cmake --build build --config Release
   ```
3. The resulting executable is located at `build/bin/Release/WinBot.exe` (or `build/bin/WinBot.exe`).

---

## Operating Modes

WinBot supports three primary execution modes:

### 1. MCP Server Mode (`--mcp`)
Runs as a headless child process communicating with an AI host (such as Claude Desktop, Open-WebUI, or custom MCP clients) via JSON-RPC 2.0 over standard I/O:
```cmd
WinBot.exe --mcp
```
* **Concurrent Request Dispatching**: Tool execution is completely decoupled from the `stdin` reading loop using a modern C++23 `ThreadPool` (`std::jthread` workers). Incoming requests execute concurrently; keepalive `ping` and discovery queries return immediately even while long-running operations are active.
* **Cooperative Cancellation**: Full support for MCP `notifications/cancelled`. In-flight tools receive an active `std::stop_token`. If cancelled, running commands are aborted immediately, child process trees are terminated via Windows Job Objects, and the standard MCP/JSON-RPC error code `-32800` (`RequestCancelled`) is returned.
* **Stream Integrity & Input Safety**: Stdout writes are synchronized via mutex to guarantee clean, non-interleaved JSON-RPC output under heavy concurrency. Low-level OS mouse and keyboard input actions are serialized via a recursive mutex, while filesystem, shell, browser, and system tools run concurrently without interference.
* **Dynamic Tool Change Notifications (`tools/list_changed`)**: Advertises `capabilities.tools.listChanged = true` during the MCP initialization handshake. Whenever dynamic Lua tools are added, updated, or reloaded, WinBot broadcasts `notifications/tools/list_changed` so AI agents can dynamically refresh their available tool definitions without server restarts.
* **Headless Security Guardrails**: `stdout` is reserved exclusively for JSON-RPC messages; all logging is redirected to `stderr`. Interactive console prompts are disabled. Security checks outside allowed paths or matching dangerous patterns are denied by default with explanatory errors rather than hanging on user prompts.

#### Connecting to an MCP Client
Add WinBot to your MCP client configuration (e.g., `claude_desktop_config.json`):
```json
{
  "mcpServers": {
    "winbot": {
      "command": "C:/path/to/WinBot.exe",
      "args": ["--mcp"]
    }
  }
}
```

### 2. Interactive CLI Mode
Running WinBot without `--mcp` launches the standalone interactive server:
```cmd
WinBot.exe
```
Accepts newline-delimited JSON commands on `stdin` and writes formatted output to `stdout`. Prompts for console confirmation (`yes`/`no`) on destructive actions outside allowed paths or matching dangerous command filters.

### 3. UI Automation Debug Mode (`--debug-uia`)
A developer-focused diagnostic REPL for inspecting and driving Windows UI elements without an active agent:
```cmd
WinBot.exe --debug-uia
```

---

## Security & Safety Architecture

WinBot is designed to safely execute autonomous LLM tasks while providing strict guardrails against prompt injection and model hallucination.

```
       Incoming Tool Call
              │
              ▼
   ┌──────────────────────┐
   │ Tool Name Whitelist  │ ── Is tool in disabled_tools? ──► [ Denied ]
   └──────────┬───────────┘
              │ Enabled
              ▼
   ┌──────────────────────┐
   │   PermissionSystem   │
   ├──────────────────────┤
   │ • checkPath()        │ ── Path canonicalization & subpath boundary check
   │ • checkProcess()     │ ── PID-to-image resolution & system process guard
   │ • checkShellCommand()│ ── Dangerous command regex & script heuristics
   │ • checkFileDelete()  │ ── Deletion authorization & confirmation gating
   └──────────┬───────────┘
              │ Passed
              ▼
   ┌──────────────────────┐
   │    Tool Execution    │
   └──────────┬───────────┘
              │
              ▼
   ┌──────────────────────┐
   │   data/audit.log     │ ◄── Audit logging
   └──────────────────────┘
```

### 1. Filesystem Guardrails (`checkPath`)
- **Strict Boundary Enforcing**: Uses component-boundary path checking (`isSubpathOrEqual`) to prevent prefix traversal bypasses (e.g. `C:\Allowed` will never inadvertently match `C:\Allowed_Other\secret.txt`).
- **Path Normalization**: Canonicalizes targets using `std::filesystem::weakly_canonical` with case-insensitive slash normalization (`/` and `\`).
- **Environment Variable Resolution**: Supports `%USERPROFILE%`, `%APPDATA%`, `%TEMP%`, etc., expanding variables before verification.
- **Coverage**: All file operations (`read_file`, `write_file`, `append_file`, `delete_file`, `copy_file` for both source and destination, and `list_directory`) are validated.
- **MCP Non-Interactive Policy**: If an LLM attempts to access paths outside `allowed_paths` in MCP mode, the operation is rejected immediately.

### 2. Process Termination Protection (`checkProcess`)
- **PID-to-Image Resolution**: Resolves numeric PIDs to executable names via Windows Toolhelp32 snapshots (`CreateToolhelp32Snapshot`). An LLM cannot bypass process blocklists by providing a PID rather than an executable name.
- **Kernel Process Immunity**: Critical system processes (PID `0` Idle, PID `4` System) and protected binaries (`winlogon.exe`, `csrss.exe`, `lsass.exe`, `smss.exe`, `services.exe`) are blocked from termination.
- **Extension Flexibility**: Automatically normalizes process names with or without the `.exe` extension (e.g., `winlogon` matches `winlogon.exe`).

### 3. Shell Command Safety (`checkShellCommand`)
Built-in and configurable case-insensitive regex patterns block catastrophic or destructive actions:
- **Disk Formatting & Partitioning**: `format`, `diskpart`
- **Recursive Wiping**: `rm -rf`, `rmdir /s /q`, `rd /s /q`, `del /f /s /q`, `Remove-Item -Recurse -Force`
- **System Configuration & Recovery Manipulation**: `bcdedit`, `vssadmin delete`, `reg delete`
- **System Power Actions**: `shutdown`, `Stop-Computer`, `Restart-Computer`
- **Obfuscated Invocations**: `powershell -enc`, `powershell -EncodedCommand`

### 4. Lua Sandbox & DoS Protection
- **Restricted Environment**: Strips `io`, `debug`, `dofile`, `loadfile`, `package.loadlib`, and `package.cpath`. Sanitizes `os` by stripping `os.execute`, `os.exit`, `os.remove`, `os.rename`, and `os.tmpname`.
- **Permission Boundaries**: Bridges `winbot.shell`, `winbot.readFile`, `winbot.writeFile`, and `LuaRuntime::execFile` through `PermissionSystem`.
- **Execution Timeouts & Instruction Limits**: Enforces a VM debug hook via `lua_sethook` with a per-execution context, terminating runaway scripts after a configurable timeout (default 10s) or instruction cap (default 5,000,000 instructions) to prevent infinite loops and denial-of-service.
- **Emergency Kill Switch Integration**: Continuously monitors the global `KillSwitch` inside hook callbacks to abort running loops immediately upon `Ctrl+Alt+X`.
- **Job Object Process Sandboxing**: Shell commands invoked via `winbot.shell` are assigned to Windows Job Objects (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`), ensuring entire child process trees terminate cleanly on timeout or cancellation without leaving orphaned child processes.

---

## Dynamic Lua Tools & Extensibility

WinBot features a dynamic tool loader that discovers, validates, and registers Lua script tools from the `data/tools/` directory (configured via `"lua_tools_dir"` in `config.json`). New tools and multi-tool suites can be added or updated without recompiling or restarting WinBot.

```
data/tools/
├── calculator.lua       # Full Windows Calculator automation suite (4 tools)
└── custom_tool.lua      # User-defined single or multi-tool scripts
```

### Tool Definition Formats

#### 1. Single Tool Definition
Return a single table defining the tool metadata and execution handler:
```lua
return {
    name = "greet_user",
    description = "Generates a personalized greeting message.",
    parameters = {
        type = "object",
        properties = {
            name = { type = "string", description = "User's name" }
        },
        required = { "name" }
    },
    execute = function(args)
        return { message = "Hello, " .. tostring(args.name) .. "!" }
    end
}
```

#### 2. Multi-Tool Suite Definition
Return an array of tool tables (or a table with a `tools` array) to bundle multiple tools in a single script:
```lua
local tools = {}

tools[1] = {
    name = "app_launch",
    description = "Launches an application by command.",
    parameters = {
        type = "object",
        properties = { cmd = { type = "string" } },
        required = { "cmd" }
    },
    execute = function(args)
        winbot.shell("start " .. args.cmd)
        return { status = "launched" }
    end
}

tools[2] = {
    name = "app_focus",
    description = "Focuses an application window.",
    parameters = {
        type = "object",
        properties = { title = { type = "string" } },
        required = { "title" }
    },
    execute = function(args)
        local win = winbot.waitForWindow(args.title, 3000)
        if not win then return nil, "Window not found" end
        return { status = "focused", title = win:name() }
    end
}

return tools
```

### Windows Calculator Suite (`data/tools/calculator.lua`)

WinBot ships with a comprehensive Windows Calculator automation suite out of the box:
- `calc_calculate`: Evaluates complete arithmetic expressions (e.g. `'12.5 + 7 * 2'`), drives Calculator buttons or keyboard inputs, and returns the parsed result.
- `calc_press`: Clicks specific calculator buttons by name (e.g. `'One'`, `'Plus'`, `'Two'`, `'Equals'`, `'Clear'`).
- `calc_get_display`: Reads and formats the current value from the Calculator display.
- `calc_clear`: Clears the Calculator display and computation state.

### Fluent `UIHandle` Chaining

Lua scripts have access to fluent UI element navigation and chaining:
```lua
local win = winbot.waitForWindow("Calculator", 4000)
if win then
    -- Chain element selection and actions
    win:select("Clear", 500):click()
    win:select("One", 1000):click()
    win:select("Plus", 1000):click()
    win:select("Two", 1000):click()
    win:select("Equals", 1000):click()

    -- Inspect element state
    local result = win:select("CalculatorResults", 1000)
    if result then
        winbot.log("Result: " .. result:name())
    end
end
```

Available `UIHandle` methods:
- **Actions**: `:click()`, `:waitClick(name, [timeoutMs], [controlType])`, `:clickChild(name, [controlType])`, `:type(text)`, `:key(key)`, `:wait(ms)`
- **Navigation**: `:select(name, [timeoutMs], [controlType])`, `:find(name, [timeoutMs], [controlType])` (safe non-throwing lookup), `:parent()`, `:window()`, `:refresh()`
- **Properties**: `:name()`, `:value()`, `:text()`, `:controlType()`, `:automationId()`, `:isEnabled()`

### Native `winbot` Lua APIs

- `winbot.waitForWindow(title, [timeoutMs])` — Wait for top-level window by title substring.
- `winbot.selectUIA(name, [timeoutMs], [controlType])` — Query element in focused window.
- `winbot.click(x, y, [button], [human])` — Mouse click with optional Bézier movement.
- `winbot.typeText(text)` / `winbot.pressKey(key)` — Direct keyboard simulation.
- `winbot.shell(cmd, [timeoutMs])` — Gated shell execution inside a Windows Job Object.
- `winbot.readFile(path)` / `winbot.writeFile(path, content)` — Path-checked filesystem I/O.
- `winbot.sleep(ms)` / `winbot.log(msg)` — Script delays and structured logging.
- `winbot.navigate(url)` / `winbot.evalJs(js)` / `winbot.readPage(url)` — CDP browser operations.

### Hot Reloading & MCP Dynamic Synchronization

- Run the `reload_lua_tools` tool to hot-reload all Lua tools from disk at runtime.
- `ToolRegistry` handles dynamic tool addition, replacement, and unregistration using `std::shared_mutex` for lock-free concurrent execution during normal operation.
- In MCP mode, tool reloads automatically broadcast `notifications/tools/list_changed` to connected clients, allowing AI models to immediately use newly defined tools without server restarts.

---

## Configuration (`config.json`)

On startup, WinBot reads or generates `config.json` next to the binary:

```json
{
  "kill_hotkey": "Ctrl+Alt+X",
  "action_delay_ms": 200,
  "browser_cdp_port": 9222,
  "browser_exe": "",
  "lua_tools_dir": "data/tools",
  "mcp_servers": [],
  "scheduled_tasks": [],
  "permission": {
    "allowed_paths": [
      "%USERPROFILE%",
      "%APPDATA%",
      "%TEMP%"
    ],
    "blocked_paths": [
      "C:\\Windows\\System32",
      "C:\\Windows\\SysWOW64",
      "%USERPROFILE%\\.ssh",
      "%USERPROFILE%\\.aws"
    ],
    "blocked_processes": [
      "winlogon.exe",
      "csrss.exe",
      "smss.exe",
      "lsass.exe",
      "services.exe"
    ],
    "dangerous_cmd_patterns": [
      "rm -rf",
      "format ",
      "del /f /s /q",
      "reg delete",
      "diskpart",
      "bcdedit",
      "shutdown",
      "Stop-Computer"
    ],
    "confirm_shell_commands": true,
    "confirm_file_delete": true,
    "confirm_process_kill": true
  },
  "disabled_tools": []
}
```

### Disabling Tool Groups
You can disable individual tools or entire functional tool categories using `disabled_tools`:

| Group Alias | Disabled Tools |
|---|---|
| `"shell"` | `run_command` |
| `"files"` | `read_file`, `write_file`, `append_file`, `list_directory`, `delete_file`, `copy_file` |
| `"browser"` | `browser_navigate`, `browser_click`, `browser_type`, `browser_get_dom`, `browser_eval` |
| `"input"` | `click`, `double_click`, `drag`, `scroll`, `type`, `key` |
| `"screen"` | `screenshot`, `screenshot_window`, `screenshot_element` |
| `"windows"` | `focus_window`, `close_window`, `get_window_list`, `ui_scan`, `ui_scan_window` |
| `"process"` | `kill_process`, `get_processes` |
| `"web"` | `http_get`, `search_web` |
| `"system"` | `get_system_info`, `get_cursor_position` |

---

## Available Tools (40+)

| Category | Tool | Description |
|---|---|---|
| **Perception** | `ui_scan` | Scan the UI Automation tree of the focused window |
| | `ui_scan_window` | Scan the UI Automation tree of a specific window by title |
| | `debug_uia` | Execute interactive UIADebugger script commands |
| | `screenshot` | Capture desktop display as base64-encoded PNG |
| | `screenshot_window` | Capture a specific window by title (works even if occluded) |
| | `screenshot_element` | Capture bounded region for an identified element |
| | `get_window_list` | Enumerate visible top-level windows and HWNDs |
| | `get_cursor_position`| Query current cursor screen coordinates |
| **Mouse & Keyboard** | `input_human_move` | Move cursor along a natural cubic Bézier curve |
| | `click` | Perform mouse click (left/right/middle) at (x, y) |
| | `double_click` | Double-click mouse button at (x, y) |
| | `drag` | Click-drag from (x1, y1) to (x2, y2) |
| | `scroll` | Scroll mouse wheel at coordinates |
| | `type` | Send sequential keyboard text input |
| | `key` | Send individual key or modifier combo (e.g. `Ctrl+C`, `Enter`) |
| | `focus_window` | Bring target window to foreground by title |
| | `close_window` | Send WM_CLOSE to window by title |
| **Shell & Files** | `run_command` | Execute shell command via CMD or PowerShell (Job Object isolated, cancelable) |
| | `read_file` | Read text file contents within allowed paths |
| | `write_file` | Create or overwrite file within allowed paths |
| | `append_file` | Append text data to file within allowed paths |
| | `list_directory` | Enumerate files/folders in directory |
| | `delete_file` | Remove file (permission & confirmation checked) |
| | `copy_file` | Copy file from source to destination |
| **System** | `get_clipboard` | Read text from system clipboard |
| | `set_clipboard` | Write text to system clipboard |
| | `get_processes` | Enumerate running processes and PIDs |
| | `kill_process` | Terminate process by PID or image name |
| | `get_system_info` | Query system CPU, RAM, and hardware metrics |
| **Web** | `http_get` | Issue HTTP GET request |
| | `search_web` | Perform DuckDuckGo web search |
| **Browser (CDP)** | `browser_navigate` | Navigate attached browser to target URL |
| | `browser_click` | Click element matching CSS selector |
| | `browser_type` | Type text into input matching CSS selector |
| | `browser_get_dom` | Retrieve simplified DOM hierarchy |
| | `browser_get_page_text`| Extract visible page text content |
| | `browser_read_page` | Extract structured data using matching *Site Profile* |
| | `browser_save_profile` | Register new *Site Profile* for target domain |
| | `browser_eval` | Execute JavaScript in browser page context |
| **Calculator (Lua)** | `calc_calculate` | Evaluates arithmetic expression on Windows Calculator (e.g. `'12.5 + 7 * 2'`) |
| | `calc_press` | Clicks specific calculator button by name (e.g. `'One'`, `'Plus'`, `'Equals'`) |
| | `calc_get_display` | Reads and parses current value from Windows Calculator display |
| | `calc_clear` | Clears Windows Calculator display and calculation state |
| **Lua Scripting** | `lua_exec` | Execute inline Lua script in sandboxed runtime |
| | `lua_run` | Run external `.lua` script file from disk |
| | `reload_lua_tools` | Hot-reload dynamic Lua tools from `data/tools` and broadcast MCP `list_changed` |
| **Utilities** | `list_tools` | Discover all registered tools and JSON schemas |
| | `ping` | Health-check returning pong |
| | `echo` | Echo payload message back |
| | `version` | Query server version and build timestamp |

---

## Running Tests

WinBot includes an automated test suite with 98 unit and integration tests covering UI automation, security guardrails, concurrency, cooperative cancellation, dynamic Lua scripting, and process sandboxing.

Run all tests via CTest:
```cmd
ctest --test-dir build --output-on-failure
```

To run only the unit test suite without live GUI window interactions:
```cmd
ctest --test-dir build --output-on-failure -E LiveGUI
```

To target specific test suites directly using the test binary:
```cmd
.\build\bin\WinBot_Tests.exe --gtest_filter=McpServerConcurrencyTest.*
.\build\bin\WinBot_Tests.exe --gtest_filter=PermissionSystemTest.*
.\build\bin\WinBot_Tests.exe --gtest_filter=LuaToolLoaderTest.*
.\build\bin\WinBot_Tests.exe --gtest_filter=LuaRuntimeTest.*
.\build\bin\WinBot_Tests.exe --gtest_filter=ShellToolsTest.*
```

