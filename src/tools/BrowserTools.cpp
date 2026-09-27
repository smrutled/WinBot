#include "tools/BrowserTools.h"
#include "platform/browser/BrowserAutomation.h"
#include "services/SiteProfileRegistry.h"
#include <format>

namespace tools {

ToolResult browserNavigate(BrowserAutomation& b, std::string_view url)
    { return b.navigate(url); }

ToolResult browserClick(BrowserAutomation& b, std::string_view selector)
    { return b.clickSelector(selector); }

ToolResult browserType(BrowserAutomation& b, std::string_view selector, std::string_view text)
    { return b.typeInto(selector, text); }

ToolResult browserGetDom(BrowserAutomation& b)
    { return b.getDom(); }

ToolResult browserGetPageText(BrowserAutomation& b)
    { return b.getPageText(); }

ToolResult browserScreenshot(BrowserAutomation& b)
    { return b.captureScreenshot(); }

ToolResult browserScroll(BrowserAutomation& b, std::string_view direction, int amount) {
    std::string js = std::format("window.scrollBy(0, {})",
        (direction == "down" ? amount : -amount) * 100);
    return b.evalJs(js);
}

ToolResult browserEval(BrowserAutomation& b, std::string_view js)
    { return b.evalJs(js); }

ToolResult browserWaitFor(BrowserAutomation& b, std::string_view selector, int timeoutMs)
    { return b.waitForSelector(selector, timeoutMs); }

} // namespace tools

// ── BrowserNavigateTool ──────────────────────────────────────────────────────
json BrowserNavigateTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {{"url", {{"type", "string"}}}}},
        {"required", {"url"}}
    };
}

ToolResult BrowserNavigateTool::execute(const json& args) {
    if (!m_browser) return err("BrowserAutomation not available");
    return tools::browserNavigate(*m_browser, args.value("url", ""));
}

// ── BrowserClickTool ─────────────────────────────────────────────────────────
json BrowserClickTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {{"selector", {{"type", "string"}}}}},
        {"required", {"selector"}}
    };
}

ToolResult BrowserClickTool::execute(const json& args) {
    if (!m_browser) return err("BrowserAutomation not available");
    return tools::browserClick(*m_browser, args.value("selector", ""));
}

// ── BrowserTypeTool ──────────────────────────────────────────────────────────
json BrowserTypeTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"selector", {{"type", "string"}}},
            {"text", {{"type", "string"}}}
        }},
        {"required", {"selector", "text"}}
    };
}

ToolResult BrowserTypeTool::execute(const json& args) {
    if (!m_browser) return err("BrowserAutomation not available");
    return tools::browserType(*m_browser, args.value("selector", ""), args.value("text", ""));
}

// ── BrowserGetDomTool ────────────────────────────────────────────────────────
ToolResult BrowserGetDomTool::execute(const json&) {
    if (!m_browser) return err("BrowserAutomation not available");
    return tools::browserGetDom(*m_browser);
}

// ── BrowserGetPageTextTool ───────────────────────────────────────────────────
ToolResult BrowserGetPageTextTool::execute(const json&) {
    if (!m_browser) return err("BrowserAutomation not available");
    return tools::browserGetPageText(*m_browser);
}

// ── BrowserReadPageTool ──────────────────────────────────────────────────────
json BrowserReadPageTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {{"url", {{"type", "string"}}}}},
        {"required", {"url"}}
    };
}

ToolResult BrowserReadPageTool::execute(const json& args) {
    if (!m_browser) return err("BrowserAutomation not available");
    std::string url = args.value("url", "");
    auto navRes = tools::browserNavigate(*m_browser, url);
    if (!navRes) return navRes;

    ::Sleep(2000); // Wait for content to settle

    if (m_siteProfiles) {
        const SiteProfile* profile = m_siteProfiles->match(url);
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
            return tools::browserEval(*m_browser, js);
        }
    }

    return tools::browserGetPageText(*m_browser);
}

// ── BrowserSaveProfileTool ───────────────────────────────────────────────────
json BrowserSaveProfileTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"name", {{"type", "string"}, {"description", "File name (e.g. 'old_reddit')"}}},
            {"profile", {{"type", "object"}, {"description", "JSON object with 'url_match' and 'extract_items' array"}}}
        }},
        {"required", {"name", "profile"}}
    };
}

ToolResult BrowserSaveProfileTool::execute(const json& args) {
    if (!m_siteProfiles) return err("SiteProfileRegistry not available");
    return m_siteProfiles->saveProfile(args.value("name", ""), args.value("profile", json::object()));
}

// ── BrowserEvalTool ──────────────────────────────────────────────────────────
json BrowserEvalTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {{"js", {{"type", "string"}}}}},
        {"required", {"js"}}
    };
}

ToolResult BrowserEvalTool::execute(const json& args) {
    if (!m_browser) return err("BrowserAutomation not available");
    return tools::browserEval(*m_browser, args.value("js", ""));
}

// ── Self-Registration ────────────────────────────────────────────────────────
REGISTER_TOOL_WITH_DEPS(BrowserNavigateTool, [](const ToolDependencies& d) {
    return std::make_unique<BrowserNavigateTool>(d.browser);
});
REGISTER_TOOL_WITH_DEPS(BrowserClickTool, [](const ToolDependencies& d) {
    return std::make_unique<BrowserClickTool>(d.browser);
});
REGISTER_TOOL_WITH_DEPS(BrowserTypeTool, [](const ToolDependencies& d) {
    return std::make_unique<BrowserTypeTool>(d.browser);
});
REGISTER_TOOL_WITH_DEPS(BrowserGetDomTool, [](const ToolDependencies& d) {
    return std::make_unique<BrowserGetDomTool>(d.browser);
});
REGISTER_TOOL_WITH_DEPS(BrowserGetPageTextTool, [](const ToolDependencies& d) {
    return std::make_unique<BrowserGetPageTextTool>(d.browser);
});
REGISTER_TOOL_WITH_DEPS(BrowserReadPageTool, [](const ToolDependencies& d) {
    return std::make_unique<BrowserReadPageTool>(d.browser, d.siteProfiles);
});
REGISTER_TOOL_WITH_DEPS(BrowserSaveProfileTool, [](const ToolDependencies& d) {
    return std::make_unique<BrowserSaveProfileTool>(d.siteProfiles);
});
REGISTER_TOOL_WITH_DEPS(BrowserEvalTool, [](const ToolDependencies& d) {
    return std::make_unique<BrowserEvalTool>(d.browser);
});

void initBrowserTools() {}
