#ifndef WINBOT_SCREENCAPTURE_H
#define WINBOT_SCREENCAPTURE_H
#include "Common.h"
#include <vector>
#include <span>

// ── ScreenCapture ─────────────────────────────────────────────────────────────
// Captures the desktop (or a specific window) using GDI BitBlt and encodes
// the result as PNG bytes in memory (no disk writes during normal operation).
class ScreenCapture {
public:
    struct CaptureResult {
        std::vector<uint8_t> pngBytes;  // PNG-encoded image data
        int width{};
        int height{};
    };

    // Capture the entire virtual desktop (all monitors)
    [[nodiscard]] static std::expected<CaptureResult, std::string> captureDesktop();

    // Capture a specific window's client area by HWND
    [[nodiscard]] static std::expected<CaptureResult, std::string> captureWindow(HWND hwnd);

    // Capture a specific window by its title substring
    [[nodiscard]] static std::expected<CaptureResult, std::string> captureWindow(
        std::string_view titleSubstr);

    // Capture a specific region of the screen
    [[nodiscard]] static std::expected<CaptureResult, std::string> captureRegion(RECT region);

    // Scale a captured image to reduce token count for vision models.
    // maxDim = maximum width or height in pixels (proportional scaling).
    [[nodiscard]] static std::expected<CaptureResult, std::string> scale(
        const CaptureResult& src, int maxDim = 1280);

private:
    // Encode a raw BGRA DIB to PNG bytes using stb_image_write
    [[nodiscard]] static std::expected<std::vector<uint8_t>, std::string> encodePng(
        std::span<const uint8_t> bgra, int width, int height);

    // Capture given an HDC source and target region
    [[nodiscard]] static std::expected<CaptureResult, std::string> captureHdc(
        HDC srcDc, int x, int y, int w, int h);
};

#endif // WINBOT_SCREENCAPTURE_H
