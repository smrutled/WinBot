#ifndef WINBOT_TOOLS_BROWSERTOOLS_H
#define WINBOT_TOOLS_BROWSERTOOLS_H
#include "../Common.h"

// Forward declarations
class BrowserAutomation;

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

#endif // WINBOT_TOOLS_BROWSERTOOLS_H
