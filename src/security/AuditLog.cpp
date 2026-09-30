#include "security/AuditLog.h"
#include <fstream>

AuditLog::AuditLog(std::filesystem::path logPath)
    : m_path(std::move(logPath))
{
    // Ensure the parent directory exists
    try {
        std::filesystem::create_directories(m_path.parent_path());
    } catch (const std::filesystem::filesystem_error& e) {
        // Never let audit logging crash the server; degrade to stderr-only.
        WINBOT_WARN("AuditLog: cannot create log directory '{}' ({}). File audit logging disabled.",
                    m_path.parent_path().string(), e.what());
        m_enabled = false;
        return;
    }
    note(std::format("=== WinBot session started {} ===", utc_timestamp()));
}

void AuditLog::record(const AuditEntry& entry) {
    if (!m_enabled) return;
    json record = {
        { "ts",     entry.timestamp },
        { "task",   entry.task      },
        { "tool",   entry.tool      },
        { "args",   entry.args      },
        { "result", entry.result    },
        { "detail", entry.detail    }
    };

    std::lock_guard lock{ m_mutex };
    std::ofstream file{ m_path, std::ios::app };
    file << record.dump() << '\n';
}

void AuditLog::note(std::string_view message) {
    if (!m_enabled) return;
    json record = {
        { "ts",   utc_timestamp()     },
        { "note", std::string(message) }
    };
    std::lock_guard lock{ m_mutex };
    std::ofstream file{ m_path, std::ios::app };
    file << record.dump() << '\n';
}
