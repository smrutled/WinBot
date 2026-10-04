#include "tools/ScreenTools.h"
#include "platform/uia/UIAutomationScanner.h"
#include "platform/screen/ScreenCapture.h"

namespace tools {

ToolResult uiScan() {
    UIAutomationScanner uia;
    auto result = uia.scanFocusedWindow();
    if (!result) {
        return err(result.error());
    }
    return ok(UIAutomationScanner::serialize(*result));
}

ToolResult screenshot() {
    auto result = ScreenCapture::captureDesktop();
    if (!result) {
        return err(result.error());
    }
    return ok(std::format("Screenshot: {}x{} ({} bytes)", result->width, result->height, result->pngBytes.size()));
}

} // namespace tools

// ── ScreenshotDesktopTool ────────────────────────────────────────────────────
ToolResult ScreenshotDesktopTool::execute(const json& /*args*/) {
    auto r = ScreenCapture::captureDesktop();
    if (!r) {
        return err(r.error());
    }
    json result = {
        {"width", r->width},
        {"height", r->height},
        {"format", "png"},
        {"data", ScreenCapture::toBase64(r->pngBytes)}
    };
    return ok(result.dump());
}

// ── ScreenshotWindowTool ─────────────────────────────────────────────────────
json ScreenshotWindowTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"title", {
                {"type", "string"},
                {"description", "Window title substring (case-insensitive)"}
            }},
            {"bring_to_front", {
                {"type", "boolean"},
                {"description", "Optional: bring the window to the foreground before capturing "
                                "(default: false; captures in background without occlusions and "
                                "falls back to foreground if offscreen capture is unsupported)"}
            }}
        }},
        {"required", {"title"}}
    };
}

ToolResult ScreenshotWindowTool::execute(const json& args) {
    if (m_uia == nullptr) {
        return err("UIAutomationScanner not available");
    }

    std::string title = args.value("title", "");
    bool bringToFront = args.value("bring_to_front", false);

    auto tree = m_uia->scanWindow(title);
    if (!tree) {
        return err(tree.error());
    }

    HWND hwnd = tree->ownerHwnd;
    if (hwnd == nullptr) {
        return err(std::format("Found UIA element for '{}' but no HWND", title));
    }

    auto r = ScreenCapture::captureWindow(hwnd, bringToFront);
    if (!r) {
        return err(r.error());
    }

    json result = {
        {"width", r->width},
        {"height", r->height},
        {"format", "png"},
        {"data", ScreenCapture::toBase64(r->pngBytes)}
    };
    return ok(result.dump());
}

// ── ScreenshotElementTool ────────────────────────────────────────────────────
json ScreenshotElementTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"left",   {{"type", "integer"}, {"description", "Left edge in screen pixels"}}},
            {"top",    {{"type", "integer"}, {"description", "Top edge in screen pixels"}}},
            {"right",  {{"type", "integer"}, {"description", "Right edge in screen pixels"}}},
            {"bottom", {{"type", "integer"}, {"description", "Bottom edge in screen pixels"}}}
        }},
        {"required", {"left", "top", "right", "bottom"}}
    };
}

ToolResult ScreenshotElementTool::execute(const json& args) {
    RECT region{
        .left = args.value("left", 0),
        .top = args.value("top", 0),
        .right = args.value("right", 0),
        .bottom = args.value("bottom", 0)
    };
    if (region.right <= region.left || region.bottom <= region.top) {
        return err("Invalid region: right must be > left and bottom must be > top");
    }

    auto r = ScreenCapture::captureRegion(region);
    if (!r) {
        return err(r.error());
    }

    json result = {
        {"width", r->width},
        {"height", r->height},
        {"format", "png"},
        {"data", ScreenCapture::toBase64(r->pngBytes)}
    };
    return ok(result.dump());
}

// ── Self-Registration ────────────────────────────────────────────────────────
REGISTER_TOOL(ScreenshotDesktopTool);
REGISTER_TOOL_WITH_DEPS(ScreenshotWindowTool, [](const ToolDependencies& d) {
    return std::make_unique<ScreenshotWindowTool>(d.uia);
});
REGISTER_TOOL(ScreenshotElementTool);

void initScreenTools() {}
