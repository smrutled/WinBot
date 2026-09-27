#ifndef WINBOT_TOOLS_WINDOWTOOLS_H
#define WINBOT_TOOLS_WINDOWTOOLS_H

#include "Common.h"
#include "core/ITool.h"

class PermissionSystem;

namespace tools {
// Window management tools

ToolResult getWindowList();
ToolResult focusWindow(std::string_view title);
ToolResult closeWindow(std::string_view title);
ToolResult getClipboard();
ToolResult setClipboard(std::string_view text);
ToolResult getProcesses();
ToolResult killProcess(std::string_view nameOrPid);
ToolResult getSystemInfo();
ToolResult getCursorPosition();

// Window focus helper
void bringWindowToForeground(HWND hwnd);

} // namespace tools

// ── Window ITool Endpoints ───────────────────────────────────────────────────

class WindowListTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "get_window_list"; }
    [[nodiscard]] std::string description() const override {
        return "List all visible windows with title and HWND";
    }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class FocusWindowTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "focus_window"; }
    [[nodiscard]] std::string description() const override {
        return "Bring a window to the foreground by title substring";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class CloseWindowTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "close_window"; }
    [[nodiscard]] std::string description() const override {
        return "Close a window by title substring";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class GetClipboardTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "get_clipboard"; }
    [[nodiscard]] std::string description() const override {
        return "Read current clipboard text";
    }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class SetClipboardTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "set_clipboard"; }
    [[nodiscard]] std::string description() const override {
        return "Write text to the clipboard";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class GetProcessesTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "get_processes"; }
    [[nodiscard]] std::string description() const override {
        return "List running processes (name + PID)";
    }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class KillProcessTool : public ITool {
public:
    explicit KillProcessTool(PermissionSystem& perms) : m_perms(&perms) {}
    explicit KillProcessTool(PermissionSystem* perms = nullptr) : m_perms(perms) {}

    [[nodiscard]] std::string name() const override { return "kill_process"; }
    [[nodiscard]] std::string description() const override {
        return "Kill a process by name or PID";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;

private:
    PermissionSystem* m_perms{nullptr};
};

class GetSystemInfoTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "get_system_info"; }
    [[nodiscard]] std::string description() const override {
        return "Get CPU, RAM, and disk usage info";
    }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class GetCursorPositionTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "get_cursor_position"; }
    [[nodiscard]] std::string description() const override {
        return "Get current mouse cursor position";
    }
    [[nodiscard]] json parametersSchema() const override {
        return {{"type", "object"}, {"properties", json::object()}};
    }
    [[nodiscard]] ToolResult execute(const json& args) override;
};

// Explicit initialization anchor
void initWindowTools();

#endif // WINBOT_TOOLS_WINDOWTOOLS_H
