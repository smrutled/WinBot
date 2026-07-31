#include "ScreenTools.h"
#include "../UIAutomationScanner.h"
#include "../ScreenCapture.h"

// Global instances shared with Agent — initialized via extern references
// In a full build these would be passed via dependency injection;
// for the tool functions we access them through a simple singleton accessor.

namespace tools {

// These functions are called through the ToolRegistry which has access to
// the Agent's UIAutomationScanner and ScreenCapture instances.
// The actual implementations are in Agent.cpp's registered lambdas.
// These stubs exist to satisfy the header declaration.

ToolResult uiScan() {
    UIAutomationScanner uia;
    auto result = uia.scanFocusedWindow();
    if (!result) return err(result.error());
    return ok(UIAutomationScanner::serialize(*result));
}

ToolResult screenshot() {
    auto result = ScreenCapture::captureDesktop();
    if (!result) return err(result.error());
    return ok(std::format("Screenshot: {}x{} ({} bytes)", result->width, result->height, result->pngBytes.size()));
}

} // namespace tools
