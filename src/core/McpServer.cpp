#include "core/McpServer.h"
#include "security/KillSwitch.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

// ──────────────────────────────────────────────────────────────────────────────
McpServer::McpServer(Config cfg, ToolRegistry& tools)
    : McpServer(std::move(cfg), tools, std::cin, std::cout) {}

McpServer::McpServer(Config cfg, ToolRegistry& tools, std::istream& in, std::ostream& out)
    : m_cfg(std::move(cfg)),
      m_tools(tools),
      m_in(&in),
      m_out(&out),
      m_threadPool(std::make_unique<ThreadPool>(m_cfg.workerThreads)) {
    m_tools.setChangeCallback([this]() {
        notifyToolsListChanged();
    });
}

McpServer::~McpServer() {
    m_tools.setChangeCallback(nullptr);
    stop();
}

// ── Wire format ──────────────────────────────────────────────────────────────
// MCP stdio transport: newline-delimited JSON-RPC 2.0 messages on stdin/stdout.
// Each message is a single JSON object on its own line, terminated by '\n'.
// Thread-safe: output is serialized using m_sendMutex.

void McpServer::send(const json& msg) {
    std::string line = msg.dump();
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (m_out) {
        (*m_out) << line << '\n';
        m_out->flush();
    }
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
    WINBOT_INFO("McpServer: MCP server ready. Listening for JSON-RPC messages.");

    std::string line;
    auto readNext = [&]() -> bool {
        if (m_in == &std::cin) {
            return readLineStdin(line);
        }
        return m_in ? static_cast<bool>(std::getline(*m_in, line)) : false;
    };

    while (m_running && readNext()) {
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

    WINBOT_INFO("McpServer: input closed or shutdown signal — exiting.");
    if (m_threadPool) {
        m_threadPool->waitIdle();
    }
    stop();
}

void McpServer::stop() {
    m_running = false;
    {
        std::lock_guard<std::mutex> lock(m_requestsMutex);
        for (auto& [key, req] : m_activeRequests) {
            req->cancelled.store(true, std::memory_order_release);
            req->stopSource.request_stop();
        }
    }
    if (m_threadPool) {
        m_threadPool->stop();
    }
}

bool McpServer::cancelRequest(const std::string& key) {
    std::shared_ptr<ActiveRequest> req;
    {
        std::lock_guard<std::mutex> lock(m_requestsMutex);
        if (auto it = m_activeRequests.find(key); it != m_activeRequests.end()) {
            req = it->second;
        }
    }
    if (req) {
        req->cancelled.store(true, std::memory_order_release);
        req->stopSource.request_stop();
        WINBOT_INFO("McpServer: Cancellation requested for active request {}", key);
        return true;
    }
    WINBOT_INFO("McpServer: Cancellation requested for unknown/completed request {}", key);
    return false;
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
        } else if (method == "notifications/cancelled") {
            json reqId = params.value("requestId", json(nullptr));
            std::string key = reqId.dump();
            WINBOT_INFO("McpServer: Client cancelled request {}", key);
            cancelRequest(key);
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
        sendResult(id, {{"resources", json::array()}});
    } else if (method == "resources/templates/list") {
        sendResult(id, {{"resourceTemplates", json::array()}});
    } else if (method == "prompts/list") {
        sendResult(id, {{"prompts", json::array()}});
    } else {
        sendError(id, kMethodNotFound,
                  std::format("Method not found: '{}'", method));
    }
}

// ── initialize ───────────────────────────────────────────────────────────────
void McpServer::handleInitialize(const json& id, const json& params) {
    if (params.contains("clientInfo")) {
        auto& ci = params["clientInfo"];
        WINBOT_INFO("McpServer: Client: {} {}",
                    ci.value("name", "unknown"),
                    ci.value("version", "?"));
    }

    std::string clientProtocolVersion = params.value("protocolVersion", "");
    WINBOT_INFO("McpServer: Client protocol version: {}", clientProtocolVersion);

    std::string negotiatedVersion = "2024-11-05";

    if (clientProtocolVersion == "2025-03-26" ||
        clientProtocolVersion == "2025-06-18" ||
        clientProtocolVersion == "2025-11-25") {
        negotiatedVersion = clientProtocolVersion;
    }

    json result = {
        {"protocolVersion", negotiatedVersion},
        {"capabilities", {
            {"tools", {
                {"listChanged", true}
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

void McpServer::notifyToolsListChanged() {
    if (m_initialized) {
        send({
            {"jsonrpc", "2.0"},
            {"method", "notifications/tools/list_changed"}
        });
        WINBOT_INFO("McpServer: Emitted notifications/tools/list_changed");
    }
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
// Dispatched asynchronously onto ThreadPool to enable concurrent tool execution
// and unblock stdin so incoming cancellations and subsequent calls are read.
void McpServer::handleToolsCall(const json& id, const json& params) {
    std::string toolName = params.value("name", "");
    if (toolName.empty()) {
        sendError(id, kInvalidParams, "Missing 'name' in tools/call params");
        return;
    }

    json args = params.value("arguments", json::object());
    std::string reqKey = id.dump();

    WINBOT_INFO("McpServer: Dispatching tools/call name='{}' (id={})", toolName, reqKey);

    auto activeReq = std::make_shared<ActiveRequest>();
    {
        std::lock_guard<std::mutex> lock(m_requestsMutex);
        m_activeRequests[reqKey] = activeReq;
    }

    m_threadPool->enqueue([this, id, toolName, args, activeReq, reqKey]() {
        // Ensure request is deregistered from active map on exit
        struct ActiveGuard {
            McpServer* server;
            std::string key;
            ~ActiveGuard() {
                std::lock_guard<std::mutex> lock(server->m_requestsMutex);
                server->m_activeRequests.erase(key);
            }
        } guard{this, reqKey};

        std::stop_token stopToken = activeReq->stopSource.get_token();

        // 1. Check if cancelled before execution started
        if (stopToken.stop_requested() || activeReq->cancelled.load(std::memory_order_acquire)) {
            WINBOT_INFO("McpServer: Tool '{}' (id={}) cancelled before execution.", toolName, reqKey);
            sendError(id, kRequestCancelled, "Request cancelled by client");
            return;
        }

        // 2. Dispatch with cooperative cancellation token
        auto result = m_tools.dispatch(json{{"tool", toolName}, {"args", args}}, stopToken);

        // 3. Check if cancelled during execution
        if (stopToken.stop_requested() || activeReq->cancelled.load(std::memory_order_acquire)) {
            WINBOT_INFO("McpServer: Tool '{}' (id={}) cancelled during execution.", toolName, reqKey);
            sendError(id, kRequestCancelled, "Request cancelled by client");
            return;
        }

        // 4. Send tool result
        if (result) {
            json content = json::array();
            bool hasImage = false;
            try {
                json parsed = json::parse(*result);
                if (parsed.is_object() && parsed.contains("data") &&
                    parsed.contains("format") && parsed["format"] == "png") {
                    hasImage = true;
                    content.push_back({
                        {"type", "text"},
                        {"text", std::format("Screenshot captured: {}x{} px",
                                 parsed.value("width", 0),
                                 parsed.value("height", 0))}
                    });
                    content.push_back({
                        {"type", "image"},
                        {"data", parsed["data"]},
                        {"mimeType", "image/png"}
                    });
                }
            } catch (...) {
                // Not JSON or not an image
            }

            if (!hasImage) {
                content.push_back({
                    {"type", "text"},
                    {"text", *result}
                });
            }

            sendResult(id, {{"content", content}, {"isError", false}});
        } else {
            json content = json::array();
            content.push_back({
                {"type", "text"},
                {"text", result.error()}
            });
            sendResult(id, {{"content", content}, {"isError", true}});
        }

        // 5. Optional inter-action delay (interruptible by cancellation or server shutdown)
        if (m_cfg.actionDelayMs > 0 && !stopToken.stop_requested()) {
            auto start = std::chrono::steady_clock::now();
            auto duration = std::chrono::milliseconds(m_cfg.actionDelayMs);
            while (m_running && !stopToken.stop_requested() &&
                   (std::chrono::steady_clock::now() - start < duration)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    });
}

// ── ping ─────────────────────────────────────────────────────────────────────
void McpServer::handlePing(const json& id) {
    sendResult(id, json::object());
}

// ── Schema conversion ────────────────────────────────────────────────────────
json McpServer::toolToMcpSchema(const ITool& tool) {
    json mcpTool = {
        {"name", tool.name()},
        {"description", tool.description()}
    };

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
