#include "Memory.h"
#include <stdexcept>

Memory::Memory(std::filesystem::path dbPath) : m_path(std::move(dbPath)) {
    std::filesystem::create_directories(m_path.parent_path());
    if (sqlite3_open(m_path.string().c_str(), &m_db) != SQLITE_OK) {
        throw std::runtime_error(std::format("SQLite open failed: {}", sqlite3_errmsg(m_db)));
    }
    initSchema();
    WINBOT_INFO("Memory: loaded from '{}'", m_path.string());
}

Memory::~Memory() {
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
}

void Memory::initSchema() {
    const char* sql = R"sql(
        CREATE TABLE IF NOT EXISTS memory (
            key        TEXT PRIMARY KEY NOT NULL,
            value      TEXT NOT NULL DEFAULT '',
            updated_at TEXT NOT NULL DEFAULT (datetime('now'))
        );
        CREATE INDEX IF NOT EXISTS idx_memory_key ON memory(key);
    )sql";
    char* errMsg = nullptr;
    sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
    if (errMsg) { sqlite3_free(errMsg); }
}

ToolResult Memory::store(std::string_view key, std::string_view value) {
    std::lock_guard lock{ m_mutex };
    const char* sql =
        "INSERT INTO memory(key, value, updated_at) VALUES(?,?,datetime('now')) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value, updated_at=excluded.updated_at;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return err(sqlite3_errmsg(m_db));

    sqlite3_bind_text(stmt, 1, key.data(),   static_cast<int>(key.size()),   SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return ok(std::format("Remembered: {} = {}", key, value));
}

ToolResult Memory::retrieve(std::string_view key) const {
    std::lock_guard lock{ m_mutex };
    const char* sql = "SELECT value FROM memory WHERE key=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return err(sqlite3_errmsg(m_db));

    sqlite3_bind_text(stmt, 1, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
    ToolResult result = err(std::format("No memory found for key: {}", key));
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        result = ok(val ? val : "");
    }
    sqlite3_finalize(stmt);
    return result;
}

ToolResult Memory::remove(std::string_view key) {
    std::lock_guard lock{ m_mutex };
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, "DELETE FROM memory WHERE key=?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return ok(std::format("Forgotten: {}", key));
}

std::vector<MemoryEntry> Memory::listAll() const {
    std::lock_guard lock{ m_mutex };
    std::vector<MemoryEntry> entries;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, "SELECT key, value, updated_at FROM memory ORDER BY key;", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        entries.push_back({
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))
        });
    }
    sqlite3_finalize(stmt);
    return entries;
}

std::string Memory::toContextString() const {
    auto entries = listAll();
    if (entries.empty()) return {};
    std::string ctx = "## Persistent Memory (facts from previous sessions):\n";
    for (const auto& [key, value, ts] : entries) {
        ctx += std::format("  {} = {}\n", key, value);
    }
    return ctx;
}
