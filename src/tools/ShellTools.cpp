#include "ShellTools.h"
#include <winhttp.h>
#include <array>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace tools {

// ── run_command ───────────────────────────────────────────────────────────────
ToolResult runCommand(std::string_view cmd, std::string_view shell, int timeoutMs) {
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

    ::CloseHandle(hWritePipe); // Close our copy of write end

    // Read output
    std::string output;
    std::array<char, 4096> buf{};
    DWORD bytesRead = 0;
    while (::ReadFile(hReadPipe, buf.data(), static_cast<DWORD>(buf.size() - 1), &bytesRead, nullptr) && bytesRead > 0) {
        output.append(buf.data(), bytesRead);
    }

    ::WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeoutMs));
    DWORD exitCode = 0;
    ::GetExitCodeProcess(pi.hProcess, &exitCode);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(hReadPipe);

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
