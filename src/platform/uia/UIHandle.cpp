#include "platform/uia/UIHandle.h"
#include <stdexcept>
#include <thread>
#include <chrono>

static UIHandle::ClickHandler s_clickHandler = nullptr;
static UIHandle::TypeHandler  s_typeHandler = nullptr;
static UIHandle::KeyHandler   s_keyHandler = nullptr;

void UIHandle::setInputHandlers(ClickHandler clickFn, TypeHandler typeFn, KeyHandler keyFn) {
    s_clickHandler = std::move(clickFn);
    s_typeHandler  = std::move(typeFn);
    s_keyHandler   = std::move(keyFn);
}

static void defaultSendMouseClick(int x, int y, std::string_view button) {
    int screenW = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int screenH = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (screenW <= 0) {
        screenW = ::GetSystemMetrics(SM_CXSCREEN);
    }
    if (screenH <= 0) {
        screenH = ::GetSystemMetrics(SM_CYSCREEN);
    }
    if (screenW <= 0) {
        screenW = 1920;
    }
    if (screenH <= 0) {
        screenH = 1080;
    }
    int screenX = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenY = ::GetSystemMetrics(SM_YVIRTUALSCREEN);

    DWORD downFlag = MOUSEEVENTF_LEFTDOWN;
    DWORD upFlag   = MOUSEEVENTF_LEFTUP;
    if (button == "right") {
        downFlag = MOUSEEVENTF_RIGHTDOWN;
        upFlag   = MOUSEEVENTF_RIGHTUP;
    } else if (button == "middle") {
        downFlag = MOUSEEVENTF_MIDDLEDOWN;
        upFlag   = MOUSEEVENTF_MIDDLEUP;
    }

    INPUT inputs[3]{};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dx = static_cast<LONG>((x - screenX) * 65535 / screenW);
    inputs[0].mi.dy = static_cast<LONG>((y - screenY) * 65535 / screenH);
    inputs[0].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE;

    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = downFlag;

    inputs[2].type = INPUT_MOUSE;
    inputs[2].mi.dwFlags = upFlag;

    ::SendInput(3, inputs, sizeof(INPUT));
}

static void defaultSendTypeText(std::string_view text) {
    for (char32_t c : text) {
        INPUT inputs[2]{};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = static_cast<WORD>(c);
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
        inputs[1] = inputs[0];
        inputs[1].ki.dwFlags |= KEYEVENTF_KEYUP;
        ::SendInput(2, inputs, sizeof(INPUT));
        ::Sleep(5);
    }
}

ToolResult UIHandle::clickAt(int x, int y, std::string_view button, bool human) {
    if (s_clickHandler != nullptr) {
        return s_clickHandler(x, y, button, human);
    }
    defaultSendMouseClick(x, y, button);
    return ok(std::format("Clicked at ({}, {})", x, y));
}

ToolResult UIHandle::sendTypeText(std::string_view text) {
    if (s_typeHandler != nullptr) {
        return s_typeHandler(text);
    }
    defaultSendTypeText(text);
    return ok(std::format("Typed {} characters", text.size()));
}

ToolResult UIHandle::sendKeyPress(std::string_view combo) {
    if (s_keyHandler != nullptr) {
        return s_keyHandler(combo);
    }
    return err("Key press handler not configured");
}

UIHandle::UIHandle(UIElement el, const UIAutomationScanner* scanner)
    : m_element(std::move(el))
    , m_scanner(scanner)
    , m_runtimeId(m_element.runtimeId)
    , m_ownerHwnd(m_element.ownerHwnd)
{}

// ── parent() — find the parent of this element ─────────────────────────────────
UIHandle UIHandle::parent() const {
    if (m_scanner == nullptr || m_ownerHwnd == nullptr) {
        throw std::runtime_error("Cannot find parent: scanner or ownerHwnd is null");
    }

    auto res = m_scanner->scanWindowByHandle(m_ownerHwnd);
    if (!res) {
        throw std::runtime_error(std::format("Cannot find parent: scan failed ({})", res.error()));
    }

    const UIElement& root = *res;
    if (const UIElement* parent = root.findParent(m_runtimeId)) {
        return UIHandle{*parent, m_scanner};
    }

    throw std::runtime_error("Element parent not found in current scan");
}

// ── window() — find the top-level window for this element ──────────────────────
// Uses Win32 GetAncestor(GA_ROOT) to walk the HWND parent chain to the true
// top-level application window — no UIA scan needed for the HWND lookup.
UIHandle UIHandle::window() const {
    if (m_scanner == nullptr || m_ownerHwnd == nullptr) {
        throw std::runtime_error("Cannot find window: scanner or ownerHwnd is null");
    }

    // GA_ROOT returns the topmost non-desktop ancestor HWND.
    // If m_ownerHwnd is already the root, it returns itself.
    HWND rootHwnd = ::GetAncestor(m_ownerHwnd, GA_ROOT);
    if (rootHwnd == nullptr) {
        rootHwnd = m_ownerHwnd; // fallback (shouldn't happen)
    }

    auto res = m_scanner->scanWindowByHandle(rootHwnd);
    if (!res) {
        throw std::runtime_error(std::format("Cannot find window: scan failed ({})", res.error()));
    }

    return UIHandle{ *res, m_scanner };
}

// ── click() — click this element's center ────────────────────────────────────
UIHandle& UIHandle::click() {
    // 1. Try high-reliability 'Invoke' pattern if supported
    if (m_element.supportsInvoke && m_scanner != nullptr) {
        auto res = m_scanner->invokeElement(m_runtimeId);
        if (res) {
            WINBOT_INFO("Invoked Default Action for [{}] '{}'.", 
                       m_element.controlType, m_element.name);
            return *this;
        }
        // If Invoke fails specifically (e.g. element hidden but actionable), 
        // we'll proceed to physical click as a fallback.
    }

    // 2. Fallback to physical mouse click
    if (m_ownerHwnd != nullptr) {
        ::SetForegroundWindow(m_ownerHwnd);
        ::Sleep(100); // Give OS time to focus
    }
    auto pt = m_element.getCenter();
    bool human = (m_scanner != nullptr) ? m_scanner->getHumanMovement() : false;
    auto res = clickAt(pt.x, pt.y, "left", human);
    if (!res) {
        WINBOT_ERROR("UIHandle::click failed: {}", res.error());
    }
    return *this;
}

// ── click(name) — find descendant then click it ───────────────────────────────
UIHandle& UIHandle::click(std::string_view nameOrId, std::string_view controlType) {
    try {
        // Use a default 2s polling timeout for robust actions
        UIHandle target = select(nameOrId, 2000, controlType);
        target.click();
    } catch (const std::exception& e) {
        WINBOT_ERROR("UIHandle::click('{}', '{}') failed: {}", nameOrId, controlType, e.what());
    }
    return *this;
}

// ── waitClick(name, timeout) ──────────────────────────────────────────────────
UIHandle& UIHandle::waitClick(std::string_view nameOrId, int timeoutMs, std::string_view controlType) {
    try {
        UIHandle target = select(nameOrId, timeoutMs, controlType);
        target.click();
    } catch (const std::exception& e) {
        WINBOT_ERROR("UIHandle::waitClick('{}', '{}') failed: {}", nameOrId, controlType, e.what());
    }
    return *this;
}

// ── type(text) ────────────────────────────────────────────────────────────────
UIHandle& UIHandle::type(std::string_view text) {
    auto res = sendTypeText(text);
    if (!res) {
        WINBOT_ERROR("UIHandle::type failed: {}", res.error());
    }
    return *this;
}

// ── key(combo) ────────────────────────────────────────────────────────────────
UIHandle& UIHandle::key(std::string_view combo) {
    // Ensure our window has focus before sending keystrokes — otherwise global
    // keys like Alt+F4 or Ctrl+S fire at whichever window the OS currently has focused.
    if (m_ownerHwnd != nullptr) {
        ::SetForegroundWindow(m_ownerHwnd);
        ::Sleep(80); // Give OS time to switch focus
    }
    auto res = sendKeyPress(combo);
    if (!res) {
        WINBOT_ERROR("UIHandle::key('{}') failed: {}", combo, res.error());
    }
    return *this;
}

// ── wait(ms) ──────────────────────────────────────────────────────────────────
UIHandle& UIHandle::wait(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return *this;
}

// ── select(nameOrId, timeoutMs) — scoped search ───────────────────────────────
UIHandle UIHandle::select(std::string_view nameOrId, int timeoutMs, std::string_view controlType) {
    const DWORD excluded = (m_element.dwProcessId == ::GetCurrentProcessId()) ? UIA_EXCLUDE_NONE : 0;

    // First try the cached subtree (instant, no re-scan)
    if (const UIElement* child = m_element.findDescendant(nameOrId, controlType, excluded)) {
        return UIHandle{ *child, m_scanner };
    }

    // If timeout requested, poll and refresh the tree from the live app
    if (timeoutMs > 0) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Re-sync with live app
            if (refresh()) {
                if (const UIElement* child = m_element.findDescendant(nameOrId, controlType, excluded)) {
                    return UIHandle{ *child, m_scanner };
                }
            }
        }
    }

    throw std::runtime_error(
        std::format("UIHandle::select('{}', '{}') — element not found (timeout {}ms)",
                    nameOrId, controlType, timeoutMs));
}

bool UIHandle::refresh() {
    if (m_scanner == nullptr || m_ownerHwnd == nullptr) {
        return false;
    }

    // Window-level re-scan using HWND. This is robust for both Native and WebView
    // because it refreshes the entire application tree scoped to that window.
    auto res = m_scanner->scanWindowByHandle(m_ownerHwnd);
    if (res) {
        // 1. Try exact RuntimeId match (most robust)
        if (!m_runtimeId.empty()) {
            if (const UIElement* found = res->findByRuntimeId(m_runtimeId)) {
                m_element = *found;
                m_runtimeId = m_element.runtimeId;
                return true;
            }
        }

        // 2. Fallback to Name/AutomationId matching via findInSubtree
        // This handles cases where RuntimeId might have changed significantly (unlikely but possible)
        std::string searchKey = m_element.automationId.empty() ? m_element.name : m_element.automationId;
        if (!searchKey.empty()) {
            if (const UIElement* found = res->findInSubtree(searchKey)) {
                m_element = *found;
                m_runtimeId = m_element.runtimeId;
                return true;
            }
        }
    }

    return false;
}
