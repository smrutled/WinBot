#include <gtest/gtest.h>
#include "tools/ShellTools.h"

namespace {

TEST(ShellToolsTest, RunCommandNegativeTimeoutClamped) {
    // Negative timeout should be clamped to default (30000ms) rather than underflowing DWORD
    auto result = tools::runCommand("echo clamped_timeout_test", "cmd", -1);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->find("clamped_timeout_test") != std::string::npos);
}

TEST(ShellToolsTest, RunCommandEchoStdout) {
    auto result = tools::runCommand("echo HelloWinBot", "cmd", 5000);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->find("HelloWinBot") != std::string::npos);
}

TEST(ShellToolsTest, RunCommandNonZeroExitReportsCodeAndOutput) {
    auto result = tools::runCommand("exit 7", "cmd", 5000);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->find("Exit 7") != std::string::npos);
}

} // namespace
