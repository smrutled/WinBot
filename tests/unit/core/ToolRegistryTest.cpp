#include <gtest/gtest.h>
#include "core/ToolRegistry.h"
#include <stdexcept>

namespace {

TEST(ToolRegistryTest, DispatchCatchesStdException) {
    ToolRegistry registry;
    registry.registerTool(std::make_unique<LambdaTool>(
        "failing_tool",
        "Throws std::runtime_error",
        json::object(),
        [](const json&) -> ToolResult {
            throw std::runtime_error("Simulated hardware/subsystem fault");
        }
    ));

    json call = {
        {"tool", "failing_tool"},
        {"args", json::object()}
    };

    auto result = registry.dispatch(call);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().contains("failing_tool"));
    EXPECT_TRUE(result.error().contains("Simulated hardware/subsystem fault"));
}

TEST(ToolRegistryTest, DispatchCatchesNonStdException) {
    ToolRegistry registry;
    registry.registerTool(std::make_unique<LambdaTool>(
        "non_std_thrower",
        "Throws an int",
        json::object(),
        [](const json&) -> ToolResult {
            throw 42;
        }
    ));

    json call = {
        {"tool", "non_std_thrower"},
        {"args", json::object()}
    };

    auto result = registry.dispatch(call);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().contains("non_std_thrower"));
    EXPECT_TRUE(result.error().contains("unknown exception"));
}

TEST(ToolRegistryTest, DispatchMissingToolField) {
    ToolRegistry registry;
    json call = {{"args", json::object()}};

    auto result = registry.dispatch(call);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().contains("missing 'tool' field"));
}

TEST(ToolRegistryTest, DispatchUnknownTool) {
    ToolRegistry registry;
    json call = {
        {"tool", "ghost_tool"},
        {"args", json::object()}
    };

    auto result = registry.dispatch(call);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().contains("Unknown tool"));
}

TEST(ToolRegistryTest, DispatchRawMalformedJson) {
    ToolRegistry registry;
    auto result = registry.dispatchRaw("{ this is not valid json }");
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().contains("JSON parse error"));
}

} // namespace
