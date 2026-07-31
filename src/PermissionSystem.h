#ifndef WINBOT_PERMISSIONSYSTEM_H
#define WINBOT_PERMISSIONSYSTEM_H
#include "Common.h"
#include <vector>
#include <regex>
#include <unordered_set>

// ── PermissionSystem ─────────────────────────────────────────────────────────
// Layered safety checks run before any potentially destructive tool call.
// All paths come from config.json at startup.
class PermissionSystem {
public:
    struct Config {
        std::vector<std::string> allowedPaths;
        std::vector<std::string> blockedPaths;
        std::vector<std::string> blockedProcesses;
        std::vector<std::string> dangerousCmdPatterns;
        bool confirmShellCommands{ true };
        bool confirmFileDelete{ true };
        bool confirmProcessKill{ true };

        // Tools listed here are excluded from the registry at startup.
        // Supports individual tool names or group aliases (e.g. "shell", "browser").
        std::unordered_set<std::string> disabledTools;
    };

    explicit PermissionSystem(Config cfg);

    // Returns ok("") if path is within an allowed root and not in a blocked root.
    [[nodiscard]] ToolResult checkPath(std::string_view path) const;

    // Returns ok("") if the process name is not in the protected list.
    [[nodiscard]] ToolResult checkProcess(std::string_view name) const;

    // Returns ok("") if the shell command contains no dangerous patterns.
    // If it matches a pattern AND confirmShellCommands is true, prompts user.
    [[nodiscard]] ToolResult checkShellCommand(std::string_view cmd) const;

    // Returns true if the named tool has NOT been disabled in config.
    // Used at registration time to skip disabled tools entirely.
    [[nodiscard]] bool isToolEnabled(std::string_view name) const;

    // Prompts the user via console for explicit yes/no confirmation.
    // Returns ok("") if user confirms, err("denied") if not.
    [[nodiscard]] static ToolResult promptUser(std::string_view prompt);

private:
    Config m_cfg;
    std::vector<std::regex> m_dangerousPatterns;

    // Expand %ENVVAR% placeholders in paths.
    [[nodiscard]] static std::string expandEnv(std::string_view path);
};

#endif // WINBOT_PERMISSIONSYSTEM_H
