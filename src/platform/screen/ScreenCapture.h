#ifndef WINBOT_TOOLS_SCREENCAPTURE_H
#define WINBOT_TOOLS_SCREENCAPTURE_H
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

    // Capture a specific window's client/frame area by HWND.
    // If bringToFront is true, brings the window to the foreground first.
    // If bringToFront is false (default), attempts off-screen capture via PrintWindow
    // so occluded or background windows are captured cleanly without stealing focus,
    // and automatically falls back to foreground capture if offscreen capture is unsupported.
    [[nodiscard]] static std::expected<CaptureResult, std::string> captureWindow(
        HWND hwnd, bool bringToFront = false);

    // Capture a specific window by its title substring
    [[nodiscard]] static std::expected<CaptureResult, std::string> captureWindow(
        std::string_view titleSubstr, bool bringToFront = false);

    // Capture a specific region of the screen
    [[nodiscard]] static std::expected<CaptureResult, std::string> captureRegion(RECT region);

    // Scale a captured image to reduce token count for vision models.
    // maxDim = maximum width or height in pixels (proportional scaling).
    [[nodiscard]] static std::expected<CaptureResult, std::string> scale(
        const CaptureResult& src, int maxDim = 1280);

    // Encode binary data to Base64 string
    [[nodiscard]] static std::string toBase64(std::span<const uint8_t> data);

private:
    // Encode a raw BGRA DIB to PNG bytes using stb_image_write
    [[nodiscard]] static std::expected<std::vector<uint8_t>, std::string> encodePng(
        std::span<const uint8_t> bgra, int width, int height);

    // Capture given an HDC source and target region
    [[nodiscard]] static std::expected<CaptureResult, std::string> captureHdc(
        HDC srcDc, int x, int y, int w, int h);

    // Off-screen capture via PrintWindow (PW_RENDERFULLCONTENT)
    [[nodiscard]] static std::expected<CaptureResult, std::string> captureWindowOffscreen(HWND hwnd);

    // Foreground capture via Desktop DC BitBlt
    [[nodiscard]] static std::expected<CaptureResult, std::string> captureWindowForeground(HWND hwnd);
};

#endif // WINBOT_TOOLS_SCREENCAPTURE_H
