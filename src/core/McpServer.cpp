#include "core/McpServer.h"
#include "security/KillSwitch.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

// ──────────────────────────────────────────────────────────────────────────────
McpServer::McpServer(Config cfg, ToolRegistry& tools)
    : m_cfg(std::move(cfg)), m_tools(tools) {}

// ── Wire format ──────────────────────────────────────────────────────────────
// MCP stdio transport: newline-delimited JSON-RPC 2.0 messages on stdin/stdout.
// Each message is a single JSON object on its own line, terminated by '\n'.

void McpServer::send(const json& msg) {
    std::string line = msg.dump();
    std::cout << line << '\n';
    std::cout.flush();
}

void McpServer::sendResult(const json& id, const json& result) {
    send({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}

void McpServer::sendError(const json& id, int code, const std::string& message,
                           const json& data) {
    json err = {{"code", code}, {"message", message}};
    if (!data.is_null()) {
        err["data"] = data;
    }
    send({{"jsonrpc", "2.0"}, {"id", id}, {"error", err}});
}

// ── Main loop ────────────────────────────────────────────────────────────────
// Reads one line from stdin, waking up periodically so the kill-switch (or
// m_running=false) is honored even when no input is arriving. A plain
// blocking std::getline would ignore Ctrl+Alt+X until the next line arrived.
// Also guards the read itself: a bad_alloc from an absurdly long line would
// otherwise escape outside any try block and terminate the process.
static bool readLineStdin(std::string& line) {
    HANDLE hStdin = ::GetStdHandle(STD_INPUT_HANDLE);
    for (;;) {
        if (KillSwitch::isTriggered()) return false;
        if (std::cin.rdbuf()->in_avail() > 0) break; // already buffered
        if (hStdin == nullptr || hStdin == INVALID_HANDLE_VALUE) break; // fall back to blocking read
        DWORD wr = ::WaitForSingleObject(hStdin, 250);
        if (wr == WAIT_OBJECT_0) break;    // input available (or pipe closed)
        if (wr != WAIT_TIMEOUT) return false;
    }
    try {
        return static_cast<bool>(std::getline(std::cin, line));
    } catch (const std::exception&) {
        return false;
    }
}

void McpServer::run() {
    m_running = true;
    WINBOT_INFO("McpServer: MCP stdio server ready. Listening for JSON-RPC on stdin.");

    std::string line;
    while (m_running && readLineStdin(line)) {
        // Skip blank lines
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }

        // Parse JSON
        json msg;
        try {
            msg = json::parse(line);
        } catch (const json::exception& e) {
            WINBOT_WARN("McpServer: JSON parse error: {}", e.what());
            sendError(nullptr, kParseError, std::format("Parse error: {}", e.what()));
            continue;
        }

        try {
            processMessage(msg);
        } catch (const std::exception& e) {
            WINBOT_ERROR("McpServer: Unhandled exception processing message: {}", e.what());
            if (msg.contains("id") && !msg["id"].is_null()) {
                sendError(msg["id"], kInternalError,
                          std::format("Internal error: {}", e.what()));
            }
        }
    }

    WINBOT_INFO("McpServer: stdin closed or shutdown signal — exiting.");
}

void McpServer::stop() {
    m_running = false;
}

// ── Message dispatch ─────────────────────────────────────────────────────────
void McpServer::processMessage(const json& msg) {
    // Validate JSON-RPC 2.0 envelope
    if (!msg.contains("jsonrpc") || msg["jsonrpc"] != "2.0") {
        if (msg.contains("id") && !msg["id"].is_null()) {
            sendError(msg["id"], kInvalidRequest,
                      "Invalid request: missing or wrong 'jsonrpc' field");
        }
        return;
    }

    std::string method = msg.value("method", "");
    json id = msg.contains("id") ? msg["id"] : json(nullptr);
    json params = msg.value("params", json::object());

    bool isNotification = !msg.contains("id");

    // ── Notifications (no id, no response expected) ──────────────────────────
    if (isNotification) {
        if (method == "notifications/initialized") {
            WINBOT_INFO("McpServer: Client sent initialized notification.");
            // Nothing to do — we already set m_initialized in handleInitialize
        } else if (method == "notifications/cancelled") {
            WINBOT_INFO("McpServer: Client cancelled request {}",
                        params.value("requestId", json(nullptr)).dump());
            // We don't support cancellation of in-flight requests, but we
            // acknowledge the notification by not erroring.
        } else {
            WINBOT_WARN("McpServer: Unknown notification method: '{}'", method);
        }
        return;
    }

    // ── Requests (have id, must respond) ─────────────────────────────────────
    if (method.empty()) {
        sendError(id, kInvalidRequest, "Invalid request: missing 'method' field");
        return;
    }

    // The 'initialize' method is special — allowed before initialization.
    if (method == "initialize") {
        handleInitialize(id, params);
        return;
    }

    // 'ping' is always allowed.
    if (method == "ping") {
        handlePing(id);
        return;
    }

    // All other methods require initialization.
    if (!m_initialized) {
        sendError(id, kNotInitialized,
                  "Server not initialized. Send 'initialize' first.");
        return;
    }

    if (method == "tools/list") {
        handleToolsList(id, params);
    } else if (method == "tools/call") {
        handleToolsCall(id, params);
    } else if (method == "resources/list") {
        // We don't expose resources, but respond with an empty list
        // so clients don't get a method-not-found error.
        sendResult(id, {{"resources", json::array()}});
    } else if (method == "resources/templates/list") {
        sendResult(id, {{"resourceTemplates", json::array()}});
    } else if (method == "prompts/list") {
        // We don't expose prompts, but respond with an empty list.
        sendResult(id, {{"prompts", json::array()}});
    } else {
        sendError(id, kMethodNotFound,
                  std::format("Method not found: '{}'", method));
    }
}

// ── initialize ───────────────────────────────────────────────────────────────
void McpServer::handleInitialize(const json& id, const json& params) {
    // Log client info
    if (params.contains("clientInfo")) {
        auto& ci = params["clientInfo"];
        WINBOT_INFO("McpServer: Client: {} {}",
                    ci.value("name", "unknown"),
                    ci.value("version", "?"));
    }

    std::string clientProtocolVersion = params.value("protocolVersion", "");
    WINBOT_INFO("McpServer: Client protocol version: {}", clientProtocolVersion);

    // We support MCP protocol version 2024-11-05 (the latest stable)
    // and also declare compatibility with 2025-11-25 which the client may
    // request. Per spec, the server responds with the version it supports.
    std::string negotiatedVersion = "2024-11-05";

    // If client requests a version we know about, echo it back
    if (clientProtocolVersion == "2025-03-26" ||
        clientProtocolVersion == "2025-06-18" ||
        clientProtocolVersion == "2025-11-25") {
        negotiatedVersion = clientProtocolVersion;
    }

    json result = {
        {"protocolVersion", negotiatedVersion},
        {"capabilities", {
            {"tools", {
                {"listChanged", false}
            }}
        }},
        {"serverInfo", {
            {"name", m_cfg.serverName},
            {"version", m_cfg.serverVersion}
        }}
    };

    sendResult(id, result);
    m_initialized = true;

    WINBOT_INFO("McpServer: Initialized successfully (protocol: {}).", negotiatedVersion);
}

// ── tools/list ───────────────────────────────────────────────────────────────
void McpServer::handleToolsList(const json& id, const json& /*params*/) {
    json toolsArray = json::array();

    for (const auto& tool : m_tools.tools()) {
        if (tool) {
            toolsArray.push_back(toolToMcpSchema(*tool));
        }
    }

    sendResult(id, {{"tools", toolsArray}});
}

// ── tools/call ───────────────────────────────────────────────────────────────
void McpServer::handleToolsCall(const json& id, const json& params) {
    std::string toolName = params.value("name", "");
    if (toolName.empty()) {
        sendError(id, kInvalidParams, "Missing 'name' in tools/call params");
        return;
    }

    json args = params.value("arguments", json::object());

    WINBOT_INFO("McpServer: tools/call name='{}' args={}", toolName, args.dump());

    // Dispatch through the existing ToolRegistry
    auto result = m_tools.dispatch(json{{"tool", toolName}, {"args", args}});

    if (result) {
        // Build MCP tool result — content array with a text item
        json content = json::array();

        // Check if the result looks like it contains image data
        // (screenshot tools return JSON with "data" field containing base64 PNG)
        bool hasImage = false;
        try {
            json parsed = json::parse(*result);
            if (parsed.is_object() && parsed.contains("data") &&
                parsed.contains("format") && parsed["format"] == "png") {
                hasImage = true;
                // Add a text summary
                content.push_back({
                    {"type", "text"},
                    {"text", std::format("Screenshot captured: {}x{} px",
                             parsed.value("width", 0),
                             parsed.value("height", 0))}
                });
                // Add the image content
                content.push_back({
                    {"type", "image"},
                    {"data", parsed["data"]},
                    {"mimeType", "image/png"}
                });
            }
        } catch (...) {
            // Not JSON or not an image — that's fine
        }

        if (!hasImage) {
            content.push_back({
                {"type", "text"},
                {"text", *result}
            });
        }

        sendResult(id, {{"content", content}, {"isError", false}});
    } else {
        // Tool returned an error — per MCP spec, we still return a successful
        // JSON-RPC response but with isError=true in the result.
        json content = json::array();
        content.push_back({
            {"type", "text"},
            {"text", result.error()}
        });
        sendResult(id, {{"content", content}, {"isError", true}});
    }

    // Optional inter-action delay
    if (m_cfg.actionDelayMs > 0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(m_cfg.actionDelayMs));
    }
}

// ── ping ─────────────────────────────────────────────────────────────────────
void McpServer::handlePing(const json& id) {
    sendResult(id, json::object());
}

// ── Schema conversion ────────────────────────────────────────────────────────
json McpServer::toolToMcpSchema(const ITool& tool) {
    // MCP tool schema format:
    // {
    //   "name": "...",
    //   "description": "...",
    //   "inputSchema": { "type": "object", "properties": {...}, "required": [...] }
    // }
    json mcpTool = {
        {"name", tool.name()},
        {"description", tool.description()}
    };

    // Convert WinBot's parametersSchema to MCP's inputSchema.
    // WinBot stores it as {"type": "object", "properties": {...}, "required": [...]}.
    // MCP expects the same JSON Schema format under "inputSchema".
    json inputSchema = {{"type", "object"}};
    json params = tool.parametersSchema();

    if (params.contains("properties")) {
        inputSchema["properties"] = params["properties"];
    } else {
        inputSchema["properties"] = json::object();
    }

    if (params.contains("required")) {
        inputSchema["required"] = params["required"];
    }

    mcpTool["inputSchema"] = inputSchema;

    return mcpTool;
}
