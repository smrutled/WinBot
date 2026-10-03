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
private:
    lua_State* m_L;
};

} // namespace

LuaRuntime::LuaRuntime(BrowserAutomation& browser, UIAutomationScanner& uia, SiteProfileRegistry& profiles, PermissionSystem* perms)
    : m_browser(&browser), m_uia(&uia), m_profiles(&profiles), m_perms(perms) {
    L = luaL_newstate();
    if (L) {
        luaL_openlibs(L);
        sandboxEnvironment();
        bindWinBotAPI();
    }
}

LuaRuntime::LuaRuntime(UIAutomationScanner* uia, BrowserAutomation* browser, SiteProfileRegistry* profiles, PermissionSystem* perms)
    : m_browser(browser), m_uia(uia), m_profiles(profiles), m_perms(perms) {
    L = luaL_newstate();
    if (L) {
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
            lua_len(L, -1);
            lua_Integer len = lua_tointeger(L, -1);
            lua_pop(L, 1);
            result = std::format("[Lua table, len: {}]", len);
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
    if (!L) return err("Lua state is null");
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
    if (!L) return err("Lua state is null");
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

bool LuaRuntime::setGlobalHandle(const std::string& name, const UIHandle& handle) {
    if (!L) return false;
    return luabridge::setGlobal(L, handle, name.c_str());
}

void LuaRuntime::bindWinBotAPI() {
    luabridge::getGlobalNamespace(L)
        .beginClass<UIHandle>("UIHandle")
            .addFunction("click", [](UIHandle* self) -> UIHandle {
                if (!self) throw std::runtime_error("UIHandle is null");
                self->click();
                return *self;
            })
            .addFunction("clickChild", [](UIHandle* self, const std::string& name, luabridge::LuaRef optType) -> UIHandle {
                if (!self) throw std::runtime_error("UIHandle is null");
                std::string typeFilter;
                if (!optType.isNil() && optType.isString()) typeFilter = optType.tostring();
                self->click(name, typeFilter);
                return *self;
            })
            .addFunction("waitClick", [](UIHandle* self, const std::string& name, luabridge::LuaRef arg2, luabridge::LuaRef arg3) -> UIHandle {
                if (!self) throw std::runtime_error("UIHandle is null");
                int timeout = 5000;
                std::string typeFilter;
                if (!arg2.isNil()) {
                    if (arg2.isNumber()) timeout = arg2.unsafe_cast<int>();
                    else if (arg2.isString()) typeFilter = arg2.tostring();
                }
                if (!arg3.isNil()) {
                    if (arg3.isString()) typeFilter = arg3.tostring();
                    else if (arg3.isNumber()) timeout = arg3.unsafe_cast<int>();
                }
                self->waitClick(name, timeout, typeFilter);
                return *self;
            })
            .addFunction("select", [](UIHandle* self, const std::string& name, luabridge::LuaRef arg2, luabridge::LuaRef arg3) -> UIHandle {
                if (!self) throw std::runtime_error("UIHandle is null");
                int timeout = 2000;
                std::string typeFilter;
                if (!arg2.isNil()) {
                    if (arg2.isNumber()) timeout = arg2.unsafe_cast<int>();
                    else if (arg2.isString()) typeFilter = arg2.tostring();
                }
                if (!arg3.isNil()) {
                    if (arg3.isString()) typeFilter = arg3.tostring();
                    else if (arg3.isNumber()) timeout = arg3.unsafe_cast<int>();
                }
                return self->select(name, timeout, typeFilter);
            })
            .addFunction("find", [](UIHandle* self, const std::string& name, luabridge::LuaRef arg2, luabridge::LuaRef arg3) -> std::optional<UIHandle> {
                if (!self) return std::nullopt;
                int timeout = 0;
                std::string typeFilter;
                if (!arg2.isNil()) {
                    if (arg2.isNumber()) timeout = arg2.unsafe_cast<int>();
                    else if (arg2.isString()) typeFilter = arg2.tostring();
                }
                if (!arg3.isNil()) {
                    if (arg3.isString()) typeFilter = arg3.tostring();
                    else if (arg3.isNumber()) timeout = arg3.unsafe_cast<int>();
                }
                try {
                    return self->select(name, timeout, typeFilter);
                } catch (...) {
                    return std::nullopt;
                }
            })
            .addFunction("type", [](UIHandle* self, const std::string& text) -> UIHandle {
                if (!self) throw std::runtime_error("UIHandle is null");
                self->type(text);
                return *self;
            })
            .addFunction("key", [](UIHandle* self, const std::string& k) -> UIHandle {
                if (!self) throw std::runtime_error("UIHandle is null");
                self->key(k);
                return *self;
            })
            .addFunction("wait", [](UIHandle* self, int ms) -> UIHandle {
                if (!self) throw std::runtime_error("UIHandle is null");
                self->wait(ms);
                return *self;
            })
            .addFunction("refresh", [](UIHandle* self) -> bool {
                return self ? self->refresh() : false;
            })
            .addFunction("parent", [](UIHandle* self) -> UIHandle {
                if (!self) throw std::runtime_error("UIHandle is null");
                return self->parent();
            })
            .addFunction("window", [](UIHandle* self) -> UIHandle {
                if (!self) throw std::runtime_error("UIHandle is null");
                return self->window();
            })
            .addFunction("name", [](UIHandle* self) -> std::string {
                return self ? self->element().name : "";
            })
            .addFunction("value", [](UIHandle* self) -> std::string {
                return self ? self->element().value : "";
            })
            .addFunction("text", [](UIHandle* self) -> std::string {
                if (!self) return "";
                return self->element().value.empty() ? self->element().name : self->element().value;
            })
            .addFunction("controlType", [](UIHandle* self) -> std::string {
                return self ? self->element().controlType : "";
            })
            .addFunction("automationId", [](UIHandle* self) -> std::string {
                return self ? self->element().automationId : "";
            })
            .addFunction("isEnabled", [](UIHandle* self) -> bool {
                return self ? self->element().isEnabled : false;
            })
        .endClass()
        .beginNamespace("winbot")
            .addFunction("log", [](const std::string& msg) {
                WINBOT_INFO("Lua: {}", msg);
            })
            .addFunction("sleep", [](int ms) {
                ::Sleep(ms);
            })
            .addFunction("shell", [this](const std::string& cmd, luabridge::LuaRef optTimeout) -> std::string {
                if (KillSwitch::isTriggered()) {
                    throw std::runtime_error("Command execution blocked: KillSwitch triggered");
                }
                if (m_perms) {
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
                if (m_perms) {
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
                if (m_perms) {
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
            .addFunction("click", [](int x, int y, luabridge::LuaRef optBtn, luabridge::LuaRef optHuman) {
                std::string btn = "left";
                bool human = true;
                if (!optBtn.isNil() && optBtn.isString()) btn = optBtn.tostring();
                if (!optHuman.isNil() && optHuman.isBool()) human = optHuman.unsafe_cast<bool>();
                (void)tools::click(x, y, btn, human);
            })
            .addFunction("typeText", [](const std::string& text) {
                (void)tools::typeText(text);
            })
            .addFunction("pressKey", [](const std::string& key) {
                (void)tools::keyPress(key);
            })
            .addFunction("selectUIA", [this](const std::string& name, luabridge::LuaRef arg2, luabridge::LuaRef arg3) -> std::optional<UIHandle> {
                if (!m_uia) return std::nullopt;
                int timeout = 2000;
                std::string controlType;
                if (!arg2.isNil()) {
                    if (arg2.isNumber()) timeout = arg2.unsafe_cast<int>();
                    else if (arg2.isString()) controlType = arg2.tostring();
                }
                if (!arg3.isNil()) {
                    if (arg3.isString()) controlType = arg3.tostring();
                    else if (arg3.isNumber()) timeout = arg3.unsafe_cast<int>();
                }
                auto res = m_uia->select(name, timeout, controlType);
                if (res) return *res;
                return std::nullopt;
            })
            .addFunction("waitForWindow", [this](const std::string& title, luabridge::LuaRef optTimeout) -> std::optional<UIHandle> {
                if (!m_uia) return std::nullopt;
                int timeout = 5000;
                if (!optTimeout.isNil() && optTimeout.isNumber()) timeout = optTimeout.unsafe_cast<int>();
                auto res = m_uia->waitForWindow(title, timeout);
                if (res) return *res;
                return std::nullopt;
            })
            .addFunction("navigate", [this](const std::string& url) -> std::string {
                if (!m_browser) return "Browser automation not available";
                auto res = m_browser->navigate(url);
                if (!res) return res.error();
                return "ok";
            })
            .addFunction("evalJs", [this](const std::string& js) -> std::string {
                if (!m_browser) return "Browser automation not available";
                auto res = m_browser->evalJs(js);
                if (!res) return res.error();
                return *res;
            })
            .addFunction("readPage", [this](const std::string& url) -> std::string {
                if (!m_browser) return "Browser automation not available";
                auto navRes = m_browser->navigate(url);
                if (!navRes) return navRes.error();
                
                ::Sleep(2000); // Wait for settle
                if (m_profiles) {
                    const SiteProfile* profile = m_profiles->match(url);
                    if (profile) {
                        std::string js = "(() => { let out = {};\n";
                        for (const auto& item : profile->items) {
                            js += std::format("try {{\n  let els = document.querySelectorAll('{}');\n", item.selector);
                            js += std::format("  if (els.length > 0) {{\n");
                            std::string extJs;
                            if (item.extract == "text") extJs = "e.innerText";
                            else if (item.extract == "html") extJs = "e.innerHTML";
                            else extJs = std::format("e.getAttribute('{}')", item.extract);
                            
                            if (item.isArray) {
                                js += std::format("    out['{}'] = Array.from(els).map(e => {}).filter(x => x);\n", item.name, extJs);
                            } else {
                                js += std::format("    out['{}'] = (e => {}) (els[0]);\n", item.name, extJs);
                            }
                            js += "  }\n} catch(e){}\n";
                        }
                        js += "return JSON.stringify(out);\n})()";
                        auto evalRes = m_browser->evalJs(js);
                        if (evalRes) return *evalRes;
                    }
                }
                // Fallback
                auto domRes = m_browser->evalJs("document.body.innerText");
                if (domRes) return *domRes;
                return "error";
            })
        .endNamespace();
}
