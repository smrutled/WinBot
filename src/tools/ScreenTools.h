#ifndef WINBOT_TOOLS_SCREENTOOLS_H
#define WINBOT_TOOLS_SCREENTOOLS_H

#include "Common.h"
#include "core/ITool.h"

class UIAutomationScanner;

namespace tools {
// Screen perception tools

ToolResult uiScan();  // Serialize UIA tree of focused window
ToolResult screenshot(); // Capture desktop to PNG, return base64

} // namespace tools

// ── Screen ITool Endpoints ───────────────────────────────────────────────────

class ScreenshotDesktopTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "screenshot"; }
    [[nodiscard]] std::string description() const override {
        return "Capture the desktop and return the image as a base64-encoded PNG";
    }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class ScreenshotWindowTool : public ITool {
public:
    explicit ScreenshotWindowTool(UIAutomationScanner& uia) : m_uia(&uia) {}
    explicit ScreenshotWindowTool(UIAutomationScanner* uia = nullptr) : m_uia(uia) {}

    [[nodiscard]] std::string name() const override { return "screenshot_window"; }
    [[nodiscard]] std::string description() const override {
        return "Capture a specific window by title substring (case-insensitive, partial match OK). "
               "Uses UI Automation to locate the window so casing doesn't matter. "
               "Captures the target window cleanly even if occluded or in the background. "
               "Set optional 'bring_to_front' to true to bring the window to the foreground first.";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    UIAutomationScanner* m_uia{nullptr};
};

class ScreenshotElementTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "screenshot_element"; }
    [[nodiscard]] std::string description() const override {
        return "Capture a specific UI element's screen region as a PNG. "
               "Get the bounds (left,top,right,bottom) from ui_scan or ui_scan_window output "
               "and pass them here to get a tight screenshot of just that element.";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

// Explicit initialization anchor
void initScreenTools();

#endif // WINBOT_TOOLS_SCREENTOOLS_H
