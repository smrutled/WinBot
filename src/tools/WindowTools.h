#ifndef WINBOT_TOOLS_WINDOWTOOLS_H
#define WINBOT_TOOLS_WINDOWTOOLS_H
#include "../Common.h"

namespace tools {
// Window management tools

ToolResult getWindowList();
ToolResult focusWindow(std::string_view title);
ToolResult closeWindow(std::string_view title);
ToolResult getClipboard();
ToolResult setClipboard(std::string_view text);
ToolResult getProcesses();
ToolResult killProcess(std::string_view nameOrPid);
ToolResult getSystemInfo();
ToolResult getCursorPosition();

} // namespace tools

#endif // WINBOT_TOOLS_WINDOWTOOLS_H
