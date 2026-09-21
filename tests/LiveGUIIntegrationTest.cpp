#include <gtest/gtest.h>
#include "TestHelpers.h"
#include "UIAutomationScanner.h"
#include "UIHandle.h"
#include "UIADebugger.h"
#include "tools/WindowTools.h"

#include <windows.h>
#include <atomic>
#include <chrono>
#include <print>
#include <thread>

// ── In-Process Native Win32 Test GUI Window ───────────────────────────────────
// Creates a 100% self-contained native GUI window with button, edit, and label
// controls to test UI automation and event firing reliably across all environments.
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

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
        }
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void windowThreadProc() {
        HINSTANCE hInst = ::GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"WinBotMockAppClass";
        wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
        ::RegisterClassExW(&wc);

        s_hwndMain = ::CreateWindowExW(
            0, L"WinBotMockAppClass", L"WinBot Native Test App",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            100, 100, 450, 320,
            nullptr, nullptr, hInst, nullptr
        );

        if (!s_hwndMain) return;

        s_hwndBtn = ::CreateWindowExW(
            0, L"BUTTON", L"Click Me",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            20, 20, 140, 40,
            s_hwndMain, reinterpret_cast<HMENU>(101), hInst, nullptr
        );

        s_hwndEdit = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            20, 80, 240, 35,
            s_hwndMain, reinterpret_cast<HMENU>(102), hInst, nullptr
        );

        s_hwndLabel = ::CreateWindowExW(
            0, L"STATIC", L"Status: Idle",
            WS_CHILD | WS_VISIBLE,
            20, 140, 240, 35,
            s_hwndMain, reinterpret_cast<HMENU>(103), hInst, nullptr
        );

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

// ── Test 1: Native Win32 Test App (Runs everywhere, fully deterministic) ──────
TEST(LiveGUITest, NativeWin32App_ButtonClickAndStateVerification) {
    MockWin32Window mockApp;
    ASSERT_TRUE(mockApp.isReady()) << "Failed to initialize mock Win32 window";

    UIAutomationScanner scanner;

    // 1. Scan window by handle
    auto winRes = scanner.scanWindowByHandle(mockApp.getHwnd());
    ASSERT_TRUE(winRes.has_value()) << "Failed to scan native test window: " << winRes.error();

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

// ── Test 2: Notepad E2E Live GUI Test ─────────────────────────────────────────
class NotepadLiveGUITest : public ::testing::Test {
protected:
    PROCESS_INFORMATION pi{};

    void SetUp() override {
        STARTUPINFO si{ sizeof(si) };
        CreateProcessW(L"C:\\Windows\\System32\\notepad.exe", nullptr,
                       nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    }

    void TearDown() override {
        if (pi.hProcess) {
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        // Ensure no lingering notepad process
        system("powershell -Command \"Stop-Process -Name 'notepad' -Force -ErrorAction SilentlyContinue\"");
    }
};

TEST_F(NotepadLiveGUITest, ControlNotepadWindow) {
    UIAutomationScanner scanner;

    auto notepadRes = scanner.waitForWindow("Notepad", 4000);
    if (!notepadRes.has_value()) {
        GTEST_SKIP() << "Notepad window not available in current session: " << notepadRes.error();
    }

    UIHandle notepad = std::move(*notepadRes);
    EXPECT_NE(notepad.element().name.find("Notepad"), std::string::npos);

    // Type text into the live Notepad editor
    EXPECT_NO_THROW({
        notepad.type("WinBot Automated Testing Verified!");
    });
}

// ── Test 3: Calculator E2E Live GUI Test ───────────────────────────────────────
class CalculatorLiveGUITest : public ::testing::Test {
protected:
    PROCESS_INFORMATION pi{};

    void SetUp() override {
        STARTUPINFO si{ sizeof(si) };
        CreateProcessW(L"C:\\Windows\\System32\\calc.exe", nullptr,
                       nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    }

    void TearDown() override {
        if (pi.hProcess) {
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        // Ensure no lingering Calculator processes
        system("powershell -Command \"Stop-Process -Name '*calc*' -Force -ErrorAction SilentlyContinue\"");
    }
};

TEST_F(CalculatorLiveGUITest, ControlCalculatorWindow) {
    UIAutomationScanner scanner;

    auto calcRes = scanner.waitForWindow("Calculator", 4000);
    if (!calcRes.has_value()) {
        GTEST_SKIP() << "Calculator window not instantiated in current session (requires interactive desktop): " 
                     << calcRes.error();
    }

    UIHandle calc = std::move(*calcRes);
    EXPECT_FALSE(calc.element().name.empty());

    // Perform calculation: 1 + 2 =
    EXPECT_NO_THROW({
        calc.select("One", 2000).click();
        calc.select("Plus", 2000).click();
        calc.select("Two", 2000).click();
        calc.select("Equals", 2000).click();
    });
}
