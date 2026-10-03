#include "tools/ShellTools.h"
#include <winhttp.h>
#include <array>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace tools {

// ── run_command ───────────────────────────────────────────────────────────────
ToolResult runCommand(std::string_view cmd, std::string_view shell, int timeoutMs, std::stop_token stopToken) {
    // Clamp: a negative timeout would become a ~49-day wait after the DWORD cast.
    if (timeoutMs < 0) timeoutMs = 30000;
    std::string fullCmd;
    if (shell == "powershell") {
        fullCmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command " + std::string(cmd) + " 2>&1";
    } else if (shell == "pwsh") {
        fullCmd = "pwsh.exe -NoProfile -ExecutionPolicy Bypass -Command " + std::string(cmd) + " 2>&1";
    } else {
        fullCmd = "cmd.exe /C " + std::string(cmd) + " 2>&1";
    }

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
    if (!::CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
        return err("CreatePipe failed");

    // Ensure the write handle is not inherited by the child's stdout reader
    ::SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb          = sizeof(si);
    si.hStdOutput  = hWritePipe;
    si.hStdError   = hWritePipe;
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::wstring wcmd = utf8_to_wide(fullCmd);
    if (!::CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr,
            TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        ::CloseHandle(hReadPipe);
        ::CloseHandle(hWritePipe);
        return err(std::format("CreateProcess failed: {}", ::GetLastError()));
    }

    HANDLE hJob = ::CreateJobObjectW(nullptr, nullptr);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ::SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        ::AssignProcessToJobObject(hJob, pi.hProcess);
    }

    ::CloseHandle(hWritePipe); // Close our copy of write end

    // Wait for the process in small slices while draining the pipe, so
    // timeoutMs is honored even when the child never closes stdout, and
    // cooperative cancellation (stopToken) can terminate the process immediately.
    constexpr size_t kMaxOutputBytes = 4u * 1024u * 1024u; // 4 MiB cap
    constexpr DWORD kSliceMs = 100;
    std::string output;
    std::array<char, 4096> buf{};
    bool truncated = false;

    auto drainPipe = [&]() {
        // PeekNamedPipe guard: never block here, and don't hang if a
        // grandchild inherited the pipe handle.
        for (;;) {
            DWORD avail = 0;
            if (!::PeekNamedPipe(hReadPipe, nullptr, 0, nullptr, &avail, nullptr) || avail == 0)
                break;
            DWORD toRead = (std::min)(avail, static_cast<DWORD>(buf.size() - 1));
            DWORD bytesRead = 0;
            if (!::ReadFile(hReadPipe, buf.data(), toRead, &bytesRead, nullptr) || bytesRead == 0)
                break;
            if (output.size() < kMaxOutputBytes) {
                size_t room = kMaxOutputBytes - output.size();
                size_t take = (std::min)(static_cast<size_t>(bytesRead), room);
                output.append(buf.data(), take);
                if (take < bytesRead) truncated = true;
            } else {
                truncated = true;
            }
        }
    };

    const ULONGLONG deadline = ::GetTickCount64() + static_cast<ULONGLONG>(timeoutMs);
    bool finished = false;
    bool timedOut = false;
    bool cancelled = false;
    while (!finished) {
        if (stopToken.stop_requested()) {
            cancelled = true;
            finished = true;
            break;
        }
        ULONGLONG now = ::GetTickCount64();
        DWORD slice = (deadline > now)
            ? static_cast<DWORD>((std::min)(deadline - now, static_cast<ULONGLONG>(kSliceMs)))
            : 0;
        DWORD wr = ::WaitForSingleObject(pi.hProcess, slice);
        drainPipe();
        if (wr == WAIT_OBJECT_0) {
            finished = true;
        } else if (::GetTickCount64() >= deadline) {
            timedOut = true;
            finished = true;
        }
    }

    if (cancelled || timedOut) {
        if (hJob) {
            ::TerminateJobObject(hJob, 1);
        } else {
            ::TerminateProcess(pi.hProcess, 1);
        }
        ::WaitForSingleObject(pi.hProcess, 1000);
        drainPipe(); // collect anything flushed on death
    }

    DWORD exitCode = 0;
    ::GetExitCodeProcess(pi.hProcess, &exitCode);
    if (hJob) ::CloseHandle(hJob);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(hReadPipe);

    if (truncated) output += "\n[WinBot: output truncated at 4 MiB]";
    if (cancelled) {
        return err(std::format("Command cancelled (process terminated)\n{}",
                               output.empty() ? "(no output)" : output));
    }
    if (timedOut) {
        return err(std::format("Command timed out after {} ms (process terminated)\n{}",
                               timeoutMs, output.empty() ? "(no output)" : output));
    }
    if (exitCode != 0) {
        // Non-zero exit doesn't mean failure — return output + exit code
        return ok(std::format("Exit {}\n{}", exitCode, output));
    }
    return ok(output.empty() ? "(no output)" : output);
}

// ── http_get ──────────────────────────────────────────────────────────────────
ToolResult httpGet(std::string_view url, std::string_view /*headers*/) {
    // Parse URL
    std::wstring wurl = utf8_to_wide(url);
    URL_COMPONENTSW comps{};
    comps.dwStructSize     = sizeof(comps);
    wchar_t scheme[32]{}, host[256]{}, path[2048]{};
    comps.lpszScheme       = scheme; comps.dwSchemeLength    = sizeof(scheme)/sizeof(wchar_t);
    comps.lpszHostName     = host;   comps.dwHostNameLength  = sizeof(host)/sizeof(wchar_t);
    comps.lpszUrlPath      = path;   comps.dwUrlPathLength   = sizeof(path)/sizeof(wchar_t);
    if (!::WinHttpCrackUrl(wurl.c_str(), 0, 0, &comps))
        return err("Failed to parse URL");

    bool isHttps = (comps.nScheme == INTERNET_SCHEME_HTTPS);
    HINTERNET session = ::WinHttpOpen(L"WinBot/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!session) return err("WinHttpOpen failed");

    HINTERNET conn = ::WinHttpConnect(session, host, comps.nPort, 0);
    if (!conn) { ::WinHttpCloseHandle(session); return err("WinHttpConnect failed"); }

    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = ::WinHttpOpenRequest(conn, L"GET", path,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req || !::WinHttpSendRequest(req, nullptr, 0, nullptr, 0, 0, 0) ||
        !::WinHttpReceiveResponse(req, nullptr)) {
        ::WinHttpCloseHandle(req);
        ::WinHttpCloseHandle(conn);
        ::WinHttpCloseHandle(session);
        return err("HTTP request failed");
    }

    std::string body;
    DWORD available = 0, downloaded = 0;
    while (::WinHttpQueryDataAvailable(req, &available) && available > 0) {
        std::string chunk(available, '\0');
        ::WinHttpReadData(req, chunk.data(), available, &downloaded);
        body.append(chunk.data(), downloaded);
    }

    ::WinHttpCloseHandle(req);
    ::WinHttpCloseHandle(conn);
    ::WinHttpCloseHandle(session);
    return ok(body);
}

// ── search_web ────────────────────────────────────────────────────────────────
ToolResult searchWeb(std::string_view query) {
    // URL-encode the query
    std::string encoded;
    for (unsigned char c : query) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else {
            encoded += std::format("%{:02X}", static_cast<int>(c));
        }
    }
    std::string url = "https://html.duckduckgo.com/html/?q=" + encoded;
    auto result = httpGet(url);
    if (!result) return result;

    // Very basic extraction: pull text between <a class="result__a"> tags
    std::string& html = *result;
    std::string output;
    size_t pos = 0;
    int count = 0;
    while (count < 5 && pos < html.size()) {
        auto anchor = html.find("result__a", pos);
        if (anchor == std::string::npos) break;
        auto start = html.find('>', anchor) + 1;
        auto end   = html.find("</a>", start);
        if (start == std::string::npos || end == std::string::npos) break;
        std::string text = html.substr(start, end - start);
        // Strip nested tags
        std::string clean;
        bool inTag = false;
        for (char c : text) {
            if (c == '<') inTag = true;
            else if (c == '>') inTag = false;
            else if (!inTag) clean += c;
        }
        if (!clean.empty()) {
            output += std::format("{}. {}\n", ++count, clean);
        }
        pos = end;
    }
    return ok(output.empty() ? "No results found" : output);
}

} // namespace tools

#include "security/PermissionSystem.h"

// ── RunCommandTool ───────────────────────────────────────────────────────────
json RunCommandTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"cmd", {{"type", "string"}, {"description", "Command to execute"}}},
            {"shell", {{"type", "string"}, {"description", "Shell to use: cmd|powershell|pwsh"}, {"default", "cmd"}}},
            {"timeout_ms", {{"type", "integer"}, {"default", 30000}}}
        }},
        {"required", {"cmd"}}
    };
}

ToolResult RunCommandTool::execute(const json& args) {
    return execute(args, std::stop_token{});
}

ToolResult RunCommandTool::execute(const json& args, std::stop_token stopToken) {
    auto cmd = args.value("cmd", "");
    auto shell = args.value("shell", "cmd");
    if (m_perms) {
        auto check = m_perms->checkShellCommand(cmd);
        if (!check) return check;
    }
    return tools::runCommand(cmd, shell, args.value("timeout_ms", 30000), stopToken);
}

// ── HttpGetTool ──────────────────────────────────────────────────────────────
json HttpGetTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"url", {{"type", "string"}}},
            {"headers", {{"type", "string"}, {"default", ""}}}
        }},
        {"required", {"url"}}
    };
}

ToolResult HttpGetTool::execute(const json& args) {
    return tools::httpGet(args.value("url", ""), args.value("headers", ""));
}

// ── SearchWebTool ────────────────────────────────────────────────────────────
json SearchWebTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"query", {{"type", "string"}}}
        }},
        {"required", {"query"}}
    };
}

ToolResult SearchWebTool::execute(const json& args) {
    return tools::searchWeb(args.value("query", ""));
}

// ── Self-Registration ────────────────────────────────────────────────────────
REGISTER_TOOL_WITH_DEPS(RunCommandTool, [](const ToolDependencies& d) {
    return std::make_unique<RunCommandTool>(d.perms);
});
REGISTER_TOOL(HttpGetTool);
REGISTER_TOOL(SearchWebTool);

void initShellTools() {}
