#include "AuditLog.h"
#include <fstream>

AuditLog::AuditLog(std::filesystem::path logPath)
    : m_path(std::move(logPath))
{
    // Ensure the parent directory exists
    std::filesystem::create_directories(m_path.parent_path());
    note(std::format("=== WinBot session started {} ===", utc_timestamp()));
}

void AuditLog::record(const AuditEntry& entry) {
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
    json record = {
        { "ts",   utc_timestamp()     },
        { "note", std::string(message) }
    };
    std::lock_guard lock{ m_mutex };
    std::ofstream file{ m_path, std::ios::app };
    file << record.dump() << '\n';
}
