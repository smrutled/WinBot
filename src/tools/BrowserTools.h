#ifndef WINBOT_TOOLS_BROWSERTOOLS_H
#define WINBOT_TOOLS_BROWSERTOOLS_H

#include "Common.h"
#include "core/ITool.h"

// Forward declarations
class BrowserAutomation;
class SiteProfileRegistry;

namespace tools {
// CDP browser automation tools
// All functions require a BrowserAutomation instance to be passed in

ToolResult browserNavigate(BrowserAutomation& browser, std::string_view url);
ToolResult browserClick(BrowserAutomation& browser, std::string_view selector);
ToolResult browserType(BrowserAutomation& browser, std::string_view selector, std::string_view text);
ToolResult browserGetDom(BrowserAutomation& browser);
ToolResult browserGetPageText(BrowserAutomation& browser);
ToolResult browserScreenshot(BrowserAutomation& browser);
ToolResult browserScroll(BrowserAutomation& browser, std::string_view direction, int amount);
ToolResult browserEval(BrowserAutomation& browser, std::string_view js);
ToolResult browserWaitFor(BrowserAutomation& browser, std::string_view selector, int timeoutMs = 5000);

} // namespace tools

// ── Browser ITool Endpoints ──────────────────────────────────────────────────

class BrowserNavigateTool : public ITool {
public:
    explicit BrowserNavigateTool(BrowserAutomation& browser) : m_browser(&browser) {}
    explicit BrowserNavigateTool(BrowserAutomation* browser = nullptr) : m_browser(browser) {}

    [[nodiscard]] std::string name() const override { return "browser_navigate"; }
    [[nodiscard]] std::string description() const override {
        return "Navigate the browser to a URL";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    BrowserAutomation* m_browser{nullptr};
};

class BrowserClickTool : public ITool {
public:
    explicit BrowserClickTool(BrowserAutomation& browser) : m_browser(&browser) {}
    explicit BrowserClickTool(BrowserAutomation* browser = nullptr) : m_browser(browser) {}

    [[nodiscard]] std::string name() const override { return "browser_click"; }
    [[nodiscard]] std::string description() const override {
        return "Click a DOM element by CSS selector";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    BrowserAutomation* m_browser{nullptr};
};

class BrowserTypeTool : public ITool {
public:
    explicit BrowserTypeTool(BrowserAutomation& browser) : m_browser(&browser) {}
    explicit BrowserTypeTool(BrowserAutomation* browser = nullptr) : m_browser(browser) {}

    [[nodiscard]] std::string name() const override { return "browser_type"; }
    [[nodiscard]] std::string description() const override {
        return "Type text into a browser input field";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    BrowserAutomation* m_browser{nullptr};
};

class BrowserGetDomTool : public ITool {
public:
    explicit BrowserGetDomTool(BrowserAutomation& browser) : m_browser(&browser) {}
    explicit BrowserGetDomTool(BrowserAutomation* browser = nullptr) : m_browser(browser) {}

    [[nodiscard]] std::string name() const override { return "browser_get_dom"; }
    [[nodiscard]] std::string description() const override {
        return "Get a simplified DOM of the current page";
    }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    BrowserAutomation* m_browser{nullptr};
};

class BrowserGetPageTextTool : public ITool {
public:
    explicit BrowserGetPageTextTool(BrowserAutomation& browser) : m_browser(&browser) {}
    explicit BrowserGetPageTextTool(BrowserAutomation* browser = nullptr) : m_browser(browser) {}

    [[nodiscard]] std::string name() const override { return "browser_get_page_text"; }
    [[nodiscard]] std::string description() const override {
        return "Get the raw readable text of the current page";
    }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    BrowserAutomation* m_browser{nullptr};
};

class BrowserReadPageTool : public ITool {
public:
    BrowserReadPageTool(BrowserAutomation& browser, SiteProfileRegistry& siteProfiles)
        : m_browser(&browser), m_siteProfiles(&siteProfiles) {}
    BrowserReadPageTool(BrowserAutomation* browser = nullptr, SiteProfileRegistry* siteProfiles = nullptr)
        : m_browser(browser), m_siteProfiles(siteProfiles) {}

    [[nodiscard]] std::string name() const override { return "browser_read_page"; }
    [[nodiscard]] std::string description() const override {
        return "Navigate to a URL and extract its content. If a site profile exists, "
               "extracts structured data. Otherwise falls back to raw page text.";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    BrowserAutomation*   m_browser{nullptr};
    SiteProfileRegistry* m_siteProfiles{nullptr};
};

class BrowserSaveProfileTool : public ITool {
public:
    explicit BrowserSaveProfileTool(SiteProfileRegistry& siteProfiles)
        : m_siteProfiles(&siteProfiles) {}
    explicit BrowserSaveProfileTool(SiteProfileRegistry* siteProfiles = nullptr)
        : m_siteProfiles(siteProfiles) {}

    [[nodiscard]] std::string name() const override { return "browser_save_profile"; }
    [[nodiscard]] std::string description() const override {
        return "Save a JSON site profile to the registry for structured extraction in browser_read_page.";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    SiteProfileRegistry* m_siteProfiles{nullptr};
};

class BrowserEvalTool : public ITool {
public:
    explicit BrowserEvalTool(BrowserAutomation& browser) : m_browser(&browser) {}
    explicit BrowserEvalTool(BrowserAutomation* browser = nullptr) : m_browser(browser) {}

    [[nodiscard]] std::string name() const override { return "browser_eval"; }
    [[nodiscard]] std::string description() const override {
        return "Execute JavaScript in the browser and return result";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    BrowserAutomation* m_browser{nullptr};
};

// Explicit initialization anchor
void initBrowserTools();

#endif // WINBOT_TOOLS_BROWSERTOOLS_H
