#ifndef WINBOT_TOOLREGISTRY_H
#define WINBOT_TOOLREGISTRY_H
#include "Common.h"
#include <functional>
#include <unordered_map>
#include <vector>

// ── ToolRegistry ──────────────────────────────────────────────────────────────
// Central registry of all callable tools. Each tool has:
//   - A name (used in LLM prompts and JSON tool calls)
//   - A JSON schema (injected into the system prompt)
//   - A handler function: (json args) -> ToolResult
//
// The LLM is constrained by JSON grammar to always produce valid tool calls.
class ToolRegistry {
public:
    using Handler = std::function<ToolResult(const json& args)>;

    struct ToolDef {
        std::string name;
        std::string description;
        json        parametersSchema;  // JSON Schema for arguments
        Handler     handler;
    };

    // Register a tool
    void registerTool(ToolDef def);

    // Dispatch a tool call from a parsed JSON object {"tool": "...", "args": {...}}
    [[nodiscard]] ToolResult dispatch(const json& toolCall) const;

    // Dispatch from raw string (parse first)
    [[nodiscard]] ToolResult dispatchRaw(std::string_view jsonStr) const;

    // Generate human-readable tools description
    [[nodiscard]] std::string buildToolsPrompt() const;

    // Serialize full tool schema as JSON array (for list_tools response)
    [[nodiscard]] json buildToolsSchema() const;

    // Check if a tool name is registered
    [[nodiscard]] bool hasTool(std::string_view name) const;

    [[nodiscard]] const std::vector<ToolDef>& tools() const { return m_tools; }

private:
    std::vector<ToolDef>                      m_tools;
    std::unordered_map<std::string, size_t>   m_index; // name → index into m_tools
};

#endif // WINBOT_TOOLREGISTRY_H
