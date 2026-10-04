#include "services/LuaRuntime.h"
#include "platform/browser/BrowserAutomation.h"
#include "platform/uia/UIAutomationScanner.h"
#include "platform/uia/UIHandle.h"
#include "services/SiteProfileRegistry.h"
#include "security/PermissionSystem.h"
#include "security/KillSwitch.h"
#include "tools/InputTools.h"
#include "tools/ShellTools.h"
#include "tools/FileTools.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <LuaBridge/LuaBridge.h>

namespace {

struct HookContext {
    std::chrono::steady_clock::time_point startTime;
    std::chrono::milliseconds timeout{10000};
    uint64_t instructionCount{0};
    uint64_t maxInstructions{5'000'000};
};

thread_local HookContext* t_currentHookCtx = nullptr;

static void luaHookCallback(lua_State* L, lua_Debug* /*ar*/) {
    if (KillSwitch::isTriggered()) {
        luaL_error(L, "Script execution aborted: KillSwitch triggered");
    }

    if (t_currentHookCtx) {
        t_currentHookCtx->instructionCount += 500;
        if (t_currentHookCtx->instructionCount > t_currentHookCtx->maxInstructions) {
            luaL_error(L, "Script execution aborted: instruction limit exceeded");
        }
        if (t_currentHookCtx->timeout.count() > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t_currentHookCtx->startTime);
            if (elapsed > t_currentHookCtx->timeout) {
                luaL_error(L, "Script execution aborted: timeout expired");
            }
        }
    }
}

class ScopedLuaHook {
public:
    ScopedLuaHook(lua_State* L, HookContext* ctx) : m_L(L) {
        if (!m_L || !ctx) return;
        t_currentHookCtx = ctx;
        lua_sethook(m_L, luaHookCallback, LUA_MASKCOUNT | LUA_MASKLINE, 500);
    }
    ~ScopedLuaHook() {
        if (m_L) {
            lua_sethook(m_L, nullptr, 0, 0);
        }
        t_currentHookCtx = nullptr;
    }

    ScopedLuaHook(const ScopedLuaHook&) = delete;
    ScopedLuaHook& operator=(const ScopedLuaHook&) = delete;
    ScopedLuaHook(ScopedLuaHook&&) = delete;
    ScopedLuaHook& operator=(ScopedLuaHook&&) = delete;

private:
    lua_State* m_L;
};

} // namespace

LuaRuntime::LuaRuntime(BrowserAutomation& browser, UIAutomationScanner& uia, SiteProfileRegistry& profiles, PermissionSystem* perms)
    : L(luaL_newstate()), m_browser(&browser), m_uia(&uia), m_profiles(&profiles), m_perms(perms) {
    if (L != nullptr) {
        luaL_openlibs(L);
        sandboxEnvironment();
        bindWinBotAPI();
    }
}

LuaRuntime::LuaRuntime(UIAutomationScanner* uia, BrowserAutomation* browser, SiteProfileRegistry* profiles, PermissionSystem* perms)
    : L(luaL_newstate()), m_browser(browser), m_uia(uia), m_profiles(profiles), m_perms(perms) {
    if (L != nullptr) {
        luaL_openlibs(L);
        sandboxEnvironment();
        bindWinBotAPI();
    }
}

LuaRuntime::~LuaRuntime() {
    if (L) {
        lua_close(L);
        L = nullptr;
    }
}

void LuaRuntime::sandboxEnvironment() {
    if (!L) return;

    // 1. Remove dangerous libraries
    // debug: reflection, hook overrides, and memory inspection
    lua_pushnil(L);
    lua_setglobal(L, "debug");

    // io: direct filesystem access bypassing WinBot's PermissionSystem
    lua_pushnil(L);
    lua_setglobal(L, "io");

    // 2. Sanitize package library to prevent loading arbitrary C DLLs
    lua_getglobal(L, "package");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -2, "loadlib");
        lua_pushnil(L);
        lua_setfield(L, -2, "cpath");
    }
    lua_pop(L, 1);

    // 3. Sanitize os library: remove exit, process execution, and file removal
    lua_getglobal(L, "os");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -2, "execute");
        lua_pushnil(L);
        lua_setfield(L, -2, "exit");
        lua_pushnil(L);
        lua_setfield(L, -2, "remove");
        lua_pushnil(L);
        lua_setfield(L, -2, "rename");
        lua_pushnil(L);
        lua_setfield(L, -2, "tmpname");
    }
    lua_pop(L, 1);

    // 4. Remove un-sandboxed global file loading functions
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");
}

static json luaValueToJson(lua_State* L, int idx) {
    int type = lua_type(L, idx);
    switch (type) {
        case LUA_TNIL:
            return nullptr;
        case LUA_TBOOLEAN:
            return static_cast<bool>(lua_toboolean(L, idx));
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) return lua_tointeger(L, idx);
            return lua_tonumber(L, idx);
        case LUA_TSTRING:
            return std::string(lua_tostring(L, idx));
        case LUA_TTABLE: {
            int absIdx = lua_absindex(L, idx);
            lua_len(L, absIdx);
            lua_Integer len = lua_tointeger(L, -1);
            lua_pop(L, 1);
            if (len > 0) {
                json arr = json::array();
                for (lua_Integer i = 1; i <= len; ++i) {
                    lua_geti(L, absIdx, i);
                    arr.push_back(luaValueToJson(L, -1));
                    lua_pop(L, 1);
                }
                return arr;
            } else {
                json obj = json::object();
                lua_pushnil(L);
                while (lua_next(L, absIdx) != 0) {
                    // key is at -2, value is at -1
                    std::string key;
                    if (lua_isstring(L, -2)) {
                        key = lua_tostring(L, -2);
                    } else if (lua_isinteger(L, -2)) {
                        key = std::to_string(lua_tointeger(L, -2));
                    }
                    if (!key.empty()) {
                        obj[key] = luaValueToJson(L, -1);
                    }
                    lua_pop(L, 1);
                }
                return obj;
            }
        }
        default:
            return std::string(lua_typename(L, type));
    }
}

static void pushJsonValue(lua_State* L, const json& j) {
    if (j.is_null()) {
        lua_pushnil(L);
    } else if (j.is_boolean()) {
        lua_pushboolean(L, j.get<bool>());
    } else if (j.is_number_integer()) {
        lua_pushinteger(L, j.get<lua_Integer>());
    } else if (j.is_number_float()) {
        lua_pushnumber(L, j.get<lua_Number>());
    } else if (j.is_string()) {
        std::string s = j.get<std::string>();
        lua_pushlstring(L, s.data(), s.size());
    } else if (j.is_array()) {
        lua_createtable(L, static_cast<int>(j.size()), 0);
        for (size_t i = 0; i < j.size(); ++i) {
            pushJsonValue(L, j[i]);
            lua_seti(L, -2, static_cast<lua_Integer>(i + 1));
        }
    } else if (j.is_object()) {
        lua_createtable(L, 0, static_cast<int>(j.size()));
        for (const auto& [k, v] : j.items()) {
            lua_pushlstring(L, k.data(), k.size());
            pushJsonValue(L, v);
            lua_settable(L, -3);
        }
    } else {
        lua_pushnil(L);
    }
}

std::string LuaRuntime::extractReturnValue() {
    int top = lua_gettop(L);
    if (top == 0) {
        return "Script executed successfully.";
    }

    int type = lua_type(L, -1);
    std::string result;
    switch (type) {
        case LUA_TBOOLEAN:
            result = lua_toboolean(L, -1) ? "true" : "false";
            break;
        case LUA_TNUMBER:
            if (lua_isinteger(L, -1)) {
                result = std::to_string(lua_tointeger(L, -1));
            } else {
                result = std::format("{:.6g}", lua_tonumber(L, -1));
            }
            break;
        case LUA_TSTRING:
            result = lua_tostring(L, -1);
            break;
        case LUA_TNIL:
            result = "nil";
            break;
        case LUA_TTABLE: {
            json j = luaValueToJson(L, -1);
            result = j.dump();
            break;
        }
        case LUA_TUSERDATA: {
            auto handle = luabridge::Stack<UIHandle*>::get(L, -1);
            if (handle && *handle) {
                const auto& el = (*handle)->element();
                result = std::format("[UIHandle: '{}' ({})]", el.name, el.controlType);
            } else {
                result = "[Userdata]";
            }
            break;
        }
        default:
            result = std::string(lua_typename(L, type));
            break;
    }
    lua_settop(L, 0);
    return result;
}

ToolResult LuaRuntime::execString(std::string_view code) {
    if (L == nullptr) {
        return err("Lua state is null");
    }
    std::scoped_lock lock(m_luaMutex);
    try {
        HookContext ctx{
            .startTime = std::chrono::steady_clock::now(),
            .timeout = m_timeout,
            .instructionCount = 0,
            .maxInstructions = m_maxInstructions
        };
        ScopedLuaHook hookGuard(L, &ctx);

        if (luaL_dostring(L, std::string(code).c_str()) != LUA_OK) {
            std::string errMsg = lua_isstring(L, -1) ? lua_tostring(L, -1) : "Lua execution error";
            lua_settop(L, 0);
            return err(errMsg);
        }

        return ok(extractReturnValue());
    } catch (const std::exception& e) {
        lua_settop(L, 0);
        return err(std::format("Lua runtime error: {}", e.what()));
    } catch (...) {
        lua_settop(L, 0);
        return err("Lua runtime error: unknown exception");
    }
}

ToolResult LuaRuntime::execFile(const std::filesystem::path& path) {
    if (L == nullptr) {
        return err("Lua state is null");
    }
    std::scoped_lock lock(m_luaMutex);
    if (m_perms) {
        auto check = m_perms->checkPath(path.string());
        if (!check) return err(std::format("Permission denied: {}", check.error()));
    }
    if (!std::filesystem::exists(path)) {
        return err(std::format("Script file not found: {}", path.string()));
    }

    try {
        HookContext ctx{
            .startTime = std::chrono::steady_clock::now(),
            .timeout = m_timeout,
            .instructionCount = 0,
            .maxInstructions = m_maxInstructions
        };
        ScopedLuaHook hookGuard(L, &ctx);

        if (luaL_dofile(L, path.string().c_str()) != LUA_OK) {
            std::string errMsg = lua_isstring(L, -1) ? lua_tostring(L, -1) : "Lua execution error";
            lua_settop(L, 0);
            return err(errMsg);
        }

        return ok(extractReturnValue());
    } catch (const std::exception& e) {
        lua_settop(L, 0);
        return err(std::format("Lua runtime error: {}", e.what()));
    } catch (...) {
        lua_settop(L, 0);
        return err("Lua runtime error: unknown exception");
    }
}

static LuaToolDefinition parseToolTable(lua_State* L, int tableIdx, const std::filesystem::path& path, const std::string& defaultName) {
    LuaToolDefinition def;
    def.scriptPath = path;
    int absIdx = lua_absindex(L, tableIdx);

    lua_getfield(L, absIdx, "name");
    if (lua_isstring(L, -1)) {
        def.name = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, absIdx, "description");
    if (lua_isstring(L, -1)) {
        def.description = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, absIdx, "parameters");
    if (lua_istable(L, -1)) {
        def.parameters = luaValueToJson(L, -1);
    } else {
        lua_pop(L, 1);
        lua_getfield(L, absIdx, "schema");
        if (lua_istable(L, -1)) {
            def.parameters = luaValueToJson(L, -1);
        }
    }
    lua_pop(L, 1);

    if (def.name.empty()) {
        def.name = defaultName;
    }
    if (def.description.empty()) {
        def.description = std::format("Lua tool loaded from {}", path.filename().string());
    }
    if (def.parameters.empty() || def.parameters.is_null()) {
        def.parameters = json{
            {"type", "object"},
            {"properties", json::object()}
        };
    }
    return def;
}

std::expected<std::vector<LuaToolDefinition>, std::string> LuaRuntime::loadToolDefinitions(const std::filesystem::path& path) {
    if (L == nullptr) {
        return std::unexpected("Lua state is null");
    }
    std::scoped_lock lock(m_luaMutex);

    if (m_perms) {
        auto check = m_perms->checkPath(path.string());
        if (!check) return std::unexpected(std::format("Permission denied: {}", check.error()));
    }
    if (!std::filesystem::exists(path)) {
        return std::unexpected(std::format("Script file not found: {}", path.string()));
    }

    try {
        HookContext ctx{
            .startTime = std::chrono::steady_clock::now(),
            .timeout = m_timeout,
            .instructionCount = 0,
            .maxInstructions = m_maxInstructions
        };
        ScopedLuaHook hookGuard(L, &ctx);

        int status = luaL_dofile(L, path.string().c_str());
        if (status != LUA_OK) {
            std::string errMsg = lua_isstring(L, -1) ? lua_tostring(L, -1) : "Lua execution error";
            lua_settop(L, 0);
            return std::unexpected(std::format("Failed to load script {}: {}", path.filename().string(), errMsg));
        }

        std::vector<LuaToolDefinition> defs;
        std::string baseStem = path.stem().string();

        if (lua_istable(L, -1)) {
            // Check if top-level table is an array of tools: return { tool1, tool2, ... }
            lua_geti(L, -1, 1);
            bool isArrayOfTools = false;
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "execute");
                bool hasExecute = lua_isfunction(L, -1);
                lua_pop(L, 1);
                lua_getfield(L, -1, "name");
                bool hasName = lua_isstring(L, -1);
                lua_pop(L, 1);
                isArrayOfTools = hasExecute || hasName;
            }
            lua_pop(L, 1);

            if (isArrayOfTools) {
                int idx = 1;
                while (true) {
                    lua_geti(L, -1, idx);
                    if (!lua_istable(L, -1)) {
                        lua_pop(L, 1);
                        break;
                    }
                    std::string defName = std::format("{}_{}", baseStem, idx);
                    defs.push_back(parseToolTable(L, -1, path, defName));
                    lua_pop(L, 1);
                    ++idx;
                }
            } else {
                // Check if table contains a "tools" key: return { tools = { ... } }
                lua_getfield(L, -1, "tools");
                if (lua_istable(L, -1)) {
                    int idx = 1;
                    while (true) {
                        lua_geti(L, -1, idx);
                        if (!lua_istable(L, -1)) {
                            lua_pop(L, 1);
                            break;
                        }
                        std::string defName = std::format("{}_{}", baseStem, idx);
                        defs.push_back(parseToolTable(L, -1, path, defName));
                        lua_pop(L, 1);
                        ++idx;
                    }
                    lua_pop(L, 1);
                } else {
                    lua_pop(L, 1);
                    // Single tool table
                    defs.push_back(parseToolTable(L, -1, path, baseStem));
                }
            }
        } else {
            // Script returned something else (e.g. a function or nil); treat as single tool
            LuaToolDefinition def;
            def.name = baseStem;
            def.description = std::format("Lua tool loaded from {}", path.filename().string());
            def.parameters = json{{"type", "object"}, {"properties", json::object()}};
            def.scriptPath = path;
            defs.push_back(std::move(def));
        }

        lua_settop(L, 0);

        if (defs.empty()) {
            LuaToolDefinition def;
            def.name = baseStem;
            def.description = std::format("Lua tool loaded from {}", path.filename().string());
            def.parameters = json{{"type", "object"}, {"properties", json::object()}};
            def.scriptPath = path;
            defs.push_back(std::move(def));
        }

        return defs;
    } catch (const std::exception& e) {
        lua_settop(L, 0);
        return std::unexpected(std::format("Exception loading tool definition {}: {}", path.filename().string(), e.what()));
    }
}

ToolResult LuaRuntime::executeTool(const std::filesystem::path& path, const std::string& toolName, const json& args) {
    if (L == nullptr) {
        return err("Lua state is null");
    }
    std::scoped_lock lock(m_luaMutex);

    if (m_perms) {
        auto check = m_perms->checkPath(path.string());
        if (!check) return err(std::format("Permission denied: {}", check.error()));
    }
    if (!std::filesystem::exists(path)) {
        return err(std::format("Script file not found: {}", path.string()));
    }

    try {
        HookContext ctx{
            .startTime = std::chrono::steady_clock::now(),
            .timeout = m_timeout,
            .instructionCount = 0,
            .maxInstructions = m_maxInstructions
        };
        ScopedLuaHook hookGuard(L, &ctx);

        pushJsonValue(L, args);
        lua_setglobal(L, "args");

        int status = luaL_dofile(L, path.string().c_str());
        if (status != LUA_OK) {
            std::string errMsg = lua_isstring(L, -1) ? lua_tostring(L, -1) : "Lua execution error";
            lua_settop(L, 0);
            return err(errMsg);
        }

        auto executeFunctionAt = [&](int tableIdx) -> std::optional<ToolResult> {
            int absIdx = lua_absindex(L, tableIdx);
            lua_getfield(L, absIdx, "execute");
            if (lua_isfunction(L, -1)) {
                pushJsonValue(L, args);
                if (lua_pcall(L, 1, 2, 0) != LUA_OK) {
                    std::string errMsg = lua_isstring(L, -1) ? lua_tostring(L, -1) : "Lua execution error in tool.execute";
                    lua_settop(L, 0);
                    return err(errMsg);
                }
                if ((lua_isnil(L, -2) || (lua_isboolean(L, -2) && !lua_toboolean(L, -2))) && lua_isstring(L, -1)) {
                    std::string errMsg = lua_tostring(L, -1);
                    lua_settop(L, 0);
                    return err(errMsg);
                }
                lua_pop(L, 1);
                return ok(extractReturnValue());
            }
            lua_pop(L, 1);
            return std::nullopt;
        };

        if (lua_istable(L, -1)) {
            // Check if returned an array of tools: find matching name
            lua_geti(L, -1, 1);
            bool isArrayOfTools = false;
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "execute");
                bool hasExecute = lua_isfunction(L, -1);
                lua_pop(L, 1);
                lua_getfield(L, -1, "name");
                bool hasName = lua_isstring(L, -1);
                lua_pop(L, 1);
                isArrayOfTools = hasExecute || hasName;
            }
            lua_pop(L, 1);

            if (isArrayOfTools) {
                int idx = 1;
                while (true) {
                    lua_geti(L, -1, idx);
                    if (!lua_istable(L, -1)) {
                        lua_pop(L, 1);
                        break;
                    }
                    lua_getfield(L, -1, "name");
                    std::string n;
                    if (lua_isstring(L, -1)) n = lua_tostring(L, -1);
                    lua_pop(L, 1);

                    if (n == toolName || (n.empty() && toolName == std::format("{}_{}", path.stem().string(), idx))) {
                        auto res = executeFunctionAt(-1);
                        lua_settop(L, 0);
                        if (res) return *res;
                        return err(std::format("Tool '{}' does not have an execute function", toolName));
                    }
                    lua_pop(L, 1);
                    ++idx;
                }
            } else {
                // Check if table contains "tools" array
                lua_getfield(L, -1, "tools");
                if (lua_istable(L, -1)) {
                    int idx = 1;
                    while (true) {
                        lua_geti(L, -1, idx);
                        if (!lua_istable(L, -1)) {
                            lua_pop(L, 1);
                            break;
                        }
                        lua_getfield(L, -1, "name");
                        std::string n;
                        if (lua_isstring(L, -1) != 0) {
                            n = lua_tostring(L, -1);
                        }
                        lua_pop(L, 1);

                        if (n == toolName || (n.empty() && toolName == std::format("{}_{}", path.stem().string(), idx))) {
                            auto res = executeFunctionAt(-1);
                            lua_settop(L, 0);
                            if (res) {
                                return *res;
                            }
                            return err(std::format("Tool '{}' does not have an execute function", toolName));
                        }
                        lua_pop(L, 1);
                        ++idx;
                    }
                    lua_pop(L, 1);
                } else {
                    lua_pop(L, 1);
                    // Single tool table
                    auto res = executeFunctionAt(-1);
                    if (res) {
                        return *res;
                    }
                }
            }
        } else if (lua_isfunction(L, -1) != 0) {
            pushJsonValue(L, args);
            if (lua_pcall(L, 1, 2, 0) != LUA_OK) {
                std::string errMsg = (lua_isstring(L, -1) != 0) ? lua_tostring(L, -1) : "Lua execution error";
                lua_settop(L, 0);
                return err(errMsg);
            }
            if (((lua_isnil(L, -2) != 0) || ((lua_isboolean(L, -2) != 0) && lua_toboolean(L, -2) == 0)) && lua_isstring(L, -1) != 0) {
                std::string errMsg = lua_tostring(L, -1);
                lua_settop(L, 0);
                return err(errMsg);
            }
            lua_pop(L, 1);
            return ok(extractReturnValue());
        }

        return ok(extractReturnValue());
    } catch (const std::exception& e) {
        lua_settop(L, 0);
        return err(std::format("Lua runtime error: {}", e.what()));
    } catch (...) {
        lua_settop(L, 0);
        return err("Lua runtime error: unknown exception");
    }
}

bool LuaRuntime::setGlobalHandle(const std::string& name, const UIHandle& handle) {
    if (L == nullptr) {
        return false;
    }
    std::scoped_lock lock(m_luaMutex);
    return luabridge::setGlobal(L, handle, name.c_str());
}

void LuaRuntime::bindWinBotAPI() {
    luabridge::getGlobalNamespace(L)
        .beginClass<UIHandle>("UIHandle")
            .addFunction("click", [](UIHandle* self) -> UIHandle {
                if (self == nullptr) {
                    throw std::runtime_error("UIHandle is null");
                }
                self->click();
                return *self;
            })
            .addFunction("clickChild", [](UIHandle* self, const std::string& name, const luabridge::LuaRef& optType) -> UIHandle {
                if (self == nullptr) {
                    throw std::runtime_error("UIHandle is null");
                }
                std::string typeFilter;
                if (!optType.isNil() && optType.isString()) {
                    typeFilter = optType.tostring();
                }
                self->click(name, typeFilter);
                return *self;
            })
            .addFunction("waitClick", [](UIHandle* self, const std::string& name, const luabridge::LuaRef& arg2, const luabridge::LuaRef& arg3) -> UIHandle {
                if (self == nullptr) {
                    throw std::runtime_error("UIHandle is null");
                }
                int timeout = 5000;
                std::string typeFilter;
                if (!arg2.isNil()) {
                    if (arg2.isNumber()) {
                        timeout = arg2.unsafe_cast<int>();
                    } else if (arg2.isString()) {
                        typeFilter = arg2.tostring();
                    }
                }
                if (!arg3.isNil()) {
                    if (arg3.isString()) {
                        typeFilter = arg3.tostring();
                    } else if (arg3.isNumber()) {
                        timeout = arg3.unsafe_cast<int>();
                    }
                }
                self->waitClick(name, timeout, typeFilter);
                return *self;
            })
            .addFunction("select", [](UIHandle* self, const std::string& name, const luabridge::LuaRef& arg2, const luabridge::LuaRef& arg3) -> UIHandle {
                if (self == nullptr) {
                    throw std::runtime_error("UIHandle is null");
                }
                int timeout = 2000;
                std::string typeFilter;
                if (!arg2.isNil()) {
                    if (arg2.isNumber()) {
                        timeout = arg2.unsafe_cast<int>();
                    } else if (arg2.isString()) {
                        typeFilter = arg2.tostring();
                    }
                }
                if (!arg3.isNil()) {
                    if (arg3.isString()) {
                        typeFilter = arg3.tostring();
                    } else if (arg3.isNumber()) {
                        timeout = arg3.unsafe_cast<int>();
                    }
                }
                return self->select(name, timeout, typeFilter);
            })
            .addFunction("find", [](UIHandle* self, const std::string& name, const luabridge::LuaRef& arg2, const luabridge::LuaRef& arg3) -> std::optional<UIHandle> {
                if (self == nullptr) {
                    return std::nullopt;
                }
                int timeout = 0;
                std::string typeFilter;
                if (!arg2.isNil()) {
                    if (arg2.isNumber()) {
                        timeout = arg2.unsafe_cast<int>();
                    } else if (arg2.isString()) {
                        typeFilter = arg2.tostring();
                    }
                }
                if (!arg3.isNil()) {
                    if (arg3.isString()) {
                        typeFilter = arg3.tostring();
                    } else if (arg3.isNumber()) {
                        timeout = arg3.unsafe_cast<int>();
                    }
                }
                try {
                    return self->select(name, timeout, typeFilter);
                } catch (...) {
                    return std::nullopt;
                }
            })
            .addFunction("type", [](UIHandle* self, const std::string& text) -> UIHandle {
                if (self == nullptr) {
                    throw std::runtime_error("UIHandle is null");
                }
                self->type(text);
                return *self;
            })
            .addFunction("key", [](UIHandle* self, const std::string& k) -> UIHandle {
                if (self == nullptr) {
                    throw std::runtime_error("UIHandle is null");
                }
                self->key(k);
                return *self;
            })
            .addFunction("wait", [](UIHandle* self, int ms) -> UIHandle {
                if (self == nullptr) {
                    throw std::runtime_error("UIHandle is null");
                }
                self->wait(ms);
                return *self;
            })
            .addFunction("refresh", [](UIHandle* self) -> bool {
                return (self != nullptr) ? self->refresh() : false;
            })
            .addFunction("parent", [](UIHandle* self) -> UIHandle {
                if (self == nullptr) {
                    throw std::runtime_error("UIHandle is null");
                }
                return self->parent();
            })
            .addFunction("window", [](UIHandle* self) -> UIHandle {
                if (self == nullptr) {
                    throw std::runtime_error("UIHandle is null");
                }
                return self->window();
            })
            .addFunction("name", [](UIHandle* self) -> std::string {
                return (self != nullptr) ? self->element().name : "";
            })
            .addFunction("value", [](UIHandle* self) -> std::string {
                return (self != nullptr) ? self->element().value : "";
            })
            .addFunction("text", [](UIHandle* self) -> std::string {
                if (self == nullptr) {
                    return "";
                }
                return self->element().value.empty() ? self->element().name : self->element().value;
            })
            .addFunction("controlType", [](UIHandle* self) -> std::string {
                return (self != nullptr) ? self->element().controlType : "";
            })
            .addFunction("automationId", [](UIHandle* self) -> std::string {
                return (self != nullptr) ? self->element().automationId : "";
            })
            .addFunction("isEnabled", [](UIHandle* self) -> bool {
                return (self != nullptr) ? self->element().isEnabled : false;
            })
        .endClass()
        .beginNamespace("winbot")
            .addFunction("log", [](const std::string& msg) {
                WINBOT_INFO("Lua: {}", msg);
            })
            .addFunction("sleep", [](int ms) {
                ::Sleep(ms);
            })
            .addFunction("shell", [this](const std::string& cmd, const luabridge::LuaRef& optTimeout) -> std::string {
                if (KillSwitch::isTriggered()) {
                    throw std::runtime_error("Command execution blocked: KillSwitch triggered");
                }
                if (m_perms != nullptr) {
                    auto check = m_perms->checkShellCommand(cmd);
                    if (!check) {
                        throw std::runtime_error(std::format("Permission denied: {}", check.error()));
                    }
                }
                int timeout = 30000;
                if (!optTimeout.isNil() && optTimeout.isNumber()) {
                    timeout = optTimeout.unsafe_cast<int>();
                }
                auto result = tools::runCommand(cmd, "cmd", timeout);
                if (!result) {
                    throw std::runtime_error(result.error());
                }
                return *result;
            })
            .addFunction("readFile", [this](const std::string& path) -> std::string {
                if (m_perms != nullptr) {
                    auto check = m_perms->checkPath(path);
                    if (!check) {
                        throw std::runtime_error(std::format("Permission denied: {}", check.error()));
                    }
                }
                auto res = tools::readFile(path);
                if (!res) {
                    throw std::runtime_error(res.error());
                }
                return *res;
            })
            .addFunction("writeFile", [this](const std::string& path, const std::string& content) -> bool {
                if (m_perms != nullptr) {
                    auto check = m_perms->checkPath(path);
                    if (!check) {
                        throw std::runtime_error(std::format("Permission denied: {}", check.error()));
                    }
                }
                auto res = tools::writeFile(path, content);
                if (!res) {
                    throw std::runtime_error(res.error());
                }
                return true;
            })
            .addFunction("click", [](int x, int y, const luabridge::LuaRef& optBtn, const luabridge::LuaRef& optHuman) {
                std::string btn = "left";
                bool human = true;
                if (!optBtn.isNil() && optBtn.isString()) {
                    btn = optBtn.tostring();
                }
                if (!optHuman.isNil() && optHuman.isBool()) {
                    human = optHuman.unsafe_cast<bool>();
                }
                auto res = tools::click(x, y, btn, human);
                if (!res) {
                    throw std::runtime_error(res.error());
                }
            })
            .addFunction("typeText", [](const std::string& text) {
                auto res = tools::typeText(text);
                if (!res) {
                    throw std::runtime_error(res.error());
                }
            })
            .addFunction("pressKey", [](const std::string& key) {
                auto res = tools::keyPress(key);
                if (!res) {
                    throw std::runtime_error(res.error());
                }
            })
            .addFunction("selectUIA", [this](const std::string& name, const luabridge::LuaRef& arg2, const luabridge::LuaRef& arg3) -> std::optional<UIHandle> {
                if (m_uia == nullptr) {
                    return std::nullopt;
                }
                int timeout = 2000;
                std::string controlType;
                if (!arg2.isNil()) {
                    if (arg2.isNumber()) {
                        timeout = arg2.unsafe_cast<int>();
                    } else if (arg2.isString()) {
                        controlType = arg2.tostring();
                    }
                }
                if (!arg3.isNil()) {
                    if (arg3.isString()) {
                        controlType = arg3.tostring();
                    } else if (arg3.isNumber()) {
                        timeout = arg3.unsafe_cast<int>();
                    }
                }
                auto res = m_uia->select(name, timeout, controlType);
                if (res) {
                    return *res;
                }
                return std::nullopt;
            })
            .addFunction("waitForWindow", [this](const std::string& title, const luabridge::LuaRef& optTimeout) -> std::optional<UIHandle> {
                if (m_uia == nullptr) {
                    return std::nullopt;
                }
                int timeout = 5000;
                if (!optTimeout.isNil() && optTimeout.isNumber()) {
                    timeout = optTimeout.unsafe_cast<int>();
                }
                auto res = m_uia->waitForWindow(title, timeout);
                if (res) {
                    return *res;
                }
                return std::nullopt;
            })
            .addFunction("navigate", [this](const std::string& url) -> std::string {
                if (m_browser == nullptr) {
                    return "Browser automation not available";
                }
                auto res = m_browser->navigate(url);
                if (!res) {
                    return res.error();
                }
                return "ok";
            })
            .addFunction("evalJs", [this](const std::string& js) -> std::string {
                if (m_browser == nullptr) {
                    return "Browser automation not available";
                }
                auto res = m_browser->evalJs(js);
                if (!res) {
                    return res.error();
                }
                return *res;
            })
            .addFunction("readPage", [this](const std::string& url) -> std::string {
                if (m_browser == nullptr) {
                    return "Browser automation not available";
                }
                auto navRes = m_browser->navigate(url);
                if (!navRes) {
                    return navRes.error();
                }
                
                ::Sleep(2000); // Wait for settle
                if (m_profiles != nullptr) {
                    const SiteProfile* profile = m_profiles->match(url);
                    if (profile != nullptr) {
                        std::string js = "(() => { let out = {};\n";
                        for (const auto& item : profile->items) {
                            js += std::format("try {{\n  let els = document.querySelectorAll('{}');\n", item.selector);
                            js += std::format("  if (els.length > 0) {{\n");
                            std::string extJs;
                            if (item.extract == "text") {
                                extJs = "e.innerText";
                            } else if (item.extract == "html") {
                                extJs = "e.innerHTML";
                            } else {
                                extJs = std::format("e.getAttribute('{}')", item.extract);
                            }
                            
                            if (item.isArray) {
                                js += std::format("    out['{}'] = Array.from(els).map(e => {}).filter(x => x);\n", item.name, extJs);
                            } else {
                                js += std::format("    out['{}'] = (e => {}) (els[0]);\n", item.name, extJs);
                            }
                            js += "  }\n} catch(e){}\n";
                        }
                        js += "return JSON.stringify(out);\n})()";
                        auto evalRes = m_browser->evalJs(js);
                        if (evalRes) {
                            return *evalRes;
                        }
                    }
                }
                // Fallback
                auto domRes = m_browser->evalJs("document.body.innerText");
                if (domRes) {
                    return *domRes;
                }
                return "error";
            })
        .endNamespace();
}
