#ifndef WINBOT_TOOLS_INPUTTOOLS_H
#define WINBOT_TOOLS_INPUTTOOLS_H
#include "../Common.h"

namespace tools {
// Mouse and keyboard control via Win32 SendInput

ToolResult click(int x, int y, std::string_view button = "left", bool humanMove = false);
ToolResult doubleClick(int x, int y, bool humanMove = false);
ToolResult drag(int x1, int y1, int x2, int y2, bool humanMove = false);
ToolResult scroll(int x, int y, int delta, bool humanMove = false);
ToolResult moveMouse(int x, int y, bool humanMove = false);
ToolResult humanMouseMove(int toX, int toY);
ToolResult typeText(std::string_view text);
ToolResult keyPress(std::string_view combo); // e.g. "Ctrl+C", "Enter", "F5"

} // namespace tools

#endif // WINBOT_TOOLS_INPUTTOOLS_H
