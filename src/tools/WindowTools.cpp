#include "WindowTools.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <pdh.h>

#pragma comment(lib, "psapi.lib")

namespace tools {

ToolResult getWindowList() {
    std::string result;

    ::EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* out = reinterpret_cast<std::string*>(lp);
        if (!::IsWindowVisible(hwnd)) return TRUE;
        wchar_t title[256]{};
        ::GetWindowTextW(hwnd, title, 256);
        if (title[0] == L'\0') return TRUE;
        DWORD pid = 0;
        ::GetWindowThreadProcessId(hwnd, &pid);
        *out += std::format("[HWND:0x{:08X} PID:{}] {}\n",
            reinterpret_cast<uintptr_t>(hwnd), pid, wide_to_utf8(title));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));

    return ok(result.empty() ? "(no visible windows)" : result);
}

ToolResult focusWindow(std::string_view title) {
    std::wstring wq = utf8_to_wide(title);
    to_lower_inplace(wq);

    struct Candidate { HWND hwnd; int score; };
    Candidate best{ nullptr, 0 };
    auto ctx_f = std::make_pair(&wq, &best);
    ::EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* ctx = reinterpret_cast<std::pair<std::wstring*, Candidate*>*>(lp);
        const std::wstring& q = *ctx->first;
        Candidate& best       = *ctx->second;
        if (!::IsWindowVisible(h)) return TRUE;
        wchar_t buf[512]{}; ::GetWindowTextW(h, buf, 512);
        if (buf[0] == L'\0') return TRUE;
        std::wstring t(buf); to_lower_inplace(t);
        int score = 0;
        if      (t == q)            score = 3;
        else if (t.starts_with(q)) score = 2;
        else if (t.contains(q))    score = 1;
        if (score > best.score) best = { h, score };
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx_f));

    if (!best.hwnd) return err(std::format("Window '{}' not found", title));
    ::ShowWindow(best.hwnd, SW_RESTORE);
    ::SetForegroundWindow(best.hwnd);
    return ok(std::format("Focused window: {}", title));
}

ToolResult closeWindow(std::string_view title) {
    std::wstring wq = utf8_to_wide(title);
    to_lower_inplace(wq);

    struct Candidate { HWND hwnd; int score; };
    Candidate best{ nullptr, 0 };
    auto ctx_c = std::make_pair(&wq, &best);
    ::EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* ctx = reinterpret_cast<std::pair<std::wstring*, Candidate*>*>(lp);
        const std::wstring& q = *ctx->first;
        Candidate& best       = *ctx->second;
        if (!::IsWindowVisible(h)) return TRUE;
        wchar_t buf[512]{}; ::GetWindowTextW(h, buf, 512);
        if (buf[0] == L'\0') return TRUE;
        std::wstring t(buf); to_lower_inplace(t);
        int score = 0;
        if      (t == q)            score = 3;
        else if (t.starts_with(q)) score = 2;
        else if (t.contains(q))    score = 1;
        if (score > best.score) best = { h, score };
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx_c));

    if (!best.hwnd) return err(std::format("Window '{}' not found", title));
    ::PostMessageW(best.hwnd, WM_CLOSE, 0, 0);
    return ok(std::format("Sent WM_CLOSE to: {}", title));
}

ToolResult getClipboard() {
    if (!::OpenClipboard(nullptr)) return err("Cannot open clipboard");
    HANDLE hData = ::GetClipboardData(CF_UNICODETEXT);
    if (!hData) { ::CloseClipboard(); return err("No text in clipboard"); }
    auto* text = static_cast<wchar_t*>(::GlobalLock(hData));
    std::string result = text ? wide_to_utf8(text) : "";
    ::GlobalUnlock(hData);
    ::CloseClipboard();
    return ok(result);
}

ToolResult setClipboard(std::string_view text) {
    std::wstring wtext = utf8_to_wide(text);
    size_t size = (wtext.size() + 1) * sizeof(wchar_t);
    HANDLE hMem = ::GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return err("GlobalAlloc failed");
    auto* dst = static_cast<wchar_t*>(::GlobalLock(hMem));
    std::memcpy(dst, wtext.data(), size);
    ::GlobalUnlock(hMem);
    if (!::OpenClipboard(nullptr)) { ::GlobalFree(hMem); return err("Cannot open clipboard"); }
    ::EmptyClipboard();
    ::SetClipboardData(CF_UNICODETEXT, hMem);
    ::CloseClipboard();
    return ok(std::format("Clipboard set ({} chars)", text.size()));
}

ToolResult getProcesses() {
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return err("CreateToolhelp32Snapshot failed");

    std::string result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (::Process32FirstW(snap, &entry)) {
        do {
            result += std::format("[PID:{}] {}\n",
                entry.th32ProcessID, wide_to_utf8(entry.szExeFile));
        } while (::Process32NextW(snap, &entry));
    }
    ::CloseHandle(snap);
    return ok(result);
}

ToolResult killProcess(std::string_view nameOrPid) {
    DWORD pid = 0;
    // Try as PID first
    try { pid = static_cast<DWORD>(std::stoul(std::string(nameOrPid))); }
    catch (...) {
        // Search by name
        HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        std::wstring wname = utf8_to_wide(nameOrPid);
        if (::Process32FirstW(snap, &entry)) {
            do {
                if (std::wstring_view{ entry.szExeFile } == wname) {
                    pid = entry.th32ProcessID; break;
                }
            } while (::Process32NextW(snap, &entry));
        }
        ::CloseHandle(snap);
    }
    if (!pid) return err(std::format("Process '{}' not found", nameOrPid));

    HANDLE hProc = ::OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProc) return err(std::format("Cannot open process PID {}", pid));
    bool ok_ = ::TerminateProcess(hProc, 1) != 0;
    ::CloseHandle(hProc);
    return ok_ ? ok(std::format("Killed PID {}", pid))
               : err(std::format("TerminateProcess failed for PID {}", pid));
}

ToolResult getSystemInfo() {
    MEMORYSTATUSEX mem{ .dwLength = sizeof(MEMORYSTATUSEX) };
    ::GlobalMemoryStatusEx(&mem);

    SYSTEM_INFO si{};
    ::GetSystemInfo(&si);

    return ok(std::format(
        "CPUs: {}\n"
        "RAM total: {} MB\n"
        "RAM available: {} MB ({}% used)\n",
        si.dwNumberOfProcessors,
        mem.ullTotalPhys / (1024*1024),
        mem.ullAvailPhys / (1024*1024),
        mem.dwMemoryLoad
    ));
}

ToolResult getCursorPosition() {
    POINT pt{};
    ::GetCursorPos(&pt);
    return ok(std::format("Cursor at ({}, {})", pt.x, pt.y));
}

} // namespace tools
