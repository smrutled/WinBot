import * as vscode from 'vscode';
import { WinBotBridge } from './WinBotBridge';

let bridge: WinBotBridge;

export function activate(context: vscode.ExtensionContext) {
  console.log('WinBot extension is now active!');
  const outputChannel = vscode.window.createOutputChannel("WinBot Main");
  outputChannel.appendLine("WinBot Extension Activating...");
  bridge = new WinBotBridge(context);

  const tools = [
    { name: "ui_scan", hasOutput: true },
    { name: "screenshot", isImage: true },
    { name: "screenshot_window", isImage: true },
    { name: "ui_scan_window", hasOutput: false },
    { name: "screenshot_element", isImage: true },
    { name: "get_window_list", hasOutput: true },
    { name: "get_cursor_position", hasOutput: true },
    { name: "click", hasOutput: false },
    { name: "double_click", hasOutput: false },
    { name: "drag", hasOutput: false },
    { name: "scroll", hasOutput: false },
    { name: "type_text", hasOutput: false },
    { name: "key", hasOutput: false },
    { name: "focus_window", hasOutput: false },
    { name: "close_window", hasOutput: false },
    { name: "run_command", hasOutput: false },
    { name: "read_file", hasOutput: false },
    { name: "write_file", hasOutput: false },
    { name: "get_clipboard", hasOutput: true },
    { name: "set_clipboard", hasOutput: false },
    { name: "append_file", hasOutput: false },
    { name: "list_directory", hasOutput: false },
    { name: "delete_file", hasOutput: false },
    { name: "copy_file", hasOutput: false },
    { name: "get_processes", hasOutput: true },
    { name: "kill_process", hasOutput: false },
    { name: "get_system_info", hasOutput: true },
    { name: "http_get", hasOutput: false },
    { name: "search_web", hasOutput: false },
    { name: "browser_navigate", hasOutput: false },
    { name: "browser_click", hasOutput: false },
    { name: "browser_type", hasOutput: false },
    { name: "browser_get_dom", hasOutput: true },
    { name: "browser_eval", hasOutput: false },
    { name: "remember", hasOutput: false },
    { name: "recall", hasOutput: false },
    { name: "recall_all", hasOutput: true },
    { name: "forget", hasOutput: false },
    { name: "lua_exec", hasOutput: false },
    { name: "lua_run", hasOutput: false },
  ];

  for (const tool of tools) {
      const toolName = `winbot_${tool.name}`;
      outputChannel.appendLine(`Registering tool: ${toolName}`);
      context.subscriptions.push(vscode.lm.registerTool(toolName, {
          async prepareInvocation(options: vscode.LanguageModelToolInvocationOptions<any>, token: vscode.CancellationToken) {
              return { invocationMessage: `Executing WinBot tool ${tool.name}` };
          },
          async invoke(options: vscode.LanguageModelToolInvocationOptions<any>, token: vscode.CancellationToken) {
              try {
                  if (tool.hasOutput) {
                      const result = await bridge.callTool(tool.name);
                      return new vscode.LanguageModelToolResult([new vscode.LanguageModelTextPart(result)]);
                  } else if (tool.isImage) {
                      const ssResultStr = await bridge.callTool(tool.name, options.input);
                      const ssResult = JSON.parse(ssResultStr);
                      // Convert base64 data back to binary bytes for rendering
                      const imageData = Buffer.from(ssResult.data, 'base64');
                      return new vscode.LanguageModelToolResult([
                        new vscode.LanguageModelTextPart(`Screenshot captured: ${ssResult.width}x${ssResult.height} px`),
                        vscode.LanguageModelDataPart.image(imageData, 'image/png')
                      ]);
                  } else {
                      const winbotTool = tool.name === 'type_text' ? 'type' : tool.name;
                      const res = await bridge.callTool(winbotTool, options.input);
                      return new vscode.LanguageModelToolResult([new vscode.LanguageModelTextPart(res)]);
                  }
              } catch (error: any) {
                   return new vscode.LanguageModelToolResult([new vscode.LanguageModelTextPart(`Error: ${error.message || 'Unknown'}`)]);
              }
          }
      }));
  }
}

export function deactivate() {
  if (bridge) {
    bridge.stop();
  }
}
