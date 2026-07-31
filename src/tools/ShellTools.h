#ifndef WINBOT_TOOLS_SHELLTOOLS_H
#define WINBOT_TOOLS_SHELLTOOLS_H
#include "../Common.h"
#include <string>

namespace tools {
// Shell command execution and web requests

ToolResult runCommand(std::string_view cmd, std::string_view shell = "cmd", int timeoutMs = 30000);
ToolResult httpGet(std::string_view url, std::string_view headers = {});
ToolResult searchWeb(std::string_view query);

} // namespace tools

#endif // WINBOT_TOOLS_SHELLTOOLS_H
