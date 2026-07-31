#include "ToolRegistry.h"

void ToolRegistry::registerTool(ToolDef def) {
    m_index[def.name] = m_tools.size();
    m_tools.push_back(std::move(def));
}

ToolResult ToolRegistry::dispatch(const json& toolCall) const {
    std::string toolName = toolCall.value("tool", "");
    if (toolName.empty()) return err("Tool call missing 'tool' field");

    auto it = m_index.find(toolName);
    if (it == m_index.end())
        return err(std::format("Unknown tool: '{}'", toolName));

    const json& args = toolCall.contains("args") ? toolCall["args"] : json::object();
    try {
        return m_tools[it->second].handler(args);
    } catch (const std::exception& e) {
        return err(std::format("Tool '{}' threw: {}", toolName, e.what()));
    }
}

ToolResult ToolRegistry::dispatchRaw(std::string_view jsonStr) const {
    // Strip markdown code fences if model wrapped the JSON
    std::string cleaned(jsonStr);
    auto start = cleaned.find('{');
    auto end   = cleaned.rfind('}');
    if (start == std::string::npos || end == std::string::npos)
        return err(std::format("No JSON object found in: {}", jsonStr));
    cleaned = cleaned.substr(start, end - start + 1);

    try {
        return dispatch(json::parse(cleaned));
    } catch (const json::exception& e) {
        return err(std::format("JSON parse error: {} in: {}", e.what(), cleaned));
    }
}

std::string ToolRegistry::buildToolsPrompt() const {
    std::string prompt = "## Available Tools\n\n"
        "Respond with a single JSON object selecting exactly one tool per turn:\n"
        "```json\n{\"tool\": \"<name>\", \"args\": {<parameters>}}\n```\n\n"
        "### Tool List\n\n";

    for (const auto& t : m_tools) {
        prompt += std::format("**{}**: {}\n", t.name, t.description);
        if (!t.parametersSchema.empty() && t.parametersSchema.contains("properties")) {
            for (const auto& [param, schema] : t.parametersSchema["properties"].items()) {
                std::string desc = schema.value("description", "");
                std::string type = schema.value("type", "string");
                prompt += std::format("  - `{}` ({}) — {}\n", param, type, desc);
            }
        }
        prompt += '\n';
    }
    return prompt;
}

json ToolRegistry::buildToolsSchema() const {
    json arr = json::array();
    for (const auto& t : m_tools) {
        arr.push_back({
            {"name",        t.name},
            {"description", t.description},
            {"schema",      t.parametersSchema}
        });
    }
    return arr;
}

bool ToolRegistry::hasTool(std::string_view name) const {
    return m_index.contains(std::string(name));
}
