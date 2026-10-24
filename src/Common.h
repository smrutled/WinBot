#ifndef WINBOT_COMMON_H
#define WINBOT_COMMON_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h> // IWYU pragma: export

#include <cstdio>
#include <cctype>
#include <cwctype>
#include <algorithm>
#include <chrono>
#include <concepts>
#include <expected>
#include <format>
#include <memory>
#include <nlohmann/json.hpp> // IWYU pragma: export
#include <print> // IWYU pragma: keep
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

using json = nlohmann::json;

// ── Core result type ────────────────────────────────────────────────────────
// All tool calls return either a string payload or a string error.
// Uses C++23 std::expected — callers MUST handle the error case.
using ToolResult = std::expected<std::string, std::string>;

[[nodiscard]] inline ToolResult ok(std::string value) {
  return ToolResult{std::move(value)};
}

[[nodiscard]] inline ToolResult err(std::string error) {
  return std::unexpected(std::move(error));
}

// ── RAII Windows handle wrapper ─────────────────────────────────────────────
struct HandleDeleter {
  void operator()(HANDLE h) const noexcept {
    if (h != nullptr && h != INVALID_HANDLE_VALUE) {
      ::CloseHandle(h);
    }
  }
};
using UniqueHandle =
    std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleDeleter>;

inline UniqueHandle make_unique_handle(HANDLE h) { return UniqueHandle{h}; }

// ── Generic function deleter for Win32/GDI handles ──────────────────────────
template <auto Fn>
struct FnDeleter {
  void operator()(auto* p) const noexcept {
    if (p != nullptr) {
      Fn(p);
    }
  }
};

using UniqueHdc = std::unique_ptr<std::remove_pointer_t<HDC>, FnDeleter<&::DeleteDC>>;
using UniqueHbitmap = std::unique_ptr<std::remove_pointer_t<HBITMAP>, FnDeleter<&::DeleteObject>>;

struct ReleaseDcDeleter {
  HWND hwnd{nullptr};
  void operator()(HDC dc) const noexcept {
    if (dc != nullptr) {
      ::ReleaseDC(hwnd, dc);
    }
  }
};
using UniqueReleaseDc = std::unique_ptr<std::remove_pointer_t<HDC>, ReleaseDcDeleter>;

class [[nodiscard]] GdiSelectScope {
public:
  GdiSelectScope(HDC dc, HGDIOBJ obj) noexcept
      : m_dc(dc), m_old(::SelectObject(dc, obj)) {}
  ~GdiSelectScope() noexcept {
    if (m_dc != nullptr && m_old != nullptr) {
      ::SelectObject(m_dc, m_old);
    }
  }
  GdiSelectScope(const GdiSelectScope&) = delete;
  GdiSelectScope& operator=(const GdiSelectScope&) = delete;
  GdiSelectScope(GdiSelectScope&& o) noexcept
      : m_dc(std::exchange(o.m_dc, nullptr)), m_old(std::exchange(o.m_old, nullptr)) {}
  GdiSelectScope& operator=(GdiSelectScope&& o) noexcept {
    if (this != &o) {
      if (m_dc != nullptr && m_old != nullptr) {
        ::SelectObject(m_dc, m_old);
      }
      m_dc = std::exchange(o.m_dc, nullptr);
      m_old = std::exchange(o.m_old, nullptr);
    }
    return *this;
  }
private:
  HDC m_dc{nullptr};
  HGDIOBJ m_old{nullptr};
};

// ── COM pointer helper ─────────────────────────────────────────────────────
template <typename T> struct ComDeleter {
  void operator()(T *p) const noexcept {
    if (p != nullptr) {
      p->Release();
    }
  }
};
template <typename T> using ComPtr = std::unique_ptr<T, ComDeleter<T>>;

// ── Generic Polling Helper ──────────────────────────────────────────────────
// Polls until attempt() evaluates to true (or has_value()), or timeout / stopToken.
template <typename F>
auto poll_until(std::chrono::milliseconds timeout,
                std::chrono::milliseconds interval,
                // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
                F&& attempt,
                const std::stop_token& stopToken = {})
    -> std::invoke_result_t<F>
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        auto result = std::invoke(attempt);
        if (static_cast<bool>(result)) {
            return result;
        }
        if (stopToken.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
            return result;
        }
        std::this_thread::sleep_for(interval);
    }
}

// ── String conversions ──────────────────────────────────────────────────────
inline std::string wide_to_utf8(std::wstring_view wide) {
  if (wide.empty()) {
    return {};
  }
  int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                  static_cast<int>(wide.size()), nullptr, 0,
                                  nullptr, nullptr);
  std::string result(len, '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        result.data(), len, nullptr, nullptr);
  return result;
}

inline std::wstring utf8_to_wide(std::string_view utf8) {
  if (utf8.empty()) {
    return {};
  }
  int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                  static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring result(len, L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        result.data(), len);
  return result;
}

// ── Case conversion helpers ────────────────────────────────────────────────
inline void to_lower_inplace(std::string& s) {
    (void)std::ranges::transform(s, s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
}
inline void to_lower_inplace(std::wstring& s) {
    (void)std::ranges::transform(s, s.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
}
inline void to_upper_inplace(std::string& s) {
    (void)std::ranges::transform(s, s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
}

// ── Timestamp helper ────────────────────────────────────────────────────────
inline std::string utc_timestamp() {
  auto now = std::chrono::system_clock::now();
  return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

// ── MCP mode flag ───────────────────────────────────────────────────────────
// When true, all log output is redirected to stderr so that stdout is
// reserved exclusively for newline-delimited JSON protocol messages.
inline bool g_mcpMode = false; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// ── Console logging ─────────────────────────────────────────────────────────
inline void winbot_log_raw(std::string_view tag, std::string_view msg) {
  auto formatted = std::format("[{}] {}\n", tag, msg);
  if (g_mcpMode) {
    (void)std::fwrite(formatted.data(), 1, formatted.size(), stderr);
  } else {
    std::print("{}", formatted);
  }
}

#define WINBOT_LOG(tag, msg, ...)                                              \
  ::winbot_log_raw(tag, std::format(msg, ##__VA_ARGS__))

#define WINBOT_INFO(msg, ...) WINBOT_LOG("WinBot", msg, ##__VA_ARGS__)
#define WINBOT_WARN(msg, ...) WINBOT_LOG("WARN  ", msg, ##__VA_ARGS__)
#define WINBOT_ERROR(msg, ...) WINBOT_LOG("ERROR ", msg, ##__VA_ARGS__)

#endif // WINBOT_COMMON_H
