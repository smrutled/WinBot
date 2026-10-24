#include <gtest/gtest.h>
#include "core/ToolRegistry.h"
#include <atomic>
#include <format>
#include <stdexcept>
#include <thread>

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

TEST(ToolRegistryTest, UnregisterDuringExecutionKeepsToolAliveAndNonBlocking) {
    ToolRegistry registry;
    std::atomic<bool> toolRunning{false};
    std::atomic<bool> unregisterDone{false};
    std::atomic<bool> proceedExecution{false};

    registry.registerTool(std::make_unique<LambdaTool>(
        "async_tool",
        "Tool that stays alive during unregister",
        json::object(),
        [&](const json&) -> ToolResult {
            toolRunning = true;
            while (!proceedExecution.load()) {
                std::this_thread::yield();
            }
            return ok("async_success");
        }
    ));

    std::thread execThread([&]() {
        auto res = registry.dispatch("async_tool", json::object());
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ(res.value(), "async_success");
    });

    while (!toolRunning.load()) {
        std::this_thread::yield();
    }

    // While the tool is actively executing on execThread, unregister it on this thread.
    // B1 ensures:
    // 1) unregisterTool does NOT block waiting for execute() to finish.
    // 2) The tool is successfully removed from the registry index.
    // 3) The tool instance remains alive via shared_ptr and finishes without use-after-free.
    EXPECT_TRUE(registry.unregisterTool("async_tool"));
    EXPECT_FALSE(registry.hasTool("async_tool"));
    unregisterDone = true;

    // Release tool to complete its execution
    proceedExecution = true;

    execThread.join();
    EXPECT_TRUE(unregisterDone.load());
}

TEST(ToolRegistryTest, SnapshotRemainsValidUnderConcurrentMutations) {
    ToolRegistry registry;
    for (int i = 0; i < 5; ++i) {
        registry.registerTool(std::make_unique<LambdaTool>(
            std::format("tool_{}", i),
            "test tool",
            json::object(),
            [](const json&) -> ToolResult { return ok("ok"); }
        ));
    }

    // Take snapshot
    auto snapshot = registry.snapshot();
    EXPECT_EQ(snapshot.size(), 5);

    // Concurrently mutate registry: unregister all existing tools and register new ones
    std::thread mutator([&]() {
        for (int i = 0; i < 5; ++i) {
            registry.unregisterTool(std::format("tool_{}", i));
        }
        for (int i = 10; i < 15; ++i) {
            registry.registerTool(std::make_unique<LambdaTool>(
                std::format("tool_{}", i),
                "new tool",
                json::object(),
                [](const json&) -> ToolResult { return ok("new_ok"); }
            ));
        }
    });

    // The snapshot captured earlier remains completely intact and valid
    for (const auto& tool : snapshot) {
        ASSERT_NE(tool, nullptr);
        EXPECT_FALSE(tool->name().empty());
        EXPECT_EQ(tool->description(), "test tool");
    }

    mutator.join();

    // Registry now has 5 new tools, but snapshot was isolated
    auto newSnapshot = registry.snapshot();
    EXPECT_EQ(newSnapshot.size(), 5);
    EXPECT_TRUE(registry.hasTool("tool_10"));
    EXPECT_FALSE(registry.hasTool("tool_0"));
}

} // namespace

