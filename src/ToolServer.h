#ifndef WINBOT_TOOLSERVER_H
#define WINBOT_TOOLSERVER_H
#include "Common.h"
#include "Protocol.h"
#include "ToolRegistry.h"
#include "UIAutomationScanner.h"
#include "ScreenCapture.h"
#include "BrowserAutomation.h"
#include "PermissionSystem.h"
#include "AuditLog.h"
#include "Memory.h"
#include "SiteProfileRegistry.h"
#include "LuaRuntime.h"

// ── ToolServer ─────────────────────────────────────────────────────────────────
// Reads newline-delimited JSON requests from stdin, dispatches them to the
// ToolRegistry, and writes newline-delimited JSON responses to stdout.
//
// This is the "agent-facing" interface — any AI agent (Python, Node, cloud API
// wrapper, etc.) can drive WinBot by spawning it as a subprocess and
// communicating via its stdin/stdout streams.
class ToolServer {
public:
    struct Config {
        int  actionDelayMs{ 200 };   // Optional inter-action throttle (ms)
        bool humanMovement{ false }; // Whether to use realistic mouse movement for UIA
    };

    ToolServer(
        Config              cfg,
        ToolRegistry&       tools,
        UIAutomationScanner& uia,
        ScreenCapture&      capture,
        BrowserAutomation&  browser,
        PermissionSystem&   perms,
        AuditLog&           audit,
        Memory&             memory,
        SiteProfileRegistry& siteProfiles,
        LuaRuntime&          luaRuntime
    );

    // Block and serve requests until stdin closes or kill-switch fires.
    void run();

private:
    Config               m_cfg;
    ToolRegistry&        m_tools;
    UIAutomationScanner& m_uia;
    ScreenCapture&       m_capture;
    BrowserAutomation&   m_browser;
    PermissionSystem&    m_perms;
    AuditLog&            m_audit;
    Memory&              m_memory;
    SiteProfileRegistry& m_siteProfiles;
    LuaRuntime&          m_luaRuntime;

    // Register all built-in tools into the registry
    void registerBuiltinTools();

    // Observe the current focused window (UIA tree, or screenshot fallback)
    [[nodiscard]] std::string observe();

    // Write one response line to stdout, flushing immediately
    static void sendResponse(const std::string& line);
};

#endif // WINBOT_TOOLSERVER_H
