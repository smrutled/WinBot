#ifndef WINBOT_PLATFORM_WIN_WINDOWUTILS_H
#define WINBOT_PLATFORM_WIN_WINDOWUTILS_H

#include "Common.h"
#include <concepts>
#include <memory>
#include <string_view>
#include <type_traits>

namespace winbot {

// Concepts-based EnumWindows wrapper
// F: invocable with (HWND) -> bool (return true to continue, false to stop enumeration early)
template <std::predicate<HWND> F>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
void enum_windows(F&& fn) {
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
    ::EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* callable = reinterpret_cast<std::remove_reference_t<F>*>(lp);
        return (*callable)(h) ? TRUE : FALSE;
    }, reinterpret_cast<LPARAM>(std::addressof(fn)));
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
}

// Find the best window matching query:
// - Supports regex if query starts with '/' (e.g. "/Notepad.* /")
// - Otherwise scores by title: exact match (3), prefix match (2), substring (1)
// - Stops enumeration early as soon as an exact match (score 3) is encountered.
[[nodiscard]] HWND find_best_window(std::string_view query);

// Bring a window to the foreground, restoring if minimized and attaching thread inputs if needed
void bring_to_foreground(HWND hwnd);

} // namespace winbot

#endif // WINBOT_PLATFORM_WIN_WINDOWUTILS_H
