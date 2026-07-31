#include "LuaRuntime.h"
#include "BrowserAutomation.h"
#include "UIAutomationScanner.h"
#include "SiteProfileRegistry.h"
#include "tools/InputTools.h"
#include "UIHandle.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <LuaBridge/LuaBridge.h>

LuaRuntime::LuaRuntime(BrowserAutomation& browser, UIAutomationScanner& uia, SiteProfileRegistry& profiles)
    : m_browser(browser), m_uia(uia), m_profiles(profiles) {
    L = luaL_newstate();
    luaL_openlibs(L);
    bindWinBotAPI();
}

LuaRuntime::~LuaRuntime() {
    if (L) lua_close(L);
}

ToolResult LuaRuntime::execString(std::string_view code) {
    if (luaL_dostring(L, std::string(code).c_str()) != LUA_OK) {
        std::string errMsg = lua_tostring(L, -1);
        lua_pop(L, 1);
        return err(errMsg);
    }
    
    // Attempt to return the string value if the script returned something
    if (lua_isstring(L, -1)) {
        std::string res = lua_tostring(L, -1);
        lua_pop(L, 1);
        return ok(res);
    }
    
    // Clear stack
    lua_settop(L, 0);
    return ok("Script executed successfully.");
}

ToolResult LuaRuntime::execFile(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return err(std::format("Script file not found: {}", path.string()));
    }
    
    if (luaL_dofile(L, path.string().c_str()) != LUA_OK) {
        std::string errMsg = lua_tostring(L, -1);
        lua_pop(L, 1);
        return err(errMsg);
    }
    
    if (lua_isstring(L, -1)) {
        std::string res = lua_tostring(L, -1);
        lua_pop(L, 1);
        return ok(res);
    }
    
    lua_settop(L, 0);
    return ok(std::format("Script {} executed successfully.", path.filename().string()));
}

void LuaRuntime::bindWinBotAPI() {
    luabridge::getGlobalNamespace(L)
        .beginClass<UIHandle>("UIHandle")
            .addFunction("click", [](UIHandle* self) { self->click(); })
            .addFunction("clickChild", [](UIHandle* self, const std::string& name) { self->click(name); })
            .addFunction("select", [](UIHandle* self, const std::string& name) -> UIHandle { return self->select(name); })
            .addFunction("type", [](UIHandle* self, const std::string& text) { self->type(text); })
            .addFunction("key", [](UIHandle* self, const std::string& k) { self->key(k); })
        .endClass()
        .beginNamespace("winbot")
            .addFunction("log", [](const std::string& msg) {
                WINBOT_INFO("Lua: {}", msg);
            })
            .addFunction("sleep", [](int ms) {
                ::Sleep(ms);
            })
            .addFunction("shell", [](const std::string& cmd) {
                system(cmd.c_str());
            })
            .addFunction("click", [](int x, int y) {
                (void)tools::click(x, y, "left", true); // Default human move to true for scripting
            })
            .addFunction("typeText", [](const std::string& text) {
                (void)tools::typeText(text);
            })
            .addFunction("pressKey", [](const std::string& key) {
                (void)tools::keyPress(key);
            })
            .addFunction("selectUIA", [this](const std::string& name) -> std::optional<UIHandle> {
                auto res = m_uia.select(name, 2000);
                if (res) return *res;
                return std::nullopt;
            })
            .addFunction("navigate", [this](const std::string& url) -> std::string {
                auto res = m_browser.navigate(url);
                if (!res) return res.error();
                return "ok";
            })
            .addFunction("evalJs", [this](const std::string& js) -> std::string {
                auto res = m_browser.evalJs(js);
                if (!res) return res.error();
                return *res;
            })
            .addFunction("readPage", [this](const std::string& url) -> std::string {
                auto navRes = m_browser.navigate(url);
                if (!navRes) return navRes.error();
                
                ::Sleep(2000); // Wait for settle
                const SiteProfile* profile = m_profiles.match(url);
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
                    auto evalRes = m_browser.evalJs(js);
                    if (evalRes) return *evalRes;
                }
                // Fallback
                auto domRes = m_browser.evalJs("document.body.innerText");
                if (domRes) return *domRes;
                return "error";
            })
        .endNamespace();
}
