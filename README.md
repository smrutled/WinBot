# WinBot

A high-performance Windows UI Automation and Desktop Control Tool Server built in modern C++23. WinBot enables AI agents (such as Claude, Gemini, etc.) to perceive, interact with, and automate standard Windows desktop environments through a simple JSON-over-stdio RPC protocol.

It includes both **Python** and **TypeScript** wrappers that bridge it to the standard [Model Context Protocol (MCP)](https://modelcontextprotocol.io) specification.

```
[ AI Agent / MCP Client ]
         │
         ▼ (MCP JSON-RPC over stdio)
[ mcp/winbot_mcp.py ] or [ mcp-ts/src/index.ts ]
         │
         ▼ (Simple JSON over stdio, with --mcp flag)
[ WinBot.exe ] (Compiled C++23 Server)
```

## Features

- **Windows UI Automation (UIA)**: Hierarchically scans and queries interactive controls, elements, buttons, input fields, and windows directly from the Windows OS UI tree.
- **Multimodal Visual Capture**: Captures PNG screenshots of the entire desktop, targeted windows, or specific element bounds for visual reasoning by downstream AI models.
- **Human-like Input Control**: Drives mouse movement using natural cubic Bézier curves (with easing and jitter), clicks (left/right/middle), click-and-drag operations, scrolling, text typing, and keyboard hotkey execution.
- **CDP-based Browser Automation**: Controls Chrome or Edge over Chrome DevTools Protocol (CDP). Supports navigating, DOM extraction, JavaScript execution, and custom *Site Profiles* for structured page data extraction.
- **Embedded Lua Scripting**: Executes Lua 5.4 scripts directly inside the WinBot engine. Scripts have full access to high-speed automation API functions via a global `winbot` namespace.
- **File & Shell Execution**: Runs PowerShell or CMD commands. Provides standard file APIs (read, write, append, copy, delete, list) protected by a permission manager.
- **SQLite Persistent Memory**: Stores key-value facts across agent runs in `data/memory.db`.
- **Built-in Security**:
  - **Global Kill Switch**: Monitors a system-wide hotkey (`Ctrl+Alt+X` by default) to immediately abort and exit.
  - **Permission Bounding**: Configurable path restrictions (e.g., user directory only), process blocklists (system processes), and dangerous shell patterns.
  - **Audit Logging**: Keeps a record of all invocations in `data/audit.log`.

---

## Getting Started

### Prerequisites
- Windows 10 or 11 with Desktop UI
- Visual Studio 2022+ with **Desktop development with C++** and **Windows SDK** workloads
- CMake 3.25+

### Building WinBot
Building WinBot fetches third-party libraries (nlohmann/json, SQLite, Lua, LuaBridge3, and stb) automatically.

1. **Configure with CMake**:
   ```cmd
   cmake -S . -B build -G Ninja -DCMAKE_CXX_STANDARD=23
   ```
2. **Build the executable**:
   ```cmd
   cmake --build build --config Release
   ```
This compiles the C++ binary to `build/bin/Release/WinBot.exe`.

---

## Operating as an MCP Server

WinBot includes official bridges to translate standard MCP tool calls into its native protocol.

### Option A: Python Bridge
1. Install dependency:
   ```cmd
   pip install -r mcp/requirements.txt
   ```
2. Test using the MCP Inspector:
   ```cmd
   npx @modelcontextprotocol/inspector python mcp/winbot_mcp.py
   ```

### Option B: TypeScript Bridge
1. Install and compile:
   ```cmd
   cd mcp-ts
   npm install
   npm run build
   ```
2. Test using the MCP Inspector:
   ```cmd
   npx @modelcontextprotocol/inspector node dist/index.js
   ```

---

## Connecting to AI Clients

### 1. Claude Desktop
Add the following to your Claude Desktop config at `%APPDATA%\Claude\claude_desktop_config.json`:

Using Python:
```json
{
  "mcpServers": {
    "winbot": {
      "command": "python",
      "args": ["C:/Projects/github/WinBot/mcp/winbot_mcp.py"]
    }
  }
}
```

Using Node (TypeScript):
```json
{
  "mcpServers": {
    "winbot": {
      "command": "node",
      "args": ["C:/Projects/github/WinBot/mcp-ts/dist/index.js"]
    }
  }
}
```
*(Make sure to use forward slashes `/` and provide the absolute path to your repository).*

### 2. Claude Code (CLI)
```cmd
claude mcp add winbot -- python C:/Projects/github/WinBot/mcp/winbot_mcp.py
```

---

## Available Tools (30+)

WinBot registers the following tools automatically:

| Category | Tool | Description |
|---|---|---|
| **Perception** | `ui_scan` | Scan the UI Automation tree of the focused window |
| | `ui_scan_window` | Scan the UI Automation tree of a specific window by title |
| | `screenshot` | Capture the desktop as base64-encoded PNG |
| | `screenshot_window` | Capture a specific window by title |
| | `screenshot_element` | Capture a tight bounding region as PNG |
| | `get_window_list` | List all visible window titles and HWNDs |
| | `get_cursor_position`| Get the current mouse coordinates |
| **Mouse & Keyboard** | `input_human_move` | Move mouse to (x, y) with human-like Bézier curve |
| | `click` | Click mouse button (left/right/middle) at (x, y) |
| | `double_click` | Double-click mouse at (x, y) |
| | `drag` | Click-drag from (x1, y1) to (x2, y2) |
| | `scroll` | Scroll mouse wheel at (x, y) |
| | `type` | Type specified text using keyboard |
| | `key` | Press a key or key combo (e.g., `Ctrl+C`, `Enter`, `F5`) |
| | `focus_window` | Bring a window to foreground by title |
| | `close_window` | Close a window by title |
| **Shell & Files** | `run_command` | Execute shell command (CMD/PowerShell) |
| | `read_file` | Read text file contents |
| | `write_file` | Create/overwrite file with contents |
| | `append_file` | Append contents to file |
| | `list_directory` | List files/subdirectories in folder |
| | `delete_file` | Delete a path (permission-checked) |
| | `copy_file` | Copy file from source to destination |
| **System** | `get_clipboard` | Read text from system clipboard |
| | `set_clipboard` | Write text to system clipboard |
| | `get_processes` | List running processes with PID |
| | `kill_process` | Terminate process by PID or name |
| | `get_system_info` | Get current CPU, RAM, and disk utilization |
| **Web** | `http_get` | Make HTTP GET request |
| | `search_web` | Query DuckDuckGo web results |
| **Browser (CDP)** | `browser_navigate` | Navigate browser instance to URL |
| | `browser_click` | Click DOM element via CSS selector |
| | `browser_type` | Type text in input element |
| | `browser_get_dom` | Get simplified DOM structure |
| | `browser_get_page_text`| Extract visible page text |
| | `browser_read_page` | Extract structured data using matching *Site Profile* |
| | `browser_save_profile` | Register custom *Site Profile* for structured scraping |
| | `browser_eval` | Execute arbitrary JS in the active tab |
| **Memory** | `remember` | Store fact key-value pair in SQLite database |
| | `recall` | Retrieve memory entry by key |
| | `recall_all` | List all stored facts |
| | `forget` | Delete fact by key |
| **Lua Scripting** | `lua_exec` | Run inline Lua 5.4 automation script |
| | `lua_run` | Execute Lua automation file from disk |

---

## Configuration (`config.json`)

On startup, WinBot will read or generate `config.json` inside its execution directory. You can configure:
- `kill_hotkey`: Keyboard shortcut to abort (default: `Ctrl+Alt+X`)
- `action_delay_ms`: Pause between actions to prevent UI issues (default: `200`)
- `browser_cdp_port`: Port for browser remote debugging (default: `9222`)
- `permission`:
  - `allowed_paths`: Array of root directories allowed for file access.
  - `blocked_paths`: Array of folders explicitly forbidden.
  - `blocked_processes`: Protected processes that WinBot cannot kill.
  - `dangerous_cmd_patterns`: Regex blocklist for dangerous shell patterns.
  - `confirm_shell_commands` / `confirm_file_delete` / `confirm_process_kill`: Set to `true` to show interactive prompt checks (automatically disabled when running in `--mcp` mode).

---

## Dependencies
- **nlohmann/json**: JSON format serialization/parsing.
- **stb**: Image encoding.
- **SQLite 3**: Persistent local database.
- **Lua 5.4 & LuaBridge3**: Embedded scripting engine.
- **Windows SDK**: Win32, UIAutomationCore, dwmapi, sapi.

