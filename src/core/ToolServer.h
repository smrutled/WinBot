#ifndef WINBOT_TOOLSERVER_H
#define WINBOT_TOOLSERVER_H

#include "core/ToolRegistry.h"
#include "security/AuditLog.h"
#include <string>

// ── ToolServer ─────────────────────────────────────────────────────────────────
// Reads newline-delimited JSON requests from stdin, dispatches them to the
// ToolRegistry, and writes newline-delimited JSON responses to stdout.
//
// This is the "agent-facing" interface — any AI agent (Python, Node, cloud API
// wrapper, etc.) can drive WinBot by spawning it as a subprocess and
// communicating via its stdin/stdout streams.
//
// ToolServer is fully decoupled from subsystem implementations and solely serves
// as the transport adapter for ToolRegistry.
class ToolServer {
public:
    struct Config {
        int actionDelayMs{ 200 }; // Optional inter-action throttle (ms)
    };

    ToolServer(
        Config        cfg,
        ToolRegistry& tools,
        AuditLog&     audit
    );
    ~ToolServer() = default;

    ToolServer(const ToolServer&) = delete;
    ToolServer& operator=(const ToolServer&) = delete;
    ToolServer(ToolServer&&) = delete;
    ToolServer& operator=(ToolServer&&) = delete;

    // Block and serve requests until stdin closes or kill-switch fires.
    void run();

private:
    Config        m_cfg;
    ToolRegistry* m_tools{nullptr};
    AuditLog*     m_audit{nullptr};

    // Write one response line to stdout, flushing immediately
    static void sendResponse(const std::string& line);
};

#endif // WINBOT_TOOLSERVER_H
