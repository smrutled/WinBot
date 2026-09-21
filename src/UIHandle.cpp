#include "UIHandle.h"
#include "tools/InputTools.h"
#include <stdexcept>
#include <print>
#include <thread>
#include <chrono>

UIHandle::UIHandle(UIElement el, const UIAutomationScanner* scanner)
    : m_element(std::move(el))
    , m_scanner(scanner)
    , m_runtimeId(m_element.runtimeId)
    , m_ownerHwnd(m_element.ownerHwnd)
{}

// ── parent() — find the parent of this element ─────────────────────────────────
UIHandle UIHandle::parent() const {
    if (!m_scanner || m_ownerHwnd == nullptr) {
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
    if (!m_scanner || m_ownerHwnd == nullptr) {
        throw std::runtime_error("Cannot find window: scanner or ownerHwnd is null");
    }

    // GA_ROOT returns the topmost non-desktop ancestor HWND.
    // If m_ownerHwnd is already the root, it returns itself.
    HWND rootHwnd = ::GetAncestor(m_ownerHwnd, GA_ROOT);
    if (!rootHwnd) rootHwnd = m_ownerHwnd; // fallback (shouldn't happen)

    auto res = m_scanner->scanWindowByHandle(rootHwnd);
    if (!res) {
        throw std::runtime_error(std::format("Cannot find window: scan failed ({})", res.error()));
    }

    return UIHandle{ *res, m_scanner };
}

// ── click() — click this element's center ────────────────────────────────────
UIHandle& UIHandle::click() {
    // 1. Try high-reliability 'Invoke' pattern if supported
    if (m_element.supportsInvoke && m_scanner) {
        auto res = m_scanner->invokeElement(m_runtimeId);
        if (res) {
            std::print("Invoked Default Action for [{}] '{}'.\n", 
                       m_element.controlType, m_element.name);
            return *this;
        }
        // If Invoke fails specifically (e.g. element hidden but actionable), 
        // we'll proceed to physical click as a fallback.
    }

    // 2. Fallback to physical mouse click
    if (m_ownerHwnd) {
        ::SetForegroundWindow(m_ownerHwnd);
        ::Sleep(100); // Give OS time to focus
    }
    auto pt = m_element.getCenter();
    bool human = m_scanner ? m_scanner->getHumanMovement() : false;
    auto res = tools::click(pt.x, pt.y, "left", human);
    if (!res) WINBOT_ERROR("UIHandle::click failed: {}", res.error());
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
    auto res = tools::typeText(text);
    if (!res) WINBOT_ERROR("UIHandle::type failed: {}", res.error());
    return *this;
}

// ── key(combo) ────────────────────────────────────────────────────────────────
UIHandle& UIHandle::key(std::string_view combo) {
    // Ensure our window has focus before sending keystrokes — otherwise global
    // keys like Alt+F4 or Ctrl+S fire at whichever window the OS currently has focused.
    if (m_ownerHwnd) {
        ::SetForegroundWindow(m_ownerHwnd);
        ::Sleep(80); // Give OS time to switch focus
    }
    auto res = tools::keyPress(combo);
    if (!res) WINBOT_ERROR("UIHandle::key('{}') failed: {}", combo, res.error());
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
    if (!m_scanner || !m_ownerHwnd) return false;

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
