#include "TestHelpers.h"
#include "core/ToolRegistry.h"
#include "platform/screen/ScreenCapture.h"
#include "platform/uia/UIADebugger.h"
#include "platform/uia/UIAutomationScanner.h"
#include "platform/uia/UIHandle.h"
#include "services/LuaRuntime.h"
#include "services/LuaToolLoader.h"
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <windows.h>

// ── In-Process Native Win32 Test GUI Window
// ─────────────────────────────────── Creates a 100% self-contained native GUI
// window with button, edit, and label controls to test UI automation and event
// firing reliably across all environments.
class MockWin32Window {
public:
  static inline std::atomic<bool> s_buttonClicked{false};
  static inline std::atomic<bool> s_textChanged{false};
  static inline HWND s_hwndMain{nullptr};
  static inline HWND s_hwndBtn{nullptr};
  static inline HWND s_hwndEdit{nullptr};
  static inline HWND s_hwndLabel{nullptr};

  MockWin32Window() {
    s_buttonClicked = false;
    s_textChanged = false;
    m_thread = std::thread(&MockWin32Window::windowThreadProc, this);

    // Wait until window is ready
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!m_isReady && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  MockWin32Window(const MockWin32Window &) = delete;
  MockWin32Window &operator=(const MockWin32Window &) = delete;
  MockWin32Window(MockWin32Window &&) = delete;
  MockWin32Window &operator=(MockWin32Window &&) = delete;

  ~MockWin32Window() {
    if (s_hwndMain) {
      ::PostMessageW(s_hwndMain, WM_CLOSE, 0, 0);
    }
    if (m_thread.joinable()) {
      m_thread.join();
    }
  }

  [[nodiscard]] bool isReady() const noexcept { return m_isReady.load(); }
  [[nodiscard]] HWND getHwnd() const noexcept { return s_hwndMain; }

private:
  std::thread m_thread;
  std::atomic<bool> m_isReady{false};

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                  LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND: {
      WORD id = LOWORD(wParam);
      WORD code = HIWORD(wParam);
      if (id == 101) { // Button clicked
        s_buttonClicked = true;
        if (s_hwndLabel) {
          ::SetWindowTextW(s_hwndLabel, L"Status: Clicked!");
        }
      } else if (id == 102 && code == EN_CHANGE) { // Text changed
        s_textChanged = true;
      }
      break;
    }
    case WM_DESTROY:
      ::PostQuitMessage(0);
      return 0;
    default:
      break;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
  }

  void windowThreadProc() {
    HINSTANCE hInst = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"WinBotMockAppClass";
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    ::RegisterClassExW(&wc);

    s_hwndMain =
        ::CreateWindowExW(0, L"WinBotMockAppClass", L"WinBot Native Test App",
                          WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 450, 320,
                          nullptr, nullptr, hInst, nullptr);

    if (!s_hwndMain)
      return;

    s_hwndBtn = ::CreateWindowExW(
        0, L"BUTTON", L"Click Me", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20,
        20, 140, 40, s_hwndMain, reinterpret_cast<HMENU>(101), hInst, nullptr);

    s_hwndEdit = ::CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        20, 80, 240, 35, s_hwndMain, reinterpret_cast<HMENU>(102), hInst,
        nullptr);

    s_hwndLabel = ::CreateWindowExW(
        0, L"STATIC", L"Status: Idle", WS_CHILD | WS_VISIBLE, 20, 140, 240, 35,
        s_hwndMain, reinterpret_cast<HMENU>(103), hInst, nullptr);

    ::ShowWindow(s_hwndMain, SW_SHOW);
    ::UpdateWindow(s_hwndMain);
    m_isReady = true;

    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0)) {
      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
    }

    s_hwndMain = nullptr;
  }
};

// ── Test 1: Native Win32 Test App (Runs everywhere, fully deterministic)
// ──────
TEST(LiveGUITest, NativeWin32App_ButtonClickAndStateVerification) {
  MockWin32Window mockApp;
  ASSERT_TRUE(mockApp.isReady()) << "Failed to initialize mock Win32 window";

  UIAutomationScanner scanner;

  // 1. Scan window by handle
  auto winRes = scanner.scanWindowByHandle(mockApp.getHwnd());
  ASSERT_TRUE(winRes.has_value())
      << "Failed to scan native test window: " << winRes.error();

  UIHandle appHandle(std::move(*winRes), &scanner);

  // 2. Select button and click it
  EXPECT_NO_THROW({
    UIHandle btn = appHandle.select("Click Me", 2000);
    EXPECT_EQ(btn.element().controlType, "Button");
    btn.click();
  });

  // Give window procedure a moment to process click message
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  // 3. Verify that the click was processed by the native application!
  EXPECT_TRUE(MockWin32Window::s_buttonClicked.load());

  // 4. Verify label text changed to "Status: Clicked!"
  wchar_t labelText[128]{};
  ::GetWindowTextW(MockWin32Window::s_hwndLabel, labelText, 128);
  EXPECT_STREQ(labelText, L"Status: Clicked!");
}

TEST(LiveGUITest, NativeWin32App_DebuggerDotChaining) {
  MockWin32Window mockApp;
  ASSERT_TRUE(mockApp.isReady()) << "Failed to initialize mock Win32 window";

  UIAutomationScanner scanner;
  UIADebugger debugger(scanner);

  auto winRes = scanner.scanWindowByHandle(mockApp.getHwnd());
  ASSERT_TRUE(winRes.has_value());
  debugger.setVar("$app", UIHandle(std::move(*winRes), &scanner));

  // Reset button clicked flag
  MockWin32Window::s_buttonClicked = false;

  // Execute dot-chain through UIADebugger
  bool ok = debugger.execute("$app.Click(\"Click Me\")");
  EXPECT_TRUE(ok);

  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_TRUE(MockWin32Window::s_buttonClicked.load());
}

// ── ScreenCapture captureWindow Tests
// ─────────────────────────────────────────
TEST(LiveGUITest, CaptureWindow_Offscreen_MockWin32Window) {
  MockWin32Window mockApp;
  ASSERT_TRUE(mockApp.isReady()) << "Failed to initialize mock Win32 window";

  auto res = ScreenCapture::captureWindow(mockApp.getHwnd(), false);
  ASSERT_TRUE(res.has_value())
      << "Offscreen capture failed: " << (res ? "" : res.error());
  EXPECT_GT(res->width, 0);
  EXPECT_GT(res->height, 0);
  EXPECT_FALSE(res->pngBytes.empty());
  // Verify PNG header: 0x89 'P' 'N' 'G'
  ASSERT_GE(res->pngBytes.size(), 8u);
  EXPECT_EQ(res->pngBytes[0], 0x89);
  EXPECT_EQ(res->pngBytes[1], 'P');
  EXPECT_EQ(res->pngBytes[2], 'N');
  EXPECT_EQ(res->pngBytes[3], 'G');
}

TEST(LiveGUITest, CaptureWindow_BringToFront_MockWin32Window) {
  MockWin32Window mockApp;
  ASSERT_TRUE(mockApp.isReady()) << "Failed to initialize mock Win32 window";

  auto res = ScreenCapture::captureWindow(mockApp.getHwnd(), true);
  ASSERT_TRUE(res.has_value())
      << "Foreground capture failed: " << (res ? "" : res.error());
  EXPECT_GT(res->width, 0);
  EXPECT_GT(res->height, 0);
  EXPECT_FALSE(res->pngBytes.empty());
  ASSERT_GE(res->pngBytes.size(), 8u);
  EXPECT_EQ(res->pngBytes[0], 0x89);
}

TEST(LiveGUITest, CaptureWindow_MinimizedWindow_MockWin32Window) {
  MockWin32Window mockApp;
  ASSERT_TRUE(mockApp.isReady()) << "Failed to initialize mock Win32 window";

  ::ShowWindow(mockApp.getHwnd(), SW_MINIMIZE);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_TRUE(::IsIconic(mockApp.getHwnd()));

  auto res = ScreenCapture::captureWindow(mockApp.getHwnd(), false);
  ASSERT_TRUE(res.has_value())
      << "Capture of minimized window failed: " << (res ? "" : res.error());
  EXPECT_GT(res->width, 0);
  EXPECT_GT(res->height, 0);
  EXPECT_FALSE(res->pngBytes.empty());
}

// ── Test 2: Notepad E2E Live GUI Test
// ─────────────────────────────────────────
class NotepadLiveGUITest : public ::testing::Test {
protected:
  PROCESS_INFORMATION pi{};

  void SetUp() override {
    STARTUPINFO si{};
    si.cb = sizeof(si);
    CreateProcessW(L"C:\\Windows\\System32\\notepad.exe", nullptr, nullptr,
                   nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
  }

  void TearDown() override {
    if (pi.hProcess) {
      TerminateProcess(pi.hProcess, 0);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
    }
    // Ensure no lingering notepad process
    system("powershell -Command \"Stop-Process -Name 'notepad' -Force "
           "-ErrorAction SilentlyContinue\"");
  }
};

TEST_F(NotepadLiveGUITest, ControlNotepadWindow) {
  UIAutomationScanner scanner;

  auto notepadRes = scanner.waitForWindow("Notepad", 4000);
  if (!notepadRes.has_value()) {
    GTEST_SKIP() << "Notepad window not available in current session: "
                 << notepadRes.error();
  }

  UIHandle notepad = std::move(*notepadRes);
  EXPECT_NE(notepad.element().name.find("Notepad"), std::string::npos);

  // Type text into the live Notepad editor
  EXPECT_NO_THROW({ notepad.type("WinBot Automated Testing Verified!"); });
}

// ── Test 3: Calculator E2E Live GUI Test
// ───────────────────────────────────────
class CalculatorLiveGUITest : public ::testing::Test {
protected:
  PROCESS_INFORMATION pi{};

  void SetUp() override {
    STARTUPINFO si{};
    si.cb = sizeof(si);
    CreateProcessW(L"C:\\Windows\\System32\\calc.exe", nullptr, nullptr,
                   nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
  }

  void TearDown() override {
    if (pi.hProcess) {
      TerminateProcess(pi.hProcess, 0);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
    }
    // Ensure no lingering Calculator processes
    system("powershell -Command \"Stop-Process -Name '*calc*' -Force "
           "-ErrorAction SilentlyContinue\"");
  }
};

TEST_F(CalculatorLiveGUITest, ControlCalculatorWindow) {
  UIAutomationScanner scanner;

  auto calcRes = scanner.waitForWindow("Calculator", 4000);
  if (!calcRes.has_value()) {
    GTEST_SKIP() << "Calculator window not instantiated in current session "
                    "(requires interactive desktop): "
                 << calcRes.error();
  }

  UIHandle calc = std::move(*calcRes);
  EXPECT_FALSE(calc.element().name.empty());

  // Perform calculation: 1 + 2 = 3

  calc.select("One", 5000).click();
  calc.select("Plus", 2000).click();
  calc.select("Two", 2000).click();
  calc.select("Equals", 2000).click();
  UIHandle result = calc.select("CalculatorResults", 2000);
  result.refresh();
  EXPECT_EQ(result.element().name, "Display is 3");
}

TEST_F(CalculatorLiveGUITest, ControlCalculatorViaLuaScript) {
  UIAutomationScanner scanner;
  LuaRuntime lua(&scanner);

  auto calcRes = scanner.waitForWindow("Calculator", 4000);
  if (!calcRes.has_value()) {
    GTEST_SKIP() << "Calculator window not instantiated in current session "
                    "(requires interactive desktop): "
                 << calcRes.error();
  }

  const char *script = R"(
    local calc = winbot.waitForWindow("Calculator", 4000)
    if not calc then return "calc_not_found" end

    calc:select("One", 5000):click()
    calc:select("Plus", 2000):click()
    calc:select("Two", 2000):click()
    calc:select("Equals", 2000):click()

    local result = calc:select("CalculatorResults", 2000)
    result:refresh()
    return result:name()
  )";

  auto res = lua.execString(script);
  ASSERT_TRUE(res.has_value()) << res.error();
  EXPECT_EQ(*res, "Display is 3");
}

TEST_F(CalculatorLiveGUITest, ControlCalculatorViaCalculatorLuaToolSuite) {
  UIAutomationScanner scanner;
  auto calcRes = scanner.waitForWindow("Calculator", 4000);
  if (!calcRes.has_value()) {
    GTEST_SKIP() << "Calculator window not instantiated in current session "
                    "(requires interactive desktop): "
                 << calcRes.error();
  }

  LuaRuntime lua(&scanner);
  ToolRegistry registry;
  auto calcScriptPath = getProjectRoot() / "data/tools/calculator.lua";
  ASSERT_TRUE(std::filesystem::exists(calcScriptPath))
      << "Could not find calculator.lua at: " << calcScriptPath
      << " (set WINBOT_ROOT environment variable to override)";

  LuaToolLoader loader(calcScriptPath.parent_path(), lua, registry);
  size_t count = loader.loadAll();
  ASSERT_GE(count, 4);
  ASSERT_TRUE(registry.hasTool("calc_calculate"));

  auto res = registry.dispatch(
      {{"tool", "calc_calculate"}, {"args", {{"expression", "10 + 20"}}}});
  ASSERT_TRUE(res.has_value()) << res.error();
  EXPECT_TRUE(res->contains("30"));
}
