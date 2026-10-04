#include <gtest/gtest.h>
#include "security/PermissionSystem.h"
#include "tools/FileTools.h"
#include <filesystem>
#include <tlhelp32.h>

class PermissionSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_tempDir = std::filesystem::temp_directory_path() / "winbot_perm_test";
        std::filesystem::create_directories(m_tempDir);
        m_prevMcpMode = g_mcpMode;
        g_mcpMode = false;
    }

    void TearDown() override {
        g_mcpMode = m_prevMcpMode;
        std::error_code ec;
        std::filesystem::remove_all(m_tempDir, ec);
    }

    std::filesystem::path m_tempDir; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
    bool m_prevMcpMode{false}; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
};

TEST_F(PermissionSystemTest, CheckPath_BlocksSubpathsOfBlockedDirectories) {
    PermissionSystem::Config cfg;
    cfg.blockedPaths = {"C:\\Windows\\System32"};
    cfg.allowedPaths = {"C:\\Windows"};
    PermissionSystem perms{cfg};

    auto res = perms.checkPath(R"(C:\Windows\System32\drivers\etc\hosts)");
    ASSERT_FALSE(res.has_value());
    EXPECT_TRUE(res.error().contains("Access denied"));
}

TEST_F(PermissionSystemTest, CheckPath_PreventsSiblingPrefixBypass) {
    auto allowedDir = m_tempDir / "work";
    auto siblingDir = m_tempDir / "work_sibling";
    std::filesystem::create_directories(allowedDir);
    std::filesystem::create_directories(siblingDir);

    PermissionSystem::Config cfg;
    cfg.allowedPaths = {allowedDir.string()};
    g_mcpMode = true; // In MCP mode, unallowed paths are strictly denied
    PermissionSystem perms{cfg};

    // Subpath within allowed must succeed
    auto okRes = perms.checkPath((allowedDir / "sub" / "file.txt").string());
    EXPECT_TRUE(okRes.has_value()) << (okRes ? "" : okRes.error());

    // Sibling directory starting with the same prefix must be denied!
    auto bypassRes = perms.checkPath((siblingDir / "secret.txt").string());
    EXPECT_FALSE(bypassRes.has_value());
    EXPECT_TRUE(bypassRes.error().contains("Access denied"));
}

TEST_F(PermissionSystemTest, CheckPath_NormalizesSlashesAndCase) {
    auto allowedDir = m_tempDir / "WorkDir";
    std::filesystem::create_directories(allowedDir);

    // Forward slashes in config, backslashes in query
    std::string forwardAllowed = allowedDir.string();
    std::ranges::replace(forwardAllowed, '\\', '/');

    PermissionSystem::Config cfg;
    cfg.allowedPaths = {forwardAllowed};
    g_mcpMode = true;
    PermissionSystem perms{cfg};

    std::string mixedQuery = (allowedDir / "SubFolder\\File.txt").string();
    auto res = perms.checkPath(mixedQuery);
    EXPECT_TRUE(res.has_value()) << (res ? "" : res.error());
}

TEST_F(PermissionSystemTest, CheckPath_ExpandsEnvironmentVariablesInRawPath) {
    PermissionSystem::Config cfg;
    cfg.allowedPaths = {"%TEMP%"};
    g_mcpMode = true;
    PermissionSystem perms{cfg};

    auto res = perms.checkPath("%TEMP%\\winbot_env_test.txt");
    EXPECT_TRUE(res.has_value()) << (res ? "" : res.error());
}

TEST_F(PermissionSystemTest, CheckPath_DeniesOutsideAllowedInMcpMode) {
    PermissionSystem::Config cfg;
    cfg.allowedPaths = {m_tempDir.string()};
    g_mcpMode = true;
    PermissionSystem perms{cfg};

    auto res = perms.checkPath("C:\\Windows\\notepad.exe");
    EXPECT_FALSE(res.has_value());
    EXPECT_TRUE(res.error().contains("Access denied"));
    EXPECT_TRUE(res.error().contains("MCP non-interactive mode"));
}

TEST_F(PermissionSystemTest, CheckProcess_BlocksNumericPidOfProtectedProcess) {
    PermissionSystem::Config cfg;
    cfg.blockedProcesses = {"csrss.exe", "winlogon.exe", "services.exe", "lsass.exe"};
    cfg.confirmProcessKill = false;
    PermissionSystem perms{cfg};

    // Find PID of a running protected process
    DWORD protectedPid = 0;
    std::string protectedName;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (::Process32FirstW(snap, &entry) != FALSE) {
            bool hasMore = true;
            while (hasMore) {
                std::string name = wide_to_utf8(std::wstring_view(static_cast<const wchar_t*>(entry.szExeFile)));
                to_lower_inplace(name);
                for (const auto& blocked : cfg.blockedProcesses) {
                    if (name == blocked) {
                        protectedPid = entry.th32ProcessID;
                        protectedName = name;
                        break;
                    }
                }
                if (protectedPid != 0) {
                    break;
                }
                hasMore = (::Process32NextW(snap, &entry) != FALSE);
            }
        }
        ::CloseHandle(snap);
    }

    if (protectedPid != 0) {
        // Passing numeric PID of protected process must be blocked!
        auto res = perms.checkProcess(std::to_string(protectedPid));
        EXPECT_FALSE(res.has_value());
        EXPECT_TRUE(res.error().contains("Cannot kill protected process"));
    }

    // Critical system PIDs 0 and 4 must always be blocked
    EXPECT_FALSE(perms.checkProcess("0").has_value());
    EXPECT_FALSE(perms.checkProcess("4").has_value());
}

TEST_F(PermissionSystemTest, CheckProcess_BlocksWithoutExtension) {
    PermissionSystem::Config cfg;
    cfg.blockedProcesses = {"winlogon.exe", "csrss.exe"};
    cfg.confirmProcessKill = false;
    PermissionSystem perms{cfg};

    EXPECT_FALSE(perms.checkProcess("winlogon").has_value());
    EXPECT_FALSE(perms.checkProcess("CSRSS").has_value());
    EXPECT_TRUE(perms.checkProcess("winlogon").error().contains("Cannot kill protected process"));
}

TEST_F(PermissionSystemTest, CheckShellCommand_RejectsDangerousInMcpMode) {
    PermissionSystem::Config cfg;
    cfg.confirmShellCommands = false;
    g_mcpMode = true;
    PermissionSystem perms{cfg};

    EXPECT_FALSE(perms.checkShellCommand("format C: /q").has_value());
    EXPECT_FALSE(perms.checkShellCommand("rm -rf /").has_value());
    EXPECT_FALSE(perms.checkShellCommand("del /f /s /q test.txt").has_value());
    EXPECT_FALSE(perms.checkShellCommand("rmdir /s /q C:\\data").has_value());
    EXPECT_FALSE(perms.checkShellCommand("bcdedit /set {default} bootstatuspolicy").has_value());
    EXPECT_FALSE(perms.checkShellCommand("Remove-Item -Recurse -Force C:\\temp").has_value());
    EXPECT_FALSE(perms.checkShellCommand("powershell -EncodedCommand aQB4").has_value());

    // Safe command should succeed
    auto safe = perms.checkShellCommand("echo hello world");
    EXPECT_TRUE(safe.has_value());
}

TEST_F(PermissionSystemTest, CheckFileDelete_HonorsConfirmSetting) {
    PermissionSystem::Config cfg;
    cfg.allowedPaths = {m_tempDir.string()};
    cfg.confirmFileDelete = false;
    g_mcpMode = true;
    PermissionSystem perms{cfg};

    // When confirmFileDelete is false and path is allowed, delete should be approved even in MCP mode
    auto res = perms.checkFileDelete((m_tempDir / "sample.txt").string());
    EXPECT_TRUE(res.has_value());

    // When confirmFileDelete is true in MCP mode, interactive confirm cannot happen so it is denied
    PermissionSystem::Config cfgStrict = cfg;
    cfgStrict.confirmFileDelete = true;
    PermissionSystem permsStrict{cfgStrict};
    auto deniedRes = permsStrict.checkFileDelete((m_tempDir / "sample.txt").string());
    EXPECT_FALSE(deniedRes.has_value());
}

TEST_F(PermissionSystemTest, CopyFileTool_GuardsSourceAndDestination) {
    PermissionSystem::Config cfg;
    cfg.blockedPaths = {"C:\\Windows\\System32"};
    cfg.allowedPaths = {m_tempDir.string()};
    g_mcpMode = true;
    PermissionSystem perms{cfg};

    CopyFileTool tool{perms};

    // Source in blocked directory
    auto res1 = tool.execute(json{
        {"src", R"(C:\Windows\System32\kernel32.dll)"},
        {"dst", (m_tempDir / "kernel32.dll").string()}
    });
    EXPECT_FALSE(res1.has_value());
    EXPECT_TRUE(res1.error().contains("Access denied"));

    // Destination in blocked directory
    auto res2 = tool.execute(json{
        {"src", (m_tempDir / "test.txt").string()},
        {"dst", R"(C:\Windows\System32\test.txt)"}
    });
    EXPECT_FALSE(res2.has_value());
    EXPECT_TRUE(res2.error().contains("Access denied"));
}

TEST_F(PermissionSystemTest, ListDirectoryTool_GuardsPath) {
    PermissionSystem::Config cfg;
    cfg.blockedPaths = {"C:\\Windows\\System32"};
    cfg.allowedPaths = {m_tempDir.string()};
    g_mcpMode = true;
    PermissionSystem perms{cfg};

    ListDirectoryTool tool{perms};

    auto res = tool.execute(json{{"path", "C:\\Windows\\System32"}});
    EXPECT_FALSE(res.has_value());
    EXPECT_TRUE(res.error().contains("Access denied"));
}
