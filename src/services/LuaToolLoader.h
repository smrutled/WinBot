#ifndef WINBOT_SERVICES_LUATOOLLOADER_H
#define WINBOT_SERVICES_LUATOOLLOADER_H

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class LuaRuntime;
class ToolRegistry;

// ── LuaToolLoader ─────────────────────────────────────────────────────────────
// Discovers, validates, and registers Lua script tools from a designated folder.
// Supports dynamic hot-reloading without server recompile or restart.
class LuaToolLoader {
public:
    LuaToolLoader(std::filesystem::path toolsDir, LuaRuntime& runtime, ToolRegistry& registry);

    // Initial scan and load of all .lua tools from toolsDir
    size_t loadAll();

    // Re-scans toolsDir: loads new tools, updates modified ones, and unregisters deleted ones
    size_t reload();

    // Fast check to see if files in toolsDir have changed
    [[nodiscard]] bool hasChanges() const;

    [[nodiscard]] const std::filesystem::path& toolsDir() const noexcept { return m_toolsDir; }

    // List all tool names currently loaded by this loader
    [[nodiscard]] std::vector<std::string> loadedToolNames() const;

private:
    std::filesystem::path                                             m_toolsDir;
    LuaRuntime&                                                       m_runtime;
    ToolRegistry&                                                     m_registry;
    mutable std::mutex                                                m_mutex;
    std::unordered_map<std::string, std::filesystem::file_time_type>  m_fileTimes;
    std::unordered_map<std::string, std::vector<std::string>>         m_pathToToolNames;
};

#endif // WINBOT_SERVICES_LUATOOLLOADER_H
