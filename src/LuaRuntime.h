#ifndef WINBOT_LUARUNTIME_H
#define WINBOT_LUARUNTIME_H

#include "Common.h"
#include <string_view>
#include <filesystem>

// Forward declarations
class BrowserAutomation;
class UIAutomationScanner;
class SiteProfileRegistry;
struct lua_State;

class LuaRuntime {
public:
    LuaRuntime(BrowserAutomation& browser, UIAutomationScanner& uia, SiteProfileRegistry& profiles);
    ~LuaRuntime();

    // Disable copy/move
    LuaRuntime(const LuaRuntime&) = delete;
    LuaRuntime& operator=(const LuaRuntime&) = delete;

    // Execute inline Lua script string
    ToolResult execString(std::string_view code);

    // Execute a Lua file from disk
    ToolResult execFile(const std::filesystem::path& path);

private:
    lua_State* L;
    BrowserAutomation& m_browser;
    UIAutomationScanner& m_uia;
    SiteProfileRegistry& m_profiles;

    void bindWinBotAPI();
};

#endif // WINBOT_LUARUNTIME_H
