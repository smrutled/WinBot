// stb_image_write — single header, compiled once here
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"
#include <dwmapi.h>
#include <algorithm>
#include <regex>

#include "ScreenCapture.h"
#include <vector>

// ──────────────────────────────────────────────────────────────────────────────
// PNG write callback that appends to a std::vector<uint8_t>
// ──────────────────────────────────────────────────────────────────────────────
static void pngWriteCallback(void* ctx, void* data, int size) {
    auto* vec = reinterpret_cast<std::vector<uint8_t>*>(ctx);
    auto* bytes = reinterpret_cast<uint8_t*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}

// ──────────────────────────────────────────────────────────────────────────────
std::expected<std::vector<uint8_t>, std::string>
ScreenCapture::encodePng(std::span<const uint8_t> bgra, int width, int height) {
    // Convert BGRA → RGBA (stb expects RGBA)
    std::vector<uint8_t> rgba(bgra.size());
    for (size_t i = 0; i + 3 < bgra.size(); i += 4) {
        rgba[i + 0] = bgra[i + 2]; // R
        rgba[i + 1] = bgra[i + 1]; // G
        rgba[i + 2] = bgra[i + 0]; // B
        rgba[i + 3] = bgra[i + 3]; // A
    }

    std::vector<uint8_t> pngBytes;
    pngBytes.reserve(static_cast<size_t>(width * height));

    int ok = stbi_write_png_to_func(
        pngWriteCallback, &pngBytes,
        width, height, 4,
        rgba.data(), width * 4
    );
    if (!ok) return std::unexpected("stb_image_write failed");
    return pngBytes;
}

// ──────────────────────────────────────────────────────────────────────────────
std::expected<ScreenCapture::CaptureResult, std::string>
ScreenCapture::captureHdc(HDC srcDc, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return std::unexpected("Invalid capture dimensions");

    HDC memDc  = ::CreateCompatibleDC(srcDc);
    HBITMAP bmp = ::CreateCompatibleBitmap(srcDc, w, h);
    HBITMAP old = reinterpret_cast<HBITMAP>(::SelectObject(memDc, bmp));

    ::BitBlt(memDc, 0, 0, w, h, srcDc, x, y, SRCCOPY);

    // Extract raw pixel data
    BITMAPINFOHEADER bi{};
    bi.biSize        = sizeof(bi);
    bi.biWidth       = w;
    bi.biHeight      = -h;  // top-down
    bi.biPlanes      = 1;
    bi.biBitCount    = 32;
    bi.biCompression = BI_RGB;

    std::vector<uint8_t> pixels(static_cast<size_t>(w * h * 4));
    ::GetDIBits(memDc, bmp, 0, h, pixels.data(),
        reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    ::SelectObject(memDc, old);
    ::DeleteObject(bmp);
    ::DeleteDC(memDc);

    auto pngResult = encodePng(pixels, w, h);
    if (!pngResult) return std::unexpected(pngResult.error());

    return CaptureResult{ std::move(*pngResult), w, h };
}

// ──────────────────────────────────────────────────────────────────────────────
std::expected<ScreenCapture::CaptureResult, std::string>
ScreenCapture::captureDesktop() {
    HDC dc = ::GetDC(nullptr);
    int x  = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y  = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w  = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h  = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    auto result = captureHdc(dc, x, y, w, h);
    ::ReleaseDC(nullptr, dc);
    return result;
}

// ──────────────────────────────────────────────────────────────────────────────
std::expected<ScreenCapture::CaptureResult, std::string>
ScreenCapture::captureWindow(HWND hwnd) {
    if (!::IsWindow(hwnd)) return std::unexpected("Invalid HWND");

    // Check if the window is minimized
    if (::IsIconic(hwnd)) {
        return std::unexpected("Window is minimized; cannot capture its contents.");
    }

    // Use DWM attribute to get the actual visual bounds (excluding drop shadows)
    RECT rect{};
    HRESULT hr = ::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect));
    if (FAILED(hr)) {
        // Fallback to standard window rect if DWM fails
        ::GetWindowRect(hwnd, &rect);
    }

    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;

    if (w <= 0 || h <= 0) return std::unexpected("Invalid window dimensions");

    // Capture from the Desktop DC is more reliable for hardware-accelerated windows
    HDC desktopDc = ::GetDC(nullptr);
    auto result = captureHdc(desktopDc, rect.left, rect.top, w, h);
    ::ReleaseDC(nullptr, desktopDc);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scored window lookup: exact > starts-with > contains (all case-insensitive).
std::expected<ScreenCapture::CaptureResult, std::string>
ScreenCapture::captureWindow(std::string_view titleSubstr) {
    // Build lowercase wide query
    std::wstring wq = utf8_to_wide(titleSubstr);
    to_lower_inplace(wq);

    struct Candidate { HWND hwnd; int score; };
    Candidate best{ nullptr, 0 };
    auto ctx_sc = std::make_pair(&wq, &best);
    ::EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* ctx = reinterpret_cast<std::pair<std::wstring*, Candidate*>*>(lp);
        const std::wstring& q = *ctx->first;
        Candidate& best       = *ctx->second;

        if (!::IsWindowVisible(h)) return TRUE;
        wchar_t buf[512]{};
        ::GetWindowTextW(h, buf, 512);
        if (buf[0] == L'\0') return TRUE;

        std::wstring titleLow(buf);
        to_lower_inplace(titleLow);

        int score = 0;
        if      (titleLow == q)              score = 3;
        else if (titleLow.starts_with(q))   score = 2;
        else if (titleLow.contains(q))      score = 1;

        if (score > best.score) best = { h, score };
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx_sc));

    if (!best.hwnd)
        return std::unexpected(std::format("Window '{}' not found", titleSubstr));
    return captureWindow(best.hwnd);
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
    if (src.width <= maxDim && src.height <= maxDim) return src;

    float ratio = static_cast<float>(maxDim) /
        static_cast<float>(std::max(src.width, src.height));
    int dstW = static_cast<int>(src.width  * ratio);
    int dstH = static_cast<int>(src.height * ratio);

    // Simple nearest-neighbor downscale for speed
    // Decode the PNG back to RGBA first
    // For simplicity, we'll just re-capture at lower resolution
    // A proper implementation would use stb_image_resize
    // TODO: integrate stb_image_resize for proper downscaling
    return src; // Return original if resize not implemented
}
