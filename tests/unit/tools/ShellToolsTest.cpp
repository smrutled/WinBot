#include <gtest/gtest.h>
#include "tools/ShellTools.h"
#include "tools/WindowTools.h"

namespace {

TEST(ShellToolsTest, RunCommandNegativeTimeoutClamped) {
    // Negative timeout should be clamped to default (30000ms) rather than underflowing DWORD
    auto result = tools::runCommand("echo clamped_timeout_test", "cmd", -1);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->contains("clamped_timeout_test"));
}

TEST(ShellToolsTest, RunCommandEchoStdout) {
    auto result = tools::runCommand("echo HelloWinBot", "cmd", 5000);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->contains("HelloWinBot"));
}

TEST(ShellToolsTest, RunCommandNonZeroExitReportsCodeAndOutput) {
    auto result = tools::runCommand("exit 7", "cmd", 5000);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->contains("Exit 7"));
}

TEST(ShellToolsTest, RunCommandCancellationTerminatesProcess) {
    std::stop_source stopSrc;
    // Launch a command that would take 10 seconds unless cancelled
    std::jthread canceller([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        stopSrc.request_stop();
    });

    auto start = std::chrono::steady_clock::now();
    auto result = tools::runCommand("ping 127.0.0.1 -n 10", "cmd", 15000, stopSrc.get_token());
    auto elapsed = std::chrono::steady_clock::now() - start;

    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().contains("Command cancelled"));
    EXPECT_LT(elapsed, std::chrono::milliseconds(3000));
}

TEST(WindowToolsTest, KillProcessRejectsPartialNumericString) {
    // "4x" must not partially parse as PID 4; it should be treated as process name "4x"
    auto result = tools::killProcess("4x");
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().contains("Process '4x' not found"));
}

} // namespace

