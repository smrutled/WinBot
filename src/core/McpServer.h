#ifndef WINBOT_MCPSERVER_H
#define WINBOT_MCPSERVER_H

#include "Common.h"
#include "core/ITool.h"
#include "core/ToolRegistry.h"
#include "core/ThreadPool.h"

#include <atomic>
#include <functional>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <unordered_map>

// ── McpServer ─────────────────────────────────────────────────────────────────
// A standards-compliant Model Context Protocol (MCP) server that communicates
// via JSON-RPC 2.0 over stdin/stdout (stdio transport).
//
// Implements the MCP 2025-11-25 specification:
//   - initialize / initialized lifecycle
//   - tools/list — returns all registered tools from ToolRegistry
//   - tools/call — dispatches tool calls concurrently via ThreadPool
//   - ping — keepalive
//   - notifications/cancelled — cooperative cancellation via std::stop_token
//
// Concurrency & Cancellation:
//   - Reading from stdin never blocks on tool execution.
//   - Concurrent tools/call requests execute asynchronously on a ThreadPool.
//   - OS input tools are serialized to protect the Windows input stream.
//   - notifications/cancelled signals std::stop_source immediately, aborting
//     in-flight commands and returning JSON-RPC error -32800 (RequestCancelled).
//   - stdout writes are mutex-synchronized to prevent JSON stream tearing.
class McpServer {
public:
    struct Config {
        std::string serverName{"WinBot"};
        std::string serverVersion{"0.2.0"};
        int actionDelayMs{200};
        size_t workerThreads{0}; // 0 = std::thread::hardware_concurrency()
    };

    McpServer(Config cfg, ToolRegistry& tools);
    McpServer(Config cfg, ToolRegistry& tools, std::istream& in, std::ostream& out);
    ~McpServer();

    // Block and serve MCP requests until stdin closes or stop() is called.
    void run();

    // Signal the server to stop (thread-safe).
    void stop();

    // Cancel an active request by its JSON-RPC ID (formatted as string key)
    bool cancelRequest(const std::string& key);

    // Emit notifications/tools/list_changed notification to connected MCP client
    void notifyToolsListChanged();

private:
    Config            m_cfg;
    ToolRegistry&     m_tools;
    std::istream*     m_in{nullptr};
    std::ostream*     m_out{nullptr};
    std::atomic<bool> m_running{false};
    bool              m_initialized{false};

    // Thread pool for concurrent execution of tools
    std::unique_ptr<ThreadPool> m_threadPool;

    // Mutex to serialize JSON-RPC messages on stdout
    std::mutex        m_sendMutex;

    // Active request tracking for cancellation
    struct ActiveRequest {
        std::stop_source  stopSource;
        std::atomic<bool> cancelled{false};
    };
    std::mutex                                                      m_requestsMutex;
    std::unordered_map<std::string, std::shared_ptr<ActiveRequest>> m_activeRequests;

    // ── JSON-RPC helpers ──────────────────────────────────────────────────────
    // Send a JSON-RPC response or notification to stdout (thread-safe).
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

    // Convert an ITool to MCP tool schema format.
    static json toolToMcpSchema(const ITool& tool);

    // Standard JSON-RPC error codes
    static constexpr int kParseError        = -32700;
    static constexpr int kInvalidRequest    = -32600;
    static constexpr int kMethodNotFound    = -32601;
    static constexpr int kInvalidParams     = -32602;
    static constexpr int kInternalError     = -32603;

    // MCP/LSP-specific error codes
    static constexpr int kNotInitialized    = -32002;
    static constexpr int kRequestCancelled  = -32800; // Standard cancellation error
};

#endif // WINBOT_MCPSERVER_H
