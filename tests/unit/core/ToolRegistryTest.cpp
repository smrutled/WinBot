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
    EXPECT_TRUE(result.error().find("failing_tool") != std::string::npos);
    EXPECT_TRUE(result.error().find("Simulated hardware/subsystem fault") != std::string::npos);
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
    EXPECT_TRUE(result.error().find("non_std_thrower") != std::string::npos);
    EXPECT_TRUE(result.error().find("unknown exception") != std::string::npos);
}

TEST(ToolRegistryTest, DispatchMissingToolField) {
    ToolRegistry registry;
    json call = {{"args", json::object()}};

    auto result = registry.dispatch(call);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().find("missing 'tool' field") != std::string::npos);
}

TEST(ToolRegistryTest, DispatchUnknownTool) {
    ToolRegistry registry;
    json call = {
        {"tool", "ghost_tool"},
        {"args", json::object()}
    };

    auto result = registry.dispatch(call);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().find("Unknown tool") != std::string::npos);
}

TEST(ToolRegistryTest, DispatchRawMalformedJson) {
    ToolRegistry registry;
    auto result = registry.dispatchRaw("{ this is not valid json }");
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().find("JSON parse error") != std::string::npos);
}

} // namespace
