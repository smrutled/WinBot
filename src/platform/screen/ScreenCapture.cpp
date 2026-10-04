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

#include "platform/screen/ScreenCapture.h"
#include "Common.h"
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
    if (width <= 0 || height <= 0) {
        return std::unexpected("Invalid image dimensions");
    }
    size_t expectedBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    if (bgra.size() < expectedBytes) {
        return std::unexpected("Buffer too small for image dimensions");
    }

    // Convert BGRA → RGBA (stb expects RGBA)
    std::vector<uint8_t> rgba(expectedBytes);
    for (size_t i = 0; i + 3 < expectedBytes; i += 4) {
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        rgba[i + 0] = bgra[i + 2]; // R
        rgba[i + 1] = bgra[i + 1]; // G
        rgba[i + 2] = bgra[i + 0]; // B
        rgba[i + 3] = bgra[i + 3]; // A
        // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    }

    std::vector<uint8_t> pngBytes;
    pngBytes.reserve(static_cast<size_t>(width) * static_cast<size_t>(height));

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

    HDC memDc = ::CreateCompatibleDC(srcDc);
    if (memDc == nullptr) {
        return std::unexpected("CreateCompatibleDC failed");
    }

    HBITMAP bmp = ::CreateCompatibleBitmap(srcDc, w, h);
    if (bmp == nullptr) {
        ::DeleteDC(memDc);
        return std::unexpected("CreateCompatibleBitmap failed");
    }

    auto *old = reinterpret_cast<HBITMAP>(::SelectObject(memDc, bmp));

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
    if (!pngResult) {
        return std::unexpected(pngResult.error());
    }

    return CaptureResult{ .pngBytes = std::move(*pngResult), .width = w, .height = h };
}

// ──────────────────────────────────────────────────────────────────────────────
std::expected<ScreenCapture::CaptureResult, std::string>
ScreenCapture::captureDesktop() {
    HDC dc = ::GetDC(nullptr);
    if (dc == nullptr) {
        return std::unexpected("GetDC(nullptr) failed");
    }
    int x  = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y  = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w  = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h  = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    auto result = captureHdc(dc, x, y, w, h);
    ::ReleaseDC(nullptr, dc);
    return result;
}

// ──────────────────────────────────────────────────────────────────────────────
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

static bool isAllBlank(std::span<const uint8_t> bgra, int width, int height) {
    if (bgra.empty() || width <= 0 || height <= 0) {
        return true;
    }
    size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    size_t maxElements = bgra.size() / sizeof(uint32_t);
    count = (std::min)(count, maxElements);
    if (count == 0) {
        return true;
    }

    const auto* p = reinterpret_cast<const uint32_t*>(bgra.data());
    size_t step = (count / 500 > 1) ? (count / 500) : 1;
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    for (size_t i = 0; i < count; i += step) {
        if ((p[i] & 0x00FFFFFF) != 0) {
            return false;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if ((p[i] & 0x00FFFFFF) != 0) {
            return false;
        }
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    return true;
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

    HDC screenDc = ::GetDC(nullptr);
    if (screenDc == nullptr) {
        return std::unexpected("Failed to get screen DC");
    }

    HDC memDc = ::CreateCompatibleDC(screenDc);
    if (memDc == nullptr) {
        ::ReleaseDC(nullptr, screenDc);
        return std::unexpected("CreateCompatibleDC failed");
    }
    HBITMAP bmp = ::CreateCompatibleBitmap(screenDc, ww, wh);
    if (bmp == nullptr) {
        ::DeleteDC(memDc);
        ::ReleaseDC(nullptr, screenDc);
        return std::unexpected("CreateCompatibleBitmap failed");
    }
    auto *oldBmp = reinterpret_cast<HBITMAP>(::SelectObject(memDc, bmp));

    BOOL pwOk = ::PrintWindow(hwnd, memDc, PW_RENDERFULLCONTENT);

    RECT frameRect{};
    HRESULT hr = ::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frameRect, sizeof(frameRect));

    int finalW = ww;
    int finalH = wh;
    HDC captureDc = memDc;
    HBITMAP cropBmp = nullptr;
    HBITMAP cropOld = nullptr;
    HDC cropDc = nullptr;

    if (SUCCEEDED(hr)) {
        int fw = frameRect.right - frameRect.left;
        int fh = frameRect.bottom - frameRect.top;
        int ox = frameRect.left - winRect.left;
        int oy = frameRect.top - winRect.top;
        if (ox >= 0 && oy >= 0 && fw > 0 && fh > 0 && (ox + fw) <= ww && (oy + fh) <= wh) {
            cropDc = ::CreateCompatibleDC(screenDc);
            cropBmp = (cropDc != nullptr) ? ::CreateCompatibleBitmap(screenDc, fw, fh) : nullptr;
            if (cropDc != nullptr && cropBmp != nullptr) {
                cropOld = reinterpret_cast<HBITMAP>(::SelectObject(cropDc, cropBmp));
                ::BitBlt(cropDc, 0, 0, fw, fh, memDc, ox, oy, SRCCOPY);
                captureDc = cropDc;
                finalW = fw;
                finalH = fh;
            } else {
                if (cropBmp != nullptr) {
                    ::DeleteObject(cropBmp);
                }
                if (cropDc != nullptr) {
                    ::DeleteDC(cropDc);
                }
                cropBmp = nullptr;
                cropDc = nullptr;
            }
        }
    }

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = finalW;
    bi.biHeight = -finalH; // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    std::vector<uint8_t> pixels(static_cast<size_t>(finalW * finalH * 4));
    HBITMAP activeBmp = (cropBmp != nullptr ? cropBmp : bmp);
    ::GetDIBits(captureDc, activeBmp, 0, finalH, pixels.data(),
                reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    if (cropDc != nullptr) {
        ::SelectObject(cropDc, cropOld);
        ::DeleteObject(cropBmp);
        ::DeleteDC(cropDc);
    }
    ::SelectObject(memDc, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(memDc);
    ::ReleaseDC(nullptr, screenDc);

    if (pwOk == 0 || isAllBlank(pixels, finalW, finalH)) {
        return std::unexpected("Offscreen render failed or produced empty image");
    }

    auto pngResult = encodePng(pixels, finalW, finalH);
    if (!pngResult) {
        return std::unexpected(pngResult.error());
    }

    return CaptureResult{ .pngBytes = std::move(*pngResult), .width = finalW, .height = finalH };
}

static void bringWindowToForeground(HWND hwnd) {
    if (hwnd == nullptr || ::IsWindow(hwnd) == 0) {
        return;
    }

    if (::IsIconic(hwnd) != 0) {
        ::ShowWindow(hwnd, SW_RESTORE);
    } else if (::IsWindowVisible(hwnd) == 0) {
        ::ShowWindow(hwnd, SW_SHOW);
    }

    HWND fgWnd = ::GetForegroundWindow();
    DWORD fgThread = (fgWnd != nullptr) ? ::GetWindowThreadProcessId(fgWnd, nullptr) : 0;
    DWORD targetThread = ::GetWindowThreadProcessId(hwnd, nullptr);
    DWORD curThread = ::GetCurrentThreadId();

    if (fgThread != 0 && fgThread != curThread) {
        ::AttachThreadInput(curThread, fgThread, TRUE);
    }
    if (targetThread != 0 && targetThread != curThread && targetThread != fgThread) {
        ::AttachThreadInput(curThread, targetThread, TRUE);
    }

    ::SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    ::BringWindowToTop(hwnd);
    ::SetForegroundWindow(hwnd);

    if (targetThread != 0 && targetThread != curThread && targetThread != fgThread) {
        ::AttachThreadInput(curThread, targetThread, FALSE);
    }
    if (fgThread != 0 && fgThread != curThread) {
        ::AttachThreadInput(curThread, fgThread, FALSE);
    }

    ::Sleep(80);
}

std::expected<ScreenCapture::CaptureResult, std::string>
ScreenCapture::captureWindowForeground(HWND hwnd) {
    bringWindowToForeground(hwnd);

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

    HDC desktopDc = ::GetDC(nullptr);
    auto result = captureHdc(desktopDc, rect.left, rect.top, w, h);
    ::ReleaseDC(nullptr, desktopDc);
    return result;
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
    // Build lowercase wide query
    std::wstring wq = utf8_to_wide(titleSubstr);
    to_lower_inplace(wq);

    struct Candidate { HWND hwnd; int score; };
    Candidate best{ .hwnd = nullptr, .score = 0 };
    auto ctx_sc = std::make_pair(&wq, &best);
    ::EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        auto* ctx = reinterpret_cast<std::pair<std::wstring*, Candidate*>*>(lp);
        const std::wstring& q = *ctx->first;
        Candidate& best       = *ctx->second;

        if (::IsWindowVisible(h) == 0) {
            return TRUE;
        }
        std::array<wchar_t, 512> buf{};
        ::GetWindowTextW(h, buf.data(), static_cast<int>(buf.size()));
        if (buf.at(0) == L'\0') {
            return TRUE;
        }

        std::wstring titleLow(buf.data());
        to_lower_inplace(titleLow);

        int score = 0;
        if (titleLow == q) {
            score = 3;
        } else if (titleLow.starts_with(q)) {
            score = 2;
        } else if (titleLow.contains(q)) {
            score = 1;
        }

        if (score > best.score) {
            best = { .hwnd = h, .score = score };
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx_sc));

    if (best.hwnd == nullptr) {
        return std::unexpected(std::format("Window '{}' not found", titleSubstr));
    }
    return captureWindow(best.hwnd, bringToFront);
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
    std::string encoded;
    encoded.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) {
            n |= static_cast<uint32_t>(data[i + 1]) << 8;
        }
        if (i + 2 < data.size()) {
            n |= static_cast<uint32_t>(data[i + 2]);
        }
        // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        encoded += b64.at((n >> 18) & 0x3F);
        encoded += b64.at((n >> 12) & 0x3F);
        encoded += (i + 1 < data.size()) ? b64.at((n >> 6) & 0x3F) : '=';
        encoded += (i + 2 < data.size()) ? b64.at(n & 0x3F) : '=';
    }
    return encoded;
}
