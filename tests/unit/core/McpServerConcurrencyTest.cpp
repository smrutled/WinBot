#include <gtest/gtest.h>
#include "Common.h"
#include "core/ITool.h"
#include "core/ToolRegistry.h"
#include "core/McpServer.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

// ── Test Tools ───────────────────────────────────────────────────────────────

class FastTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "fast_tool"; }
    [[nodiscard]] std::string description() const override { return "Returns quickly"; }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}};
    }
    [[nodiscard]] ToolResult execute(const json& /*args*/) override {
        return ok("fast_done");
    }
};

class SlowCancelableTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "slow_cancelable"; }
    [[nodiscard]] std::string description() const override { return "Sleeps unless cancelled"; }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}};
    }
    [[nodiscard]] ToolResult execute(const json& args) override {
        return execute(args, std::stop_token{});
    }
    [[nodiscard]] ToolResult execute(const json& /*args*/, const std::stop_token& stopToken) override {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200)) {
            if (stopToken.stop_requested()) {
                return err("cancelled_internally");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return ok("slow_done");
    }
};

// ── Tests ────────────────────────────────────────────────────────────────────

TEST(McpServerConcurrencyTest, ParallelExecutionOrder) {
    ToolRegistry registry;
    registry.registerTool(std::make_unique<SlowCancelableTool>());
    registry.registerTool(std::make_unique<FastTool>());

    std::stringstream in;
    std::stringstream out;

    // Send initialize
    in << json({
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {{"protocolVersion", "2024-11-05"}}}
    }).dump() << "\n";

    // Send slow call first (id 2)
    in << json({
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/call"},
        {"params", {{"name", "slow_cancelable"}, {"arguments", json::object()}}}
    }).dump() << "\n";

    // Send fast call second (id 3)
    in << json({
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "tools/call"},
        {"params", {{"name", "fast_tool"}, {"arguments", json::object()}}}
    }).dump() << "\n";

    McpServer::Config cfg{
        .serverName = "TestWinBot",
        .serverVersion = "1.0",
        .actionDelayMs = 0,
        .workerThreads = 4
    };

    McpServer server(cfg, registry, in, out);
    server.run();

    // Parse all responses line by line
    std::string line;
    std::vector<json> responses;
    while (std::getline(out, line)) {
        if (!line.empty() && line.find_first_not_of(" \t\r\n") != std::string::npos) {
            responses.push_back(json::parse(line));
        }
    }

    ASSERT_GE(responses.size(), 3U);

    // Response 0 should be initialize (id 1)
    EXPECT_EQ(responses.at(0).at("id"), 1);

    // Response 1 should be fast_tool (id 3) because it finishes before slow_cancelable (id 2)
    EXPECT_EQ(responses.at(1).at("id"), 3);
    EXPECT_EQ(responses.at(1).at("result").at("content").at(0).at("text"), "fast_done");

    // Response 2 should be slow_cancelable (id 2)
    EXPECT_EQ(responses.at(2).at("id"), 2);
    EXPECT_EQ(responses.at(2).at("result").at("content").at(0).at("text"), "slow_done");
}

TEST(McpServerConcurrencyTest, CancellationWithNotification) {
    ToolRegistry registry;
    registry.registerTool(std::make_unique<SlowCancelableTool>());

    std::stringstream in;
    std::stringstream out;

    // Send initialize
    in << json({
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {{"protocolVersion", "2024-11-05"}}}
    }).dump() << "\n";

    // Send slow call with id 100
    in << json({
        {"jsonrpc", "2.0"},
        {"id", 100},
        {"method", "tools/call"},
        {"params", {{"name", "slow_cancelable"}, {"arguments", json::object()}}}
    }).dump() << "\n";

    // Immediately send cancellation notification for request 100
    in << json({
        {"jsonrpc", "2.0"},
        {"method", "notifications/cancelled"},
        {"params", {{"requestId", 100}}}
    }).dump() << "\n";

    McpServer::Config cfg{
        .serverName = "TestWinBot",
        .serverVersion = "1.0",
        .actionDelayMs = 0,
        .workerThreads = 2
    };

    McpServer server(cfg, registry, in, out);
    server.run();

    std::string line;
    std::vector<json> responses;
    while (std::getline(out, line)) {
        if (!line.empty() && line.find_first_not_of(" \t\r\n") != std::string::npos) {
            responses.push_back(json::parse(line));
        }
    }

    ASSERT_EQ(responses.size(), 2U);
    EXPECT_EQ(responses.at(0).at("id"), 1);

    // Cancelled response should return error with code -32800 (kRequestCancelled)
    EXPECT_EQ(responses.at(1).at("id"), 100);
    ASSERT_TRUE(responses.at(1).contains("error"));
    EXPECT_EQ(responses.at(1).at("error").at("code"), -32800);
}

TEST(McpServerConcurrencyTest, HighConcurrencyStreamIntegrity) {
    ToolRegistry registry;
    registry.registerTool(std::make_unique<FastTool>());

    std::stringstream in;
    std::stringstream out;

    // Send initialize
    in << json({
        {"jsonrpc", "2.0"},
        {"id", 0},
        {"method", "initialize"},
        {"params", {{"protocolVersion", "2024-11-05"}}}
    }).dump() << "\n";

    constexpr int kNumRequests = 30;
    for (int i = 1; i <= kNumRequests; ++i) {
        in << json({
            {"jsonrpc", "2.0"},
            {"id", i},
            {"method", "tools/call"},
            {"params", {{"name", "fast_tool"}, {"arguments", json::object()}}}
        }).dump() << "\n";
    }

    McpServer::Config cfg{
        .serverName = "TestWinBot",
        .serverVersion = "1.0",
        .actionDelayMs = 0,
        .workerThreads = 8
    };

    McpServer server(cfg, registry, in, out);
    server.run();

    std::string line;
    std::vector<json> responses;
    while (std::getline(out, line)) {
        if (!line.empty() && line.find_first_not_of(" \t\r\n") != std::string::npos) {
            // Must parse cleanly as JSON without corruption or tearing
            json parsed = json::parse(line);
            responses.push_back(parsed);
        }
    }

    EXPECT_EQ(responses.size(), static_cast<size_t>(kNumRequests + 1));
}

TEST(McpServerTest, EmitsToolsListChangedNotification) {
    ToolRegistry registry;
    registry.registerTool(std::make_unique<FastTool>());

    std::stringstream in;
    std::stringstream out;

    in << json({
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {{"protocolVersion", "2024-11-05"}}}
    }).dump() << "\n";

    McpServer::Config cfg{
        .serverName = "TestWinBot",
        .serverVersion = "1.0",
        .actionDelayMs = 0,
        .workerThreads = 2
    };

    McpServer server(cfg, registry, in, out);

    std::jthread serverThread([&]() {
        server.run();
    });

    // Wait until initialized
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Mutate registry dynamically while server is active
    registry.registerTool(std::make_unique<SlowCancelableTool>());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.stop();
    if (serverThread.joinable()) {
        serverThread.join();
    }

    std::string line;
    bool foundListChangedCapability = false;
    bool receivedListChangedNotification = false;

    while (std::getline(out, line)) {
        if (!line.empty() && line.find_first_not_of(" \t\r\n") != std::string::npos) {
            json msg = json::parse(line);
            if (msg.contains("result") && msg.at("result").contains("capabilities")) {
                const auto& caps = msg.at("result").at("capabilities");
                if (caps.contains("tools") && caps.at("tools").contains("listChanged") &&
                    caps.at("tools").at("listChanged") == true) {
                    foundListChangedCapability = true;
                }
            }
            if (msg.value("method", "") == "notifications/tools/list_changed") {
                receivedListChangedNotification = true;
            }
        }
    }

    EXPECT_TRUE(foundListChangedCapability);
    EXPECT_TRUE(receivedListChangedNotification);
}

