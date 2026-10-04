#include "services/LuaToolLoader.h"
#include "services/LuaRuntime.h"
#include "core/ToolRegistry.h"
#include "tools/LuaScriptTool.h"

#include <algorithm>
#include <system_error>

LuaToolLoader::LuaToolLoader(std::filesystem::path toolsDir, LuaRuntime& runtime, ToolRegistry& registry)
    : m_toolsDir(std::move(toolsDir)),
      m_runtime(runtime),
      m_registry(registry) {
    std::error_code ec;
    if (!std::filesystem::exists(m_toolsDir, ec)) {
        std::filesystem::create_directories(m_toolsDir, ec);
    }
}

size_t LuaToolLoader::loadAll() {
    return reload();
}

size_t LuaToolLoader::reload() {
    std::scoped_lock lock(m_mutex);

    std::error_code ec;
    if (!std::filesystem::exists(m_toolsDir, ec)) {
        std::filesystem::create_directories(m_toolsDir, ec);
        return 0;
    }

    std::unordered_map<std::string, std::filesystem::file_time_type> currentFiles;

    for (const auto& entry : std::filesystem::directory_iterator(m_toolsDir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".lua") {
            continue;
        }

        auto writeTime = entry.last_write_time(ec);
        if (ec) {
            continue;
        }

        currentFiles[entry.path().string()] = writeTime;
    }

    // 1. Unregister tools whose .lua files were deleted
    for (auto it = m_fileTimes.begin(); it != m_fileTimes.end(); ) {
        const std::string& pathStr = it->first;
        if (!currentFiles.contains(pathStr)) {
            auto toolIt = m_pathToToolNames.find(pathStr);
            if (toolIt != m_pathToToolNames.end()) {
                for (const auto& name : toolIt->second) {
                    m_registry.unregisterTool(name);
                    WINBOT_INFO("Unregistered removed Lua tool: {} ({})", name, pathStr);
                }
                m_pathToToolNames.erase(toolIt);
            }
            it = m_fileTimes.erase(it);
        } else {
            ++it;
        }
    }

    // 2. Load new or updated scripts
    for (const auto& [pathStr, writeTime] : currentFiles) {
        auto existingIt = m_fileTimes.find(pathStr);
        bool isNew = (existingIt == m_fileTimes.end());
        bool isModified = (!isNew && existingIt->second < writeTime);

        if (isNew || isModified) {
            std::filesystem::path p(pathStr);
            auto defsRes = m_runtime.loadToolDefinitions(p);
            if (!defsRes.has_value()) {
                WINBOT_ERROR("Failed to load Lua tool definitions from {}: {}", p.filename().string(), defsRes.error());
                continue;
            }

            // Unregister old tool names from this file if updating
            auto oldToolIt = m_pathToToolNames.find(pathStr);
            if (oldToolIt != m_pathToToolNames.end()) {
                for (const auto& name : oldToolIt->second) {
                    m_registry.unregisterTool(name);
                }
                m_pathToToolNames.erase(oldToolIt);
            }

            std::vector<std::string> registeredNames;
            for (const auto& def : *defsRes) {
                auto scriptTool = std::make_unique<LuaScriptTool>(
                    def.name,
                    def.description,
                    def.parameters,
                    p,
                    m_runtime
                );

                m_registry.registerOrReplaceTool(std::move(scriptTool));
                registeredNames.push_back(def.name);
                WINBOT_INFO("{} Lua tool: '{}' from {}", isNew ? "Loaded" : "Reloaded", def.name, p.filename().string());
            }

            m_pathToToolNames[pathStr] = std::move(registeredNames);
            m_fileTimes[pathStr] = writeTime;
        }
    }

    size_t totalTools = 0;
    for (const auto& [_, names] : m_pathToToolNames) {
        totalTools += names.size();
    }
    return totalTools;
}

bool LuaToolLoader::hasChanges() const {
    std::scoped_lock lock(m_mutex);

    std::error_code ec;
    if (!std::filesystem::exists(m_toolsDir, ec)) {
        return false;
    }

    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(m_toolsDir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".lua") {
            continue;
        }

        ++count;
        auto writeTime = entry.last_write_time(ec);
        if (ec) {
            continue;
        }

        std::string pathStr = entry.path().string();
        auto it = m_fileTimes.find(pathStr);
        if (it == m_fileTimes.end() || it->second < writeTime) {
            return true;
        }
    }

    return count != m_fileTimes.size();
}

std::vector<std::string> LuaToolLoader::loadedToolNames() const {
    std::scoped_lock lock(m_mutex);
    std::vector<std::string> names;
    for (const auto& [_, toolNames] : m_pathToToolNames) {
        for (const auto& name : toolNames) {
            names.push_back(name);
        }
    }
    std::ranges::sort(names);
    return names;
}
