#include "InputTools.h"
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <random>
#include <cmath>

namespace tools {

// ── Mouse helpers ─────────────────────────────────────────────────────────────
static void sendMouseEvent(int x, int y, DWORD flags, DWORD data = 0) {
    int screenW = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int screenH = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int screenX = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenY = ::GetSystemMetrics(SM_YVIRTUALSCREEN);

    INPUT input{};
    input.type           = INPUT_MOUSE;
    input.mi.dx          = static_cast<LONG>((x - screenX) * 65535 / screenW);
    input.mi.dy          = static_cast<LONG>((y - screenY) * 65535 / screenH);
    input.mi.dwFlags     = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | flags;
    input.mi.mouseData   = data;
    ::SendInput(1, &input, sizeof(INPUT));
}

ToolResult moveMouse(int x, int y, bool humanMove) {
    if (humanMove) return humanMouseMove(x, y);
    sendMouseEvent(x, y, MOUSEEVENTF_MOVE);
    return ok(std::format("Mouse moved to ({}, {})", x, y));
}

ToolResult humanMouseMove(int toX, int toY) {
    POINT from;
    ::GetCursorPos(&from);
    
    double dist = std::hypot(toX - from.x, toY - from.y);
    if (dist < 5.0) {
        sendMouseEvent(toX, toY, MOUSEEVENTF_MOVE);
        return ok(std::format("Mouse moved to ({}, {})", toX, toY));
    }

    static std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<double> distDist(0.1, 0.4);
    std::uniform_real_distribution<double> offsetDist(-0.3, 0.3);

    // Control points for cubic bezier
    double p1x = from.x + (toX - from.x) * distDist(gen) + (toY - from.y) * offsetDist(gen);
    double p1y = from.y + (toY - from.y) * distDist(gen) - (toX - from.x) * offsetDist(gen);

    double p2x = from.x + (toX - from.x) * (1.0 - distDist(gen)) - (toY - from.y) * offsetDist(gen);
    double p2y = from.y + (toY - from.y) * (1.0 - distDist(gen)) + (toX - from.x) * offsetDist(gen);

    // Slight overshoot
    std::uniform_real_distribution<double> overDist(0.0, 5.0); // 0-5px overshoot
    double overX = toX + ((toX - from.x) / dist) * overDist(gen);
    double overY = toY + ((toY - from.y) / dist) * overDist(gen);

    int steps = std::max(15, std::min(80, static_cast<int>(dist / 6.0)));
    std::uniform_int_distribution<int> jitterDist(-1, 1);

    auto cubicBezier = [](double t, double p0, double p1, double p2, double p3) {
        double u = 1.0 - t;
        return u*u*u*p0 + 3*u*u*t*p1 + 3*u*t*t*p2 + t*t*t*p3;
    };

    // Main move to overshoot
    for (int i = 1; i <= steps; ++i) {
        double t = static_cast<double>(i) / steps;
        
        // Ease in-out (smoothstep-like)
        double easeT = t * t * (3.0 - 2.0 * t);

        int cx = static_cast<int>(cubicBezier(easeT, from.x, p1x, p2x, overX)) + jitterDist(gen);
        int cy = static_cast<int>(cubicBezier(easeT, from.y, p1y, p2y, overY)) + jitterDist(gen);
        
        sendMouseEvent(cx, cy, MOUSEEVENTF_MOVE);
        
        // Slower at ends, faster in middle (e.g. 2ms to 8ms)
        int delay = 2 + static_cast<int>(std::abs(easeT - 0.5) * 12);
        ::Sleep(delay);
    }
    
    // Correction move back to target if we overshot
    ::Sleep(20 + std::max(0, jitterDist(gen) * 10));
    sendMouseEvent(toX, toY, MOUSEEVENTF_MOVE);
    
    return ok(std::format("Human mouse moved to ({}, {})", toX, toY));
}

ToolResult click(int x, int y, std::string_view button, bool humanMove) {
    if (humanMove) {
        humanMouseMove(x, y);
        ::Sleep(30 + (rand() % 40));
    } else {
        sendMouseEvent(x, y, MOUSEEVENTF_MOVE);
        ::Sleep(30);
    }

    DWORD downFlag{}, upFlag{};
    if (button == "right") {
        downFlag = MOUSEEVENTF_RIGHTDOWN; upFlag = MOUSEEVENTF_RIGHTUP;
    } else if (button == "middle") {
        downFlag = MOUSEEVENTF_MIDDLEDOWN; upFlag = MOUSEEVENTF_MIDDLEUP;
    } else {
        downFlag = MOUSEEVENTF_LEFTDOWN; upFlag = MOUSEEVENTF_LEFTUP;
    }
    sendMouseEvent(x, y, downFlag);
    ::Sleep(30);
    sendMouseEvent(x, y, upFlag);
    return ok(std::format("{} click at ({}, {})", button, x, y));
}

ToolResult doubleClick(int x, int y, bool humanMove) {
    (void)click(x, y, "left", humanMove);
    ::Sleep(50);
    (void)click(x, y, "left", false); // Second click doesn't need to move again
    return ok(std::format("Double click at ({}, {})", x, y));
}

ToolResult drag(int x1, int y1, int x2, int y2, bool humanMove) {
    if (humanMove) {
        humanMouseMove(x1, y1);
        ::Sleep(30 + (rand() % 40));
        sendMouseEvent(x1, y1, MOUSEEVENTF_LEFTDOWN);
        ::Sleep(50 + (rand() % 40));
        // Drag with human movement
        humanMouseMove(x2, y2);
        ::Sleep(20 + (rand() % 20));
    } else {
        sendMouseEvent(x1, y1, MOUSEEVENTF_MOVE);
        ::Sleep(30);
        sendMouseEvent(x1, y1, MOUSEEVENTF_LEFTDOWN);
        ::Sleep(50);
        // Move smoothy in steps
        int steps = 20;
        for (int i = 1; i <= steps; ++i) {
            int x = x1 + (x2 - x1) * i / steps;
            int y = y1 + (y2 - y1) * i / steps;
            sendMouseEvent(x, y, MOUSEEVENTF_MOVE);
            ::Sleep(10);
        }
    }
    // Move smoothy in steps
    sendMouseEvent(x2, y2, MOUSEEVENTF_LEFTUP);
    return ok(std::format("Dragged ({},{}) → ({},{})", x1, y1, x2, y2));
}

ToolResult scroll(int x, int y, int delta, bool humanMove) {
    if (humanMove) {
        humanMouseMove(x, y);
        ::Sleep(30 + (rand() % 30));
    } else {
        sendMouseEvent(x, y, MOUSEEVENTF_MOVE);
        ::Sleep(30);
    }
    sendMouseEvent(x, y, MOUSEEVENTF_WHEEL,
        static_cast<DWORD>(delta * WHEEL_DELTA));
    return ok(std::format("Scrolled {} at ({}, {})", delta, x, y));
}

// ── Keyboard helpers ──────────────────────────────────────────────────────────
static const std::unordered_map<std::string, WORD> kKeyMap = {
    {"enter", VK_RETURN}, {"return", VK_RETURN},
    {"tab", VK_TAB}, {"escape", VK_ESCAPE}, {"esc", VK_ESCAPE},
    {"space", VK_SPACE}, {"backspace", VK_BACK}, {"delete", VK_DELETE},
    {"home", VK_HOME}, {"end", VK_END},
    {"pageup", VK_PRIOR}, {"pagedown", VK_NEXT},
    {"left", VK_LEFT}, {"right", VK_RIGHT}, {"up", VK_UP}, {"down", VK_DOWN},
    {"f1",VK_F1},{"f2",VK_F2},{"f3",VK_F3},{"f4",VK_F4},{"f5",VK_F5},
    {"f6",VK_F6},{"f7",VK_F7},{"f8",VK_F8},{"f9",VK_F9},
    {"f10",VK_F10},{"f11",VK_F11},{"f12",VK_F12},
    {"ctrl", VK_CONTROL}, {"alt", VK_MENU}, {"shift", VK_SHIFT},
    {"win", VK_LWIN}, {"windows", VK_LWIN},
    {"insert", VK_INSERT}, {"printscreen", VK_SNAPSHOT},
};

static void sendKeyEvent(WORD vk, bool down) {
    INPUT input{};
    input.type        = INPUT_KEYBOARD;
    input.ki.wVk      = vk;
    input.ki.dwFlags  = down ? 0 : KEYEVENTF_KEYUP;
    ::SendInput(1, &input, sizeof(INPUT));
}

ToolResult keyPress(std::string_view combo) {
    // Parse "Ctrl+Alt+Delete", "Enter", "F5", etc.
    std::string lower(combo);
    to_lower_inplace(lower);

    std::vector<WORD> modifiers, keys;
    size_t start = 0;
    while (start <= lower.size()) {
        size_t plus = lower.find('+', start);
        std::string token = lower.substr(start, (plus == std::string::npos ? lower.size() : plus) - start);
        // Trim whitespace
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) token.erase(token.begin());
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))  token.pop_back();

        WORD vk = 0;
        bool isModifier = false;
        if (auto it = kKeyMap.find(token); it != kKeyMap.end()) {
            vk = it->second;
            isModifier = (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN);
        } else if (token.size() == 1) {
            vk = static_cast<WORD>(::VkKeyScanA(token[0]) & 0xFF);
        }

        if (vk != 0) {
            if (isModifier) modifiers.push_back(vk);
            else            keys.push_back(vk);
        }
        if (plus == std::string::npos) break;
        start = plus + 1;
    }

    // Press modifiers down
    for (WORD mod : modifiers) sendKeyEvent(mod, true);
    ::Sleep(20);
    // Press keys
    for (WORD k : keys) { sendKeyEvent(k, true); ::Sleep(20); sendKeyEvent(k, false); }
    ::Sleep(20);
    // Release modifiers
    for (auto it = modifiers.rbegin(); it != modifiers.rend(); ++it) sendKeyEvent(*it, false);

    return ok(std::format("Key pressed: {}", combo));
}

ToolResult typeText(std::string_view text) {
    // Type each character using KEYEVENTF_UNICODE for full Unicode support
    for (char32_t c : text) {
        INPUT inputs[2]{};
        inputs[0].type            = INPUT_KEYBOARD;
        inputs[0].ki.wScan        = static_cast<WORD>(c);
        inputs[0].ki.dwFlags      = KEYEVENTF_UNICODE;
        inputs[1]                 = inputs[0];
        inputs[1].ki.dwFlags     |= KEYEVENTF_KEYUP;
        ::SendInput(2, inputs, sizeof(INPUT));
        ::Sleep(5); // Small delay between characters for reliability
    }
    return ok(std::format("Typed {} characters", text.size()));
}

} // namespace tools
