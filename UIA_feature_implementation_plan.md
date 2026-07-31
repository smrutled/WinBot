# Fluent UI Automation — Detailed Feature Plan

## Goal

Enable commands in the `--debug-uia` REPL (and eventually the Agent) to:

1. **Chain** operations so each step narrows its search to the previously selected element, eliminating global mis-clicks (e.g. "One" → OneDrive).
2. **Save** a discovered element to a named variable so it can be re-used in later commands without re-scanning.
3. **Cache** elements automatically when using `Select`/`WaitSelect` so that dot-chained follow-up calls don't re-scan.
4. **Wait** for elements that appear asynchronously (e.g. a window after launch, or a WebView2 result list).

---

## Core Concept: UIHandle

Introduce a lightweight **`UIHandle`** wrapper that carries a copy of the matched `UIElement` (its subtree snapshot) and provides all chainable methods. A `UIHandle` is the return type of every selection command.

```
UIHandle
  ├── element: UIElement          (the matched subtree snapshot)
  ├── click() -> UIHandle         (clicks element center, returns self for chaining)
  ├── type(text) -> UIHandle      (types text into element, returns self)
  ├── key(combo) -> UIHandle      (sends key combo, returns self)
  ├── select(nameOrId, timeout?) -> UIHandle   (scoped search within this element, blocks until found)
  └── wait(ms) -> UIHandle        (sleep, returns self for inline pauses)
```

All `select()` calls on a `UIHandle` only search within that element's stored subtree. This is what prevents "One" from matching "OneDrive" — after `select("Calculator")`, every subsequent call is implicitly scoped to the Calculator window tree.

---

## Session Variable Store

The Debug CLI maintains a `std::unordered_map<std::string, UIHandle>` that lives for the duration of one session (cleared on `exit`).

```
$calc = Select("Calculator")       # Saves handle to $calc
$calc.Click("One")                 # Scoped click — only searches inside $calc
```

- Variables are prefixed with `$`.
- A `Select` or `WaitSelect` at global scope always scans the **focused window** unless a window title is specified.
- The **last result** of any `select`/`WaitSelect` call is implicitly stored in `$_` so you can chain without explicitly naming it.

---

## Command Syntax

### Semicolon-separated statements (existing, unchanged)

```
Click("Start") ; wait 1000 ; type "Calculator"
```

### Dot-notation chaining (NEW)

Each `.Method(args)` operates on the result of the previous call:

```
Select("Calculator", 2000).Click("One").Click("Plus").Click("Two").Click("Equals")
```

### Variable assignment (NEW)

```
$calc = Select("Calculator", 5000)
$calc.Click("One") ; $calc.Click("Plus") ; $calc.Click("Two")
```

### Mixing styles

```
Click("Start") ; wait 1000 ; type "Calculator" ; wait 1000 ; $calc = Select("Calculator", 3000) ; $calc.Click("One")
```

---

## Full Calculator Walkthrough

```
# 1. Click Start (global scan of focused window)
Click("Start")

# 2. Type search term, wait for result to appear, click the Calculator icon
wait 800 ; type "Calculator" ; wait 1000 ; Select("Calculator", 2000).Click()

# 3. Wait for the Calculator window to open and capture a handle to it
$calc = Select("Calculator", 5000)

# 4. All clicks now scoped to Calculator — "One" will never hit OneDrive
$calc.Click("One").Click("Plus").Click("Two").Click("Equals")
```

---

## Proposed Code Changes

### New: `UIHandle` wrapper  (`src/UIHandle.h` + `src/UIHandle.cpp`)

```cpp
class UIHandle {
public:
    explicit UIHandle(UIElement el, UIAutomationScanner& scanner);

    UIHandle& click();                          // Click element center
    UIHandle& type(std::string_view text);      // Type text
    UIHandle& key(std::string_view combo);      // Press key combo
    UIHandle& wait(int ms);                     // Sleep inline

    // Scoped search within m_element's subtree snapshot.
    // If timeoutMs > 0, re-scans the owning window until element appears.
    UIHandle select(std::string_view nameOrId, int timeoutMs = 0);

    const UIElement& element() const { return m_element; }

private:
    UIElement m_element;             // Snapshot of the matched subtree
    UIAutomationScanner& m_scanner;  // Ref used for re-scan on WaitSelect
};
```

### [MODIFY] `UIAutomationScanner.h`

**`UIElement` struct additions:**
- `const UIElement* findDescendant(std::string_view nameOrId) const` — Depth-first search, case-insensitive substring. Used internally by `UIHandle::select`.
- `POINT getCenter() const` — Returns absolute center of bounding rect.

**`UIAutomationScanner` class additions:**
- `std::expected<UIHandle, std::string> select(std::string_view nameOrId, int timeoutMs = 0, std::string_view windowTitle = "") const` — Global (unscoped) search. Polls focused window if timeoutMs > 0.
- `std::expected<UIHandle, std::string> waitForWindow(std::string_view title, int timeoutMs = 5000) const` — Polls until a window with the given title is found.

### [MODIFY] `main.cpp` REPL parser

**Variable store:**
```cpp
std::unordered_map<std::string, UIHandle> vars;
std::optional<UIHandle> lastResult;   // $_
```

**Per `;`-segment parser pipeline:**
1. Check for `$var = ...` assignment — evaluate RHS, store in `vars`.
2. Check if segment starts with `$var` reference — look up in `vars`, use as initial UIHandle.
3. Otherwise treat first token as a global command (`Click`, `Select`, `WaitSelect`, `Focus`, `wait`, `type`, `key`, `scan`).
4. For each subsequent `.Method(args)` in the chain, dispatch on the current `UIHandle`.
5. Store final result in `$_`.

**Command table:**

| Command | Scope | Behavior |
|---|---|---|
| `Select("name", ms?)` | Global | Scan focused window; poll if ms > 0 |
| `WaitSelect("name", ms)` | Global | Alias for `Select` with explicit timeout |
| `Focus("title")` | Global | Bring window to foreground |
| `$var = Select(...)` | Global | Save returned handle to named variable |
| `$var.Click("name")` | Scoped | Find "name" inside `$var` subtree and click |
| `$var.Click()` | Scoped | Click center of `$var` itself |
| `$var.Select("name", ms?)` | Scoped | Find "name" inside `$var`; return new handle |
| `.Method(...)` (no prefix) | `$_` scoped | Implicitly uses last returned handle |

---

## Open Questions

Decisions needed before implementation begins.

- **Subtree staleness**: `UIHandle` holds a cached snapshot. If the UI changes after capture (e.g. after clicking a button a submenu appears), `select()` on the handle will miss new elements. Should we auto-refresh? **Proposed**: Use snapshot for speed; re-scan only on `WaitSelect` or explicit `$var.Refresh()`.
- **Timeout default**: If `Select("name")` is called with no timeout and the element is absent, fail immediately? **Proposed**: Yes. Polling requires an explicit timeout.
- **Chain abort on failure**: If `.Select("X")` fails mid-chain, abort the rest and print error. **Proposed**: Yes.
- **Variable lifetime**: Variables live for the session. Support `clear $var` or `clear all`? **Proposed**: Yes.

---

## Verification Plan

```
# Scoped click — "One" should NOT hit OneDrive
Click("Start") ; wait 800 ; type "Calculator" ; wait 1200
$calc = Select("Calculator", 3000)
$calc.Click("One") ; $calc.Click("Plus") ; $calc.Click("One") ; $calc.Click("Equals")

# Fully chained, no variable
Select("Calculator", 3000).Click("Two").Click("Multiply").Click("Three").Click("Equals")

# Multi-line re-use of saved variable
$calc = Select("Calculator", 3000)
$calc.Click("One")
$calc.Click("Plus")
$calc.Click("Two")
$calc.Click("Equals")
```
