#ifndef WINBOT_MEMORY_H
#define WINBOT_MEMORY_H
#include "Common.h"
#include <sqlite3.h>
#include <filesystem>
#include <mutex>
#include <vector>

// ── Memory ────────────────────────────────────────────────────────────────────
// Persistent key-value store backed by SQLite.
// The agent uses this to remember facts across sessions.
struct MemoryEntry {
    std::string key;
    std::string value;
    std::string updatedAt;
};

class Memory {
public:
    explicit Memory(std::filesystem::path dbPath);
    ~Memory();

    Memory(const Memory&)            = delete;
    Memory& operator=(const Memory&) = delete;

    [[nodiscard]] ToolResult store(std::string_view key, std::string_view value);
    [[nodiscard]] ToolResult retrieve(std::string_view key) const;
    [[nodiscard]] ToolResult remove(std::string_view key);
    [[nodiscard]] std::vector<MemoryEntry> listAll() const;

    // Generate a startup context string summarizing all memories
    [[nodiscard]] std::string toContextString() const;

private:
    sqlite3*              m_db{ nullptr };
    std::filesystem::path m_path;
    mutable std::mutex    m_mutex;

    void initSchema();
};

#endif // WINBOT_MEMORY_H
