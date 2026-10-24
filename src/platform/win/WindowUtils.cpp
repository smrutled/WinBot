#include "platform/win/WindowUtils.h"
#include <array>
#include <regex>
#include <string>

namespace winbot {

HWND find_best_window(std::string_view query) {
    if (query.empty()) {
        return nullptr;
    }

    // ── Regex mode: "/pattern/" or "/pattern" ─────────────────────────────────
    if (query.size() >= 2 && query.front() == '/') {
        std::string pat(query.substr(1));
        if (!pat.empty() && pat.back() == '/') {
            pat.pop_back();
        }
        try {
            std::wregex re(utf8_to_wide(pat), std::regex_constants::icase);
            HWND found = nullptr;
            enum_windows([&](HWND h) {
                if (::IsWindowVisible(h) == 0) {
                    return true;
                }
                std::array<wchar_t, 512> buf{};
                int len = ::GetWindowTextW(h, buf.data(), static_cast<int>(buf.size()));
                if (len <= 0) {
                    return true;
                }
                std::wstring_view title(buf.data(), static_cast<size_t>(len));
                if (std::regex_search(title.begin(), title.end(), re)) {
                    found = h;
                    return false; // Stop enumeration immediately on regex match
                }
                return true;
            });
            return found;
        } catch (...) {
            return nullptr; // Invalid regex pattern
        }
    }

    // ── Scored substring mode: exact (3), prefix (2), contains (1) ─────────────
    std::wstring wq = utf8_to_wide(query);
    to_lower_inplace(wq);

    HWND bestHwnd = nullptr;
    int bestScore = 0;

    enum_windows([&](HWND h) {
        if (::IsWindowVisible(h) == 0) {
            return true;
        }
        std::array<wchar_t, 512> buf{};
        int len = ::GetWindowTextW(h, buf.data(), static_cast<int>(buf.size()));
        if (len <= 0) {
            return true;
        }

        std::wstring titleLow(buf.data(), static_cast<size_t>(len));
        to_lower_inplace(titleLow);

        int score = 0;
        if (titleLow == wq) {
            score = 3; // Exact match
        } else if (titleLow.starts_with(wq)) {
            score = 2; // Prefix match
        } else if (titleLow.contains(wq)) {
            score = 1; // Substring match
        }

        if (score > bestScore) {
            bestScore = score;
            bestHwnd = h;
            if (score == 3) {
                return false; // Can't beat exact match; stop enumeration early!
            }
        }
        return true;
    });

    return bestHwnd;
}

void bring_to_foreground(HWND hwnd) {
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

} // namespace winbot
