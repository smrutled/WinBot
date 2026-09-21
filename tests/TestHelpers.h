#pragma once

#include <gtest/gtest.h>
#include "UIAutomationScanner.h"
#include <objbase.h>
#include <string>
#include <vector>
#include <format>

// ── GoogleTest COM Environment (RAII) ────────────────────────────────────────
// Ensures COM is initialized once for the test run and properly cleaned up.
class ComEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        // RPC_E_CHANGED_MODE (0x80010106) is acceptable if already initialized in another mode
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            std::print("ComEnvironment: CoInitializeEx returned 0x{:08X}\n", static_cast<uint32_t>(hr));
        }
    }

    void TearDown() override {
        ::CoUninitialize();
    }
};

// ── Synthetic UI Tree Construction Helpers ────────────────────────────────────

inline UIElement createMockElement(
    std::string name,
    std::string controlType,
    std::string automationId = "",
    RECT bounds = {0, 0, 100, 30},
    bool isEnabled = true,
    bool isFocusable = true,
    std::vector<int> runtimeId = {1},
    DWORD dwProcessId = 1234,
    HWND ownerHwnd = reinterpret_cast<HWND>(0x1000)
) {
    UIElement el;
    el.name = std::move(name);
    el.controlType = std::move(controlType);
    el.automationId = std::move(automationId);
    el.bounds = bounds;
    el.isEnabled = isEnabled;
    el.isFocusable = isFocusable;
    el.runtimeId = std::move(runtimeId);
    el.dwProcessId = dwProcessId;
    el.ownerHwnd = ownerHwnd;
    return el;
}

// Builds a mock Calculator window with buttons and display
inline UIElement createCalculatorTree(DWORD pid = 1234, HWND hwnd = reinterpret_cast<HWND>(0x2000)) {
    UIElement calc = createMockElement("Calculator", "Window", "CalculatorWindow", {100, 100, 500, 700}, true, true, {100}, pid, hwnd);

    UIElement displayGroup = createMockElement("Display", "Group", "DisplayGroup", {110, 110, 490, 200}, true, false, {100, 1}, pid, hwnd);
    UIElement resultText = createMockElement("0", "Text", "CalculatorResults", {120, 120, 480, 190}, true, false, {100, 1, 1}, pid, hwnd);
    resultText.value = "0";
    displayGroup.children.push_back(std::move(resultText));
    calc.children.push_back(std::move(displayGroup));

    UIElement keypad = createMockElement("Number pad", "Group", "NumberPad", {110, 210, 490, 680}, true, false, {100, 2}, pid, hwnd);

    // Add buttons
    keypad.children.push_back(createMockElement("One", "Button", "num1Button", {120, 220, 200, 280}, true, true, {100, 2, 1}, pid, hwnd));
    keypad.children.push_back(createMockElement("Two", "Button", "num2Button", {210, 220, 290, 280}, true, true, {100, 2, 2}, pid, hwnd));
    keypad.children.push_back(createMockElement("Three", "Button", "num3Button", {300, 220, 380, 280}, true, true, {100, 2, 3}, pid, hwnd));
    keypad.children.push_back(createMockElement("Plus", "Button", "plusButton", {390, 220, 470, 280}, true, true, {100, 2, 4}, pid, hwnd));
    keypad.children.push_back(createMockElement("Equals", "Button", "equalButton", {390, 290, 470, 350}, true, true, {100, 2, 5}, pid, hwnd));

    calc.children.push_back(std::move(keypad));
    return calc;
}

// Builds a mock Desktop containing Calculator and sibling windows (like OneDrive, Notepad)
inline UIElement createDesktopWithSiblings(DWORD calcPid = 1234, DWORD otherPid = 5678) {
    UIElement desktop = createMockElement("Desktop", "Pane", "", {0, 0, 1920, 1080}, true, false, {1}, 0, nullptr);

    // Sibling 1: Calculator
    desktop.children.push_back(createCalculatorTree(calcPid, reinterpret_cast<HWND>(0x2000)));

    // Sibling 2: OneDrive window (tests the classic "One" vs "OneDrive" scoping clash)
    UIElement oneDrive = createMockElement("OneDrive", "Window", "OneDriveWindow", {600, 100, 1000, 600}, true, true, {200}, otherPid, reinterpret_cast<HWND>(0x3000));
    oneDrive.children.push_back(createMockElement("One Sync Status", "Text", "SyncStatus", {610, 110, 990, 150}, true, false, {200, 1}, otherPid, reinterpret_cast<HWND>(0x3000)));
    oneDrive.children.push_back(createMockElement("Open Folder", "Button", "OpenBtn", {610, 160, 750, 200}, true, true, {200, 2}, otherPid, reinterpret_cast<HWND>(0x3000)));
    desktop.children.push_back(std::move(oneDrive));

    // Sibling 3: Notepad
    UIElement notepad = createMockElement("Untitled - Notepad", "Window", "Notepad", {200, 200, 800, 700}, true, true, {300}, otherPid, reinterpret_cast<HWND>(0x4000));
    notepad.children.push_back(createMockElement("Text Editor", "Document", "Editor", {210, 240, 790, 690}, true, true, {300, 1}, otherPid, reinterpret_cast<HWND>(0x4000)));
    desktop.children.push_back(std::move(notepad));

    return desktop;
}

// Builds a deeply nested tree to test recursion and stack safety
inline UIElement createDeepTree(int depth, std::string leafName = "TargetLeaf") {
    UIElement root = createMockElement("DeepRoot", "Window", "Root", {0, 0, 500, 500}, true, true, {1});
    UIElement* current = &root;

    for (int i = 1; i <= depth; ++i) {
        std::vector<int> rId = {1};
        rId.push_back(i);
        std::string name = (i == depth) ? leafName : std::format("Level_{}", i);
        std::string type = (i == depth) ? "Button" : "Group";
        current->children.push_back(createMockElement(name, type, std::format("id_{}", i), {0, 0, 100, 100}, true, true, rId));
        current = &current->children.back();
    }
    return root;
}

#include <functional>

// Builds a wide and structured tree with N total nodes grouped into panels
inline UIElement createLargeTree(int totalNodes) {
    UIElement root = createMockElement("RootApp", "Window", "RootWin", {0, 0, 1920, 1080}, true, true, {1});
    if (totalNodes <= 1) return root;

    int remaining = totalNodes - 1;
    int numGroups = (std::max)(1, remaining / 50);
    int itemsPerGroup = remaining / numGroups;

    int created = 1;
    root.children.reserve(numGroups);

    for (int g = 0; g < numGroups; ++g) {
        std::vector<int> groupRId = {1, g + 1};
        UIElement group = createMockElement(
            std::format("Group_{}", g + 1), "Group", std::format("groupId_{}", g + 1),
            {0, 0, 800, 600}, true, false, groupRId
        );

        int itemsInThisGroup = (g == numGroups - 1) ? (totalNodes - created) : itemsPerGroup;
        group.children.reserve(itemsInThisGroup);

        for (int i = 0; i < itemsInThisGroup; ++i) {
            ++created;
            std::vector<int> itemRId = {1, g + 1, i + 1};
            std::string type = (created % 3 == 0) ? "Button" : ((created % 3 == 1) ? "Edit" : "Text");
            std::string name = std::format("Element_{}", created);
            group.children.push_back(createMockElement(
                name, type, std::format("autoId_{}", created),
                {10, 10, 100, 50}, true, true, std::move(itemRId)
            ));
        }

        root.children.push_back(std::move(group));
    }

    return root;
}
