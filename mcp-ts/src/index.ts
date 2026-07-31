import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ErrorCode,
  ListToolsRequestSchema,
  McpError,
} from "@modelcontextprotocol/sdk/types.js";
import { spawn, ChildProcessByStdio } from "child_process";
import { createInterface, Interface } from "readline";
import path from "path";
import fs from "fs";
import { fileURLToPath } from "url";
import type { Writable, Readable } from "stream";

// --- Configuration ---

const __dirname = path.dirname(fileURLToPath(import.meta.url));

function findWinBotExe(): string {
  const projectRoot = path.resolve(__dirname, "../../");
  const candidates = [
    path.join(projectRoot, "build/bin/Release/WinBot.exe"),
    path.join(projectRoot, "build/bin/Debug/WinBot.exe"),
    path.join(projectRoot, "build/Release/WinBot.exe"),
    path.join(projectRoot, "build/Debug/WinBot.exe"),
  ];

  for (const p of candidates) {
    if (fs.existsSync(p)) return p;
  }

  // Fallback to path (simplified - usually we'd use 'which' but let's stick to Node basics or assume it's built)
  try {
    // Simple check on Windows
    return "WinBot.exe"; 
  } catch {
    throw new Error("Could not locate WinBot.exe. Build the project first.");
  }
}

// --- WinBot subprocess Manager ---

class WinBotBridge {
  private exePath: string;
  private proc: ChildProcessByStdio<Writable, Readable, Readable> | null = null;
  private rl: Interface | null = null;
  private requestId = 0;
  private started = false;
  private commandQueue: Array<{
    resolve: (val: string) => void;
    reject: (err: any) => void;
    request: string;
  }> = [];
  private isProcessing = false;

  constructor(exePath: string) {
    this.exePath = exePath;
  }

  async start(): Promise<void> {
    if (this.started) return;

    console.error(`[MCP] Launching WinBot: ${this.exePath}`);

    const exeDir = path.dirname(this.exePath);

    this.proc = spawn(this.exePath, ["--mcp"], {
      cwd: exeDir,
      stdio: ["pipe", "pipe", "pipe"],
      windowsHide: true,
    }) as any;

    if (!this.proc) throw new Error("Failed to spawn WinBot process");

    this.rl = createInterface({
      input: this.proc.stdout,
      terminal: false,
    });

    this.proc.stderr.on("data", (data: any) => {
      console.error(`[WinBot stderr] ${data.toString().trim()}`);
    });

    this.proc.on("exit", (code: any) => {
      console.error(`[MCP] WinBot exited with code ${code}`);
      this.started = false;
      this.proc = null;
    });

    this.started = true;
    // Wait a bit for initialization
    await new Promise((resolve) => setTimeout(resolve, 500));
    console.error("[MCP] WinBot process started successfully.");
  }

  async callTool(tool: string, args: any = {}): Promise<string> {
    if (!this.started || !this.proc) {
      await this.start();
    }

    return new Promise((resolve, reject) => {
      this.requestId++;
      const req = JSON.stringify({
        id: this.requestId,
        tool,
        args,
      }) + "\n";

      this.commandQueue.push({ resolve, reject, request: req });
      this.processQueue();
    });
  }

  private async processQueue() {
    if (this.isProcessing || this.commandQueue.length === 0) return;
    this.isProcessing = true;

    while (this.commandQueue.length > 0) {
      const { resolve, reject, request } = this.commandQueue.shift()!;

      try {
        if (!this.proc || !this.proc.stdin || !this.rl) {
          throw new Error("WinBot process not available");
        }

        // We use a promise to wait for the specific line response
        const responsePromise = new Promise<string>((res, rej) => {
          const timeout = setTimeout(() => {
            this.rl?.removeListener("line", handler);
            rej(new Error("WinBot response timeout"));
          }, 60000);

          const handler = (line: string) => {
            const decoded = line.trim();
            if (!decoded || !decoded.startsWith("{")) {
              if (decoded) console.error(`[WinBot stdout] ${decoded}`);
              return;
            }

            try {
              const resp = JSON.parse(decoded);
              if (resp && typeof resp === "object" && "id" in resp) {
                clearTimeout(timeout);
                this.rl?.removeListener("line", handler);
                if (resp.ok) {
                  res(resp.result || "");
                } else {
                  rej(new Error(resp.error || "Unknown WinBot error"));
                }
              } else {
                console.error(`[WinBot stdout] ${decoded}`);
              }
            } catch (e) {
              console.error(`[WinBot stdout] ${decoded}`);
            }
          };

          this.rl?.on("line", handler);
        });

        this.proc.stdin.write(request);
        const result = await responsePromise;
        resolve(result);

      } catch (err) {
        reject(err);
      }
    }

    this.isProcessing = false;
  }

  stop() {
    if (this.proc) {
      this.proc.kill();
      this.started = false;
    }
  }
}

// --- Tool Schemas and Handlers ---

const bridge = new WinBotBridge(findWinBotExe());

const server = new Server(
  {
    name: "WinBot",
    version: "1.0.0",
  },
  {
    capabilities: {
      tools: {},
    },
  }
);

// Define Tools
server.setRequestHandler(ListToolsRequestSchema, async () => {
  return {
    tools: [
      {
        name: "ui_scan",
        description: "Scan the UI Automation tree of the currently focused window. Returns a structured text representation of all interactive UI elements (buttons, text fields, lists, etc.) including their names, roles, and screen coordinates. Use this to identify elements before clicking or typing.",
        inputSchema: { type: "object", properties: {} },
      },
      {
        name: "screenshot",
        description: "Capture a full-resolution screenshot of the entire desktop (all monitors). Returns the image as a PNG. Use this to visually verify the state of the screen or to see non-interactive elements.",
        inputSchema: { type: "object", properties: {} },
      },
      {
        name: "screenshot_window",
        description: "Capture a screenshot of a specific window identified by a title substring. Returns the window image as a PNG.",
        inputSchema: {
          type: "object",
          properties: {
            title: { type: "string", description: "Sub-string of the window title to match (case-insensitive). For example, 'Notepad' or 'Chrome'." },
          },
          required: ["title"],
        },
      },
      {
        name: "ui_scan_window",
        description: "Scan the UI Automation tree of a specific window (searching by title substring). Returns all interactive elements with their roles, names, and exact screen bounds (left, top, right, bottom). These bounds can be passed to 'screenshot_element' for detailed inspection.",
        inputSchema: {
          type: "object",
          properties: {
            title: { type: "string", description: "Window title substring to search for (case-insensitive)." },
          },
          required: ["title"],
        },
      },
      {
        name: "screenshot_element",
        description: "Capture a high-detail screenshot of a specific UI element using its screen coordinates. \n\nExample Workflow:\n1. Call ui_scan_window('Notepad') to find an element\n2. Locate the bounds, e.g., (100, 200, 800, 600)\n3. Call screenshot_element(left=100, top=200, right=800, bottom=600)\n",
        inputSchema: {
          type: "object",
          properties: {
            left: { type: "number", description: "Left edge in screen pixels." },
            top: { type: "number", description: "Top edge in screen pixels." },
            right: { type: "number", description: "Right edge in screen pixels." },
            bottom: { type: "number", description: "Bottom edge in screen pixels." },
          },
          required: ["left", "top", "right", "bottom"],
        },
      },
      {
        name: "get_window_list",
        description: "List all visible windows with their titles and handles.",
        inputSchema: { type: "object", properties: {} },
      },
      {
        name: "get_cursor_position",
        description: "Get the current mouse cursor position.",
        inputSchema: { type: "object", properties: {} },
      },
      {
        name: "click",
        description: "Simulate a mouse click at specific screen coordinates.",
        inputSchema: {
          type: "object",
          properties: {
            x: { type: "number", description: "The X screen coordinate (pixels from left edge)." },
            y: { type: "number", description: "The Y screen coordinate (pixels from top edge)." },
            button: { type: "string", enum: ["left", "right", "middle"], default: "left", description: "Which mouse button to click." },
          },
          required: ["x", "y"],
        },
      },
      {
        name: "double_click",
        description: "Perform a rapid double-click at the specified screen coordinates.",
        inputSchema: {
          type: "object",
          properties: {
            x: { type: "number", description: "The X screen coordinate." },
            y: { type: "number", description: "The Y screen coordinate." },
          },
          required: ["x", "y"],
        },
      },
      {
        name: "drag",
        description: "Press and hold the left mouse button at (x1, y1), move to (x2, y2), and release. Use this for moving windows, sliders, or drawing.",
        inputSchema: {
          type: "object",
          properties: {
            x1: { type: "number", description: "Start X coordinate." },
            y1: { type: "number", description: "Start Y coordinate." },
            x2: { type: "number", description: "End X coordinate." },
            y2: { type: "number", description: "End Y coordinate." },
          },
          required: ["x1", "y1", "x2", "y2"],
        },
      },
      {
        name: "scroll",
        description: "Scroll the mouse wheel at a specific location. Use a positive delta to scroll up and a negative delta to scroll down.",
        inputSchema: {
          type: "object",
          properties: {
            x: { type: "number", description: "X coordinate to scroll at." },
            y: { type: "number", description: "Y coordinate to scroll at." },
            delta: { type: "number", description: "Amount of scroll increments (positive: up, negative: down). Typical value is 120 or -120 for one notch." },
          },
          required: ["x", "y", "delta"],
        },
      },
      {
        name: "type_text",
        description: "Simulate keyboard typing. This types into the currently focused application as if the user were typing normally.",
        inputSchema: {
          type: "object",
          properties: {
            text: { type: "string", description: "The text string to type. Special keys should be sent via the 'key' tool instead." },
          },
          required: ["text"],
        },
      },
      {
        name: "key",
        description: "Press a specific key or key combination. Supports names like 'Enter', 'Escape', 'F5', and combinations like 'Ctrl+C', 'Alt+Tab', or 'Win+R'.",
        inputSchema: {
          type: "object",
          properties: {
            combo: { type: "string", description: "The key or shortcut combination to press." },
          },
          required: ["combo"],
        },
      },
      {
          name: "focus_window",
          description: "Bring a window to the foreground and give it input focus by searching for a title substring. This is essential before using 'ui_scan' or keyboard tools if the target window is buried.",
          inputSchema: {
            type: "object",
            properties: {
              title: { type: "string", description: "A substring of the window title (case-insensitive)." },
            },
            required: ["title"],
          },
      },
      {
          name: "close_window",
          description: "Request a window to close by searching for its title. This sends a standard WM_CLOSE message to the window.",
          inputSchema: {
            type: "object",
            properties: {
              title: { type: "string", description: "Substring of the window title to close." },
            },
            required: ["title"],
          },
      },
      {
          name: "run_command",
          description: "Execute a command in PowerShell or CMD and capture the output. Use this for system tasks, running scripts, or checking configuration. Note: Commands are subject to a security white-list.",
          inputSchema: {
            type: "object",
            properties: {
              cmd: { type: "string", description: "The full command line to execute." },
              timeout_ms: { type: "number", default: 30000, description: "Maximum execution time in milliseconds before the command is terminated." },
            },
            required: ["cmd"],
          },
      },
      {
          name: "read_file",
          description: "Read the full text content of a file. Use absolute paths or paths relative to the WinBot directory.",
          inputSchema: {
            type: "object",
            properties: {
              path: { type: "string", description: "Path to the file to read." },
            },
            required: ["path"],
          },
      },
      {
          name: "write_file",
          description: "Create or overwrite a file with the provided text content.",
          inputSchema: {
            type: "object",
            properties: {
              path: { type: "string", description: "Path to the destination file." },
              content: { type: "string", description: "Whole text content to be written." },
            },
            required: ["path", "content"],
          },
      },
      {
          name: "get_clipboard",
          description: "Get the current text currently stored in the Windows system clipboard.",
          inputSchema: { type: "object", properties: {} },
      },
      {
          name: "set_clipboard",
          description: "Copy a new text string to the Windows system clipboard.",
          inputSchema: {
            type: "object",
            properties: {
              text: { type: "string", description: "The text to place onto the clipboard." },
            },
            required: ["text"],
          },
      },
      {
          name: "append_file",
          description: "Add text to the end of an existing file without overwriting it.",
          inputSchema: {
            type: "object",
            properties: {
              path: { type: "string", description: "Path to the file." },
              content: { type: "string", description: "Text to append to the end." },
            },
            required: ["path", "content"],
          },
      },
      {
          name: "list_directory",
          description: "Enumerate all files and sub-folders within a specific directory.",
          inputSchema: {
            type: "object",
            properties: {
              path: { type: "string", default: ".", description: "Directory path (defaults to current working directory)." },
            },
          },
      },
      {
          name: "delete_file",
          description: "Permanently delete a file from the filesystem. Use with caution.",
          inputSchema: {
            type: "object",
            properties: {
              path: { type: "string", description: "Path to the file to be deleted." },
            },
            required: ["path"],
          },
      },
      {
          name: "copy_file",
          description: "Copy a file from a source path to a destination path.",
          inputSchema: {
            type: "object",
            properties: {
              src: { type: "string", description: "Source file path." },
              dst: { type: "string", description: "Destination file path." },
            },
            required: ["src", "dst"],
          },
      },
      {
          name: "get_processes",
          description: "List all currently running system processes. Includes Process ID (PID) and executable name.",
          inputSchema: { type: "object", properties: {} },
      },
      {
          name: "kill_process",
          description: "Terminate a running process. This is powerful and should be used with care to stop misbehaving applications.",
          inputSchema: {
            type: "object",
            properties: {
              name_or_pid: { type: "string", description: "The PID number or the process name (e.g., 'notepad.exe')." },
            },
            required: ["name_or_pid"],
          },
      },
      {
          name: "get_system_info",
          description: "Retrieve hardware and OS status, including CPU load, available RAM, and disk space across all volumes.",
          inputSchema: { type: "object", properties: {} },
      },
      {
          name: "http_get",
          description: "Fetch content from a URL using a GET request. Returns the raw text response.",
          inputSchema: {
            type: "object",
            properties: {
              url: { type: "string", description: "The full URL to request (e.g., https://example.com)." },
              headers: { type: "string", default: "", description: "Optional JSON-formatted headers string." },
            },
            required: ["url"],
          },
      },
      {
          name: "search_web",
          description: "Perform a web search using DuckDuckGo. Returns a list of titles, URLs, and snippets of the top search results.",
          inputSchema: {
            type: "object",
            properties: {
              query: { type: "string", description: "The search query keywords." },
            },
            required: ["query"],
          },
      },
      {
          name: "browser_navigate",
          description: "Direct the automation browser to a specific web address. Useful for starting a web-based task.",
          inputSchema: {
            type: "object",
            properties: {
              url: { type: "string", description: "The destination URL (must include http:// or https://)." },
            },
            required: ["url"],
          },
      },
      {
          name: "browser_click",
          description: "Click on a specific element within the web page using a CSS selector. Ensure the page has finished loading before using this.",
          inputSchema: {
            type: "object",
            properties: {
              selector: { type: "string", description: "The CSS selector targeting the element (e.g., '#submit-button', '.login-link')." },
            },
            required: ["selector"],
          },
      },
      {
          name: "browser_type",
          description: "Input text into a form field or other editable element in the browser. Clears the field before typing.",
          inputSchema: {
            type: "object",
            properties: {
              selector: { type: "string", description: "CSS selector for the target input element." },
              text: { type: "string", description: "The text to be typed into the field." },
            },
            required: ["selector", "text"],
          },
      },
      {
          name: "browser_get_dom",
          description: "Obtain a simplified, text-based tree representing the visible DOM structure of the current web page. Use this to identify CSS selectors for other browser tools.",
          inputSchema: { type: "object", properties: {} },
      },
      {
          name: "browser_eval",
          description: "Run arbitrary JavaScript code within the context of the current web page and return the result. Use this for complex data extraction or interaction not covered by other tools.",
          inputSchema: {
            type: "object",
            properties: {
              js: { type: "string", description: "The JavaScript snippet to execute." },
            },
            required: ["js"],
          },
      },
      {
          name: "remember",
          description: "Persist a key-value pair to WinBot's long-term SQLite memory. Use this to maintain state, user preferences, or important facts across different sessions.",
          inputSchema: {
            type: "object",
            properties: {
              key: { type: "string", description: "The name/ID of the memory entry." },
              value: { type: "string", description: "The string content to store." },
            },
            required: ["key", "value"],
          },
      },
      {
          name: "recall",
          description: "Fetch a specific piece of information from long-term memory using its key name.",
          inputSchema: {
            type: "object",
            properties: {
              key: { type: "string", description: "The key of the entry to retrieve." },
            },
            required: ["key"],
          },
      },
      {
          name: "recall_all",
          description: "List every entry currently stored in WinBot's persistent memory. Use this to get an overview of what the agent has previously learned.",
          inputSchema: { type: "object", properties: {} },
      },
      {
          name: "forget",
          description: "Remove a specific entry from long-term memory. Use this to delete outdated or incorrect information.",
          inputSchema: {
            type: "object",
            properties: {
              key: { type: "string", description: "The key of the memory entry to purge." },
            },
            required: ["key"],
          },
      },
      {
        name: "lua_exec",
        description: "Execute an inline Lua 5.4 script. The 'winbot' table is available globally for automation (navigate, click, evalJs, log, sleep, typeText, pressKey, readPage).",
        inputSchema: {
          type: "object",
          properties: {
            code: { type: "string", description: "Raw Lua source code to execute" },
          },
          required: ["code"],
        },
      },
      {
        name: "lua_run",
        description: "Execute a Lua script file from disk.",
        inputSchema: {
          type: "object",
          properties: {
            path: { type: "string", description: "Absolute path to the .lua file" },
          },
          required: ["path"],
        },
      },
    ],
  };
});

// Implementation of call_tool
server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args } = request.params;

  try {
    switch (name) {
      case "ui_scan":
      case "get_window_list":
      case "get_cursor_position":
      case "get_clipboard":
      case "get_processes":
      case "get_system_info":
      case "browser_get_dom":
      case "recall_all":
        const result = await bridge.callTool(name);
        return { content: [{ type: "text", text: result }] };

      case "screenshot":
      case "screenshot_window":
      case "screenshot_element":
        const ssResultStr = await bridge.callTool(name, args);
        const ssResult = JSON.parse(ssResultStr);
        return {
          content: [
            {
              type: "text",
              text: `Screenshot captured: ${ssResult.width}x${ssResult.height} px`,
            },
            {
              type: "image",
              data: ssResult.data,
              mimeType: "image/png",
            },
          ],
        };

      case "ui_scan_window":
      case "click":
      case "double_click":
      case "drag":
      case "scroll":
      case "type_text":
      case "key":
      case "focus_window":
      case "close_window":
      case "run_command":
      case "read_file":
      case "write_file":
      case "append_file":
      case "list_directory":
      case "delete_file":
      case "copy_file":
      case "kill_process":
      case "set_clipboard":
      case "http_get":
      case "search_web":
      case "browser_navigate":
      case "browser_click":
      case "browser_type":
      case "browser_eval":
      case "remember":
      case "recall":
      case "forget":
      case "lua_exec":
      case "lua_run":
        // Map type_text to WinBot's "type" tool
        const winbotTool = name === "type_text" ? "type" : name;
        const res = await bridge.callTool(winbotTool, args);
        return { content: [{ type: "text", text: res }] };

      default:
        throw new McpError(ErrorCode.MethodNotFound, `Tool not found: ${name}`);
    }
  } catch (error: any) {
    return {
      isError: true,
      content: [{ type: "text", text: error.message || "Unknown error" }],
    };
  }
});

// --- Start Server ---

async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error("WinBot MCP Server (TypeScript) running on stdio");
}

main().catch((error) => {
  console.error("Server error:", error);
  process.exit(1);
});

// Cleanup
process.on("SIGINT", () => {
    bridge.stop();
    process.exit(0);
});
process.on("exit", () => {
    bridge.stop();
});
