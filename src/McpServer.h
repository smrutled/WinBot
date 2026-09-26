#ifndef WINBOT_MCPSERVER_H
#define WINBOT_MCPSERVER_H

#include "Common.h"
#include "ToolRegistry.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

// ── McpServer ─────────────────────────────────────────────────────────────────
// A standards-compliant Model Context Protocol (MCP) server that communicates
// via JSON-RPC 2.0 over stdin/stdout (stdio transport).
//
// Implements the MCP 2025-11-25 specification:
//   - initialize / initialized lifecycle
//   - tools/list — returns all registered tools from ToolRegistry
//   - tools/call — dispatches tool calls through ToolRegistry
//   - ping — keepalive
//   - notifications/cancelled — cancellation (acknowledged, not acted upon)
//
// This replaces the custom WinBot protocol when running in --mcp mode.
// Logging is directed to stderr to keep stdout clean for JSON-RPC messages.
class McpServer {
public:
    struct Config {
        std::string serverName{"WinBot"};
        std::string serverVersion{"0.2.0"};
        int actionDelayMs{200};
    };

    McpServer(Config cfg, ToolRegistry& tools);

    // Block and serve MCP requests until stdin closes or stop() is called.
    void run();

    // Signal the server to stop (thread-safe).
    void stop();

private:
    Config         m_cfg;
    ToolRegistry&  m_tools;
    std::atomic<bool> m_running{false};
    bool           m_initialized{false};

    // ── JSON-RPC helpers ──────────────────────────────────────────────────────
    // Send a JSON-RPC response or notification to stdout.
    void send(const json& msg);

    // Send a JSON-RPC success result.
    void sendResult(const json& id, const json& result);

    // Send a JSON-RPC error.
    void sendError(const json& id, int code, const std::string& message,
                   const json& data = nullptr);

    // ── MCP method handlers ──────────────────────────────────────────────────
    void handleInitialize(const json& id, const json& params);
    void handleToolsList(const json& id, const json& params);
    void handleToolsCall(const json& id, const json& params);
    void handlePing(const json& id);

    // ── Protocol helpers ─────────────────────────────────────────────────────
    // Process one JSON-RPC message (request or notification).
    void processMessage(const json& msg);

    // Convert a ToolRegistry::ToolDef to MCP tool schema format.
    static json toolDefToMcpSchema(const ToolRegistry::ToolDef& def);

    // Standard JSON-RPC error codes
    static constexpr int kParseError      = -32700;
    static constexpr int kInvalidRequest  = -32600;
    static constexpr int kMethodNotFound  = -32601;
    static constexpr int kInvalidParams   = -32602;
    static constexpr int kInternalError   = -32603;

    // MCP-specific error codes
    static constexpr int kNotInitialized  = -32002;
};

#endif // WINBOT_MCPSERVER_H
