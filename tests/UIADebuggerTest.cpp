#include <gtest/gtest.h>
#include "TestHelpers.h"
#include "UIADebugger.h"
#include "UIAutomationScanner.h"

TEST(UIADebuggerTest, StripQuotes) {
    EXPECT_EQ(UIADebugger::stripQuotes(""), "");
    EXPECT_EQ(UIADebugger::stripQuotes("   "), "");
    EXPECT_EQ(UIADebugger::stripQuotes("Button"), "Button");
    EXPECT_EQ(UIADebugger::stripQuotes("  Button  "), "Button");
    EXPECT_EQ(UIADebugger::stripQuotes("\"Button\""), "Button");
    EXPECT_EQ(UIADebugger::stripQuotes("  \"Button\"  "), "Button");
    // Escaped internal quotes
    EXPECT_EQ(UIADebugger::stripQuotes("\"Button \\\"OK\\\"\""), "Button \"OK\"");
}

TEST(UIADebuggerTest, ParseCommandArgs_SingleArg) {
    auto args = UIADebugger::parseCommandArgs("(\"Calculator\")");
    EXPECT_EQ(args.name, "Calculator");
    EXPECT_EQ(args.type, "");
    EXPECT_EQ(args.timeoutMs, 0);

    // Without outer parens
    auto args2 = UIADebugger::parseCommandArgs("\"Calculator\"");
    EXPECT_EQ(args2.name, "Calculator");
}

TEST(UIADebuggerTest, ParseCommandArgs_WithTimeout) {
    auto args = UIADebugger::parseCommandArgs("(\"Calculator\", 5000)");
    EXPECT_EQ(args.name, "Calculator");
    EXPECT_EQ(args.type, "");
    EXPECT_EQ(args.timeoutMs, 5000);
}

TEST(UIADebuggerTest, ParseCommandArgs_WithTypeAndName) {
    auto args = UIADebugger::parseCommandArgs("(\"Button\", \"Submit\")");
    EXPECT_EQ(args.type, "Button");
    EXPECT_EQ(args.name, "Submit");
    EXPECT_EQ(args.timeoutMs, 0);
}

TEST(UIADebuggerTest, ParseCommandArgs_WithTypeAndNameAndTimeout) {
    auto args = UIADebugger::parseCommandArgs("(\"Button\", \"Submit\", 3500)");
    EXPECT_EQ(args.type, "Button");
    EXPECT_EQ(args.name, "Submit");
    EXPECT_EQ(args.timeoutMs, 3500);
}

TEST(UIADebuggerTest, ParseCommandArgs_EscapedQuotes) {
    auto args = UIADebugger::parseCommandArgs("(\"Button \\\"OK\\\"\", 2000)");
    EXPECT_EQ(args.name, "Button \"OK\"");
    EXPECT_EQ(args.timeoutMs, 2000);
}

TEST(UIADebuggerTest, DanglingDot_Leading) {
    UIAutomationScanner scanner;
    UIADebugger debugger(scanner);

    // Leading or double dots must be rejected gracefully
    EXPECT_FALSE(debugger.execSegment("..Click()"));
    EXPECT_FALSE(debugger.execSegment(".Click()"));
}

TEST(UIADebuggerTest, DanglingDot_Trailing) {
    UIAutomationScanner scanner;
    UIADebugger debugger(scanner);

    // Trailing dot must be rejected gracefully
    EXPECT_FALSE(debugger.execSegment("Select(\"App\")."));
}

TEST(UIADebuggerTest, DotChain_StringWithDots) {
    UIAutomationScanner scanner;
    UIADebugger debugger(scanner);

    // Inject a mock variable $calc
    UIElement calcTree = createCalculatorTree();
    debugger.setVar("$calc", UIHandle(calcTree, &scanner));

    // Dot-chaining on $calc where arguments might contain dots
    bool ok = debugger.execSegment("$calc.Select(\"Plus\")");
    EXPECT_TRUE(ok);

    const auto& last = debugger.getLastResult();
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->element().name, "Plus");
}

TEST(UIADebuggerTest, VariableAssignmentAndRetrieval) {
    UIAutomationScanner scanner;
    UIADebugger debugger(scanner);

    UIElement calcTree = createCalculatorTree();
    debugger.setVar("$calc", UIHandle(calcTree, &scanner));

    // Assign $btn = $calc.Select("Plus")
    bool ok = debugger.execSegment("$btn = $calc.Select(\"Plus\")");
    EXPECT_TRUE(ok);

    const auto& vars = debugger.getVars();
    ASSERT_NE(vars.find("$btn"), vars.end());
    EXPECT_EQ(vars.at("$btn").element().name, "Plus");
    EXPECT_EQ(vars.at("$btn").element().controlType, "Button");
}

TEST(UIADebuggerTest, SessionStateClear) {
    UIAutomationScanner scanner;
    UIADebugger debugger(scanner);

    UIElement calcTree = createCalculatorTree();
    debugger.setVar("$calc", UIHandle(calcTree, &scanner));
    debugger.setVar("$btn", UIHandle(calcTree.children[1].children[0], &scanner));

    EXPECT_EQ(debugger.getVars().size(), 2);

    // Clear single variable
    debugger.execSegment("Clear(\"$btn\")");
    EXPECT_EQ(debugger.getVars().size(), 1);
    EXPECT_EQ(debugger.getVars().count("$btn"), 0);
    EXPECT_EQ(debugger.getVars().count("$calc"), 1);

    // Clear all variables
    debugger.execSegment("Clear(\"All\")");
    EXPECT_EQ(debugger.getVars().size(), 0);
    EXPECT_FALSE(debugger.getLastResult().has_value());
}

TEST(UIADebuggerTest, SemicolonDelimitedExecution) {
    UIAutomationScanner scanner;
    UIADebugger debugger(scanner);

    UIElement calcTree = createCalculatorTree();
    debugger.setVar("$calc", UIHandle(calcTree, &scanner));

    // Multi-statement line
    bool ok = debugger.execute("$one = $calc.Select(\"One\") ; $two = $calc.Select(\"Two\") ; Vars()");
    EXPECT_TRUE(ok);

    EXPECT_EQ(debugger.getVars().count("$one"), 1);
    EXPECT_EQ(debugger.getVars().count("$two"), 1);
    EXPECT_EQ(debugger.getVars().at("$one").element().name, "One");
    EXPECT_EQ(debugger.getVars().at("$two").element().name, "Two");
}
