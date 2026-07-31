#include "BrowserTools.h"
#include "../BrowserAutomation.h"

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
