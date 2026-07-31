#include "UIAutomationScanner.h"
#include "UIHandle.h"

// UIAutomation.h (client-side) includes UIAutomationCore.h which needs
// rpcndr.h (for MIDL_INTERFACE). Must NOT have WIN32_LEAN_AND_MEAN active
// when these are first included — Windows.h in Common.h may strip RPC headers.
// We work around this by including UIAutomation.h BEFORE Common.h
// in a separate preinclude approach, or by including the COM headers manually.
#include <rpcndr.h>      // Defines MIDL_INTERFACE (must come before UIAutomation.h)
#include <objbase.h>     // COM base (CoCreateInstance, etc.)
#include <UIAutomation.h> // IUIAutomation, IUIAutomationElement, etc.

#include <algorithm>
#include <chrono>
#include <regex>
#include <thread>

// Convenience macro for safe COM release
#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = nullptr; } } while(0)

UIAutomationScanner::UIAutomationScanner() {
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IUIAutomation* automation = nullptr;
    HRESULT hr = ::CoCreateInstance(
        __uuidof(CUIAutomation),
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(IUIAutomation),
        reinterpret_cast<void**>(&automation)
    );
    if (FAILED(hr)) {
        WINBOT_ERROR("UIAutomationScanner: CoCreateInstance failed 0x{:08X}", static_cast<uint32_t>(hr));
        return;
    }
    m_automation = automation;

    IUIAutomationTreeWalker* walker = nullptr;
    automation->get_RawViewWalker(&walker);
    m_treeWalker = walker;
}

UIAutomationScanner::~UIAutomationScanner() {
    SAFE_RELEASE(reinterpret_cast<IUIAutomationTreeWalker*&>(m_treeWalker));
    SAFE_RELEASE(reinterpret_cast<IUIAutomation*&>(m_automation));
    ::CoUninitialize();
}

std::expected<UIElement, std::string> UIAutomationScanner::scanFocusedWindow() const {
    if (!m_automation) return std::unexpected("UI Automation not initialized");

    auto* automation = reinterpret_cast<IUIAutomation*>(m_automation);
    IUIAutomationElement* focusedEl = nullptr;
    HRESULT hr = automation->GetFocusedElement(&focusedEl);
    if (FAILED(hr) || !focusedEl) return std::unexpected("No focused element");

    // Walk up to the root window to get context
    auto* walker = reinterpret_cast<IUIAutomationTreeWalker*>(m_treeWalker);
    IUIAutomationElement* current = focusedEl;
    IUIAutomationElement* parent  = nullptr;
    HWND hwnd = nullptr;

    while (current) {
        current->get_CurrentNativeWindowHandle((UIA_HWND*)&hwnd);
        if (hwnd) break;

        hr = walker->GetParentElement(current, &parent);
        if (FAILED(hr) || !parent) break;
        if (current != focusedEl) current->Release();
        current = parent;
    }

    UIElement result = walkElement(current, 0, hwnd);
    if (current && current != focusedEl) current->Release();
    focusedEl->Release();
    return result;
}

// ── findBestWindow ───────────────────────────────────────────────────────────
// Scores all visible top-level windows against `query` and returns the
// best-matching HWND. Match priority (case-insensitive):
//   3 = exact match    e.g. query "Calculator" vs title "Calculator"
//   2 = starts-with    e.g. query "Calc" vs title "Calculator"
//   1 = contains       e.g. query "Calc" vs title "Windows Calculator"
// If `query` starts with '/' it is treated as a regex (no scoring tiers).
static HWND findBestWindow(std::string_view query) {
    // ── Regex mode ────────────────────────────────────────────────────────────
    if (query.size() >= 2 && query.front() == '/') {
        // Strip leading slash. Trailing slash is optional.
        std::string pat(query.substr(1));
        if (!pat.empty() && pat.back() == '/') pat.pop_back();
        try {
            std::wregex re(utf8_to_wide(pat), std::regex_constants::icase);
            HWND found = nullptr;
            auto ctx_pair = std::make_pair(&re, &found);
            ::EnumWindows([](HWND h, LPARAM lp) -> BOOL {
                auto* ctx = reinterpret_cast<std::pair<std::wregex*, HWND*>*>(lp);
                if (!::IsWindowVisible(h)) return TRUE;
                wchar_t buf[512]{};
                ::GetWindowTextW(h, buf, 512);
                if (std::regex_search(std::wstring(buf), *ctx->first)) {
                    *ctx->second = h;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&ctx_pair));
            return found;
        } catch (...) {
            return nullptr; // Bad regex
        }
    }

    // ── Scored substring mode ─────────────────────────────────────────────────
    std::wstring wquery = utf8_to_wide(query);
    // Lowercase the query once
    std::wstring wqLow = wquery;
    to_lower_inplace(wqLow);

    struct Candidate { HWND hwnd; int score; };
    Candidate best{ nullptr, 0 };

    auto ctx_scored = std::make_pair(&wqLow, &best);
    ::EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* ctx = reinterpret_cast<std::pair<std::wstring*, Candidate*>*>(lp);
        const std::wstring& q = *ctx->first;
        Candidate& best       = *ctx->second;

        if (!::IsWindowVisible(h)) return TRUE;
        wchar_t buf[512]{};
        ::GetWindowTextW(h, buf, 512);
        if (buf[0] == L'\0') return TRUE;

        std::wstring title(buf);
        std::wstring titleLow = title;
        to_lower_inplace(titleLow);

        int score = 0;
        if (titleLow == q)                     score = 3; // exact
        else if (titleLow.starts_with(q))      score = 2; // prefix
        else if (titleLow.contains(q))         score = 1; // substring

        if (score > best.score) {
            best = { h, score };
        }
        return TRUE; // Keep scanning — we want the BEST, not just the first
    }, reinterpret_cast<LPARAM>(&ctx_scored));

    return best.hwnd;
}

std::expected<UIElement, std::string> UIAutomationScanner::scanWindow(
    std::string_view title) const
{
    HWND found = findBestWindow(title);

    if (!found) {
        // FALLBACK: Search Desktop Root for any direct child matching the name.
        if (auto desktop = scanDesktop(); desktop) {
            if (const UIElement* best = desktop->findBestMatch(title)) {
                return *best;
            }
        }
        return std::unexpected(std::format("Window '{}' not found", title));
    }

    return scanWindowByHandle(found);
}

std::expected<UIElement, std::string> UIAutomationScanner::scanWindowByHandle(HWND hwnd) const {
    if (!m_automation) return std::unexpected("UI Automation not initialized");
    auto* automation = reinterpret_cast<IUIAutomation*>(m_automation);
    IUIAutomationElement* el = nullptr;
    HRESULT hr = automation->ElementFromHandle(hwnd, &el);
    if (FAILED(hr) || !el) return std::unexpected("Could not get UIA element for window handle");

    UIElement result = walkElement(el, 0, hwnd);
    el->Release();
    return result;
}

std::expected<UIElement, std::string> UIAutomationScanner::scanDesktop() const {
    if (!m_automation) return std::unexpected("UI Automation not initialized");

    auto* automation = reinterpret_cast<IUIAutomation*>(m_automation);
    IUIAutomationElement* root = nullptr;
    HRESULT hr = automation->GetRootElement(&root);
    if (FAILED(hr) || !root) return std::unexpected("Could not get desktop root element");

    UIElement result = walkElement(root, 0, nullptr);
    root->Release();
    return result;
}

std::expected<void, std::string> UIAutomationScanner::invokeElement(const std::vector<int>& runtimeId) const {
    if (!m_automation) return std::unexpected("UI Automation not initialized");
    auto* automation = reinterpret_cast<IUIAutomation*>(m_automation);

    IUIAutomationElement* root = nullptr;
    if (FAILED(automation->GetRootElement(&root)) || !root) 
        return std::unexpected("Could not get desktop root for invoke");

    // Search for element by RuntimeId
    IUIAutomationCondition* condition = nullptr;
    VARIANT varId;
    VariantInit(&varId);
    varId.vt = VT_ARRAY | VT_I4;
    SAFEARRAYBOUND bound{ (ULONG)runtimeId.size(), 0 };
    varId.parray = SafeArrayCreate(VT_I4, 1, &bound);
    for (long i = 0; i < (long)runtimeId.size(); ++i) {
        int val = runtimeId[i];
        SafeArrayPutElement(varId.parray, &i, &val);
    }

    automation->CreatePropertyCondition(UIA_RuntimeIdPropertyId, varId, &condition);
    VariantClear(&varId);

    IUIAutomationElement* target = nullptr;
    HRESULT hr = root->FindFirst(TreeScope_Descendants, condition, &target);
    root->Release();
    condition->Release();

    if (FAILED(hr) || !target) 
        return std::unexpected("Could not find element to invoke (did it change or vanish?)");

    // Try Invoke Pattern
    IUIAutomationInvokePattern* invoke = nullptr;
    hr = target->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke));
    target->Release();

    if (FAILED(hr) || !invoke) 
        return std::unexpected("Element does not support Invoke pattern after all");

    hr = invoke->Invoke();
    invoke->Release();

    if (FAILED(hr)) return std::unexpected("Call to Invoke() failed");
    return {};
}

UIElement UIAutomationScanner::walkElement(void* elPtr, int depth, HWND ownerHwnd) const {
    auto* el = reinterpret_cast<IUIAutomationElement*>(elPtr);
    UIElement result;

    // Always prefer the element's own NativeWindowHandle.
    // Falling back to the inherited ownerHwnd only if the element has none.
    // This prevents Desktop-rooted scans from stamping the Desktop HWND on
    // every descendant application element.
    HWND currentHwnd = nullptr;
    el->get_CurrentNativeWindowHandle((UIA_HWND*)&currentHwnd);
    if (!currentHwnd) currentHwnd = ownerHwnd;
    result.ownerHwnd = currentHwnd;

    // Capture Process ID
    int pid = 0;
    if (SUCCEEDED(el->get_CurrentProcessId(&pid))) {
        result.dwProcessId = (DWORD)pid;
    }

    // Name — use raw BSTR (no ATL dependency)
    BSTR name = nullptr;
    if (SUCCEEDED(el->get_CurrentName(&name)) && name) {
        result.name = wide_to_utf8(std::wstring_view{ name });
        ::SysFreeString(name);
    }

    // Control type
    CONTROLTYPEID ctypeId = 0;
    if (SUCCEEDED(el->get_CurrentControlType(&ctypeId))) {
        result.controlType = controlTypeToString(ctypeId);
    }

    // AutomationId
    BSTR automId = nullptr;
    if (SUCCEEDED(el->get_CurrentAutomationId(&automId)) && automId) {
        result.automationId = wide_to_utf8(std::wstring_view{ automId });
        ::SysFreeString(automId);
    }

    // Capture RuntimeId
    SAFEARRAY* saId = nullptr;
    if (SUCCEEDED(el->GetRuntimeId(&saId)) && saId) {
        long lb, ub;
        SafeArrayGetLBound(saId, 1, &lb);
        SafeArrayGetUBound(saId, 1, &ub);
        for (long i = lb; i <= ub; ++i) {
            int val = 0;
            SafeArrayGetElement(saId, &i, &val);
            result.runtimeId.push_back(val);
        }
        SafeArrayDestroy(saId);
    }

    // Bounding rectangle
    RECT rect{};
    if (SUCCEEDED(el->get_CurrentBoundingRectangle(&rect))) {
        result.bounds = rect;
    }

    // Properties
    BOOL enabled = FALSE;
    if (SUCCEEDED(el->get_CurrentIsEnabled(&enabled))) result.isEnabled = (enabled != 0);
    BOOL focusable = FALSE;
    if (SUCCEEDED(el->get_CurrentIsKeyboardFocusable(&focusable))) result.isFocusable = (focusable != 0);
    BOOL focused = FALSE;
    if (SUCCEEDED(el->get_CurrentHasKeyboardFocus(&focused))) result.isFocused = (focused != 0);

    // Check for Invoke Pattern support
    IUnknown* pUnk = nullptr;
    if (SUCCEEDED(el->GetCurrentPattern(UIA_InvokePatternId, &pUnk)) && pUnk) {
        result.supportsInvoke = true;
        pUnk->Release();
    }

    // Extract accessible text
    IUIAutomationTextPattern* textPattern = nullptr;
    if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&textPattern))) && textPattern) {
        IUIAutomationTextRange* range = nullptr;
        if (SUCCEEDED(textPattern->get_DocumentRange(&range)) && range) {
            BSTR textStr = nullptr;
            if (SUCCEEDED(range->GetText(1024, &textStr)) && textStr) {
                result.value = wide_to_utf8(std::wstring_view{textStr});
                // Clean up whitespace
                std::erase_if(result.value, [](char c){ return c == '\r'; });
                while (!result.value.empty() && std::isspace(static_cast<unsigned char>(result.value.back()))) {
                    result.value.pop_back();
                }
                ::SysFreeString(textStr);
            }
            range->Release();
        }
        textPattern->Release();
    }

    // Fallback to ValuePattern for inputs
    if (result.value.empty()) {
        IUIAutomationValuePattern* valuePattern = nullptr;
        if (SUCCEEDED(el->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern))) && valuePattern) {
            BSTR valStr = nullptr;
            if (SUCCEEDED(valuePattern->get_CurrentValue(&valStr)) && valStr) {
                result.value = wide_to_utf8(std::wstring_view{valStr});
                ::SysFreeString(valStr);
            }
            valuePattern->Release();
        }
    }

    // Recurse into children (max depth 32 to accurately step into deeply nested WebView2 DOMs)
    if (depth < 32) {
        auto* automation = reinterpret_cast<IUIAutomation*>(m_automation);
        IUIAutomationCondition* condition = nullptr;
        automation->CreateTrueCondition(&condition);

        IUIAutomationElementArray* childrenArray = nullptr;
        // FindAll using TreeScope_Children explicitly forces Chromium providers to evaluate 
        // and physically bridge inner web DOMs rather than lazily skipping them.
        if (SUCCEEDED(el->FindAll(TreeScope_Children, condition, &childrenArray)) && childrenArray) {
            int length = 0;
            childrenArray->get_Length(&length);
            for (int i = 0; i < length; ++i) {
                IUIAutomationElement* child = nullptr;
                if (SUCCEEDED(childrenArray->GetElement(i, &child)) && child) {
                    result.children.push_back(walkElement(child, depth + 1, currentHwnd));
                    child->Release();
                }
            }
            childrenArray->Release();
        }
        if (condition) condition->Release();
    }

    return result;
}

std::string UIAutomationScanner::controlTypeToString(long id) noexcept {
    switch (id) {
        case UIA_ButtonControlTypeId:       return "Button";
        case UIA_CalendarControlTypeId:     return "Calendar";
        case UIA_CheckBoxControlTypeId:     return "CheckBox";
        case UIA_ComboBoxControlTypeId:     return "ComboBox";
        case UIA_EditControlTypeId:         return "Edit";
        case UIA_HyperlinkControlTypeId:    return "Hyperlink";
        case UIA_ImageControlTypeId:        return "Image";
        case UIA_ListItemControlTypeId:     return "ListItem";
        case UIA_ListControlTypeId:         return "List";
        case UIA_MenuControlTypeId:         return "Menu";
        case UIA_MenuBarControlTypeId:      return "MenuBar";
        case UIA_MenuItemControlTypeId:     return "MenuItem";
        case UIA_ProgressBarControlTypeId:  return "ProgressBar";
        case UIA_RadioButtonControlTypeId:  return "RadioButton";
        case UIA_ScrollBarControlTypeId:    return "ScrollBar";
        case UIA_SliderControlTypeId:       return "Slider";
        case UIA_SpinnerControlTypeId:      return "Spinner";
        case UIA_StatusBarControlTypeId:    return "StatusBar";
        case UIA_TabControlTypeId:          return "Tab";
        case UIA_TabItemControlTypeId:      return "TabItem";
        case UIA_TextControlTypeId:         return "Text";
        case UIA_ToolBarControlTypeId:      return "ToolBar";
        case UIA_ToolTipControlTypeId:      return "ToolTip";
        case UIA_TreeControlTypeId:         return "Tree";
        case UIA_TreeItemControlTypeId:     return "TreeItem";
        case UIA_WindowControlTypeId:       return "Window";
        case UIA_PaneControlTypeId:         return "Pane";
        case UIA_DocumentControlTypeId:     return "Document";
        default:                            return "Unknown";
    }
}

std::string UIAutomationScanner::serialize(const UIElement& el, int indent) {
    bool hasData = !el.name.empty() || !el.automationId.empty() || el.isFocusable || !el.value.empty() || el.controlType == "Document";
    bool isVisible = (el.bounds.right > el.bounds.left && el.bounds.bottom > el.bounds.top);
    bool shouldPrint = hasData && isVisible;

    std::string out;
    if (shouldPrint) {
        std::string spaces(static_cast<size_t>(indent * 2), ' ');
        out = std::format("{}[{}: \"{}\"", spaces, el.controlType, el.name);
        
        if (!el.value.empty()) {
            std::string dispVal = el.value;
            std::replace(dispVal.begin(), dispVal.end(), '\n', ' ');
            if (dispVal.length() > 60) dispVal = dispVal.substr(0, 57) + "...";
            out += std::format(" value=\"{}\"", dispVal);
        }
        
        if (!el.automationId.empty()) out += std::format(" id=\"{}\"", el.automationId);
        out += std::format(" at ({},{},{},{})",
            el.bounds.left, el.bounds.top, el.bounds.right, el.bounds.bottom);
        if (el.isFocused)    out += " focused";
        if (!el.isEnabled)   out += " disabled";
        if (el.isFocusable)  out += " focusable";
        if (el.supportsInvoke) out += " invokable";
        out += "]\n";
    }

    int childIndent = shouldPrint ? (indent + 1) : indent;
    for (const auto& child : el.children) {
        out += serialize(child, childIndent);
    }
    return out;
}

int UIAutomationScanner::countInteractive(const UIElement& el) {
    int count = (!el.name.empty() && el.isEnabled && el.isFocusable) ? 1 : 0;
    for (const auto& child : el.children) count += countInteractive(child);
    return count;
}

std::optional<POINT> UIAutomationScanner::findElementCenter(
    const UIElement& root, std::string_view name)
{
    std::string lowerName(name);
    to_lower_inplace(lowerName);

    std::string elName = root.name;
    to_lower_inplace(elName);

    std::string elId = root.automationId;
    to_lower_inplace(elId);

    if ((elName.contains(lowerName) || elId.contains(lowerName)) && root.isEnabled) {
        POINT center{
            (root.bounds.left + root.bounds.right)  / 2,
            (root.bounds.top  + root.bounds.bottom) / 2
        };
        return center;
    }
    for (const auto& child : root.children) {
        if (auto pt = findElementCenter(child, name); pt.has_value()) return pt;
    }
    return std::nullopt;
}

// ── UIElement::findParent ────────────────────────────────────────────────────
const UIElement* UIElement::findParent(const std::vector<int>& targetRuntimeId) const {
    if (targetRuntimeId.empty()) return nullptr;

    for (const auto& child : children) {
        if (child.runtimeId == targetRuntimeId) {
            return this;
        }
        if (const UIElement* found = child.findParent(targetRuntimeId)) {
            return found;
        }
    }
    return nullptr;
}

// ── UIElement::findInSubtree ──────────────────────────────────────────────────
const UIElement* UIElement::findInSubtree(std::string_view nameOrId, std::string_view controlTypeFilter) const {

    if (auto* res = findBestMatch(nameOrId, controlTypeFilter)) return res;
    return nullptr;
}

const UIElement* UIElement::findBestMatch(std::string_view nameOrId, std::string_view controlTypeFilter) const {
    std::string query(nameOrId);
    to_lower_inplace(query);
    
    std::string typeFilter(controlTypeFilter);
    to_lower_inplace(typeFilter);

    const UIElement* best = nullptr;
    int bestScore = -1;

    auto check = [&](const UIElement& el) {
        // STRICT SELF-EXCLUSION: Completely ignore any elements from our own process.
        // This prevents WinBot from ever 'seeing' itself and its own REPL text.
        static DWORD selfPid = ::GetCurrentProcessId();
        if (el.dwProcessId == selfPid) return;

        int score = 0;
        
        // Control type filtering
        if (!typeFilter.empty()) {
            std::string elType = el.controlType;
            to_lower_inplace(elType);
            if (elType != typeFilter) return; // Must match type exactly if filter provided
            score += 100; // Bonus for matching requested type
        }

        // Match against Name or AutomationId
        std::string n = el.name;
        to_lower_inplace(n);
        std::string aid = el.automationId;
        to_lower_inplace(aid);

        bool exactMatch = (n == query || aid == query);
        bool partialMatch = (n.contains(query) || aid.contains(query));

        if (exactMatch) score = 50;
        else if (partialMatch) score = 10;
        else return; // No match

        // Heavy weight for 'Window' control type
        if (el.controlType == "Window") score += 100;

        // Weight for interactive vs plain elements
        if (el.isFocusable || el.isEnabled) score += 5;

        if (score > bestScore) {
            bestScore = score;
            best = &el;
        }
    };

    // DFS search through subtree
    std::vector<const UIElement*> stack;
    stack.push_back(this);

    while (!stack.empty()) {
        const UIElement* el = stack.back();
        stack.pop_back();

        check(*el);
        // If we found a perfect window match exactly, we could potentially stop early...
        // but it's safer to check the whole tree since it's small and we want the 'best'.
        if (bestScore >= 150) break; 

        for (auto it = el->children.rbegin(); it != el->children.rend(); ++it) {
            stack.push_back(&(*it));
        }
    }

    return best;
}

const UIElement* UIElement::findByRuntimeId(const std::vector<int>& id) const {
    if (id.empty()) return nullptr;
    if (runtimeId == id) return this;

    std::vector<const UIElement*> stack;
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        stack.push_back(&(*it));
    }

    while (!stack.empty()) {
        const UIElement* el = stack.back();
        stack.pop_back();

        if (el->runtimeId == id) return el;

        for (auto it = el->children.rbegin(); it != el->children.rend(); ++it) {
            stack.push_back(&(*it));
        }
    }
    return nullptr;
}

// ── UIElement::findDescendant ─────────────────────────────────────────────────
const UIElement* UIElement::findDescendant(std::string_view nameOrId, std::string_view controlTypeFilter) const {
    // Only search children (important for WaitSelect/Click children)
    for (const auto& child : children) {
        if (const UIElement* res = child.findInSubtree(nameOrId, controlTypeFilter)) return res;
    }
    return nullptr;
}

// ── UIAutomationScanner::select ───────────────────────────────────────────────
std::expected<UIHandle, std::string> UIAutomationScanner::select(
    std::string_view nameOrId, int timeoutMs, std::string_view controlTypeFilter, std::string_view windowTitle) const
{
    auto scanOnce = [&]() -> std::expected<UIElement, std::string> {
        if (windowTitle.empty())
            return scanFocusedWindow();
        return scanWindow(windowTitle);
    };

    auto tryFind = [&](const UIElement& tree) -> std::optional<UIElement> {
        if (const UIElement* el = tree.findBestMatch(nameOrId, controlTypeFilter))
            return *el;
        return std::nullopt;
    };

    // Try primary scan (focused window or named window)
    if (auto tree = scanOnce(); tree) {
        if (auto el = tryFind(*tree)) {
            return UIHandle{ std::move(*el), this };
        }
    }

    // FALLBACK: If windowTitle was empty, try a global Desktop search
    if (windowTitle.empty()) {
        if (auto tree = scanDesktop(); tree) {
            if (auto el = tryFind(*tree)) {
                return UIHandle{ std::move(*el), this };
            }
        }
    }

    if (timeoutMs <= 0)
        return std::unexpected(std::format("select('{}') — not found in focused window or desktop", nameOrId));

    // Poll until timeout
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (auto tree = scanOnce(); tree) {
            if (auto el = tryFind(*tree))
                return UIHandle{ std::move(*el), this };
        }
    }
    return std::unexpected(
        std::format("select('{}') — timed out after {} ms", nameOrId, timeoutMs));
}

// ── UIAutomationScanner::waitForWindow ────────────────────────────────────────
std::expected<UIHandle, std::string> UIAutomationScanner::waitForWindow(
    std::string_view title, int timeoutMs) const
{
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);

    do {
        auto tree = scanWindow(title);
        // Only return if we found the window AND it has a name (or we're out of time)
        bool outOfTime = std::chrono::steady_clock::now() > (deadline - std::chrono::milliseconds(500));
        if (tree && (!tree->name.empty() || outOfTime)) {
            return UIHandle{ std::move(*tree), this };
        }
        
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    } while (std::chrono::steady_clock::now() < deadline);
    
    return std::unexpected(
        std::format("waitForWindow('{}') — timed out after {} ms", title, timeoutMs));
}
