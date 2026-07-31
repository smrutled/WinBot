# WinBot MCP Server

A [Model Context Protocol (MCP)](https://modelcontextprotocol.io) server that exposes WinBot's Windows UI automation capabilities to AI agents like Claude, Gemini, and others.

## How It Works

The MCP server acts as a **bridge** between the MCP protocol and WinBot's native JSON-over-stdio protocol:

```
AI Agent (Claude, etc.)  ←── MCP (JSON-RPC) ──→  winbot_mcp.py  ←── stdio JSON ──→  WinBot.exe
```

The server spawns `WinBot.exe` as a subprocess and translates MCP tool calls into WinBot's internal protocol, forwarding results back.

## Setup

### 1. Build WinBot

Make sure WinBot is built first (from the project root):

```cmd
cmake -S . -B build -G Ninja -DCMAKE_CXX_STANDARD=23
cmake --build build --config Release
```

### 2. Install Python Dependencies

```cmd
pip install -r requirements.txt
```

### 3. Test the Server

You can test the MCP server with the MCP Inspector:

```cmd
npx @modelcontextprotocol/inspector python mcp/winbot_mcp.py
```

## Connecting to AI Agents

### Claude Desktop

Add this to your Claude Desktop config file at `%APPDATA%\Claude\claude_desktop_config.json`:

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

### Claude Code (CLI)

```cmd
claude mcp add winbot -- python C:/Projects/github/WinBot/mcp/winbot_mcp.py
```

### Custom MCP Client

The server uses **stdio transport** — any MCP-compatible client can connect by spawning the script:

```cmd
python mcp/winbot_mcp.py
```

### Specifying a Custom WinBot Path

If `WinBot.exe` isn't in the standard build output directory:

```cmd
python mcp/winbot_mcp.py --exe "C:/path/to/WinBot.exe"
```

## Available Tools (30+)

| Category             | Tools                                                   |
|---------------------|---------------------------------------------------------|
| **Perception**      | `ui_scan`, `screenshot`, `get_window_list`, `get_cursor_position` |
| **Mouse & Keyboard**| `click`, `double_click`, `drag`, `scroll`, `type_text`, `key` |
| **Window Mgmt**     | `focus_window`, `close_window`                          |
| **Shell & Files**   | `run_command`, `read_file`, `write_file`, `append_file`, `list_directory`, `delete_file`, `copy_file` |
| **System**          | `get_clipboard`, `set_clipboard`, `get_processes`, `kill_process`, `get_system_info` |
| **Web**             | `http_get`, `search_web`                                |
| **Browser (CDP)**   | `browser_navigate`, `browser_click`, `browser_type`, `browser_get_dom`, `browser_eval` |
| **Memory**          | `remember`, `recall`, `recall_all`, `forget`            |

## Security

All of WinBot's built-in security features are active:

- **Permission system**: Path restrictions, blocked processes, dangerous command detection
- **User confirmation prompts**: For file deletion, process killing, and shell commands  
- **Kill switch**: `Ctrl+Alt+X` terminates WinBot immediately
- **Audit logging**: All tool calls are logged to `data/audit.log`
