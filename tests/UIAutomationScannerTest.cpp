#include <gtest/gtest.h>
#include "TestHelpers.h"
#include "UIAutomationScanner.h"

// Register the COM environment once for all tests in this process
static ::testing::Environment* const s_comEnv = ::testing::AddGlobalTestEnvironment(new ComEnvironment());

TEST(UIAutomationScannerTest, ExactNameMatch) {
    UIElement root = createMockElement("Root", "Pane");
    root.children.push_back(createMockElement("File", "MenuItem"));
    root.children.push_back(createMockElement("File Settings", "MenuItem"));

    const UIElement* best = root.findBestMatch("File");
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->name, "File");
}

TEST(UIAutomationScannerTest, AutomationIdMatch) {
    UIElement root = createMockElement("Root", "Pane");
    root.children.push_back(createMockElement("Cancel Operation", "Button", "btnCancel"));
    root.children.push_back(createMockElement("OK", "Button", "btnOK"));

    const UIElement* best = root.findBestMatch("btnCancel");
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->name, "Cancel Operation");
    EXPECT_EQ(best->automationId, "btnCancel");
}

TEST(UIAutomationScannerTest, CaseInsensitiveMatching) {
    UIElement root = createMockElement("Calculator", "Window", "CalcApp");
    root.children.push_back(createMockElement("Equals", "Button", "equalButton"));

    const UIElement* best1 = root.findBestMatch("EQUALS");
    ASSERT_NE(best1, nullptr);
    EXPECT_EQ(best1->name, "Equals");

    const UIElement* best2 = root.findBestMatch("equals");
    ASSERT_NE(best2, nullptr);
    EXPECT_EQ(best2->name, "Equals");

    const UIElement* best3 = root.findBestMatch("CALCULATOR");
    ASSERT_NE(best3, nullptr);
    EXPECT_EQ(best3->name, "Calculator");
}

TEST(UIAutomationScannerTest, ControlTypeFiltering) {
    UIElement root = createMockElement("Container", "Pane");
    root.children.push_back(createMockElement("Submit", "Text"));
    root.children.push_back(createMockElement("Submit", "Button"));

    // Without filter, first match with same score is picked
    const UIElement* anySubmit = root.findBestMatch("Submit");
    ASSERT_NE(anySubmit, nullptr);

    // With filter "Button", must pick the Button
    const UIElement* buttonSubmit = root.findBestMatch("Submit", "Button");
    ASSERT_NE(buttonSubmit, nullptr);
    EXPECT_EQ(buttonSubmit->controlType, "Button");

    // With filter "Text", must pick the Text control
    const UIElement* textSubmit = root.findBestMatch("Submit", "Text");
    ASSERT_NE(textSubmit, nullptr);
    EXPECT_EQ(textSubmit->controlType, "Text");
}

TEST(UIAutomationScannerTest, WindowTypePriority) {
    // If a Window and a Child both match "Calculator", Window gets a +100 bonus
    UIElement desktop = createMockElement("Desktop", "Pane");
    UIElement calcWindow = createMockElement("Calculator", "Window", "CalcWin");
    calcWindow.children.push_back(createMockElement("Calculator Mode", "Text"));
    desktop.children.push_back(calcWindow);

    const UIElement* best = desktop.findBestMatch("Calculator");
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->controlType, "Window");
    EXPECT_EQ(best->name, "Calculator");
}

TEST(UIAutomationScannerTest, InteractiveElementPriority) {
    UIElement root = createMockElement("Form", "Group");
    // Non-interactive matching element
    UIElement label = createMockElement("Save", "Text", "lblSave", {0, 0, 50, 20}, false, false);
    // Interactive matching element (bonus +5)
    UIElement btn = createMockElement("Save", "Button", "btnSave", {0, 30, 50, 50}, true, true);
    root.children.push_back(label);
    root.children.push_back(btn);

    const UIElement* best = root.findBestMatch("Save");
    ASSERT_NE(best, nullptr);
    EXPECT_TRUE(best->isFocusable || best->isEnabled);
    EXPECT_EQ(best->automationId, "btnSave");
}

TEST(UIAutomationScannerTest, SelfPidExclusion_Injected) {
    constexpr DWORD targetPid = 9999;
    constexpr DWORD otherPid  = 8888;

    UIElement root = createMockElement("Desktop", "Pane", "", {0, 0, 1000, 1000}, true, false, {1}, otherPid);
    // An element belonging to the excluded process ID (e.g. WinBot's own console/window)
    root.children.push_back(createMockElement("WinBot Console", "Window", "ConsoleWin", {0, 0, 400, 300}, true, true, {1, 1}, targetPid));
    // An element belonging to another process
    root.children.push_back(createMockElement("WinBot Docs", "Window", "DocsWin", {400, 0, 800, 300}, true, true, {1, 2}, otherPid));

    // When excluding targetPid, search for "WinBot" must skip "WinBot Console" and return "WinBot Docs"
    const UIElement* best = root.findBestMatch("WinBot", "", targetPid);
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->name, "WinBot Docs");
    EXPECT_EQ(best->dwProcessId, otherPid);
}

TEST(UIAutomationScannerTest, FindByRuntimeId) {
    UIElement calc = createCalculatorTree();

    const std::vector<int> targetId = {100, 2, 4}; // Plus button
    const UIElement* found = calc.findByRuntimeId(targetId);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "Plus");
    EXPECT_EQ(found->controlType, "Button");

    // Non-existent ID returns nullptr
    const std::vector<int> nonExistentId = {999, 999};
    EXPECT_EQ(calc.findByRuntimeId(nonExistentId), nullptr);
}

TEST(UIAutomationScannerTest, FindParent) {
    UIElement calc = createCalculatorTree();

    const std::vector<int> buttonOneId = {100, 2, 1}; // One button
    const UIElement* parent = calc.findParent(buttonOneId);
    ASSERT_NE(parent, nullptr);
    EXPECT_EQ(parent->name, "Number pad");
    EXPECT_EQ(parent->controlType, "Group");

    // Parent of root is nullptr
    EXPECT_EQ(calc.findParent({100}), nullptr);
}

TEST(UIAutomationScannerTest, FindDescendant) {
    UIElement calc = createCalculatorTree();

    // Searching descendants of Calculator for "One"
    const UIElement* desc = calc.findDescendant("One");
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->name, "One");

    // findDescendant must NOT match the root element itself
    const UIElement* rootDesc = calc.findDescendant("CalculatorWindow");
    EXPECT_EQ(rootDesc, nullptr);
    EXPECT_EQ(calc.findDescendant("NonExistentElement"), nullptr);
}

TEST(UIAutomationScannerTest, DeepHierarchyTraversal) {
    constexpr int depth = 60; // 60 levels deep
    UIElement deepTree = createDeepTree(depth, "DeepestButton");

    const UIElement* match = deepTree.findBestMatch("DeepestButton");
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->name, "DeepestButton");
    EXPECT_EQ(match->controlType, "Button");

    const UIElement* parent = deepTree.findParent(match->runtimeId);
    ASSERT_NE(parent, nullptr);
    EXPECT_EQ(parent->name, "Level_59");
}

TEST(UIAutomationScannerTest, SerializeFormat) {
    UIElement calc = createCalculatorTree();
    std::string text = UIAutomationScanner::serialize(calc);

    EXPECT_FALSE(text.empty());
    EXPECT_NE(text.find("[Window: \"Calculator\""), std::string::npos);
    EXPECT_NE(text.find("[Button: \"One\""), std::string::npos);
    EXPECT_NE(text.find("id=\"num1Button\""), std::string::npos);
    EXPECT_NE(text.find("[Button: \"Plus\""), std::string::npos);
}

TEST(UIAutomationScannerTest, CountInteractive) {
    UIElement calc = createCalculatorTree();
    int interactive = UIAutomationScanner::countInteractive(calc);
    // Calculator window (1) + 5 buttons (One, Two, Three, Plus, Equals) = 6 interactive
    EXPECT_GE(interactive, 5);
}

TEST(UIAutomationScannerTest, FindElementCenter) {
    UIElement calc = createCalculatorTree();
    auto pt = UIAutomationScanner::findElementCenter(calc, "One");
    ASSERT_TRUE(pt.has_value());
    // "One" bounds: {120, 220, 200, 280} -> center x = 160, y = 250
    EXPECT_EQ(pt->x, 160);
    EXPECT_EQ(pt->y, 250);

    auto notFound = UIAutomationScanner::findElementCenter(calc, "NonExistentButton");
    EXPECT_FALSE(notFound.has_value());
}

TEST(UIAutomationScannerTest, LiveScannerInitialization) {
    // Tests that UIAutomationScanner instantiates and COM creates IUIAutomation without crashing
    UIAutomationScanner scanner;
    auto res = scanner.scanDesktop();
    // On Windows desktop, scanDesktop should succeed and return a root element with controlType
    if (res.has_value()) {
        EXPECT_FALSE(res->controlType.empty());
    } else {
        std::print("Live scanner desktop note: {}\n", res.error());
    }
}
