#ifndef WINBOT_AUDITLOG_H
#define WINBOT_AUDITLOG_H
#include "Common.h"
#include <filesystem>
#include <mutex>

// ── AuditLog ─────────────────────────────────────────────────────────────────
// Append-only, newline-delimited JSON log of every tool call the agent makes.
// Thread-safe — multiple threads may log concurrently.
struct AuditEntry {
    std::string timestamp;   // ISO 8601 UTC
    std::string task;        // Current task description
    std::string tool;        // Tool name
    json        args;        // Tool arguments
    std::string result;      // "ok" or error string
    std::string detail;      // First 256 chars of tool return value
};

class AuditLog {
public:
    explicit AuditLog(std::filesystem::path logPath);

    void record(const AuditEntry& entry);

    // Write a human-readable header line (e.g. on startup/shutdown).
    void note(std::string_view message);

private:
    std::filesystem::path m_path;
    std::mutex            m_mutex;
};

#endif // WINBOT_AUDITLOG_H
