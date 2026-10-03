#include "tools/LuaScriptTool.h"

LuaScriptTool::LuaScriptTool(std::string name,
                             std::string description,
                             json schema,
                             std::filesystem::path scriptPath,
                             LuaRuntime& runtime)
    : m_name(std::move(name)),
      m_description(std::move(description)),
      m_schema(std::move(schema)),
      m_scriptPath(std::move(scriptPath)),
      m_runtime(runtime) {}

ToolResult LuaScriptTool::execute(const json& args) {
    return m_runtime.executeTool(m_scriptPath, m_name, args);
}

ToolResult LuaScriptTool::execute(const json& args, std::stop_token stopToken) {
    if (stopToken.stop_requested()) {
        return err("Tool execution cancelled");
    }
    return execute(args);
}
