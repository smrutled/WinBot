#ifndef WINBOT_UIAUTOMATIONSCANNER_H
#define WINBOT_UIAUTOMATIONSCANNER_H
#include "Common.h"
#include <chrono>
#include <vector>

// Forward declarations
class UIHandle;
class UIAutomationScanner;

// ── UIElement
// ─────────────────────────────────────────────────────────────────
struct UIElement {
  std::string name;         // Accessible name (button label, field text, etc.)
  std::string value;        // Inner text or value (for Text/Edit controls)
  std::string controlType;  // "Button", "Edit", "Window", "List", etc.
  std::string automationId; // AutomationId if available
  RECT bounds{};            // Bounding rectangle in screen coords
  bool isEnabled{true};
  bool isFocusable{false};
  bool isFocused{false};
  std::vector<int> runtimeId; // UIA RuntimeId for re-location
  HWND ownerHwnd{nullptr};    // HWND of the owning window
  DWORD dwProcessId{0};       // Process ID that owns this element
  bool supportsInvoke{false}; // Whether UIA_InvokePatternId is supported
  std::vector<UIElement> children;

  // Absolute screen center of this element.
  [[nodiscard]] POINT getCenter() const noexcept {
    return POINT{(bounds.left + bounds.right) / 2,
                 (bounds.top + bounds.bottom) / 2};
  }

  // Search the tree starting from THIS element (inclusive).
  [[nodiscard]] const UIElement *
  findInSubtree(std::string_view nameOrId,
                std::string_view controlTypeFilter = "") const;

  // Search for the BEST matching element in the subtree (inclusive).
  // Prioritizes: Exact Name/ID match + Window type > Exact Match > Substring
  // Window > Substring match.
  [[nodiscard]] const UIElement *
  findBestMatch(std::string_view nameOrId,
                std::string_view controlTypeFilter = "") const;

  // Search the tree by UIA RuntimeId.
  [[nodiscard]] const UIElement *
  findByRuntimeId(const std::vector<int> &id) const;

  // Search the tree starting from this element's CHILDREN (exclusive).
  [[nodiscard]] const UIElement *
  findDescendant(std::string_view nameOrId,
                 std::string_view controlTypeFilter = "") const;

  // Search the tree for the parent of the element with the given RuntimeId.
  [[nodiscard]] const UIElement *
  findParent(const std::vector<int> &targetRuntimeId) const;
};

// ── UIAutomationScanner
// ─────────────────────────────────────────────────────── Walks the Windows UI
// Automation tree to produce a structured element list that can be serialized
// to text and fed to the LLM as a screen observation.
class UIAutomationScanner {
public:
  UIAutomationScanner();
  ~UIAutomationScanner();

  UIAutomationScanner(const UIAutomationScanner &) = delete;
  UIAutomationScanner &operator=(const UIAutomationScanner &) = delete;

  // Scan the currently focused window and return a tree of UI elements.
  [[nodiscard]] std::expected<UIElement, std::string> scanFocusedWindow() const;

  // Scan a specific window by its title substring.
  [[nodiscard]] std::expected<UIElement, std::string>
  scanWindow(std::string_view title) const;

  // Scan a specific window by its native handle.
  [[nodiscard]] std::expected<UIElement, std::string>
  scanWindowByHandle(HWND hwnd) const;

  // Scan the entire desktop (all windows) and return a tree.
  [[nodiscard]] std::expected<UIElement, std::string> scanDesktop() const;

  // Programmatically invoke an element by its RuntimeId.
  [[nodiscard]] std::expected<void, std::string>
  invokeElement(const std::vector<int> &runtimeId) const;

  // Serialize a UI element tree into a compact human-readable text block
  // suitable for inclusion in an LLM prompt.
  [[nodiscard]] static std::string serialize(const UIElement &root,
                                             int indent = 0);

  // Count the number of interactive elements in a tree.
  [[nodiscard]] static int countInteractive(const UIElement &root);

  // Find the center point of the first element matching a name substring.
  [[nodiscard]] static std::optional<POINT>
  findElementCenter(const UIElement &root, std::string_view name);

  // Global (unscoped) search across the focused window (or a named window).
  // If timeoutMs == 0, single scan; if > 0, polls at 100ms intervals until
  // the element appears or the timeout elapses.
  [[nodiscard]] std::expected<UIHandle, std::string>
  select(std::string_view nameOrId, int timeoutMs = 0,
         std::string_view controlTypeFilter = "",
         std::string_view windowTitle = "") const;

  // Poll until the window whose title contains 'title' exists, then
  // return a UIHandle wrapping its full element tree.
  [[nodiscard]] std::expected<UIHandle, std::string>
  waitForWindow(std::string_view title, int timeoutMs = 5000) const;

  void setHumanMovement(bool human) { m_humanMovement = human; }
  [[nodiscard]] bool getHumanMovement() const { return m_humanMovement; }

private:
  bool m_humanMovement{false};
  // COM interface pointers stored as void* to avoid header pollution
  void *m_automation{nullptr}; // IUIAutomation*
  void *m_treeWalker{nullptr}; // IUIAutomationTreeWalker*

  // Recursively walk the UIA tree from a given element
  [[nodiscard]] UIElement walkElement(void *element, int depth = 0,
                                      HWND ownerHwnd = nullptr) const;

  // Convert UIA ControlType ID to human-readable string
  [[nodiscard]] static std::string
  controlTypeToString(long controlTypeId) noexcept;
};

#endif // WINBOT_UIAUTOMATIONSCANNER_H
