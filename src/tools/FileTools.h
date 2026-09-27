#ifndef WINBOT_TOOLS_FILETOOLS_H
#define WINBOT_TOOLS_FILETOOLS_H

#include "Common.h"
#include "core/ITool.h"

class PermissionSystem;

namespace tools {
// File system operations

ToolResult readFile(std::string_view path);
ToolResult writeFile(std::string_view path, std::string_view content);
ToolResult appendFile(std::string_view path, std::string_view content);
ToolResult listDirectory(std::string_view path);
ToolResult deleteFile(std::string_view path);
ToolResult copyFile(std::string_view src, std::string_view dst);

} // namespace tools

// ── File ITool Endpoints ─────────────────────────────────────────────────────

class ReadFileTool : public ITool {
public:
    explicit ReadFileTool(PermissionSystem& perms) : m_perms(&perms) {}
    explicit ReadFileTool(PermissionSystem* perms = nullptr) : m_perms(perms) {}

    [[nodiscard]] std::string name() const override { return "read_file"; }
    [[nodiscard]] std::string description() const override {
        return "Read a file's text contents";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    PermissionSystem* m_perms{nullptr};
};

class WriteFileTool : public ITool {
public:
    explicit WriteFileTool(PermissionSystem& perms) : m_perms(&perms) {}
    explicit WriteFileTool(PermissionSystem* perms = nullptr) : m_perms(perms) {}

    [[nodiscard]] std::string name() const override { return "write_file"; }
    [[nodiscard]] std::string description() const override {
        return "Write content to a file (creates or overwrites)";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    PermissionSystem* m_perms{nullptr};
};

class AppendFileTool : public ITool {
public:
    explicit AppendFileTool(PermissionSystem& perms) : m_perms(&perms) {}
    explicit AppendFileTool(PermissionSystem* perms = nullptr) : m_perms(perms) {}

    [[nodiscard]] std::string name() const override { return "append_file"; }
    [[nodiscard]] std::string description() const override {
        return "Append content to a file";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    PermissionSystem* m_perms{nullptr};
};

class ListDirectoryTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "list_directory"; }
    [[nodiscard]] std::string description() const override {
        return "List files in a directory";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class DeleteFileTool : public ITool {
public:
    explicit DeleteFileTool(PermissionSystem& perms) : m_perms(&perms) {}
    explicit DeleteFileTool(PermissionSystem* perms = nullptr) : m_perms(perms) {}

    [[nodiscard]] std::string name() const override { return "delete_file"; }
    [[nodiscard]] std::string description() const override {
        return "Delete a file (permission-checked)";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    PermissionSystem* m_perms{nullptr};
};

class CopyFileTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "copy_file"; }
    [[nodiscard]] std::string description() const override {
        return "Copy a file from src to dst";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

// Explicit initialization anchor
void initFileTools();

#endif // WINBOT_TOOLS_FILETOOLS_H
