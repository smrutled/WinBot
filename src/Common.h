#ifndef WINBOT_COMMON_H
#define WINBOT_COMMON_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>
#include <cctype>
#include <cwctype>
#include <algorithm>
#include <expected>
#include <format>
#include <memory>
#include <nlohmann/json.hpp>
#include <print> // IWYU pragma: keep
#include <string>
#include <string_view>

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
    if (h && h != INVALID_HANDLE_VALUE)
      ::CloseHandle(h);
  }
};
using UniqueHandle =
    std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleDeleter>;

inline UniqueHandle make_unique_handle(HANDLE h) { return UniqueHandle{h}; }

// ── COM pointer helper ─────────────────────────────────────────────────────
template <typename T> struct ComDeleter {
  void operator()(T *p) const noexcept {
    if (p)
      p->Release();
  }
};
template <typename T> using ComPtr = std::unique_ptr<T, ComDeleter<T>>;

// ── String conversions ──────────────────────────────────────────────────────
inline std::string wide_to_utf8(std::wstring_view wide) {
  if (wide.empty())
    return {};
  int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                  static_cast<int>(wide.size()), nullptr, 0,
                                  nullptr, nullptr);
  std::string result(len, '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        result.data(), len, nullptr, nullptr);
  return result;
}

inline std::wstring utf8_to_wide(std::string_view utf8) {
  if (utf8.empty())
    return {};
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
inline bool g_mcpMode = false;

// ── Console logging macros ──────────────────────────────────────────────────
#define WINBOT_LOG(tag, msg, ...)                                              \
  do {                                                                         \
    auto _winbot_msg_ =                                                        \
        std::format("[{}] {}\n", tag, std::format(msg, ##__VA_ARGS__));        \
    if (g_mcpMode)                                                             \
      std::fwrite(_winbot_msg_.data(), 1, _winbot_msg_.size(), stderr);        \
    else                                                                       \
      std::print("{}", _winbot_msg_);                                          \
  } while (0)

#define WINBOT_INFO(msg, ...) WINBOT_LOG("WinBot", msg, ##__VA_ARGS__)
#define WINBOT_WARN(msg, ...) WINBOT_LOG("WARN  ", msg, ##__VA_ARGS__)
#define WINBOT_ERROR(msg, ...) WINBOT_LOG("ERROR ", msg, ##__VA_ARGS__)

#endif // WINBOT_COMMON_H
