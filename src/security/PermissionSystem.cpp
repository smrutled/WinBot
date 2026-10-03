#include "security/PermissionSystem.h"
#include <iostream>
#include <print>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <tlhelp32.h>

static bool isSubpathOrEqual(const std::filesystem::path& base, const std::filesystem::path& target) {
    if (base.empty() || target.empty()) return false;

    std::wstring bStr = base.lexically_normal().wstring();
    std::wstring tStr = target.lexically_normal().wstring();

    std::replace(bStr.begin(), bStr.end(), L'/', L'\\');
    std::replace(tStr.begin(), tStr.end(), L'/', L'\\');

    // Case-insensitive normalization
    for (auto& c : bStr) c = std::towlower(c);
    for (auto& c : tStr) c = std::towlower(c);

    // If drive root like "c:" make sure it has trailing slash "c:\"
    if (bStr.size() == 2 && bStr[1] == L':') bStr += L'\\';
    if (tStr.size() == 2 && tStr[1] == L':') tStr += L'\\';

    // Trim trailing backslashes unless root like "c:\"
    while (bStr.size() > 3 && bStr.back() == L'\\') bStr.pop_back();
    while (tStr.size() > 3 && tStr.back() == L'\\') tStr.pop_back();

    if (tStr == bStr) return true;

    if (tStr.starts_with(bStr)) {
        if (bStr.ends_with(L'\\')) return true;
        if (tStr.size() > bStr.size() && tStr[bStr.size()] == L'\\') return true;
    }
    return false;
}

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
    std::string out;
    out.reserve(result.size());
    size_t i = 0;
    while (i < result.size()) {
        if (result[i] == '%') {
            auto end = result.find('%', i + 1);
            if (end != std::string::npos) {
                std::string varName = result.substr(i + 1, end - i - 1);
                std::wstring wVarName = utf8_to_wide(varName);
                DWORD bufSize = ::GetEnvironmentVariableW(wVarName.c_str(), nullptr, 0);
                if (bufSize > 0) {
                    std::wstring wVal(bufSize, L'\0');
                    DWORD len = ::GetEnvironmentVariableW(wVarName.c_str(), wVal.data(), bufSize);
                    if (len > 0) {
                        wVal.resize(len);
                        out += wide_to_utf8(wVal);
                        i = end + 1;
                        continue;
                    }
                }
                out += result.substr(i, end - i + 1);
                i = end + 1;
                continue;
            }
        }
        out += result[i++];
    }
    return out;
}

ToolResult PermissionSystem::checkPath(std::string_view rawPath) const {
    if (rawPath.empty()) {
        return err("Path cannot be empty");
    }

    std::string expandedRaw = expandEnv(rawPath);
    std::filesystem::path targetPath;
    try {
        targetPath = std::filesystem::weakly_canonical(utf8_to_wide(expandedRaw));
    } catch (...) {
        targetPath = std::filesystem::path(utf8_to_wide(expandedRaw)).lexically_normal();
    }

    // Check blocked paths first (higher priority)
    for (const auto& blocked : m_cfg.blockedPaths) {
        std::string expandedBlocked = expandEnv(blocked);
        std::filesystem::path blockedPath;
        try {
            blockedPath = std::filesystem::weakly_canonical(utf8_to_wide(expandedBlocked));
        } catch (...) {
            blockedPath = std::filesystem::path(utf8_to_wide(expandedBlocked)).lexically_normal();
        }

        if (isSubpathOrEqual(blockedPath, targetPath)) {
            return err(std::format("Access denied — path '{}' is in a blocked directory", rawPath));
        }
    }

    // Check if within an allowed root
    if (!m_cfg.allowedPaths.empty()) {
        bool allowed = false;
        for (const auto& allow : m_cfg.allowedPaths) {
            std::string expandedAllow = expandEnv(allow);
            std::filesystem::path allowPath;
            try {
                allowPath = std::filesystem::weakly_canonical(utf8_to_wide(expandedAllow));
            } catch (...) {
                allowPath = std::filesystem::path(utf8_to_wide(expandedAllow)).lexically_normal();
            }

            if (isSubpathOrEqual(allowPath, targetPath)) {
                allowed = true;
                break;
            }
        }

        if (!allowed) {
            if (g_mcpMode) {
                return err(std::format(
                    "Access denied — path '{}' is outside configured allowed paths (MCP non-interactive mode)",
                    rawPath));
            }
            return promptUser(std::format(
                "WinBot wants to access '{}' which is outside configured allowed paths. Allow?", rawPath
            ));
        }
    }

    return ok("");
}

ToolResult PermissionSystem::checkFileDelete(std::string_view path) const {
    auto pathCheck = checkPath(path);
    if (!pathCheck) return pathCheck;

    if (m_cfg.confirmFileDelete) {
        if (g_mcpMode) {
            return err(std::format(
                "File deletion rejected for '{}' — interactive confirmation unavailable in MCP mode", path));
        }
        return promptUser(std::format("Delete file '{}'?", path));
    }
    return ok("");
}

ToolResult PermissionSystem::checkProcess(std::string_view name) const {
    if (name.empty()) {
        return err("Process name or PID cannot be empty");
    }

    std::string lower(name);
    to_lower_inplace(lower);

    // Check if numeric PID
    DWORD pid = 0;
    bool isPid = false;
    try {
        size_t idx = 0;
        unsigned long val = std::stoul(std::string(name), &idx);
        if (idx == name.size()) {
            pid = static_cast<DWORD>(val);
            isPid = true;
        }
    } catch (...) {}

    // Special case for critical Windows kernel PIDs
    if (isPid && (pid == 0 || pid == 4)) {
        return err(std::format("Cannot kill critical system process (PID {})", pid));
    }

    // Resolve PID to process executable name if PID was provided
    std::string procExeName;
    if (isPid) {
        HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (::Process32FirstW(snap, &entry)) {
                do {
                    if (entry.th32ProcessID == pid) {
                        procExeName = wide_to_utf8(entry.szExeFile);
                        to_lower_inplace(procExeName);
                        break;
                    }
                } while (::Process32NextW(snap, &entry));
            }
            ::CloseHandle(snap);
        }
    }

    auto matchesProcess = [](std::string_view target, std::string_view blocked) {
        if (target.empty() || blocked.empty()) return false;
        if (target == blocked) return true;
        if (!target.ends_with(".exe") && (std::string(target) + ".exe") == blocked) return true;
        if (target.ends_with(".exe") && target.substr(0, target.size() - 4) == blocked) return true;
        return false;
    };

    for (const auto& blocked : m_cfg.blockedProcesses) {
        std::string blockedLower(blocked);
        to_lower_inplace(blockedLower);

        if (matchesProcess(lower, blockedLower) || (!procExeName.empty() && matchesProcess(procExeName, blockedLower))) {
            return err(std::format("Cannot kill protected process '{}'", name));
        }
    }

    if (m_cfg.confirmProcessKill) {
        if (g_mcpMode) {
            return err(std::format("Killing process '{}' rejected — confirmation unavailable in MCP mode", name));
        }
        return promptUser(std::format("WinBot wants to kill process '{}'. Allow?", name));
    }

    return ok("");
}

ToolResult PermissionSystem::checkShellCommand(std::string_view cmd) const {
    if (cmd.empty()) {
        return ok("");
    }

    std::string cmdStr(cmd);

    // Check for user-configured dangerous patterns
    bool isDangerous = std::ranges::any_of(m_dangerousPatterns,
        [&](const std::regex& pat) {
            return std::regex_search(cmdStr, pat);
        }
    );

    // Built-in hard guardrails against common destructive LLM shell commands
    if (!isDangerous) {
        static const std::vector<std::regex> s_builtInPatterns = [] {
            const std::vector<std::string> patterns = {
                R"(\brm\s+-rf\b)",
                R"(\bformat\s+[a-zA-Z]:)",
                R"(\bformat\s+)",
                R"(\bdel\s+.*(/s|/f)\b)",
                R"(\b(rmdir|rd)\s+.*(/s|/q)\b)",
                R"(\breg\s+delete\b)",
                R"(\bRemove-Item\b.*(-Recurse|-Force))",
                R"(\bdiskpart\b)",
                R"(\bbcdedit\b)",
                R"(\bvssadmin\s+delete\b)",
                R"(\b(shutdown|Stop-Computer|Restart-Computer)\b)",
                R"(\bpowershell.*(-enc|-encodedcommand)\b)"
            };
            std::vector<std::regex> compiled;
            for (const auto& p : patterns) {
                try {
                    compiled.emplace_back(p, std::regex_constants::icase | std::regex_constants::ECMAScript);
                } catch (...) {}
            }
            return compiled;
        }();

        isDangerous = std::ranges::any_of(s_builtInPatterns,
            [&](const std::regex& pat) {
                return std::regex_search(cmdStr, pat);
            }
        );
    }

    if (isDangerous) {
        if (g_mcpMode) {
            return err(std::format("Command rejected — dangerous command pattern detected in non-interactive/MCP mode: {}", cmd));
        }
        return promptUser(std::format(
            "WinBot wants to run a potentially dangerous command:\n  {}\nAllow?", cmd
        ));
    }

    if (m_cfg.confirmShellCommands) {
        if (g_mcpMode) {
            return err(std::format("Command rejected — confirmation required but unavailable in MCP mode: {}", cmd));
        }
        return promptUser(std::format("WinBot wants to run:\n  {}\nAllow?", cmd));
    }

    return ok("");
}

bool PermissionSystem::isToolEnabled(std::string_view name) const {
    return !m_cfg.disabledTools.contains(std::string(name));
}

ToolResult PermissionSystem::promptUser(std::string_view prompt) {
    if (g_mcpMode) {
        WINBOT_WARN("PermissionSystem: prompt denied (MCP mode): {}", prompt);
        return err("Action denied — interactive confirmation unavailable in non-interactive/MCP mode");
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
