#include "BrowserAutomation.h"
#include "tools/InputTools.h"
#include <winhttp.h>
#include <shellapi.h>
#include <chrono>
#include <thread>

BrowserAutomation::BrowserAutomation(int debugPort, std::string_view browserExe)
    : m_port(debugPort), m_browserExe(browserExe)
{
    m_session = ::WinHttpOpen(L"WinBot-CDP/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);

    if (!m_session) {
        WINBOT_WARN("BrowserAutomation: WinHttpOpen failed");
        return;
    }

    // Attempt connection; if it fails, try to launch the browser
    if (!connectToPage()) {
        if (!m_browserExe.empty()) {
            WINBOT_INFO("BrowserAutomation: launching browser...");
            std::wstring args = std::format(L"--remote-debugging-port={} --no-first-run --no-default-browser-check about:blank",
                m_port);
            ::ShellExecuteW(nullptr, L"open", utf8_to_wide(m_browserExe).c_str(),
                args.c_str(), nullptr, SW_SHOW);
            // Wait for the browser to start
            std::this_thread::sleep_for(std::chrono::seconds(2));
            connectToPage();
        }
    }
}

BrowserAutomation::~BrowserAutomation() {
    if (m_wsHandle) ::WinHttpCloseHandle(reinterpret_cast<HINTERNET>(m_wsHandle));
    if (m_conn)     ::WinHttpCloseHandle(reinterpret_cast<HINTERNET>(m_conn));
    if (m_session)  ::WinHttpCloseHandle(reinterpret_cast<HINTERNET>(m_session));
}

std::expected<json, std::string> BrowserAutomation::fetchPageList() {
    // Direct WinHTTP GET to avoid circular dependency with tools::httpGet
    std::wstring host = L"localhost";
    std::wstring path = std::format(L"/json/list");

    HINTERNET sess = ::WinHttpOpen(L"WinBot-CDP/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    if (!sess) return std::unexpected("WinHttpOpen failed");

    HINTERNET conn = ::WinHttpConnect(sess, host.c_str(),
        static_cast<INTERNET_PORT>(m_port), 0);
    HINTERNET req  = conn ? ::WinHttpOpenRequest(conn, L"GET", path.c_str(),
        nullptr, nullptr, nullptr, 0) : nullptr;

    if (!req || !::WinHttpSendRequest(req, nullptr, 0, nullptr, 0, 0, 0) ||
        !::WinHttpReceiveResponse(req, nullptr)) {
        if (req)  ::WinHttpCloseHandle(req);
        if (conn) ::WinHttpCloseHandle(conn);
        ::WinHttpCloseHandle(sess);
        return std::unexpected("Failed to connect to CDP endpoint");
    }

    std::string body;
    DWORD avail = 0, read = 0;
    while (::WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
        std::string chunk(avail, '\0');
        ::WinHttpReadData(req, chunk.data(), avail, &read);
        body.append(chunk.data(), read);
    }
    ::WinHttpCloseHandle(req);
    ::WinHttpCloseHandle(conn);
    ::WinHttpCloseHandle(sess);

    try { return json::parse(body); }
    catch (...) { return std::unexpected("Failed to parse CDP page list"); }
}

bool BrowserAutomation::connectToPage() {
    auto pages = fetchPageList();
    if (!pages || pages->empty()) return false;

    // Find the first page (not DevTools, not extension)
    std::string wsUrl;
    for (const auto& page : *pages) {
        std::string type = page.value("type", "");
        if (type == "page") {
            wsUrl = page.value("webSocketDebuggerUrl", "");
            break;
        }
    }
    if (wsUrl.empty()) return false;

    // Parse the WebSocket URL: ws://localhost:PORT/devtools/page/ID
    // We connect: host=localhost, port=debugPort, path=/devtools/page/ID
    std::wstring wPath;
    auto pathStart = wsUrl.find("/devtools");
    if (pathStart != std::string::npos) {
        wPath = utf8_to_wide(wsUrl.substr(pathStart));
    } else {
        return false;
    }

    HINTERNET conn = ::WinHttpConnect(
        reinterpret_cast<HINTERNET>(m_session),
        L"localhost",
        static_cast<INTERNET_PORT>(m_port),
        0
    );
    if (!conn) return false;
    m_conn = conn;

    HINTERNET req = ::WinHttpOpenRequest(conn, L"GET", wPath.c_str(),
        nullptr, nullptr, nullptr, 0);
    if (!req) return false;

    // Upgrade to WebSocket
    ::WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);
    ::WinHttpAddRequestHeaders(req,
        L"Upgrade: websocket\r\nConnection: Upgrade",
        static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);

    if (!::WinHttpSendRequest(req, nullptr, 0, nullptr, 0, 0, 0) ||
        !::WinHttpReceiveResponse(req, nullptr)) {
        ::WinHttpCloseHandle(req);
        return false;
    }

    HINTERNET ws = ::WinHttpWebSocketCompleteUpgrade(req, 0);
    ::WinHttpCloseHandle(req);
    if (!ws) return false;

    m_wsHandle  = ws;
    m_connected = true;
    WINBOT_INFO("BrowserAutomation: connected to CDP on port {}", m_port);
    return true;
}

bool BrowserAutomation::wsSend(std::string_view message) {
    if (!m_wsHandle) return false;
    DWORD err = ::WinHttpWebSocketSend(
        reinterpret_cast<HINTERNET>(m_wsHandle),
        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        const_cast<void*>(reinterpret_cast<const void*>(message.data())),
        static_cast<DWORD>(message.size())
    );
    return (err == ERROR_SUCCESS);
}

std::expected<std::string, std::string> BrowserAutomation::wsReceive(int /*timeoutMs*/) {
    if (!m_wsHandle) return std::unexpected("Not connected");

    std::string response;
    response.resize(65536);
    DWORD bytesRead = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE bufType{};

    DWORD err = ::WinHttpWebSocketReceive(
        reinterpret_cast<HINTERNET>(m_wsHandle),
        response.data(),
        static_cast<DWORD>(response.size()),
        &bytesRead,
        &bufType
    );
    if (err != ERROR_SUCCESS) return std::unexpected(std::format("wsReceive error: {}", err));
    response.resize(bytesRead);
    return response;
}

std::expected<json, std::string> BrowserAutomation::sendCdpCommand(
    std::string_view method, json params)
{
    if (!m_connected) return std::unexpected("Not connected to browser");

    json cmd = {
        { "id",     m_msgId++ },
        { "method", method    },
        { "params", params    }
    };
    if (!wsSend(cmd.dump())) return std::unexpected("WebSocket send failed");

    // Wait for our response (matching id)
    auto start = std::chrono::steady_clock::now();
    while (true) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > std::chrono::seconds(10)) return std::unexpected("CDP timeout");

        auto raw = wsReceive();
        if (!raw) return std::unexpected(raw.error());

        try {
            json resp = json::parse(*raw);
            if (resp.contains("id") && resp["id"] == cmd["id"]) {
                if (resp.contains("error")) {
                    return std::unexpected(resp["error"].value("message", "CDP error"));
                }
                return resp.value("result", json::object());
            }
        } catch (...) {}
    }
}

ToolResult BrowserAutomation::navigate(std::string_view url) {
    auto result = sendCdpCommand("Page.navigate", { {"url", url} });
    if (!result) return err(result.error());
    // Wait for load
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return ok(std::format("Navigated to {}", url));
}

ToolResult BrowserAutomation::evalJs(std::string_view js) {
    auto result = sendCdpCommand("Runtime.evaluate", {
        {"expression",    js   },
        {"returnByValue", true }
    });
    if (!result) return err(result.error());
    try {
        auto& rv = (*result)["result"];
        if (rv.contains("value")) return ok(rv["value"].dump());
        return ok(rv.dump());
    } catch (...) { return ok("{}"); }
}

ToolResult BrowserAutomation::clickSelector(std::string_view selector) {
    // Attempt to trace human mouse movement if element has bounding box
    std::string posJs = std::format(
        "(() => {{"
        "  let el = document.querySelector('{}');"
        "  if (!el) return 'null';"
        "  let rect = el.getBoundingClientRect();"
        "  return JSON.stringify({{"
        "    x: Math.round(window.screenX + rect.left + rect.width/2),"
        "    y: Math.round(window.screenY + (window.outerHeight - window.innerHeight) + rect.top + rect.height/2)"
        "  }});"
        "}})()", selector);
    
    if (auto res = evalJs(posJs); res && *res != "null" && !res->empty()) {
        try {
            // Check if returned JSON has x,y
            auto parsed = json::parse(*res);
            if (parsed.contains("x") && parsed.contains("y")) {
                int sx = parsed["x"].get<int>();
                int sy = parsed["y"].get<int>();
                (void)tools::humanMouseMove(sx, sy);
                ::Sleep(50);
            }
        } catch (...) {}
    }

    // Always dispatch the actual click via JS to guarantee execution even if browser is in background
    std::string js = std::format(
        "(() => {{ let el = document.querySelector('{}'); "
        "if (!el) return 'not found'; el.click(); return 'clicked'; }})()",
        selector);
    return evalJs(js);
}

ToolResult BrowserAutomation::typeInto(std::string_view selector, std::string_view text) {
    // Focus the element first
    std::string focusJs = std::format(
        "document.querySelector('{}')?.focus()", selector);
    (void)evalJs(focusJs);

    // Type each character using CDP Input.dispatchKeyEvent
    for (char c : text) {
        std::string ch(1, c);
        (void)sendCdpCommand("Input.dispatchKeyEvent", {
            {"type", "char"}, {"text", ch}
        });
    }
    return ok(std::format("Typed '{}' into '{}'", text, selector));
}

ToolResult BrowserAutomation::getDom() {
    // Get a simplified DOM as text (richer structure)
    std::string js = R"js(
        (() => {
            function extract(el, depth) {
                if (depth > 8) return '';
                const tag = el.tagName?.toLowerCase() || '';
                if (['script','style','head','noscript','meta','link','svg'].includes(tag)) return '';
                
                // Get direct text content only (not children's text)
                let text = '';
                if (el.childNodes) {
                    text = Array.from(el.childNodes)
                        .filter(n => n.nodeType === 3)
                        .map(n => n.textContent)
                        .join(' ').trim().replace(/\s+/g, ' ');
                }
                if (text.length > 80) text = text.substring(0, 80) + '...';
                
                const role = el.getAttribute?.('role') || '';
                const id   = el.id ? '#' + el.id : '';
                const clsRaw = el.className && typeof el.className === 'string' ? el.className : '';
                const cls  = clsRaw ? '.' + clsRaw.split(/\s+/).filter(c=>c).join('.') : '';
                
                const ariaLabel = el.getAttribute?.('aria-label') || '';
                const href = el.getAttribute?.('href') || '';
                
                let attrs = '';
                if (role) attrs += ` role=${role}`;
                if (ariaLabel) attrs += ` aria-label="${ariaLabel}"`;
                if (href) attrs += ` href="${href}"`;

                let line = '';
                if (id || cls || attrs || text) {
                    line = `[${tag}${id}${cls}${attrs}] ${text}\n`;
                }

                let children = '';
                for (const child of el.children || []) {
                    children += extract(child, depth+1);
                }
                return line + children;
            }
            return extract(document.body, 0).substring(0, 16384);
        })()
    )js";
    return evalJs(js);
}

ToolResult BrowserAutomation::getPageText() {
    std::string js = "document.body.innerText.substring(0, 16384)";
    return evalJs(js);
}

ToolResult BrowserAutomation::captureScreenshot() {
    auto result = sendCdpCommand("Page.captureScreenshot", {
        {"format", "png"}, {"quality", 80}
    });
    if (!result) return err(result.error());
    std::string base64 = result->value("data", "");
    if (base64.empty()) return err("Screenshot returned empty data");
    return ok(std::format("screenshot:base64:{}", base64));
}

ToolResult BrowserAutomation::waitForSelector(std::string_view selector, int timeoutMs) {
    auto start = std::chrono::steady_clock::now();
    std::string js = std::format(
        "!!document.querySelector('{}')", selector);

    while (true) {
        auto result = evalJs(js);
        if (result && *result == "true") return ok(std::format("Element '{}' found", selector));

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > std::chrono::milliseconds(timeoutMs))
            return err(std::format("Timeout waiting for '{}'", selector));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}
