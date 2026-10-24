#include "platform/uia/UIAutomationScanner.h"
#include "platform/uia/UIHandle.h"

// UIAutomation.h (client-side) includes UIAutomationCore.h which needs
// rpcndr.h (for MIDL_INTERFACE). Must NOT have WIN32_LEAN_AND_MEAN active
// when these are first included — Windows.h in Common.h may strip RPC headers.
// We work around this by including UIAutomation.h BEFORE Common.h
// in a separate preinclude approach, or by including the COM headers manually.
#include <rpcndr.h>      // Defines MIDL_INTERFACE (must come before UIAutomation.h)
#include <objbase.h>     // COM base (CoCreateInstance, etc.)
#include <UIAutomation.h> // IUIAutomation, IUIAutomationElement, etc.
#include "Common.h"
#include "platform/win/WindowUtils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ranges>
#include <regex>
#include <span>
#include <thread>

constexpr char ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

constexpr auto ci_eq = [](char a, char b) noexcept {
    return ascii_lower(a) == ascii_lower(b);
};

inline bool ci_equal(std::string_view a, std::string_view b) noexcept {
    return std::ranges::equal(a, b, ci_eq);
}

inline bool ci_contains(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    return !std::ranges::search(haystack, needle, ci_eq).empty();
}

template <typename T>
static void safeRelease(void*& p) noexcept {
    if (p != nullptr) {
        static_cast<T*>(p)->Release();
        p = nullptr;
    }
}

UIAutomationScanner::UIAutomationScanner() {
    HRESULT hrInit = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hrInit) && hrInit != RPC_E_CHANGED_MODE) {
        WINBOT_WARN("UIAutomationScanner: CoInitializeEx returned 0x{:08X}", static_cast<uint32_t>(hrInit));
    }

    void* rawAutomation = nullptr;
    HRESULT hr = ::CoCreateInstance(
        __uuidof(CUIAutomation),
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(IUIAutomation),
        &rawAutomation
    );
    if (FAILED(hr)) {
        WINBOT_ERROR("UIAutomationScanner: CoCreateInstance failed 0x{:08X}", static_cast<uint32_t>(hr));
        return;
    }
    m_automation = rawAutomation;
    auto* automation = static_cast<IUIAutomation*>(rawAutomation);

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
    safeRelease<IUIAutomationCacheRequest>(m_cacheRequest);
    safeRelease<IUIAutomationCondition>(m_trueCondition);
    safeRelease<IUIAutomationTreeWalker>(m_treeWalker);
    safeRelease<IUIAutomation>(m_automation);
    ::CoUninitialize();
}

std::expected<UIElement, std::string> UIAutomationScanner::scanFocusedWindow() const {
    if (!m_automation) return std::unexpected("UI Automation not initialized");

    auto* automation = static_cast<IUIAutomation*>(m_automation);
    IUIAutomationElement* focusedEl = nullptr;
    HRESULT hr = automation->GetFocusedElement(&focusedEl);
    if (FAILED(hr) || !focusedEl) return std::unexpected("No focused element");

    // Walk up to the root window to get context
    auto* walker = static_cast<IUIAutomationTreeWalker*>(m_treeWalker);
    if (!walker) {
        focusedEl->Release();
        return std::unexpected("UI Automation TreeWalker not initialized");
    }
    IUIAutomationElement* current = focusedEl;
    IUIAutomationElement* parent  = nullptr;
    HWND hwnd = nullptr;

    while (current) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        current->get_CurrentNativeWindowHandle(reinterpret_cast<UIA_HWND*>(&hwnd));
        if (hwnd) break;

        hr = walker->GetParentElement(current, &parent);
        if (FAILED(hr) || !parent) break;
        if (current != focusedEl) current->Release();
        current = parent;
    }

    auto* cacheReq = static_cast<IUIAutomationCacheRequest*>(m_cacheRequest);
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
static HWND findBestWindow(std::string_view query) {
    return winbot::find_best_window(query);
}

std::expected<UIElement, std::string> UIAutomationScanner::scanWindow(
    std::string_view title) const
{
    HWND found = findBestWindow(title);

    if (!found) {
        // FALLBACK: Search Desktop Root for any Window matching the name.
        if (auto desktop = scanDesktop(); desktop) {
            if (const UIElement* best = desktop->findBestMatch(title, "Window")) {
                return *best;
            }
        }
        return std::unexpected(std::format("Window '{}' not found", title));
    }

    return scanWindowByHandle(found);
}

std::expected<UIElement, std::string> UIAutomationScanner::scanWindowByHandle(HWND hwnd) const {
    if (!m_automation) return std::unexpected("UI Automation not initialized");
    auto* automation = static_cast<IUIAutomation*>(m_automation);
    IUIAutomationElement* el = nullptr;
    HRESULT hr = automation->ElementFromHandle(hwnd, &el);
    if (FAILED(hr) || !el) return std::unexpected("Could not get UIA element for window handle");

    auto* cacheReq = static_cast<IUIAutomationCacheRequest*>(m_cacheRequest);
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

    auto* automation = static_cast<IUIAutomation*>(m_automation);
    IUIAutomationElement* root = nullptr;
    HRESULT hr = automation->GetRootElement(&root);
    if (FAILED(hr) || !root) return std::unexpected("Could not get desktop root element");

    auto* condition = static_cast<IUIAutomationCondition*>(m_trueCondition);

    // Populate desktop root element metadata
    UIElement result;
    UIA_HWND desktopUiaHwnd = nullptr;
    root->get_CurrentNativeWindowHandle(&desktopUiaHwnd);
    HWND desktopHwnd = static_cast<HWND>(desktopUiaHwnd);
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

    // Enumerate direct children of desktop root (top-level windows)
    IUIAutomationElementArray* childrenArray = nullptr;
    if (condition) {
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
    if (runtimeId.empty()) return std::unexpected("RuntimeId is empty");
    auto* automation = static_cast<IUIAutomation*>(m_automation);

    IUIAutomationElement* root = nullptr;
    if (FAILED(automation->GetRootElement(&root)) || !root) 
        return std::unexpected("Could not get desktop root for invoke");

    // Search for element by RuntimeId
    IUIAutomationCondition* condition = nullptr;
    VARIANT varId;
    VariantInit(&varId);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    varId.vt = VT_ARRAY | VT_I4;
    SAFEARRAYBOUND bound{ static_cast<ULONG>(runtimeId.size()), 0 };
    varId.parray = SafeArrayCreate(VT_I4, 1, &bound);
    if (!varId.parray) {
        root->Release();
        return std::unexpected("Failed to allocate SAFEARRAY for RuntimeId");
    }
    for (long i = 0; i < static_cast<long>(runtimeId.size()); ++i) {
        int val = runtimeId.at(static_cast<size_t>(i));
        HRESULT hrPut = SafeArrayPutElement(varId.parray, &i, &val);
        if (FAILED(hrPut)) {
            VariantClear(&varId);
            root->Release();
            return std::unexpected("Failed to populate SAFEARRAY element for RuntimeId");
        }
    }
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)

    HRESULT hrCond = automation->CreatePropertyCondition(UIA_RuntimeIdPropertyId, varId, &condition);
    VariantClear(&varId);
    if (FAILED(hrCond) || !condition) {
        root->Release();
        return std::unexpected("Failed to create property condition for invoke");
    }

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
    UIElement result;
    if (!elPtr) return result;
    auto* el = static_cast<IUIAutomationElement*>(elPtr);

    // 1. Native Window Handle (prefer cached, fallback to current, fallback to ownerHwnd)
    HWND currentHwnd = nullptr;
    UIA_HWND uiaHwnd = nullptr;
    if (FAILED(el->get_CachedNativeWindowHandle(&uiaHwnd)) || !uiaHwnd) {
        el->get_CurrentNativeWindowHandle(&uiaHwnd);
    }
    if (uiaHwnd) currentHwnd = static_cast<HWND>(uiaHwnd);
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
        long lb = 0;
        long ub = -1;
        SafeArrayGetLBound(saId, 1, &lb);
        SafeArrayGetUBound(saId, 1, &ub);
        if (ub >= lb) {
            void* rawData = nullptr;
            if (SUCCEEDED(::SafeArrayAccessData(saId, &rawData)) && rawData != nullptr) {
                auto span = std::span<const int>(static_cast<const int*>(rawData), static_cast<size_t>(ub - lb + 1));
                result.runtimeId.assign_range(span);
                (void)::SafeArrayUnaccessData(saId);
            }
        }
        SafeArrayDestroy(saId);
    }

    // 7. Bounding rectangle
    RECT rect{};
    if (SUCCEEDED(el->get_CachedBoundingRectangle(&rect)) ||
        SUCCEEDED(el->get_CurrentBoundingRectangle(&rect))) {
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
    constexpr std::array<long, 7> kInvokableTypes = {{
        UIA_ButtonControlTypeId,
        UIA_CheckBoxControlTypeId,
        UIA_RadioButtonControlTypeId,
        UIA_MenuItemControlTypeId,
        UIA_HyperlinkControlTypeId,
        UIA_SplitButtonControlTypeId,
        UIA_TabItemControlTypeId,
    }};
    bool canInvoke = std::ranges::contains(kInvokableTypes, ctypeId);
    if (canInvoke) {
        VARIANT varInv;
        VariantInit(&varInv);
        // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
        if (SUCCEEDED(el->GetCachedPropertyValue(UIA_IsInvokePatternAvailablePropertyId, &varInv)) &&
            varInv.vt == VT_BOOL) {
            result.supportsInvoke = (varInv.boolVal == VARIANT_TRUE);
        // NOLINTEND(cppcoreguidelines-pro-type-union-access)
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
        // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
        if (SUCCEEDED(el->GetCachedPropertyValue(UIA_IsTextPatternAvailablePropertyId, &varText)) &&
            varText.vt == VT_BOOL) {
            hasTextPattern = (varText.boolVal == VARIANT_TRUE);
        }
        // NOLINTEND(cppcoreguidelines-pro-type-union-access)
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
        // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
        if (SUCCEEDED(el->GetCachedPropertyValue(UIA_IsValuePatternAvailablePropertyId, &varVal)) &&
            varVal.vt == VT_BOOL) {
            hasValPattern = (varVal.boolVal == VARIANT_TRUE);
        }
        // NOLINTEND(cppcoreguidelines-pro-type-union-access)
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
        auto* cacheReq = static_cast<IUIAutomationCacheRequest*>(m_cacheRequest);
        auto* condition = static_cast<IUIAutomationCondition*>(m_trueCondition);

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

std::string_view UIAutomationScanner::controlTypeToString(long id) noexcept {
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

static void serializeImpl(const UIElement& el, int indent, std::string& out) {
    bool hasData = !el.name.empty() || !el.automationId.empty() || el.isFocusable || !el.value.empty() || el.controlType == "Document";
    bool isVisible = (el.bounds.right > el.bounds.left && el.bounds.bottom > el.bounds.top);
    bool shouldPrint = hasData && isVisible;

    if (shouldPrint) {
        std::format_to(std::back_inserter(out), "{:{}}[{}: \"{}\"", "", indent * 2, el.controlType, el.name);
        
        if (!el.value.empty()) {
            std::string dispVal = el.value;
            std::ranges::replace(dispVal, '\n', ' ');
            if (dispVal.length() > 60) {
                dispVal.resize(57);
                dispVal += "...";
            }
            std::format_to(std::back_inserter(out), " value=\"{}\"", dispVal);
        }
        
        if (!el.automationId.empty()) {
            std::format_to(std::back_inserter(out), " id=\"{}\"", el.automationId);
        }
        std::format_to(std::back_inserter(out), " at ({},{},{},{})",
            el.bounds.left, el.bounds.top, el.bounds.right, el.bounds.bottom);
        if (el.isFocused)      out += " focused";
        if (!el.isEnabled)     out += " disabled";
        if (el.isFocusable)    out += " focusable";
        if (el.supportsInvoke) out += " invokable";
        out += "]\n";
    }

    int childIndent = shouldPrint ? (indent + 1) : indent;
    for (const auto& child : el.children) {
        serializeImpl(child, childIndent, out);
    }
}

std::string UIAutomationScanner::serialize(const UIElement& el, int indent) {
    std::string out;
    out.reserve(4096);
    serializeImpl(el, indent, out);
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
    if ((ci_contains(root.name, name) || ci_contains(root.automationId, name)) && root.isEnabled) {
        return root.getCenter();
    }
    for (const auto& child : root.children) {
        if (auto pt = findElementCenter(child, name); pt.has_value()) return pt;
    }
    return std::nullopt;
}

// ── UIElement finders with deducing this ──────────────────────────────────────

template <class Self>
auto UIElement::findParent(this Self& self, const std::vector<int>& targetRuntimeId)
    -> std::conditional_t<std::is_const_v<Self>, const UIElement*, UIElement*>
{
    if (targetRuntimeId.empty()) return nullptr;

    for (auto& child : self.children) {
        if (child.runtimeId == targetRuntimeId) {
            return &self;
        }
        if (auto* found = child.findParent(targetRuntimeId)) {
            return found;
        }
    }
    return nullptr;
}

template <class Self>
auto UIElement::findBestMatch(this Self& self,
                              std::string_view nameOrId,
                              std::string_view controlTypeFilter,
                              DWORD excludedPid)
    -> std::conditional_t<std::is_const_v<Self>, const UIElement*, UIElement*>
{
    using ElemPtr = std::conditional_t<std::is_const_v<Self>, const UIElement*, UIElement*>;

    ElemPtr best = nullptr;
    int bestScore = -1;

    DWORD targetExcludedPid = 0;
    if (excludedPid == UIA_EXCLUDE_NONE) {
        targetExcludedPid = 0;
    } else if (excludedPid != 0) {
        targetExcludedPid = excludedPid;
    } else {
        targetExcludedPid = ::GetCurrentProcessId();
    }

    auto check = [&](ElemPtr el) {
        if (targetExcludedPid != 0 && el->dwProcessId == targetExcludedPid) return;

        int score = 0;

        if (!controlTypeFilter.empty()) {
            if (!ci_equal(el->controlType, controlTypeFilter)) return;
            score += 100;
        }

        bool exactMatch = (ci_equal(el->name, nameOrId) || ci_equal(el->automationId, nameOrId));
        bool partialMatch = (ci_contains(el->name, nameOrId) || ci_contains(el->automationId, nameOrId));

        if (exactMatch) score += 50;
        else if (partialMatch) score += 10;
        else return;

        if (el->controlType == "Window") score += 100;
        if (el->isFocusable || el->isEnabled) score += 5;

        if (score > bestScore) {
            bestScore = score;
            best = el;
        }
    };

    std::vector<ElemPtr> stack;
    stack.push_back(&self);

    while (!stack.empty()) {
        ElemPtr el = stack.back();
        stack.pop_back();

        check(el);
        if (bestScore >= 150) break;

        stack.append_range(el->children | std::views::reverse | std::views::transform([](auto& c) -> ElemPtr {
            return &c;
        }));
    }

    return best;
}

template <class Self>
auto UIElement::findByRuntimeId(this Self& self, const std::vector<int>& id)
    -> std::conditional_t<std::is_const_v<Self>, const UIElement*, UIElement*>
{
    using ElemPtr = std::conditional_t<std::is_const_v<Self>, const UIElement*, UIElement*>;
    if (id.empty()) return nullptr;
    if (self.runtimeId == id) return &self;

    std::vector<ElemPtr> stack;
    stack.append_range(self.children | std::views::reverse | std::views::transform([](auto& c) -> ElemPtr {
        return &c;
    }));

    while (!stack.empty()) {
        ElemPtr el = stack.back();
        stack.pop_back();

        if (el->runtimeId == id) return el;

        stack.append_range(el->children | std::views::reverse | std::views::transform([](auto& c) -> ElemPtr {
            return &c;
        }));
    }
    return nullptr;
}

template <class Self>
auto UIElement::findDescendant(this Self& self,
                               std::string_view nameOrId,
                               std::string_view controlTypeFilter,
                               DWORD excludedPid)
    -> std::conditional_t<std::is_const_v<Self>, const UIElement*, UIElement*>
{
    for (auto& child : self.children) {
        if (auto* res = child.findInSubtree(nameOrId, controlTypeFilter, excludedPid)) {
            return res;
        }
    }
    return nullptr;
}

// Explicit template instantiations
template auto UIElement::findParent(this UIElement&, const std::vector<int>&) -> UIElement*;
template auto UIElement::findParent(this const UIElement&, const std::vector<int>&) -> const UIElement*;

template auto UIElement::findBestMatch(this UIElement&, std::string_view, std::string_view, DWORD) -> UIElement*;
template auto UIElement::findBestMatch(this const UIElement&, std::string_view, std::string_view, DWORD) -> const UIElement*;

template auto UIElement::findByRuntimeId(this UIElement&, const std::vector<int>&) -> UIElement*;
template auto UIElement::findByRuntimeId(this const UIElement&, const std::vector<int>&) -> const UIElement*;

template auto UIElement::findDescendant(this UIElement&, std::string_view, std::string_view, DWORD) -> UIElement*;
template auto UIElement::findDescendant(this const UIElement&, std::string_view, std::string_view, DWORD) -> const UIElement*;

// ── UIAutomationScanner::select ───────────────────────────────────────────────
std::expected<UIHandle, std::string> UIAutomationScanner::select(
    std::string_view nameOrId, int timeoutMs, std::string_view controlTypeFilter, std::string_view windowTitle) const
{
    auto scanOnce = [&]() -> std::expected<UIElement, std::string> {
        if (windowTitle.empty())
            return scanFocusedWindow();
        return scanWindow(windowTitle);
    };

    auto tryFind = [&](auto& tree) -> std::optional<UIHandle> {
        if (auto* el = tree.findBestMatch(nameOrId, controlTypeFilter)) {
            return UIHandle{ std::move(*el), this };
        }
        return std::nullopt;
    };

    // Try primary scan (focused window or named window)
    if (auto tree = scanOnce(); tree) {
        if (auto h = tryFind(*tree)) {
            return std::move(*h);
        }
    }

    // FALLBACK: If windowTitle was empty, try a global Desktop search
    if (windowTitle.empty()) {
        if (auto tree = scanDesktop(); tree) {
            if (auto h = tryFind(*tree)) {
                return std::move(*h);
            }
        }
    }

    if (timeoutMs <= 0)
        return std::unexpected(std::format("select('{}') — not found in focused window or desktop", nameOrId));

    using namespace std::chrono_literals;
    auto match = poll_until(std::chrono::milliseconds(timeoutMs), 100ms, [&]() -> std::optional<UIHandle> {
        if (auto tree = scanOnce(); tree) {
            if (auto h = tryFind(*tree)) {
                return std::move(*h);
            }
        }
        return std::nullopt;
    });

    if (match.has_value()) {
        return std::move(*match);
    }
    return std::unexpected(
        std::format("select('{}') — timed out after {} ms", nameOrId, timeoutMs));
}

// ── UIAutomationScanner::waitForWindow ────────────────────────────────────────
std::expected<UIHandle, std::string> UIAutomationScanner::waitForWindow(
    std::string_view title, int timeoutMs) const
{
    using namespace std::chrono_literals;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    auto match = poll_until(std::chrono::milliseconds(timeoutMs), 200ms, [&]() -> std::optional<UIHandle> {
        auto tree = scanWindow(title);
        bool outOfTime = std::chrono::steady_clock::now() > (deadline - 500ms);
        if (tree && (tree->controlType == "Window" || outOfTime) && (!tree->name.empty() || outOfTime)) {
            return UIHandle{ std::move(*tree), this };
        }
        return std::nullopt;
    });

    if (match.has_value()) {
        return std::move(*match);
    }
    
    return std::unexpected(
        std::format("waitForWindow('{}') — timed out after {} ms", title, timeoutMs));
}
