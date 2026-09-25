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
#include <future>
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

    IUIAutomationCondition* condition = nullptr;
    if (SUCCEEDED(automation->CreateTrueCondition(&condition))) {
        m_trueCondition = condition;
    }

    IUIAutomationCacheRequest* cacheReq = nullptr;
    if (SUCCEEDED(automation->CreateCacheRequest(&cacheReq)) && cacheReq) {
        cacheReq->AddProperty(UIA_NativeWindowHandlePropertyId);
        cacheReq->AddProperty(UIA_ProcessIdPropertyId);
        cacheReq->AddProperty(UIA_NamePropertyId);
        cacheReq->AddProperty(UIA_ControlTypePropertyId);
        cacheReq->AddProperty(UIA_AutomationIdPropertyId);
        cacheReq->AddProperty(UIA_BoundingRectanglePropertyId);
        cacheReq->AddProperty(UIA_IsEnabledPropertyId);
        cacheReq->AddProperty(UIA_IsKeyboardFocusablePropertyId);
        cacheReq->AddProperty(UIA_HasKeyboardFocusPropertyId);
        cacheReq->AddProperty(UIA_IsInvokePatternAvailablePropertyId);
        cacheReq->AddProperty(UIA_IsTextPatternAvailablePropertyId);
        cacheReq->AddProperty(UIA_IsValuePatternAvailablePropertyId);
        cacheReq->put_TreeScope(TreeScope_Element);
        cacheReq->put_AutomationElementMode(AutomationElementMode_Full);
        m_cacheRequest = cacheReq;
    }
}

UIAutomationScanner::~UIAutomationScanner() {
    SAFE_RELEASE(reinterpret_cast<IUIAutomationCacheRequest*&>(m_cacheRequest));
    SAFE_RELEASE(reinterpret_cast<IUIAutomationCondition*&>(m_trueCondition));
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

    auto* cacheReq = reinterpret_cast<IUIAutomationCacheRequest*>(m_cacheRequest);
    IUIAutomationElement* cachedEl = nullptr;
    if (cacheReq && SUCCEEDED(current->BuildUpdatedCache(cacheReq, &cachedEl)) && cachedEl) {
        if (current != focusedEl) current->Release();
        current = cachedEl;
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

    auto* cacheReq = reinterpret_cast<IUIAutomationCacheRequest*>(m_cacheRequest);
    IUIAutomationElement* cachedEl = nullptr;
    if (cacheReq && SUCCEEDED(el->BuildUpdatedCache(cacheReq, &cachedEl)) && cachedEl) {
        el->Release();
        el = cachedEl;
    }

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

    auto* cacheReq = reinterpret_cast<IUIAutomationCacheRequest*>(m_cacheRequest);
    auto* condition = reinterpret_cast<IUIAutomationCondition*>(m_trueCondition);

    // Populate desktop root element metadata
    UIElement result;
    HWND desktopHwnd = nullptr;
    root->get_CurrentNativeWindowHandle((UIA_HWND*)&desktopHwnd);
    result.ownerHwnd = desktopHwnd;

    int rootPid = 0;
    if (SUCCEEDED(root->get_CurrentProcessId(&rootPid))) {
        result.dwProcessId = static_cast<DWORD>(rootPid);
    }

    BSTR rootName = nullptr;
    if (SUCCEEDED(root->get_CurrentName(&rootName)) && rootName) {
        result.name = wide_to_utf8(std::wstring_view{ rootName });
        ::SysFreeString(rootName);
    }
    if (result.name.empty()) {
        result.name = "Desktop";
    }

    CONTROLTYPEID rootCtypeId = 0;
    if (SUCCEEDED(root->get_CurrentControlType(&rootCtypeId))) {
        result.controlType = controlTypeToString(rootCtypeId);
    }
    if (result.controlType.empty()) {
        result.controlType = "Pane";
    }

    RECT rootRect{};
    if (SUCCEEDED(root->get_CurrentBoundingRectangle(&rootRect))) {
        result.bounds = rootRect;
    }

    // Enumerate direct children of desktop root (top-level windows) using cache
    IUIAutomationElementArray* childrenArray = nullptr;
    if (cacheReq && condition) {
        hr = root->FindAllBuildCache(TreeScope_Children, condition, cacheReq, &childrenArray);
    } else if (condition) {
        hr = root->FindAll(TreeScope_Children, condition, &childrenArray);
    }
    root->Release();

    if (FAILED(hr) || !childrenArray) {
        return std::unexpected("Could not enumerate desktop top-level windows");
    }

    int length = 0;
    childrenArray->get_Length(&length);
    result.children.reserve(static_cast<size_t>(length));

    const DWORD selfPid = ::GetCurrentProcessId();
    for (int i = 0; i < length; ++i) {
        IUIAutomationElement* child = nullptr;
        if (SUCCEEDED(childrenArray->GetElement(i, &child)) && child) {
            int pid = 0;
            if ((SUCCEEDED(child->get_CachedProcessId(&pid)) || SUCCEEDED(child->get_CurrentProcessId(&pid))) &&
                static_cast<DWORD>(pid) == selfPid) {
                // Filter out own process per user instruction
                child->Release();
                continue;
            }
            result.children.push_back(walkElement(child, 1, desktopHwnd));
            child->Release();
        }
    }
    childrenArray->Release();

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

    // 1. Native Window Handle (prefer cached, fallback to current, fallback to ownerHwnd)
    HWND currentHwnd = nullptr;
    UIA_HWND uiaHwnd = nullptr;
    if (FAILED(el->get_CachedNativeWindowHandle(&uiaHwnd)) || !uiaHwnd) {
        el->get_CurrentNativeWindowHandle(&uiaHwnd);
    }
    if (uiaHwnd) currentHwnd = reinterpret_cast<HWND>(uiaHwnd);
    if (!currentHwnd) currentHwnd = ownerHwnd;
    result.ownerHwnd = currentHwnd;

    // 2. Process ID
    int pid = 0;
    if (SUCCEEDED(el->get_CachedProcessId(&pid)) || SUCCEEDED(el->get_CurrentProcessId(&pid))) {
        result.dwProcessId = static_cast<DWORD>(pid);
    }

    // 3. Name
    BSTR name = nullptr;
    if (FAILED(el->get_CachedName(&name)) || !name) {
        el->get_CurrentName(&name);
    }
    if (name) {
        result.name = wide_to_utf8(std::wstring_view{ name });
        ::SysFreeString(name);
    }

    // 4. Control type
    CONTROLTYPEID ctypeId = 0;
    if (SUCCEEDED(el->get_CachedControlType(&ctypeId)) || SUCCEEDED(el->get_CurrentControlType(&ctypeId))) {
        result.controlType = controlTypeToString(ctypeId);
    }

    // 5. AutomationId
    BSTR automId = nullptr;
    if (FAILED(el->get_CachedAutomationId(&automId)) || !automId) {
        el->get_CurrentAutomationId(&automId);
    }
    if (automId) {
        result.automationId = wide_to_utf8(std::wstring_view{ automId });
        ::SysFreeString(automId);
    }

    // 6. RuntimeId
    SAFEARRAY* saId = nullptr;
    if (SUCCEEDED(el->GetRuntimeId(&saId)) && saId) {
        long lb = 0, ub = -1;
        SafeArrayGetLBound(saId, 1, &lb);
        SafeArrayGetUBound(saId, 1, &ub);
        if (ub >= lb) {
            result.runtimeId.reserve(static_cast<size_t>(ub - lb + 1));
            for (long i = lb; i <= ub; ++i) {
                int val = 0;
                SafeArrayGetElement(saId, &i, &val);
                result.runtimeId.push_back(val);
            }
        }
        SafeArrayDestroy(saId);
    }

    // 7. Bounding rectangle
    RECT rect{};
    if (SUCCEEDED(el->get_CachedBoundingRectangle(&rect))) {
        result.bounds = rect;
    } else if (SUCCEEDED(el->get_CurrentBoundingRectangle(&rect))) {
        result.bounds = rect;
    }

    // 8. Boolean Properties
    BOOL enabled = FALSE;
    if (SUCCEEDED(el->get_CachedIsEnabled(&enabled)) || SUCCEEDED(el->get_CurrentIsEnabled(&enabled))) {
        result.isEnabled = (enabled != 0);
    }

    BOOL focusable = FALSE;
    if (SUCCEEDED(el->get_CachedIsKeyboardFocusable(&focusable)) || SUCCEEDED(el->get_CurrentIsKeyboardFocusable(&focusable))) {
        result.isFocusable = (focusable != 0);
    }

    BOOL focused = FALSE;
    if (SUCCEEDED(el->get_CachedHasKeyboardFocus(&focused)) || SUCCEEDED(el->get_CurrentHasKeyboardFocus(&focused))) {
        result.isFocused = (focused != 0);
    }

    // 9. Check for Invoke Pattern (only for invokable control types)
    bool canInvoke = (ctypeId == UIA_ButtonControlTypeId ||
                      ctypeId == UIA_CheckBoxControlTypeId ||
                      ctypeId == UIA_RadioButtonControlTypeId ||
                      ctypeId == UIA_MenuItemControlTypeId ||
                      ctypeId == UIA_HyperlinkControlTypeId ||
                      ctypeId == UIA_SplitButtonControlTypeId ||
                      ctypeId == UIA_TabItemControlTypeId);
    if (canInvoke) {
        VARIANT varInv;
        VariantInit(&varInv);
        if (SUCCEEDED(el->GetCachedPropertyValue(UIA_IsInvokePatternAvailablePropertyId, &varInv)) &&
            varInv.vt == VT_BOOL) {
            result.supportsInvoke = (varInv.boolVal == VARIANT_TRUE);
        } else {
            IUnknown* pUnk = nullptr;
            if (SUCCEEDED(el->GetCurrentPattern(UIA_InvokePatternId, &pUnk)) && pUnk) {
                result.supportsInvoke = true;
                pUnk->Release();
            }
        }
        VariantClear(&varInv);
    }

    // 10. Extract accessible text (only for Document or Edit control types)
    if (ctypeId == UIA_DocumentControlTypeId || ctypeId == UIA_EditControlTypeId) {
        VARIANT varText;
        VariantInit(&varText);
        bool hasTextPattern = true;
        if (SUCCEEDED(el->GetCachedPropertyValue(UIA_IsTextPatternAvailablePropertyId, &varText)) &&
            varText.vt == VT_BOOL) {
            hasTextPattern = (varText.boolVal == VARIANT_TRUE);
        }
        VariantClear(&varText);

        if (hasTextPattern) {
            IUIAutomationTextPattern* textPattern = nullptr;
            if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&textPattern))) && textPattern) {
                IUIAutomationTextRange* range = nullptr;
                if (SUCCEEDED(textPattern->get_DocumentRange(&range)) && range) {
                    BSTR textStr = nullptr;
                    if (SUCCEEDED(range->GetText(1024, &textStr)) && textStr) {
                        result.value = wide_to_utf8(std::wstring_view{textStr});
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
        }
    }

    // 11. Fallback to ValuePattern for input controls
    if (result.value.empty() &&
        (ctypeId == UIA_EditControlTypeId || ctypeId == UIA_ComboBoxControlTypeId ||
         ctypeId == UIA_ProgressBarControlTypeId || ctypeId == UIA_SliderControlTypeId ||
         ctypeId == UIA_SpinnerControlTypeId || ctypeId == UIA_DocumentControlTypeId)) {
        VARIANT varVal;
        VariantInit(&varVal);
        bool hasValPattern = true;
        if (SUCCEEDED(el->GetCachedPropertyValue(UIA_IsValuePatternAvailablePropertyId, &varVal)) &&
            varVal.vt == VT_BOOL) {
            hasValPattern = (varVal.boolVal == VARIANT_TRUE);
        }
        VariantClear(&varVal);

        if (hasValPattern) {
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
    }

    // 12. Recurse into children (max depth 32) using cached batching
    if (depth < 32) {
        auto* cacheReq = reinterpret_cast<IUIAutomationCacheRequest*>(m_cacheRequest);
        auto* condition = reinterpret_cast<IUIAutomationCondition*>(m_trueCondition);

        IUIAutomationElementArray* childrenArray = nullptr;
        HRESULT hr = E_FAIL;
        if (cacheReq && condition) {
            hr = el->FindAllBuildCache(TreeScope_Children, condition, cacheReq, &childrenArray);
        } else if (condition) {
            hr = el->FindAll(TreeScope_Children, condition, &childrenArray);
        }

        if (SUCCEEDED(hr) && childrenArray) {
            int length = 0;
            childrenArray->get_Length(&length);
            result.children.reserve(static_cast<size_t>(length));
            for (int i = 0; i < length; ++i) {
                IUIAutomationElement* child = nullptr;
                if (SUCCEEDED(childrenArray->GetElement(i, &child)) && child) {
                    result.children.push_back(walkElement(child, depth + 1, currentHwnd));
                    child->Release();
                }
            }
            childrenArray->Release();
        }
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
const UIElement* UIElement::findInSubtree(std::string_view nameOrId, std::string_view controlTypeFilter, DWORD excludedPid) const {

    if (auto* res = findBestMatch(nameOrId, controlTypeFilter, excludedPid)) return res;
    return nullptr;
}

const UIElement* UIElement::findBestMatch(std::string_view nameOrId, std::string_view controlTypeFilter, DWORD excludedPid) const {
    std::string query(nameOrId);
    to_lower_inplace(query);
    
    std::string typeFilter(controlTypeFilter);
    to_lower_inplace(typeFilter);

    const UIElement* best = nullptr;
    int bestScore = -1;

    DWORD targetExcludedPid = 0;
    if (excludedPid == UIA_EXCLUDE_NONE) {
        targetExcludedPid = 0;
    } else if (excludedPid != 0) {
        targetExcludedPid = excludedPid;
    } else {
        targetExcludedPid = ::GetCurrentProcessId();
    }

    auto check = [&](const UIElement& el) {
        // STRICT SELF-EXCLUSION: Completely ignore any elements from our own process (or injected test PID).
        // This prevents WinBot from ever 'seeing' itself and its own REPL text.
        if (targetExcludedPid != 0 && el.dwProcessId == targetExcludedPid) return;

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
const UIElement* UIElement::findDescendant(std::string_view nameOrId, std::string_view controlTypeFilter, DWORD excludedPid) const {
    // Only search children (important for WaitSelect/Click children)
    for (const auto& child : children) {
        if (const UIElement* res = child.findInSubtree(nameOrId, controlTypeFilter, excludedPid)) return res;
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
