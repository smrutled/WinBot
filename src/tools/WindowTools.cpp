#include "tools/WindowTools.h"
#include <array>
#include <tlhelp32.h>
#include <psapi.h>
#include <pdh.h>

#pragma comment(lib, "psapi.lib")

namespace tools {

ToolResult getWindowList() {
    std::string result;

    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr) - Win32 EnumWindows LPARAM context passing
    ::EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* out = reinterpret_cast<std::string*>(lp);
        if (!::IsWindowVisible(hwnd)) return TRUE;
        std::array<wchar_t, 256> title{};
        ::GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
        if (title.front() == L'\0') return TRUE;
        DWORD pid = 0;
        ::GetWindowThreadProcessId(hwnd, &pid);
        *out += std::format("[HWND:0x{:08X} PID:{}] {}\n",
            reinterpret_cast<uintptr_t>(hwnd), pid, wide_to_utf8(title.data()));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)

    return ok(result.empty() ? "(no visible windows)" : result);
}

void bringWindowToForeground(HWND hwnd) {
    if (!hwnd || !::IsWindow(hwnd)) return;

    if (::IsIconic(hwnd)) {
        ::ShowWindow(hwnd, SW_RESTORE);
    } else if (!::IsWindowVisible(hwnd)) {
        ::ShowWindow(hwnd, SW_SHOW);
    }

    HWND fgWnd = ::GetForegroundWindow();
    DWORD fgThread = fgWnd ? ::GetWindowThreadProcessId(fgWnd, nullptr) : 0;
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

ToolResult focusWindow(std::string_view title) {
    std::wstring wq = utf8_to_wide(title);
    to_lower_inplace(wq);

    struct Candidate { HWND hwnd; int score; };
    Candidate best{ nullptr, 0 };
    auto ctx_f = std::make_pair(&wq, &best);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr) - Win32 EnumWindows LPARAM context passing
    ::EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* ctx = reinterpret_cast<std::pair<std::wstring*, Candidate*>*>(lp);
        const std::wstring& q = *ctx->first;
        Candidate& best       = *ctx->second;
        if (!::IsWindowVisible(h)) return TRUE;
        std::array<wchar_t, 512> buf{};
        ::GetWindowTextW(h, buf.data(), static_cast<int>(buf.size()));
        if (buf.front() == L'\0') return TRUE;
        std::wstring t(buf.data()); to_lower_inplace(t);
        int score = 0;
        if      (t == q)            score = 3;
        else if (t.starts_with(q)) score = 2;
        else if (t.contains(q))    score = 1;
        if (score > best.score) best = { h, score };
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx_f));
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)

    if (!best.hwnd) return err(std::format("Window '{}' not found", title));
    bringWindowToForeground(best.hwnd);
    return ok(std::format("Focused window: {}", title));
}

ToolResult closeWindow(std::string_view title) {
    std::wstring wq = utf8_to_wide(title);
    to_lower_inplace(wq);

    struct Candidate { HWND hwnd; int score; };
    Candidate best{ nullptr, 0 };
    auto ctx_c = std::make_pair(&wq, &best);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr) - Win32 EnumWindows LPARAM context passing
    ::EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* ctx = reinterpret_cast<std::pair<std::wstring*, Candidate*>*>(lp);
        const std::wstring& q = *ctx->first;
        Candidate& best       = *ctx->second;
        if (!::IsWindowVisible(h)) return TRUE;
        std::array<wchar_t, 512> buf{};
        ::GetWindowTextW(h, buf.data(), static_cast<int>(buf.size()));
        if (buf.front() == L'\0') return TRUE;
        std::wstring t(buf.data()); to_lower_inplace(t);
        int score = 0;
        if      (t == q)            score = 3;
        else if (t.starts_with(q)) score = 2;
        else if (t.contains(q))    score = 1;
        if (score > best.score) best = { h, score };
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx_c));
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)

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
    if (!dst) {
        ::GlobalFree(hMem);
        return err("GlobalLock failed");
    }
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
    for (BOOL hasProcess = ::Process32FirstW(snap, &entry); hasProcess != FALSE; hasProcess = ::Process32NextW(snap, &entry)) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay) - szExeFile is a fixed C-array from Win32 API
        result += std::format("[PID:{}] {}\n", entry.th32ProcessID, wide_to_utf8(entry.szExeFile));
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
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            std::wstring wname = utf8_to_wide(nameOrPid);
            for (BOOL hasProcess = ::Process32FirstW(snap, &entry); hasProcess != FALSE; hasProcess = ::Process32NextW(snap, &entry)) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay) - szExeFile is a fixed C-array from Win32 API
                if (std::wstring_view{ entry.szExeFile } == wname) {
                    pid = entry.th32ProcessID;
                    break;
                }
            }
            ::CloseHandle(snap);
        }
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
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(MEMORYSTATUSEX);
    ::GlobalMemoryStatusEx(&mem);

    SYSTEM_INFO si{};
    ::GetSystemInfo(&si);

    return ok(std::format(
        "CPUs: {}\n"
        "RAM total: {} MB\n"
        "RAM available: {} MB ({}% used)\n",
        si.dwNumberOfProcessors,
        mem.ullTotalPhys / (1024ULL * 1024ULL),
        mem.ullAvailPhys / (1024ULL * 1024ULL),
        mem.dwMemoryLoad
    ));
}

ToolResult getCursorPosition() {
    POINT pt{};
    ::GetCursorPos(&pt);
    return ok(std::format("Cursor at ({}, {})", pt.x, pt.y));
}

} // namespace tools

#include "security/PermissionSystem.h"

// ── WindowListTool ───────────────────────────────────────────────────────────
ToolResult WindowListTool::execute(const json& /*args*/) {
    return tools::getWindowList();
}

// ── FocusWindowTool ──────────────────────────────────────────────────────────
json FocusWindowTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"title", {{"type", "string"}, {"description", "Window title substring"}}}
        }},
        {"required", {"title"}}
    };
}

ToolResult FocusWindowTool::execute(const json& args) {
    return tools::focusWindow(args.value("title", ""));
}

// ── CloseWindowTool ──────────────────────────────────────────────────────────
json CloseWindowTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"title", {{"type", "string"}}}
        }},
        {"required", {"title"}}
    };
}

ToolResult CloseWindowTool::execute(const json& args) {
    return tools::closeWindow(args.value("title", ""));
}

// ── GetClipboardTool ─────────────────────────────────────────────────────────
ToolResult GetClipboardTool::execute(const json& /*args*/) {
    return tools::getClipboard();
}

// ── SetClipboardTool ─────────────────────────────────────────────────────────
json SetClipboardTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"text", {{"type", "string"}}}
        }},
        {"required", {"text"}}
    };
}

ToolResult SetClipboardTool::execute(const json& args) {
    return tools::setClipboard(args.value("text", ""));
}

// ── GetProcessesTool ─────────────────────────────────────────────────────────
ToolResult GetProcessesTool::execute(const json& /*args*/) {
    return tools::getProcesses();
}

// ── KillProcessTool ──────────────────────────────────────────────────────────
json KillProcessTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"name_or_pid", {{"type", "string"}}}
        }},
        {"required", {"name_or_pid"}}
    };
}

ToolResult KillProcessTool::execute(const json& args) {
    auto nameOrPid = args.value("name_or_pid", "");
    if (m_perms) {
        auto check = m_perms->checkProcess(nameOrPid);
        if (!check) return check;
    }
    return tools::killProcess(nameOrPid);
}

// ── GetSystemInfoTool ────────────────────────────────────────────────────────
ToolResult GetSystemInfoTool::execute(const json& /*args*/) {
    return tools::getSystemInfo();
}

// ── GetCursorPositionTool ────────────────────────────────────────────────────
ToolResult GetCursorPositionTool::execute(const json& /*args*/) {
    return tools::getCursorPosition();
}

// ── Self-Registration ────────────────────────────────────────────────────────
REGISTER_TOOL(WindowListTool);
REGISTER_TOOL(FocusWindowTool);
REGISTER_TOOL(CloseWindowTool);
REGISTER_TOOL(GetClipboardTool);
REGISTER_TOOL(SetClipboardTool);
REGISTER_TOOL(GetProcessesTool);
REGISTER_TOOL_WITH_DEPS(KillProcessTool, [](const ToolDependencies& d) {
    return std::make_unique<KillProcessTool>(d.perms);
});
REGISTER_TOOL(GetSystemInfoTool);
REGISTER_TOOL(GetCursorPositionTool);

void initWindowTools() {}
