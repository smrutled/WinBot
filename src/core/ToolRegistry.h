#ifndef WINBOT_TOOLREGISTRY_H
#define WINBOT_TOOLREGISTRY_H
#include "Common.h"
#include "core/ITool.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <stop_token>
#include <string_view>
#include <unordered_map>
#include <vector>

// ── ToolRegistry ──────────────────────────────────────────────────────────────
// Central registry of all callable tools implementing the ITool interface.
//
// The LLM is constrained by JSON grammar to always produce valid tool calls.
class ToolRegistry {
public:
    // Register a tool via the ITool interface (replaces if name already exists)
    void registerTool(std::unique_ptr<ITool> tool);

    // Explicitly register or replace an existing tool
    void registerOrReplaceTool(std::unique_ptr<ITool> tool);

    // Unregister a tool by name (returns true if found and removed)
    bool unregisterTool(std::string_view name);

    // Callback fired when tools are added, replaced, or removed
    using ChangeCallback = std::function<void()>;
    void setChangeCallback(ChangeCallback cb);

    // Register all tools self-registered via REGISTER_TOOL
    void registerSelfRegisteredTools(const ToolDependencies& deps = {});

    // Dispatch a tool call from a parsed JSON object {"tool": "...", "args": {...}}
    [[nodiscard]] ToolResult dispatch(const json& toolCall, const std::stop_token& stopToken = {}) const;

    // Dispatch from raw string (parse first)
    [[nodiscard]] ToolResult dispatchRaw(std::string_view jsonStr, const std::stop_token& stopToken = {}) const;

    // Generate human-readable tools description
    [[nodiscard]] std::string buildToolsPrompt() const;

    // Serialize full tool schema as JSON array (for list_tools response)
    [[nodiscard]] json buildToolsSchema() const;

    // Check if a tool name is registered
    [[nodiscard]] bool hasTool(std::string_view name) const;

    [[nodiscard]] const std::vector<std::unique_ptr<ITool>>& tools() const { return m_tools; }

private:
    mutable std::shared_mutex                 m_mutex;
    std::vector<std::unique_ptr<ITool>>       m_tools;
    std::unordered_map<std::string, size_t>   m_index; // name → index into m_tools
    ChangeCallback                            m_changeCallback;
};

#endif // WINBOT_TOOLREGISTRY_H
