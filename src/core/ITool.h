#ifndef WINBOT_CORE_ITOOL_H
#define WINBOT_CORE_ITOOL_H

#include "Common.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <stop_token>

// Forward declarations of subsystems that tools can optionally depend on
class UIAutomationScanner;
class BrowserAutomation;
class PermissionSystem;
class SiteProfileRegistry;
class LuaRuntime;
class LuaToolLoader;
class ToolRegistry;

struct ToolDependencies {
    UIAutomationScanner*   uia           = nullptr;
    BrowserAutomation*     browser       = nullptr;
    PermissionSystem*      perms         = nullptr;
    SiteProfileRegistry*   siteProfiles  = nullptr;
    LuaRuntime*            luaRuntime    = nullptr;
    LuaToolLoader*         luaToolLoader = nullptr;
    const ToolRegistry*    registry      = nullptr;
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

    // Optional cooperative cancellation overload (C++20/23 std::stop_token)
    [[nodiscard]] virtual ToolResult execute(const json& args, std::stop_token /*stopToken*/) {
        return execute(args);
    }
};

// ── LambdaTool ───────────────────────────────────────────────────────────────
// Convenient adapter for wrapping callable objects into an ITool.
class LambdaTool : public ITool {
public:
    using Handler = std::function<ToolResult(const json& args)>;
    using CancelableHandler = std::function<ToolResult(const json& args, std::stop_token stopToken)>;

    LambdaTool(std::string name, std::string description, json schema, Handler handler)
        : m_name(std::move(name)), m_description(std::move(description)),
          m_schema(std::move(schema)), m_handler(std::move(handler)) {}

    LambdaTool(std::string name, std::string description, json schema, CancelableHandler cancelableHandler)
        : m_name(std::move(name)), m_description(std::move(description)),
          m_schema(std::move(schema)), m_cancelableHandler(std::move(cancelableHandler)) {}

    [[nodiscard]] std::string name() const override { return m_name; }
    [[nodiscard]] std::string description() const override { return m_description; }
    [[nodiscard]] json parametersSchema() const override { return m_schema; }

    [[nodiscard]] ToolResult execute(const json& args) override {
        if (m_cancelableHandler) {
            return m_cancelableHandler(args, std::stop_token{});
        }
        return m_handler ? m_handler(args) : err("No handler defined");
    }

    [[nodiscard]] ToolResult execute(const json& args, std::stop_token stopToken) override {
        if (m_cancelableHandler) {
            return m_cancelableHandler(args, stopToken);
        }
        return m_handler ? m_handler(args) : err("No handler defined");
    }

private:
    std::string       m_name;
    std::string       m_description;
    json              m_schema;
    Handler           m_handler;
    CancelableHandler m_cancelableHandler;
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
