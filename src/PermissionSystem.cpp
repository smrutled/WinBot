#include "PermissionSystem.h"
#include <iostream>
#include <print>
#include <algorithm>

PermissionSystem::PermissionSystem(Config cfg)
    : m_cfg(std::move(cfg))
{
    // Pre-compile dangerous command regex patterns (case-insensitive)
    for (const auto& pat : m_cfg.dangerousCmdPatterns) {
        try {
            m_dangerousPatterns.emplace_back(
                pat,
                std::regex_constants::icase | std::regex_constants::ECMAScript
            );
        } catch (const std::regex_error& e) {
            WINBOT_WARN("PermissionSystem: invalid regex '{}': {}", pat, e.what());
        }
    }
}

std::string PermissionSystem::expandEnv(std::string_view path) {
    std::string result(path);
    // Replace %VAR% patterns with environment variable values
    std::string out;
    out.reserve(result.size());
    size_t i = 0;
    while (i < result.size()) {
        if (result[i] == '%') {
            auto end = result.find('%', i + 1);
            if (end != std::string::npos) {
                std::string varName = result.substr(i + 1, end - i - 1);
                if (const char* val = std::getenv(varName.c_str())) {
                    out += val;
                } else {
                    out += result.substr(i, end - i + 1);
                }
                i = end + 1;
                continue;
            }
        }
        out += result[i++];
    }
    return out;
}

ToolResult PermissionSystem::checkPath(std::string_view rawPath) const {
    std::filesystem::path path = std::filesystem::weakly_canonical(
        utf8_to_wide(rawPath)
    );
    std::string pathStr = wide_to_utf8(path.wstring());
    to_lower_inplace(pathStr);

    // Check blocked paths first (higher priority)
    for (const auto& blocked : m_cfg.blockedPaths) {
        std::string expandedBlocked = expandEnv(blocked);
        to_lower_inplace(expandedBlocked);
        if (pathStr.starts_with(expandedBlocked)) {
            return err(std::format("Access denied — path '{}' is in a blocked directory", rawPath));
        }
    }

    // Check if within an allowed root
    for (const auto& allowed : m_cfg.allowedPaths) {
        std::string expandedAllowed = expandEnv(allowed);
        to_lower_inplace(expandedAllowed);
        if (pathStr.starts_with(expandedAllowed)) {
            return ok("");
        }
    }

    // Not in any allowed path — prompt user
    return promptUser(std::format(
        "WinBot wants to access '{}' which is outside configured allowed paths. Allow?", rawPath
    ));
}

ToolResult PermissionSystem::checkProcess(std::string_view name) const {
    std::string lower(name);
    to_lower_inplace(lower);

    for (const auto& blocked : m_cfg.blockedProcesses) {
        std::string blockedLower(blocked);
        to_lower_inplace(blockedLower);
        if (lower == blockedLower) {
            return err(std::format("Cannot kill protected process '{}'", name));
        }
    }
    if (m_cfg.confirmProcessKill) {
        return promptUser(std::format("WinBot wants to kill process '{}'. Allow?", name));
    }
    return ok("");
}

ToolResult PermissionSystem::checkShellCommand(std::string_view cmd) const {
    // Check for dangerous patterns
    bool isDangerous = std::ranges::any_of(m_dangerousPatterns,
        [&](const std::regex& pat) {
            return std::regex_search(std::string(cmd), pat);
        }
    );

    if (isDangerous) {
        return promptUser(std::format(
            "WinBot wants to run a potentially dangerous command:\n  {}\nAllow?", cmd
        ));
    }

    if (m_cfg.confirmShellCommands) {
        return promptUser(std::format("WinBot wants to run:\n  {}\nAllow?", cmd));
    }

    return ok("");
}

bool PermissionSystem::isToolEnabled(std::string_view name) const {
    return !m_cfg.disabledTools.contains(std::string(name));
}

ToolResult PermissionSystem::promptUser(std::string_view prompt) {
    // In MCP mode, interactive prompts are disabled (would deadlock).
    // Blocked paths/processes are still enforced — only the interactive
    // "are you sure?" prompts are auto-approved.
    if (g_mcpMode) {
        WINBOT_INFO("PermissionSystem: auto-approved (MCP mode): {}", prompt);
        return ok("");
    }

    std::print(stderr, "\n[PERMISSION] {}\nEnter 'yes' to allow, anything else to deny: ", prompt);
    std::string response;
    std::getline(std::cin, response);
    to_lower_inplace(response);
    if (response == "yes" || response == "y") {
        return ok("");
    }
    return err("User denied permission");
}
