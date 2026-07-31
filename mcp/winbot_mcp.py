"""
WinBot MCP Server
─────────────────
A Model Context Protocol (MCP) server that bridges AI agents (Claude, etc.)
to WinBot's Windows UI automation capabilities.

This server spawns WinBot.exe as a subprocess and translates MCP tool calls
into WinBot's newline-delimited JSON protocol, forwarding results back.

Usage:
    python winbot_mcp.py                        # auto-detect WinBot.exe
    python winbot_mcp.py --exe path/to/WinBot.exe
"""

import asyncio
import base64
import json
import os
import sys
import subprocess
import signal
from pathlib import Path
from typing import Any
from mcp.server.fastmcp import FastMCP

# ── Configuration ────────────────────────────────────────────────────────────

# Resolve WinBot.exe location
def _find_winbot_exe() -> Path:
    """Search common build output paths for WinBot.exe."""
    script_dir = Path(__file__).parent
    project_root = script_dir.parent

    candidates = [
        project_root / "build" / "bin" / "Release" / "WinBot.exe",
        project_root / "build" / "bin" / "Debug" / "WinBot.exe",
        project_root / "build" / "Release" / "WinBot.exe",
        project_root / "build" / "Debug" / "WinBot.exe",
    ]

    for p in candidates:
        if p.exists():
            return p

    # Fall back to PATH
    import shutil
    found = shutil.which("WinBot.exe") or shutil.which("WinBot")
    if found:
        return Path(found)

    raise FileNotFoundError(
        "Could not locate WinBot.exe. Build the project first or pass --exe."
    )


# ── WinBot Subprocess Manager ───────────────────────────────────────────────

class WinBotBridge:
    """Manages a persistent WinBot subprocess and provides an async RPC layer."""

    def __init__(self, exe_path: Path) -> None:
        self.exe_path = exe_path
        self._proc: subprocess.Popen | None = None
        self._lock = asyncio.Lock()
        self._request_id = 0
        self._started = False

    async def start(self) -> None:
        """Launch the WinBot subprocess."""
        if self._started:
            return

        print(f"[MCP] Launching WinBot: {self.exe_path}", file=sys.stderr)

        # Run WinBot.exe with cwd set to its directory (so config.json is found)
        exe_dir = str(self.exe_path.parent)

        self._proc = subprocess.Popen(
            [str(self.exe_path), "--mcp"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=exe_dir,
            bufsize=0,
            creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
        )
        self._started = True

        # Give WinBot a moment to initialize
        await asyncio.sleep(0.5)

        # Drain any startup banner from stderr
        self._drain_stderr()

        print("[MCP] WinBot process started successfully.", file=sys.stderr)

    def _drain_stderr(self) -> None:
        """Read and log any available stderr without blocking."""
        if self._proc and self._proc.stderr:
            import select
            # On Windows, select doesn't work on pipes, so we use a non-blocking approach
            try:
                os.set_blocking(self._proc.stderr.fileno(), False)
                data = self._proc.stderr.read()
                if data:
                    print(f"[WinBot stderr] {data.decode('utf-8', errors='replace').strip()}", file=sys.stderr)
            except (OSError, TypeError):
                pass
            finally:
                try:
                    os.set_blocking(self._proc.stderr.fileno(), True)
                except (OSError, TypeError):
                    pass

    async def call_tool(self, tool_name: str, args: dict[str, Any] | None = None) -> str:
        """
        Send a tool call to WinBot and return the result string.
        Raises RuntimeError on errors.
        """
        async with self._lock:
            if not self._started or self._proc is None or self._proc.poll() is not None:
                await self.start()

            self._request_id += 1
            req_id = self._request_id

            request = json.dumps({
                "id": req_id,
                "tool": tool_name,
                "args": args or {}
            }) + "\n"

            # Send request
            assert self._proc is not None
            assert self._proc.stdin is not None
            assert self._proc.stdout is not None

            try:
                self._proc.stdin.write(request.encode("utf-8"))
                self._proc.stdin.flush()
            except (BrokenPipeError, OSError) as e:
                raise RuntimeError(f"WinBot process died: {e}")

            # Read response lines, skipping non-JSON output (banner, log lines).
            # WinBot writes its ASCII banner and [WinBot]/[WARN] log lines to
            # stdout alongside JSON responses, so we must filter them out.
            loop = asyncio.get_event_loop()
            max_lines = 200  # safety limit to avoid infinite loops
            for _ in range(max_lines):
                line = await loop.run_in_executor(None, self._proc.stdout.readline)

                if not line:
                    raise RuntimeError("WinBot process closed stdout unexpectedly")

                decoded = line.decode("utf-8", errors="replace").strip()

                # Skip empty lines and obvious non-JSON lines
                if not decoded or not decoded.startswith("{"):
                    print(f"[WinBot stdout] {decoded}", file=sys.stderr)
                    continue

                # Try to parse as JSON
                try:
                    resp = json.loads(decoded)
                except json.JSONDecodeError:
                    print(f"[WinBot stdout] {decoded}", file=sys.stderr)
                    continue

                # Verify this is actually a response (has "id" field)
                if "id" not in resp:
                    print(f"[WinBot stdout] {decoded}", file=sys.stderr)
                    continue

                # Drain stderr for logging
                self._drain_stderr()

                if resp.get("ok"):
                    return resp.get("result", "")
                else:
                    raise RuntimeError(resp.get("error", "Unknown WinBot error"))

            raise RuntimeError(f"No JSON response from WinBot after {max_lines} lines of output")

    async def discover_tools(self) -> list[dict[str, Any]]:
        """Fetch the tool schema from WinBot via list_tools."""
        result = await self.call_tool("list_tools")
        return json.loads(result)

    def stop(self) -> None:
        """Terminate the WinBot subprocess."""
        if self._proc and self._proc.poll() is None:
            print("[MCP] Shutting down WinBot process.", file=sys.stderr)
            self._proc.stdin.close()
            try:
                self._proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._proc.kill()
        self._started = False


# ── MCP Server Setup ────────────────────────────────────────────────────────

mcp = FastMCP(
    "WinBot",
    instructions=(
        "Windows UI Automation Agent — control the mouse, keyboard, windows, "
        "browser, shell, files, clipboard, and more on a Windows desktop."
    ),
)

# Global bridge instance (initialized on first use)
_bridge: WinBotBridge | None = None

def _get_bridge() -> WinBotBridge:
    global _bridge
    if _bridge is None:
        # Check for --exe argument
        exe_path = None
        if "--exe" in sys.argv:
            idx = sys.argv.index("--exe")
            if idx + 1 < len(sys.argv):
                exe_path = Path(sys.argv[idx + 1])
        if exe_path is None:
            exe_path = _find_winbot_exe()
        _bridge = WinBotBridge(exe_path)
    return _bridge


# ── Perception Tools ─────────────────────────────────────────────────────────

@mcp.tool()
async def ui_scan() -> str:
    """Scan the UI Automation tree of the currently focused window.
    Returns a structured text representation of all interactive UI elements
    (buttons, text fields, menus, etc.) with their names, types, and coordinates."""
    return await _get_bridge().call_tool("ui_scan")


@mcp.tool()
async def screenshot():
    """Capture a screenshot of the entire desktop.
    Returns the actual PNG image so you can see what's on screen."""
    from mcp.types import ImageContent, TextContent
    result_str = await _get_bridge().call_tool("screenshot")
    result = json.loads(result_str)
    b64_data = result["data"]
    return [
        TextContent(
            type="text",
            text=f"Screenshot captured: {result['width']}x{result['height']} px",
        ),
        ImageContent(
            type="image",
            data=b64_data,
            mimeType="image/png",
        ),
    ]


@mcp.tool()
async def screenshot_window(title: str):
    """Capture a screenshot of a specific window by title substring.
    Returns the actual PNG image of that window.

    Args:
        title: Window title substring (case-insensitive)
    """
    from mcp.types import ImageContent, TextContent
    result_str = await _get_bridge().call_tool("screenshot_window", {"title": title})
    result = json.loads(result_str)
    b64_data = result["data"]
    return [
        TextContent(
            type="text",
            text=f"Screenshot of '{title}': {result['width']}x{result['height']} px",
        ),
        ImageContent(
            type="image",
            data=b64_data,
            mimeType="image/png",
        ),
    ]


@mcp.tool()
async def ui_scan_window(title: str) -> str:
    """Scan the UI Automation tree of a specific window (case-insensitive, partial title match).
    Returns all interactive elements with their bounds as (left,top,right,bottom) in screen pixels.
    Pass those bounds directly to screenshot_element to capture any individual element.

    Args:
        title: Window title substring (case-insensitive, e.g. 'notepad', 'chrome', 'settings')
    """
    return await _get_bridge().call_tool("ui_scan_window", {"title": title})


@mcp.tool()
async def screenshot_element(left: int, top: int, right: int, bottom: int):
    """Capture a screenshot of a specific UI element by its screen bounds.
    Get the bounds (left,top,right,bottom) from ui_scan or ui_scan_window output,
    then pass them here to get a tight screenshot of just that element.

    Example workflow:
      1. Call ui_scan_window('notepad') to see all elements and their bounds
      2. Find an element like: [Edit: "text" at (100,200,800,600)]
      3. Call screenshot_element(left=100, top=200, right=800, bottom=600)

    Args:
        left: Left edge in screen pixels
        top: Top edge in screen pixels
        right: Right edge in screen pixels
        bottom: Bottom edge in screen pixels
    """
    from mcp.types import ImageContent, TextContent
    result_str = await _get_bridge().call_tool(
        "screenshot_element",
        {"left": left, "top": top, "right": right, "bottom": bottom}
    )
    result = json.loads(result_str)
    b64_data = result["data"]
    return [
        TextContent(
            type="text",
            text=f"Element screenshot: {result['width']}x{result['height']} px "
                 f"(region {left},{top} → {right},{bottom})",
        ),
        ImageContent(
            type="image",
            data=b64_data,
            mimeType="image/png",
        ),
    ]


@mcp.tool()
async def get_window_list() -> str:
    """List all visible windows with their titles and window handles (HWND).
    Useful for finding which applications are open and their identifiers."""
    return await _get_bridge().call_tool("get_window_list")


@mcp.tool()
async def get_cursor_position() -> str:
    """Get the current mouse cursor position on screen as (x, y) coordinates."""
    return await _get_bridge().call_tool("get_cursor_position")


# ── Mouse & Keyboard Tools ──────────────────────────────────────────────────

@mcp.tool()
async def click(x: int, y: int, button: str = "left") -> str:
    """Click the mouse at screen coordinates (x, y).

    Args:
        x: Screen X coordinate (pixels from left edge)
        y: Screen Y coordinate (pixels from top edge)
        button: Mouse button to click — "left", "right", or "middle"
    """
    return await _get_bridge().call_tool("click", {"x": x, "y": y, "button": button})


@mcp.tool()
async def double_click(x: int, y: int) -> str:
    """Double-click the mouse at screen coordinates (x, y).

    Args:
        x: Screen X coordinate
        y: Screen Y coordinate
    """
    return await _get_bridge().call_tool("double_click", {"x": x, "y": y})


@mcp.tool()
async def drag(x1: int, y1: int, x2: int, y2: int) -> str:
    """Click and drag from (x1, y1) to (x2, y2).

    Args:
        x1: Start X coordinate
        y1: Start Y coordinate
        x2: End X coordinate
        y2: End Y coordinate
    """
    return await _get_bridge().call_tool("drag", {"x1": x1, "y1": y1, "x2": x2, "y2": y2})


@mcp.tool()
async def scroll(x: int, y: int, delta: int) -> str:
    """Scroll the mouse wheel at position (x, y).

    Args:
        x: Screen X coordinate to scroll at
        y: Screen Y coordinate to scroll at
        delta: Scroll amount — positive for up, negative for down
    """
    return await _get_bridge().call_tool("scroll", {"x": x, "y": y, "delta": delta})


@mcp.tool()
async def type_text(text: str) -> str:
    """Type text using the keyboard as if the user were typing it.

    Args:
        text: The text string to type
    """
    return await _get_bridge().call_tool("type", {"text": text})


@mcp.tool()
async def key(combo: str) -> str:
    """Press a key or key combination.

    Args:
        combo: Key combination string, e.g. "Ctrl+C", "Enter", "Alt+F4", "F5", "Shift+Tab"
    """
    return await _get_bridge().call_tool("key", {"combo": combo})


# ── Window Management Tools ─────────────────────────────────────────────────

@mcp.tool()
async def focus_window(title: str) -> str:
    """Bring a window to the foreground by searching for a title substring.

    Args:
        title: A substring of the window title to search for (case-insensitive)
    """
    return await _get_bridge().call_tool("focus_window", {"title": title})


@mcp.tool()
async def close_window(title: str) -> str:
    """Close a window by searching for a title substring.

    Args:
        title: A substring of the window title to search for
    """
    return await _get_bridge().call_tool("close_window", {"title": title})


# ── Shell & File Tools ───────────────────────────────────────────────────────

@mcp.tool()
async def run_command(cmd: str, timeout_ms: int = 30000) -> str:
    """Run a PowerShell/cmd command and return stdout + stderr.
    Commands are permission-checked against the WinBot security policy.

    Args:
        cmd: The command to execute
        timeout_ms: Timeout in milliseconds (default: 30000)
    """
    return await _get_bridge().call_tool("run_command", {"cmd": cmd, "timeout_ms": timeout_ms})


@mcp.tool()
async def read_file(path: str) -> str:
    """Read a file's text contents. Path is permission-checked.

    Args:
        path: Absolute or relative file path to read
    """
    return await _get_bridge().call_tool("read_file", {"path": path})


@mcp.tool()
async def write_file(path: str, content: str) -> str:
    """Write content to a file (creates or overwrites). Path is permission-checked.

    Args:
        path: File path to write to
        content: Text content to write
    """
    return await _get_bridge().call_tool("write_file", {"path": path, "content": content})


@mcp.tool()
async def append_file(path: str, content: str) -> str:
    """Append content to a file. Path is permission-checked.

    Args:
        path: File path to append to
        content: Text content to append
    """
    return await _get_bridge().call_tool("append_file", {"path": path, "content": content})


@mcp.tool()
async def list_directory(path: str = ".") -> str:
    """List files and subdirectories in a directory.

    Args:
        path: Directory path to list (default: current directory)
    """
    return await _get_bridge().call_tool("list_directory", {"path": path})


@mcp.tool()
async def delete_file(path: str) -> str:
    """Delete a file. Path is permission-checked and user confirmation is required.

    Args:
        path: File path to delete
    """
    return await _get_bridge().call_tool("delete_file", {"path": path})


@mcp.tool()
async def copy_file(src: str, dst: str) -> str:
    """Copy a file from src to dst.

    Args:
        src: Source file path
        dst: Destination file path
    """
    return await _get_bridge().call_tool("copy_file", {"src": src, "dst": dst})


# ── System Tools ─────────────────────────────────────────────────────────────

@mcp.tool()
async def get_clipboard() -> str:
    """Read the current text content of the Windows clipboard."""
    return await _get_bridge().call_tool("get_clipboard")


@mcp.tool()
async def set_clipboard(text: str) -> str:
    """Write text to the Windows clipboard.

    Args:
        text: Text to copy to the clipboard
    """
    return await _get_bridge().call_tool("set_clipboard", {"text": text})


@mcp.tool()
async def get_processes() -> str:
    """List all running processes with their names and PIDs."""
    return await _get_bridge().call_tool("get_processes")


@mcp.tool()
async def kill_process(name_or_pid: str) -> str:
    """Kill a process by name or PID. Permission-checked against the security policy.

    Args:
        name_or_pid: Process name (e.g. "notepad.exe") or PID number
    """
    return await _get_bridge().call_tool("kill_process", {"name_or_pid": name_or_pid})


@mcp.tool()
async def get_system_info() -> str:
    """Get system information including CPU, RAM, and disk usage."""
    return await _get_bridge().call_tool("get_system_info")


# ── Web Tools ────────────────────────────────────────────────────────────────

@mcp.tool()
async def http_get(url: str, headers: str = "") -> str:
    """Make an HTTP GET request and return the response body.

    Args:
        url: The URL to fetch
        headers: Optional HTTP headers as a string
    """
    return await _get_bridge().call_tool("http_get", {"url": url, "headers": headers})


@mcp.tool()
async def search_web(query: str) -> str:
    """Search DuckDuckGo and return the top result URLs and snippets.

    Args:
        query: Search query string
    """
    return await _get_bridge().call_tool("search_web", {"query": query})


# ── Browser Automation Tools (Chrome DevTools Protocol) ──────────────────────

@mcp.tool()
async def browser_navigate(url: str) -> str:
    """Navigate the browser to a URL using Chrome DevTools Protocol.

    Args:
        url: URL to navigate to
    """
    return await _get_bridge().call_tool("browser_navigate", {"url": url})


@mcp.tool()
async def browser_click(selector: str) -> str:
    """Click a DOM element in the browser by CSS selector.

    Args:
        selector: CSS selector targeting the element to click
    """
    return await _get_bridge().call_tool("browser_click", {"selector": selector})


@mcp.tool()
async def browser_type(selector: str, text: str) -> str:
    """Type text into a browser input field identified by CSS selector.

    Args:
        selector: CSS selector targeting the input element
        text: Text to type into the field
    """
    return await _get_bridge().call_tool("browser_type", {"selector": selector, "text": text})


@mcp.tool()
async def browser_get_dom() -> str:
    """Get a simplified DOM representation of the currently loaded browser page."""
    return await _get_bridge().call_tool("browser_get_dom")


@mcp.tool()
async def browser_eval(js: str) -> str:
    """Execute JavaScript in the browser and return the result.

    Args:
        js: JavaScript code to evaluate
    """
    return await _get_bridge().call_tool("browser_eval", {"js": js})


# ── Memory Tools ─────────────────────────────────────────────────────────────

@mcp.tool()
async def remember(key: str, value: str) -> str:
    """Save a key-value fact to WinBot's persistent SQLite memory.

    Args:
        key: Memory key/name
        value: Value to store
    """
    return await _get_bridge().call_tool("remember", {"key": key, "value": value})


@mcp.tool()
async def recall(key: str) -> str:
    """Retrieve a fact from WinBot's persistent memory by key.

    Args:
        key: Memory key to look up
    """
    return await _get_bridge().call_tool("recall", {"key": key})


@mcp.tool()
async def recall_all() -> str:
    """List all entries stored in WinBot's persistent memory."""
    return await _get_bridge().call_tool("recall_all")


@mcp.tool()
async def forget(key: str) -> str:
    """Delete a memory entry by key from WinBot's persistent memory.

    Args:
        key: Memory key to delete
    """
    return await _get_bridge().call_tool("forget", {"key": key})


# ── Lifecycle ────────────────────────────────────────────────────────────────

import atexit

def _cleanup():
    if _bridge:
        _bridge.stop()

atexit.register(_cleanup)


# ── Entry Point ──────────────────────────────────────────────────────────────

if __name__ == "__main__":
    print("[MCP] WinBot MCP Server starting...", file=sys.stderr)
    mcp.run(transport="stdio")
