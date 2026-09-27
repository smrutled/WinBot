#ifndef WINBOT_CORE_ITOOL_H
#define WINBOT_CORE_ITOOL_H

#include "Common.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Forward declarations of subsystems that tools can optionally depend on
class UIAutomationScanner;
class BrowserAutomation;
class PermissionSystem;
class SiteProfileRegistry;
class LuaRuntime;
class ToolRegistry;

struct ToolDependencies {
    UIAutomationScanner*   uia          = nullptr;
    BrowserAutomation*     browser      = nullptr;
    PermissionSystem*      perms        = nullptr;
    SiteProfileRegistry*   siteProfiles = nullptr;
    LuaRuntime*            luaRuntime   = nullptr;
    const ToolRegistry*    registry     = nullptr;
};

// ── ITool ─────────────────────────────────────────────────────────────────────
// Abstract interface that all modular tools inherit from.
// Encapsulates tool identity, schema metadata, and argument execution logic.
class ITool {
public:
    virtual ~ITool() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::string description() const = 0;
    [[nodiscard]] virtual json parametersSchema() const = 0;
    [[nodiscard]] virtual json schema() const { return parametersSchema(); }
    [[nodiscard]] virtual ToolResult execute(const json& args) = 0;
};

// ── Self-registration helper ─────────────────────────────────────────────────
class ToolRegistrar {
public:
    using FactoryFunc = std::function<std::unique_ptr<ITool>(const ToolDependencies&)>;

    struct Entry {
        std::string name;
        FactoryFunc factory;
    };

    ToolRegistrar(const std::string& name, FactoryFunc factory) {
        getRegistry().push_back(Entry{name, std::move(factory)});
    }

    ToolRegistrar(const std::string& name, std::function<std::unique_ptr<ITool>()> factory) {
        getRegistry().push_back(Entry{
            name,
            [f = std::move(factory)](const ToolDependencies&) { return f(); }
        });
    }

    static std::vector<Entry>& getRegistry() {
        static std::vector<Entry> s_entries;
        return s_entries;
    }
};

#define REGISTER_TOOL(ToolClass) \
    static ToolRegistrar s_registrar_##ToolClass( \
        #ToolClass, []() -> std::unique_ptr<ITool> { return std::make_unique<ToolClass>(); } \
    )

#define REGISTER_TOOL_WITH_DEPS(ToolClass, FactoryLambda) \
    static ToolRegistrar s_registrar_##ToolClass( \
        #ToolClass, FactoryLambda \
    )

#endif // WINBOT_CORE_ITOOL_H
