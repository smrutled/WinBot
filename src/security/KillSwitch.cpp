#include "security/KillSwitch.h"
#include "Common.h"
#include <cctype>

void KillSwitch::install(std::string_view hotkey) {
  // Parse hotkey string e.g. "Ctrl+Alt+X"
  s_needCtrl = false;
  s_needAlt = false;
  s_needShift = false;
  s_vkCode = 0;

  std::string upper(hotkey);
  to_upper_inplace(upper);

  if (upper.contains("CTRL")) {
    s_needCtrl = true;
  }
  if (upper.contains("ALT")) {
    s_needAlt = true;
  }
  if (upper.contains("SHIFT")) {
    s_needShift = true;
  }

  // Extract the single key character (last token after all modifiers)
  auto pos = upper.rfind('+');
  std::string key = (pos != std::string::npos) ? upper.substr(pos + 1) : upper;

  if (key.size() == 1 && std::isalpha(static_cast<unsigned char>(key.at(0))) != 0) {
    s_vkCode = static_cast<DWORD>(static_cast<unsigned char>(key.at(0))); // 'A'–'Z'
  } else if (key == "ESCAPE" || key == "ESC") {
    s_vkCode = VK_ESCAPE;
  } else if (key == "F12") {
    s_vkCode = VK_F12;
  } else {
    // Fallback: use 'X'
    s_vkCode = 'X';
  }

  s_triggered.store(false, std::memory_order_relaxed);

  // Run the hook on a dedicated background thread to prevent systemic typing
  // latency when the main thread blocks on std::cin or sleep.
  s_thread = std::jthread([hotkey_str = std::string(hotkey)](const std::stop_token& stopToken) {
    // Ensure message queue is created before publishing thread ID
    MSG msg;
    ::PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);
    s_threadId.store(::GetCurrentThreadId(), std::memory_order_release);
    s_threadId.notify_one();

    s_hook = ::SetWindowsHookExW(WH_KEYBOARD_LL, lowLevelKeyboardProc,
                                 ::GetModuleHandleW(nullptr), 0);

    if (!s_hook) {
      WINBOT_WARN("KillSwitch: failed to install keyboard hook (error {})",
                  ::GetLastError());
      return;
    }
    WINBOT_INFO("KillSwitch installed — press {} to halt agent", hotkey_str);

    while (!stopToken.stop_requested()) {
      BOOL ret = ::GetMessageW(&msg, nullptr, 0, 0);
      if (ret <= 0) {
        break; // Error or WM_QUIT
      }
      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
    }

    if (s_hook) {
      ::UnhookWindowsHookEx(s_hook);
      s_hook = nullptr;
    }
  });

  // Synchronize: wait until hook thread has initialized its message queue and stored thread id
  while (s_threadId.load(std::memory_order_acquire) == 0) {
    s_threadId.wait(0);
  }
}

void KillSwitch::uninstall() noexcept {
  DWORD tid = s_threadId.exchange(0, std::memory_order_acq_rel);
  if (tid != 0 && s_thread.joinable()) {
    s_thread.request_stop();
    ::PostThreadMessageW(tid, WM_QUIT, 0, 0);
    s_thread.join();
  }
}

LRESULT CALLBACK KillSwitch::lowLevelKeyboardProc(int nCode, WPARAM wParam,
                                                  LPARAM lParam) noexcept {
  if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
    const auto *kbd = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
    bool ctrlDown = (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool altDown = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    bool shiftDown = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    bool modifiersMatch = (!s_needCtrl || ctrlDown) &&
                          (!s_needAlt || altDown) &&
                          (!s_needShift || shiftDown);

    if (modifiersMatch && kbd->vkCode == s_vkCode) {
      s_triggered.store(true, std::memory_order_relaxed);
      (void)std::fputs("\n[KillSwitch] *** HALT REQUESTED — agent stopping ***\n",
                       stderr);
    }
  }
  return ::CallNextHookEx(s_hook, nCode, wParam, lParam);
}
