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
    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };

    // Register a tool via the ITool interface (replaces if name already exists)
    void registerTool(std::shared_ptr<ITool> tool);

    // Explicitly register or replace an existing tool
    void registerOrReplaceTool(std::shared_ptr<ITool> tool);

    // Unregister a tool by name (returns true if found and removed)
    bool unregisterTool(std::string_view name);

    // Callback fired when tools are added, replaced, or removed
    using ChangeCallback = std::function<void()>;
    void setChangeCallback(ChangeCallback cb);

    // Register all tools self-registered via REGISTER_TOOL
    void registerSelfRegisteredTools(const ToolDependencies& deps = {});

    // Dispatch a tool call by tool name and arguments (avoids JSON wrapping copies)
    [[nodiscard]] ToolResult dispatch(std::string_view toolName, const json& args, const std::stop_token& stopToken = {}) const;

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

    // Return a thread-safe snapshot of all registered tools
    [[nodiscard]] std::vector<std::shared_ptr<const ITool>> snapshot() const;
    [[nodiscard]] std::vector<std::shared_ptr<const ITool>> tools() const { return snapshot(); }

private:
    mutable std::shared_mutex                                                             m_mutex;
    std::vector<std::shared_ptr<ITool>>                                                   m_tools;
    std::unordered_map<std::string, std::shared_ptr<ITool>, StringHash, std::equal_to<>> m_index;
    ChangeCallback                                                                        m_changeCallback;
};

#endif // WINBOT_TOOLREGISTRY_H
