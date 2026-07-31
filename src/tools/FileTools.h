#ifndef WINBOT_TOOLS_FILETOOLS_H
#define WINBOT_TOOLS_FILETOOLS_H
#include "../Common.h"

namespace tools {
// File system operations

ToolResult readFile(std::string_view path);
ToolResult writeFile(std::string_view path, std::string_view content);
ToolResult appendFile(std::string_view path, std::string_view content);
ToolResult listDirectory(std::string_view path);
ToolResult deleteFile(std::string_view path);
ToolResult copyFile(std::string_view src, std::string_view dst);

} // namespace tools

#endif // WINBOT_TOOLS_FILETOOLS_H
