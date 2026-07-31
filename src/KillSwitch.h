#ifndef WINBOT_KILLSWITCH_H
#define WINBOT_KILLSWITCH_H
#include "Common.h"
#include <string_view>

#include <thread>

// ── KillSwitch ───────────────────────────────────────────────────────────────
// Installs a low-level global keyboard hook (WH_KEYBOARD_LL) that sets an
// atomic flag when the configured hotkey is pressed, allowing all threads
// to observe a graceful shutdown request.
class KillSwitch {
public:
  // Install the global keyboard hook. Call once at startup.
  // hotkey format: "Ctrl+Alt+X" (any combination of Ctrl/Alt/Shift + key)
  static void install(std::string_view hotkey = "Ctrl+Alt+X");

  // Remove the hook. Called automatically at process exit.
  static void uninstall() noexcept;

  // Returns true if the kill hotkey has been pressed.
  [[nodiscard]] static bool isTriggered() noexcept {
    return s_triggered.load(std::memory_order_relaxed);
  }

  // Reset the flag (e.g. to resume after a pause).
  static void reset() noexcept {
    s_triggered.store(false, std::memory_order_relaxed);
  }

private:
  static LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam,
                                               LPARAM lParam) noexcept;

  static inline std::atomic<bool> s_triggered{false};
  static inline HHOOK s_hook{nullptr};
  static inline bool s_needCtrl{true};
  static inline bool s_needAlt{true};
  static inline bool s_needShift{false};
  static inline DWORD s_vkCode{'X'};

  static inline std::thread s_thread;
  static inline std::atomic<bool> s_stopThread{false};
  static inline DWORD s_threadId{0};
};

#endif // WINBOT_KILLSWITCH_H
