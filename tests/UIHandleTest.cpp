#include <gtest/gtest.h>
#include "TestHelpers.h"
#include "UIHandle.h"
#include <stdexcept>

TEST(UIHandleTest, ScopedSearchIsolation) {
    // Desktop contains both Calculator and OneDrive.
    // In global unscoped search, searching for "One" could hit "OneDrive" or "One Sync Status".
    // But scoped to Calculator, select("One") MUST only hit Calculator's "One" button!
    UIElement calcTree = createCalculatorTree();
    UIHandle calcHandle(calcTree, nullptr);

    // Scoped select inside Calculator
    UIHandle buttonOne = calcHandle.select("One");
    EXPECT_EQ(buttonOne.element().name, "One");
    EXPECT_EQ(buttonOne.element().controlType, "Button");
    EXPECT_EQ(buttonOne.element().automationId, "num1Button");

    // "OneDrive" does NOT exist inside Calculator's subtree -> must throw
    EXPECT_THROW((void)calcHandle.select("OneDrive"), std::runtime_error);
    EXPECT_THROW((void)calcHandle.select("One Sync Status"), std::runtime_error);
}

TEST(UIHandleTest, CachedSubtreeInstantLookup) {
    UIElement calcTree = createCalculatorTree();
    UIHandle calcHandle(calcTree, nullptr);

    // Multiple chained selects within cached tree
    UIHandle plusBtn = calcHandle.select("Plus");
    EXPECT_EQ(plusBtn.element().name, "Plus");

    UIHandle equalsBtn = calcHandle.select("Equals");
    EXPECT_EQ(equalsBtn.element().name, "Equals");
}

TEST(UIHandleTest, NonExistentElementThrows) {
    UIElement calcTree = createCalculatorTree();
    UIHandle calcHandle(calcTree, nullptr);

    try {
        (void)calcHandle.select("SquareRoot", 0);
        FAIL() << "Expected std::runtime_error was not thrown";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("SquareRoot"), std::string::npos);
        EXPECT_NE(msg.find("element not found"), std::string::npos);
    }
}

TEST(UIHandleTest, WaitChaining) {
    UIElement calcTree = createCalculatorTree();
    UIHandle calcHandle(calcTree, nullptr);

    // UIHandle::wait returns UIHandle& for chaining
    UIHandle& ref = calcHandle.wait(1);
    EXPECT_EQ(&ref, &calcHandle);
}

TEST(UIHandleTest, ElementAccessor) {
    UIElement calcTree = createCalculatorTree();
    UIHandle calcHandle(calcTree, nullptr);

    const UIElement& snapshot = calcHandle.element();
    EXPECT_EQ(snapshot.name, "Calculator");
    EXPECT_EQ(snapshot.controlType, "Window");
    EXPECT_EQ(snapshot.children.size(), 2);
}

TEST(UIHandleTest, StaleHandle_ParentFailureWithoutScanner) {
    UIElement calcTree = createCalculatorTree();
    UIHandle calcHandle(calcTree, nullptr); // Null scanner

    // Calling parent() or window() without a valid scanner or valid HWND must throw std::runtime_error
    EXPECT_THROW((void)calcHandle.parent(), std::runtime_error);
    EXPECT_THROW((void)calcHandle.window(), std::runtime_error);
}
