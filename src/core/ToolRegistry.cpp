#include "core/ToolRegistry.h"
#include "security/PermissionSystem.h"
#include <cstddef>
#include <exception>
#include <format>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>

void ToolRegistry::setChangeCallback(ChangeCallback cb) {
    std::unique_lock lock(m_mutex);
    m_changeCallback = std::move(cb);
}

void ToolRegistry::registerTool(std::shared_ptr<ITool> tool) {
    registerOrReplaceTool(std::move(tool));
}

void ToolRegistry::registerOrReplaceTool(std::shared_ptr<ITool> tool) {
    if (!tool) {
        return;
    }
    ChangeCallback cb;
    {
        std::unique_lock lock(m_mutex);
        std::string name = tool->name();
        auto it = m_index.find(name);
        if (it != m_index.end()) {
            for (auto& t : m_tools) {
                if (t && t->name() == name) {
                    t = tool;
                    break;
                }
            }
            it->second = std::move(tool);
        } else {
            m_tools.push_back(tool);
            m_index[std::move(name)] = std::move(tool);
        }
        cb = m_changeCallback;
    }
    if (cb) {
        cb();
    }
}

bool ToolRegistry::unregisterTool(std::string_view name) {
    ChangeCallback cb;
    {
        std::unique_lock lock(m_mutex);
        auto it = m_index.find(name);
        if (it == m_index.end()) {
            return false;
        }

        std::erase_if(m_tools, [&](const std::shared_ptr<ITool>& t) {
            return t && t->name() == name;
        });
        m_index.erase(it);
        cb = m_changeCallback;
    }
    if (cb) {
        cb();
    }
    return true;
}

void ToolRegistry::registerSelfRegisteredTools(const ToolDependencies& deps) {
    for (const auto& entry : ToolRegistrar::getRegistry()) {
        auto tool = entry.factory(deps);
        if (tool) {
            if (deps.perms == nullptr || deps.perms->isToolEnabled(tool->name())) {
                registerTool(std::move(tool));
            }
        }
    }
}

ToolResult ToolRegistry::dispatch(std::string_view toolName, const json& args, const std::stop_token& stopToken) const {
    if (toolName.empty()) {
        return err("Tool call missing 'tool' field");
    }

    std::shared_ptr<ITool> tool;
    {
        std::shared_lock lock(m_mutex);
        auto it = m_index.find(toolName);
        if (it != m_index.end()) {
            tool = it->second;
        }
    }

    if (!tool) {
        return err(std::format("Unknown tool: '{}'", toolName));
    }

    try {
        return tool->execute(args, stopToken);
    } catch (const std::exception& e) {
        return err(std::format("Tool '{}' threw: {}", toolName, e.what()));
    } catch (...) {
        return err(std::format("Tool '{}' threw an unknown exception", toolName));
    }
}

ToolResult ToolRegistry::dispatch(const json& toolCall, const std::stop_token& stopToken) const {
    std::string toolName = toolCall.value("tool", "");
    if (toolName.empty()) {
        return err("Tool call missing 'tool' field");
    }

    static const json kEmptyArgs = json::object();
    const json* argsPtr = &kEmptyArgs;
    if (auto it = toolCall.find("args"); it != toolCall.end() && !it->is_null()) {
        argsPtr = &(*it);
    }

    return dispatch(toolName, *argsPtr, stopToken);
}

ToolResult ToolRegistry::dispatchRaw(std::string_view jsonStr, const std::stop_token& stopToken) const {
    // Strip markdown code fences if model wrapped the JSON
    std::string cleaned(jsonStr);
    auto start = cleaned.find('{');
    auto end   = cleaned.rfind('}');
    if (start == std::string::npos || end == std::string::npos) {
        return err(std::format("No JSON object found in: {}", jsonStr));
    }
    cleaned = cleaned.substr(start, end - start + 1);

    try {
        return dispatch(json::parse(cleaned), stopToken);
    } catch (const json::exception& e) {
        return err(std::format("JSON parse error: {} in: {}", e.what(), cleaned));
    }
}

std::string ToolRegistry::buildToolsPrompt() const {
    std::shared_lock lock(m_mutex);
    std::string prompt = "## Available Tools\n\n"
        "Respond with a single JSON object selecting exactly one tool per turn:\n"
        "```json\n{\"tool\": \"<name>\", \"args\": {<parameters>}}\n```\n\n"
        "### Tool List\n\n";

    for (const auto& t : m_tools) {
        if (!t) {
            continue;
        }
        prompt += std::format("**{}**: {}\n", t->name(), t->description());
        auto schema = t->parametersSchema();
        if (!schema.empty() && schema.contains("properties")) {
            for (const auto& [param, paramSchema] : schema.at("properties").items()) {
                std::string desc = paramSchema.value("description", "");
                std::string type = paramSchema.value("type", "string");
                prompt += std::format("  - `{}` ({}) — {}\n", param, type, desc);
            }
        }
        prompt += '\n';
    }
    return prompt;
}

json ToolRegistry::buildToolsSchema() const {
    std::shared_lock lock(m_mutex);
    json arr = json::array();
    for (const auto& t : m_tools) {
        if (!t) {
            continue;
        }
        arr.push_back({
            {"name",        t->name()},
            {"description", t->description()},
            {"schema",      t->parametersSchema()}
        });
    }
    return arr;
}

bool ToolRegistry::hasTool(std::string_view name) const {
    std::shared_lock lock(m_mutex);
    return m_index.contains(name);
}

std::vector<std::shared_ptr<const ITool>> ToolRegistry::snapshot() const {
    std::shared_lock lock(m_mutex);
    return {m_tools.begin(), m_tools.end()};
}
