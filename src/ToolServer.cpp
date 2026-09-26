#include "ToolServer.h"
#include "KillSwitch.h"
#include <chrono>
#include <iostream>
#include <thread>

// ──────────────────────────────────────────────────────────────────────────────
ToolServer::ToolServer(Config cfg, ToolRegistry& tools, AuditLog& audit)
    : m_cfg(cfg), m_tools(tools), m_audit(audit) {}

// ──────────────────────────────────────────────────────────────────────────────
void ToolServer::sendResponse(const std::string& line) {
    std::cout << line << '\n';
    std::cout.flush();
}

// ──────────────────────────────────────────────────────────────────────────────
void ToolServer::run() {
    WINBOT_INFO("ToolServer: ready. Listening for JSON requests on stdin.");
    WINBOT_INFO(
        "ToolServer: send {{\"id\":1,\"tool\":\"list_tools\",\"args\":{{}}}} to "
        "discover tools.");

    std::string line;
    while (!KillSwitch::isTriggered() && std::getline(std::cin, line)) {
        auto req = protocol::parseRequest(line);
        if (!req) {
            // Empty line or parse error
            if (!req.error().empty())
                WINBOT_WARN("ToolServer: {}", req.error());
            continue;
        }

        WINBOT_INFO("ToolServer: [id={}] tool='{}' args={}", req->id, req->tool,
                    req->args.dump());

        auto result =
            m_tools.dispatch(json{{"tool", req->tool}, {"args", req->args}});

        std::string response;
        if (result) {
            m_audit.record({utc_timestamp(), req->tool, "tool_call",
                            json{{"tool", req->tool}, {"args", req->args}}, "ok",
                            result->substr(0, 256)});
            response = protocol::makeOk(req->id, *result);
        } else {
            m_audit.record({utc_timestamp(), req->tool, "tool_call",
                            json{{"tool", req->tool}, {"args", req->args}}, "error",
                            result.error()});
            response = protocol::makeErr(req->id, result.error());
        }

        sendResponse(response);

        // Optional inter-action delay to avoid hammering the OS
        if (m_cfg.actionDelayMs > 0)
            std::this_thread::sleep_for(
                std::chrono::milliseconds(m_cfg.actionDelayMs));
    }

    WINBOT_INFO("ToolServer: stdin closed or kill-switch fired — shutting down.");
}
