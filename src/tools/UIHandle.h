#ifndef WINBOT_UIHANDLE_H
#define WINBOT_UIHANDLE_H
#include "UIAutomationScanner.h"

// ── UIHandle ──────────────────────────────────────────────────────────────────
// A chainable wrapper around a UIElement snapshot.  Every selection command
// returns a UIHandle, and all methods on UIHandle are scoped to that element's
// subtree — preventing accidental global mis-matches (e.g. "One" vs "OneDrive").
//
// Usage examples:
//   Select("Calculator", 3000).Click("One").Click("Plus").Click("Two")
//   $calc = Select("Calculator", 3000)
//   $calc.Click("One") ; $calc.Click("Plus")
class UIHandle {
public:
    UIHandle(UIElement el, const UIAutomationScanner* scanner);

    // ── Action methods (return *this for chaining) ─────────────────────────

    // Click the center of this element.
    UIHandle& click();

    // Click the first descendant whose name/ID matches nameOrId.
    UIHandle& click(std::string_view nameOrId, std::string_view controlType = "");

    // Wait for a descendant to appear (up to timeoutMs) then click it.
    UIHandle& waitClick(std::string_view nameOrId, int timeoutMs = 5000, std::string_view controlType = "");

    // Type text (no target — types into currently focused control).
    UIHandle& type(std::string_view text);

    // Send a key combination (e.g. "Enter", "Ctrl+A").
    UIHandle& key(std::string_view combo);

    // Sleep for ms milliseconds, then continue chain.
    UIHandle& wait(int ms);

    // ── Selection methods (return a NEW UIHandle scoped to the found child) ─
    
    // Search within this element's subtree for nameOrId.
    // If timeoutMs > 0, re-scans the owning window repeatedly until found.
    // Throws std::runtime_error if not found (chain aborts).
    [[nodiscard]] UIHandle select(std::string_view nameOrId, int timeoutMs = 0, std::string_view controlType = "");

    // Returns a new UIHandle for the parent of this element.
    // Throws std::runtime_error if parent cannot be found or scanned.
    [[nodiscard]] UIHandle parent() const;

    // Returns a new UIHandle for the top-level window containing this element.
    [[nodiscard]] UIHandle window() const;

    // Refresh the internal UIElement snapshot from the live app.

    // Returns true if successfully refreshed.
    bool refresh();

    // ── Accessors ──────────────────────────────────────────────────────────
    [[nodiscard]] const UIElement& element() const noexcept { return m_element; }

private:
    UIElement                    m_element;
    const UIAutomationScanner*   m_scanner;  // non-owning; allows copy/assign
    std::vector<int>             m_runtimeId;
    HWND                         m_ownerHwnd;
};

#endif // WINBOT_UIHANDLE_H
