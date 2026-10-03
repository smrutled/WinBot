#ifndef WINBOT_TOOLS_LUASCRIPTTOOL_H
#define WINBOT_TOOLS_LUASCRIPTTOOL_H

#include "Common.h"
#include "core/ITool.h"
#include "services/LuaRuntime.h"
#include <filesystem>
#include <string>

// ── LuaScriptTool ─────────────────────────────────────────────────────────────
// An ITool implementation backed by an external Lua script.
// Allows users to define and hot-reload custom UI automation tools without
// recompiling the WinBot binary.
class LuaScriptTool : public ITool {
public:
    LuaScriptTool(std::string name,
                  std::string description,
                  json schema,
                  std::filesystem::path scriptPath,
                  LuaRuntime& runtime);

    [[nodiscard]] std::string name() const override { return m_name; }
    [[nodiscard]] std::string description() const override { return m_description; }
    [[nodiscard]] json parametersSchema() const override { return m_schema; }

    [[nodiscard]] ToolResult execute(const json& args) override;
    [[nodiscard]] ToolResult execute(const json& args, std::stop_token stopToken) override;

    [[nodiscard]] const std::filesystem::path& scriptPath() const noexcept { return m_scriptPath; }

private:
    std::string           m_name;
    std::string           m_description;
    json                  m_schema;
    std::filesystem::path m_scriptPath;
    LuaRuntime&           m_runtime;
};

#endif // WINBOT_TOOLS_LUASCRIPTTOOL_H
