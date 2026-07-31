import { spawn, ChildProcessByStdio } from "child_process";
import { createInterface, Interface } from "readline";
import * as path from "path";
import * as fs from "fs";
import * as vscode from "vscode";
import type { Writable, Readable } from "stream";

export class WinBotBridge {
  private exePath: string;
  private proc: ChildProcessByStdio<Writable, Readable, Readable> | null = null;
  private rl: Interface | null = null;
  private requestId = 0;
  private started = false;
  private outputChannel: vscode.OutputChannel;
  private commandQueue: Array<{
    resolve: (val: string) => void;
    reject: (err: any) => void;
    request: string;
  }> = [];
  private isProcessing = false;

  constructor(context: vscode.ExtensionContext) {
    this.exePath = context.asAbsolutePath(path.join("bin", "WinBot.exe"));
    this.outputChannel = vscode.window.createOutputChannel("WinBot");
  }

  async start(): Promise<void> {
    if (this.started) return;

    if (!fs.existsSync(this.exePath)) {
        throw new Error("WinBot.exe not found. Did you run the build script? Path: " + this.exePath);
    }

    this.outputChannel.appendLine(`[Bridge] Launching WinBot: ${this.exePath}`);

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
      this.outputChannel.appendLine(`[WinBot stderr] ${data.toString().trim()}`);
    });

    this.proc.on("exit", (code: any) => {
      this.outputChannel.appendLine(`[Bridge] WinBot exited with code ${code}`);
      this.started = false;
      this.proc = null;
    });

    this.started = true;
    await new Promise((resolve) => setTimeout(resolve, 500));
    this.outputChannel.appendLine("[Bridge] WinBot process started successfully.");
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

        const responsePromise = new Promise<string>((res, rej) => {
          const timeout = setTimeout(() => {
            this.rl?.removeListener("line", handler);
            rej(new Error("WinBot response timeout"));
          }, 60000);

          const handler = (line: string) => {
            const decoded = line.trim();
            if (!decoded || !decoded.startsWith("{")) {
              if (decoded) this.outputChannel.appendLine(`[WinBot stdout] ${decoded}`);
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
                this.outputChannel.appendLine(`[WinBot stdout] ${decoded}`);
              }
            } catch (e) {
              this.outputChannel.appendLine(`[WinBot stdout] ${decoded}`);
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
