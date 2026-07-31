#ifndef WINBOT_TOOLS_SCREENTOOLS_H
#define WINBOT_TOOLS_SCREENTOOLS_H
#include "../Common.h"

namespace tools {
// Screen perception tools

ToolResult uiScan();  // Serialize UIA tree of focused window
ToolResult screenshot(); // Capture desktop to PNG, return base64

} // namespace tools

#endif // WINBOT_TOOLS_SCREENTOOLS_H
