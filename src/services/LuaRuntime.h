#ifndef WINBOT_LUARUNTIME_H
#define WINBOT_LUARUNTIME_H

#include "Common.h"
#include <string_view>
#include <filesystem>
#include <chrono>
#include <mutex>
#include <expected>

// Forward declarations
class BrowserAutomation;
class UIAutomationScanner;
class SiteProfileRegistry;
class PermissionSystem;
class UIHandle;
struct lua_State;

struct LuaToolDefinition {
    std::string name;
    std::string description;
    json parameters;
    std::filesystem::path scriptPath;
};

class LuaRuntime {
public:
    LuaRuntime(BrowserAutomation& browser, UIAutomationScanner& uia, SiteProfileRegistry& profiles, PermissionSystem* perms = nullptr);
    explicit LuaRuntime(UIAutomationScanner* uia = nullptr, BrowserAutomation* browser = nullptr, SiteProfileRegistry* profiles = nullptr, PermissionSystem* perms = nullptr);
    ~LuaRuntime();

    // Disable copy/move
    LuaRuntime(const LuaRuntime&) = delete;
    LuaRuntime& operator=(const LuaRuntime&) = delete;
    LuaRuntime(LuaRuntime&&) = delete;
    LuaRuntime& operator=(LuaRuntime&&) = delete;

    // Execute inline Lua script string
    ToolResult execString(std::string_view code);

    // Execute a Lua file from disk
    ToolResult execFile(const std::filesystem::path& path);

    // Load tool metadata and schema from a Lua tool script (supports single or multiple tools)
    [[nodiscard]] std::expected<std::vector<LuaToolDefinition>, std::string> loadToolDefinitions(const std::filesystem::path& path);

    // Execute a specific tool from a Lua script with arguments
    ToolResult executeTool(const std::filesystem::path& path, const std::string& toolName, const json& args);

    // Timeout & instruction limit control
    void setTimeout(std::chrono::milliseconds timeout) noexcept { m_timeout = timeout; }
    [[nodiscard]] std::chrono::milliseconds timeout() const noexcept { return m_timeout; }

    void setMaxInstructions(uint64_t maxInstructions) noexcept { m_maxInstructions = maxInstructions; }
    [[nodiscard]] uint64_t maxInstructions() const noexcept { return m_maxInstructions; }

    // Helper to inject a UIHandle global for scripting or testing
    bool setGlobalHandle(const std::string& name, const UIHandle& handle);

    // Underlying lua_State accessor
    [[nodiscard]] lua_State* state() const noexcept { return L; }

private:
    mutable std::mutex m_luaMutex;
    lua_State* L{nullptr};
    BrowserAutomation* m_browser{nullptr};
    UIAutomationScanner* m_uia{nullptr};
    SiteProfileRegistry* m_profiles{nullptr};
    PermissionSystem* m_perms{nullptr};

    std::chrono::milliseconds m_timeout{10000};
    uint64_t m_maxInstructions{5'000'000};

    void sandboxEnvironment();
    void bindWinBotAPI();
    std::string extractReturnValue();
};

#endif // WINBOT_LUARUNTIME_H
