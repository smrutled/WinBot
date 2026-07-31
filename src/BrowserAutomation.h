#ifndef WINBOT_BROWSERAUTOMATION_H
#define WINBOT_BROWSERAUTOMATION_H

#include "Common.h"
#include <UIAutomation.h>
#include <string>

// ── BrowserAutomation
// ───────────────────────────────────────────────────────── Controls a running
// Chrome or Edge instance via the Chrome DevTools Protocol (CDP) over a
// WebSocket connection on the debug port.
//
// Launch Chrome with: --remote-debugging-port=9222
class BrowserAutomation {
public:
  explicit BrowserAutomation(int debugPort = 9222,
                             std::string_view browserExe = {});
  ~BrowserAutomation();

  BrowserAutomation(const BrowserAutomation &) = delete;
  BrowserAutomation &operator=(const BrowserAutomation &) = delete;

  // Navigate to a URL
  [[nodiscard]] ToolResult navigate(std::string_view url);

  // Click a DOM element identified by CSS selector or visible text
  [[nodiscard]] ToolResult clickSelector(std::string_view selector);

  // Type text into a focused input element
  [[nodiscard]] ToolResult typeInto(std::string_view selector,
                                    std::string_view text);

  // Return a simplified text representation of the page DOM
  [[nodiscard]] ToolResult getDom();

  // Return the raw readable innerText of the page
  [[nodiscard]] ToolResult getPageText();

  // Capture a screenshot of the browser viewport (returns base64 PNG)
  [[nodiscard]] ToolResult captureScreenshot();

  // Execute JavaScript and return the result as a string
  [[nodiscard]] ToolResult evalJs(std::string_view js);

  // Wait until a selector exists in the DOM, up to timeoutMs
  [[nodiscard]] ToolResult waitForSelector(std::string_view selector,
                                           int timeoutMs = 5000);

  [[nodiscard]] bool isConnected() const noexcept { return m_connected; }

private:
  int m_port{9222};
  std::string m_browserExe;
  bool m_connected{false};
  int m_msgId{1};

  // WinHTTP WebSocket handles (void* to avoid header pollution)
  void *m_session{nullptr};
  void *m_conn{nullptr};
  void *m_wsHandle{nullptr};

  // Connect to the CDP endpoint of the first available page
  bool connectToPage();

  // Send a CDP command and return the parsed result JSON
  [[nodiscard]] std::expected<json, std::string>
  sendCdpCommand(std::string_view method, json params = json::object());

  // Low-level WebSocket send/receive
  [[nodiscard]] bool wsSend(std::string_view message);
  [[nodiscard]] std::expected<std::string, std::string>
  wsReceive(int timeoutMs = 5000);

  // Fetch the list of debuggable pages via HTTP /json/list
  [[nodiscard]] std::expected<json, std::string> fetchPageList();
};

#endif // WINBOT_BROWSERAUTOMATION_H
