// stb_image_write — single header, compiled once here
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996 4505)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <dwmapi.h>
#include <algorithm>
#include <bit>

#include "platform/screen/ScreenCapture.h"
#include "platform/win/WindowUtils.h"
#include "Common.h"
#include <vector>

// ──────────────────────────────────────────────────────────────────────────────
// PNG write callback that appends to a std::vector<uint8_t>
// ──────────────────────────────────────────────────────────────────────────────
static void pngWriteCallback(void* ctx, void* data, int size) {
    auto* vec = static_cast<std::vector<uint8_t>*>(ctx);
    auto* bytes = static_cast<uint8_t*>(data);
    std::span<const uint8_t> span(bytes, size);
    vec->insert(vec->end(), span.begin(), span.end());
}

// ──────────────────────────────────────────────────────────────────────────────
std::expected<std::vector<uint8_t>, std::string>
ScreenCapture::encodePng(std::span<const uint32_t> rgba, int width, int height) {
    if (width <= 0 || height <= 0) {
        return std::unexpected("Invalid image dimensions");
    }
    size_t expectedPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (rgba.size() < expectedPixels) {
        return std::unexpected("Buffer too small for image dimensions");
    }

    std::vector<uint8_t> pngBytes;
    pngBytes.reserve(expectedPixels);

    int ok = stbi_write_png_to_func(
        pngWriteCallback, &pngBytes,
        width, height, 4,
        rgba.data(), width * 4
    );
    if (ok == 0) {
        return std::unexpected("stb_image_write failed");
    }
    return pngBytes;
}

// ──────────────────────────────────────────────────────────────────────────────
std::expected<ScreenCapture::CaptureResult, std::string>
ScreenCapture::captureHdc(HDC srcDc, int x, int y, int w, int h) {
    if (srcDc == nullptr) {
        return std::unexpected("Source DC is null");
    }
    if (w <= 0 || h <= 0) {
        return std::unexpected("Invalid capture dimensions");
    }

    UniqueHdc memDc(::CreateCompatibleDC(srcDc));
    if (!memDc) {
        return std::unexpected("CreateCompatibleDC failed");
    }

    UniqueHbitmap bmp(::CreateCompatibleBitmap(srcDc, w, h));
    if (!bmp) {
        return std::unexpected("CreateCompatibleBitmap failed");
    }

    GdiSelectScope selMem(memDc.get(), bmp.get());

    ::BitBlt(memDc.get(), 0, 0, w, h, srcDc, x, y, SRCCOPY);

    // Extract raw pixel data
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;  // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    size_t pixelCount = static_cast<size_t>(w) * static_cast<size_t>(h);
    std::vector<uint32_t> pixels(pixelCount);
    ::GetDIBits(memDc.get(), bmp.get(), 0, h, pixels.data(),
        &bi, DIB_RGB_COLORS);

    // Swizzle BGRA -> RGBA in place
    for (auto& p : pixels) {
        p = (p & 0xFF00FF00U) | std::rotl(p & 0x00FF00FFU, 16);
    }

    auto pngResult = encodePng(pixels, w, h);
    if (!pngResult) {
        return std::unexpected(pngResult.error());
    }

    return CaptureResult{ .pngBytes = std::move(*pngResult), .width = w, .height = h };
}

// ──────────────────────────────────────────────────────────────────────────────
std::expected<ScreenCapture::CaptureResult, std::string>
ScreenCapture::captureDesktop() {
    UniqueReleaseDc dc(::GetDC(nullptr));
    if (!dc) {
        return std::unexpected("GetDC(nullptr) failed");
    }
    int x  = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y  = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w  = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h  = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return captureHdc(dc.get(), x, y, w, h);
}

// ──────────────────────────────────────────────────────────────────────────────
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

static bool isAllBlank(std::span<const uint32_t> pixels) {
    if (pixels.empty()) {
        return true;
    }
    return std::ranges::none_of(pixels, [](uint32_t p) {
        return (p & 0x00FFFFFFU) != 0;
    });
}

std::expected<ScreenCapture::CaptureResult, std::string>
ScreenCapture::captureWindowOffscreen(HWND hwnd) {
    RECT winRect{};
    if (::GetWindowRect(hwnd, &winRect) == 0) {
        return std::unexpected("GetWindowRect failed");
    }

    int ww = winRect.right - winRect.left;
    int wh = winRect.bottom - winRect.top;
    if (ww <= 0 || wh <= 0) {
        return std::unexpected("Invalid window dimensions");
    }

    UniqueReleaseDc screenDc(::GetDC(nullptr));
    if (!screenDc) {
        return std::unexpected("Failed to get screen DC");
    }

    UniqueHdc memDc(::CreateCompatibleDC(screenDc.get()));
    if (!memDc) {
        return std::unexpected("CreateCompatibleDC failed");
    }
    UniqueHbitmap bmp(::CreateCompatibleBitmap(screenDc.get(), ww, wh));
    if (!bmp) {
        return std::unexpected("CreateCompatibleBitmap failed");
    }
    GdiSelectScope selMem(memDc.get(), bmp.get());

    BOOL pwOk = ::PrintWindow(hwnd, memDc.get(), PW_RENDERFULLCONTENT);

    RECT frameRect{};
    HRESULT hr = ::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frameRect, sizeof(frameRect));

    int finalW = ww;
    int finalH = wh;
    HDC captureDc = memDc.get();
    UniqueHbitmap cropBmp;
    std::optional<GdiSelectScope> selCrop;
    UniqueHdc cropDc;

    if (SUCCEEDED(hr)) {
        int fw = frameRect.right - frameRect.left;
        int fh = frameRect.bottom - frameRect.top;
        int ox = frameRect.left - winRect.left;
        int oy = frameRect.top - winRect.top;
        if (ox >= 0 && oy >= 0 && fw > 0 && fh > 0 && (ox + fw) <= ww && (oy + fh) <= wh) {
            cropDc.reset(::CreateCompatibleDC(screenDc.get()));
            if (cropDc) {
                cropBmp.reset(::CreateCompatibleBitmap(screenDc.get(), fw, fh));
            }
            if (cropDc && cropBmp) {
                selCrop.emplace(cropDc.get(), cropBmp.get());
                ::BitBlt(cropDc.get(), 0, 0, fw, fh, memDc.get(), ox, oy, SRCCOPY);
                captureDc = cropDc.get();
                finalW = fw;
                finalH = fh;
            } else {
                cropBmp.reset();
                cropDc.reset();
            }
        }
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = finalW;
    bi.bmiHeader.biHeight = -finalH; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    size_t pixelCount = static_cast<size_t>(finalW) * static_cast<size_t>(finalH);
    std::vector<uint32_t> pixels(pixelCount);
    HBITMAP activeBmp = cropBmp ? cropBmp.get() : bmp.get();
    ::GetDIBits(captureDc, activeBmp, 0, finalH, pixels.data(),
                &bi, DIB_RGB_COLORS);

    if (pwOk == 0 || isAllBlank(pixels)) {
        return std::unexpected("Offscreen render failed or produced empty image");
    }

    // Swizzle BGRA -> RGBA in place
    for (auto& p : pixels) {
        p = (p & 0xFF00FF00U) | std::rotl(p & 0x00FF00FFU, 16);
    }

    auto pngResult = encodePng(pixels, finalW, finalH);
    if (!pngResult) {
        return std::unexpected(pngResult.error());
    }

    return CaptureResult{ .pngBytes = std::move(*pngResult), .width = finalW, .height = finalH };
}

std::expected<ScreenCapture::CaptureResult, std::string>
ScreenCapture::captureWindowForeground(HWND hwnd) {
    winbot::bring_to_foreground(hwnd);

    RECT rect{};
    HRESULT hr = ::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect));
    if (FAILED(hr)) {
        ::GetWindowRect(hwnd, &rect);
    }

    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) {
        return std::unexpected("Invalid window dimensions");
    }

    UniqueReleaseDc desktopDc(::GetDC(nullptr));
    if (!desktopDc) {
        return std::unexpected("Failed to get desktop DC");
    }
    return captureHdc(desktopDc.get(), rect.left, rect.top, w, h);
}

// ──────────────────────────────────────────────────────────────────────────────
std::expected<ScreenCapture::CaptureResult, std::string>
ScreenCapture::captureWindow(HWND hwnd, bool bringToFront) {
    if (::IsWindow(hwnd) == 0) {
        return std::unexpected("Invalid HWND");
    }

    if (bringToFront) {
        return captureWindowForeground(hwnd);
    }

    // If minimized, restore without activating so the window has a rendering surface
    if (::IsIconic(hwnd) != 0) {
        ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        ::Sleep(50);
    }

    // Try off-screen capture first so occluded or background windows
    // are captured directly without other windows covering them and without stealing focus.
    auto offscreen = captureWindowOffscreen(hwnd);
    if (offscreen) {
        return offscreen;
    }

    // Fallback: bring window to foreground and capture via desktop DC
    return captureWindowForeground(hwnd);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scored window lookup: exact > starts-with > contains (all case-insensitive).
std::expected<ScreenCapture::CaptureResult, std::string>
ScreenCapture::captureWindow(std::string_view titleSubstr, bool bringToFront) {
    HWND hwnd = winbot::find_best_window(titleSubstr);
    if (hwnd == nullptr) {
        return std::unexpected(std::format("Window '{}' not found", titleSubstr));
    }
    return captureWindow(hwnd, bringToFront);
}

// ──────────────────────────────────────────────────────────────────────────────
std::expected<ScreenCapture::CaptureResult, std::string>
ScreenCapture::captureRegion(RECT region) {
    HDC dc = ::GetDC(nullptr);
    auto result = captureHdc(dc,
        region.left, region.top,
        region.right - region.left,
        region.bottom - region.top);
    ::ReleaseDC(nullptr, dc);
    return result;
}

// ──────────────────────────────────────────────────────────────────────────────
std::expected<ScreenCapture::CaptureResult, std::string>
ScreenCapture::scale(const CaptureResult& src, int maxDim) {
    if (src.width <= maxDim && src.height <= maxDim) {
        return src;
    }

    float ratio = static_cast<float>(maxDim) /
        static_cast<float>(std::max(src.width, src.height));
    [[maybe_unused]] auto dstW = static_cast<int>(static_cast<float>(src.width)  * ratio);
    [[maybe_unused]] auto dstH = static_cast<int>(static_cast<float>(src.height) * ratio);

    // Simple nearest-neighbor downscale for speed
    // Decode the PNG back to RGBA first
    // For simplicity, we'll just re-capture at lower resolution
    // A proper implementation would use stb_image_resize
    // TODO: integrate stb_image_resize for proper downscaling
    return src; // Return original if resize not implemented
}

// ──────────────────────────────────────────────────────────────────────────────
std::string ScreenCapture::toBase64(std::span<const uint8_t> data) {
    static constexpr std::string_view b64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (data.empty()) {
        return {};
    }
    size_t outLen = ((data.size() + 2) / 3) * 4;
    std::string encoded(outLen, '=');
    size_t outIdx = 0;
    size_t inIdx = 0;

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    while (inIdx + 3 <= data.size()) {
        uint32_t n = (static_cast<uint32_t>(data[inIdx]) << 16) |
                     (static_cast<uint32_t>(data[inIdx + 1]) << 8) |
                     static_cast<uint32_t>(data[inIdx + 2]);
        encoded[outIdx++] = b64[(n >> 18) & 0x3F];
        encoded[outIdx++] = b64[(n >> 12) & 0x3F];
        encoded[outIdx++] = b64[(n >> 6) & 0x3F];
        encoded[outIdx++] = b64[n & 0x3F];
        inIdx += 3;
    }

    if (inIdx < data.size()) {
        uint32_t n = static_cast<uint32_t>(data[inIdx]) << 16;
        bool hasSecond = (inIdx + 1 < data.size());
        if (hasSecond) {
            n |= static_cast<uint32_t>(data[inIdx + 1]) << 8;
        }
        encoded[outIdx++] = b64[(n >> 18) & 0x3F];
        encoded[outIdx++] = b64[(n >> 12) & 0x3F];
        if (hasSecond) {
            encoded[outIdx++] = b64[(n >> 6) & 0x3F];
        }
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    return encoded;
}
