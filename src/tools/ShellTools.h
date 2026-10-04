#ifndef WINBOT_TOOLS_SHELLTOOLS_H
#define WINBOT_TOOLS_SHELLTOOLS_H

#include "Common.h"
#include "core/ITool.h"
#include <stop_token>
#include <string>
#include <string_view>

class PermissionSystem;

namespace tools {
// Shell command execution and web requests

ToolResult runCommand(std::string_view cmd, std::string_view shell = "cmd", int timeoutMs = 30000, const std::stop_token& stopToken = {});
ToolResult httpGet(std::string_view url, std::string_view headers = {});
ToolResult searchWeb(std::string_view query);

} // namespace tools

// ── Shell ITool Endpoints ────────────────────────────────────────────────────

class RunCommandTool : public ITool {
public:
    explicit RunCommandTool(PermissionSystem& perms) : m_perms(&perms) {}
    explicit RunCommandTool(PermissionSystem* perms = nullptr) : m_perms(perms) {}
    ~RunCommandTool() override = default;
    RunCommandTool(const RunCommandTool&) = delete;
    RunCommandTool& operator=(const RunCommandTool&) = delete;
    RunCommandTool(RunCommandTool&&) noexcept = default;
    RunCommandTool& operator=(RunCommandTool&&) noexcept = default;

    [[nodiscard]] std::string name() const override { return "run_command"; }
    [[nodiscard]] std::string description() const override {
        return "Run a PowerShell/cmd command and return stdout+stderr";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
    [[nodiscard]] ToolResult execute(const json& args, const std::stop_token& stopToken) override;

private:
    PermissionSystem* m_perms{nullptr};
};

class HttpGetTool : public ITool {
public:
    HttpGetTool() = default;
    ~HttpGetTool() override = default;
    HttpGetTool(const HttpGetTool&) = delete;
    HttpGetTool& operator=(const HttpGetTool&) = delete;
    HttpGetTool(HttpGetTool&&) noexcept = default;
    HttpGetTool& operator=(HttpGetTool&&) noexcept = default;

    [[nodiscard]] std::string name() const override { return "http_get"; }
    [[nodiscard]] std::string description() const override {
        return "Make an HTTP GET request and return the body";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class SearchWebTool : public ITool {
public:
    SearchWebTool() = default;
    ~SearchWebTool() override = default;
    SearchWebTool(const SearchWebTool&) = delete;
    SearchWebTool& operator=(const SearchWebTool&) = delete;
    SearchWebTool(SearchWebTool&&) noexcept = default;
    SearchWebTool& operator=(SearchWebTool&&) noexcept = default;

    [[nodiscard]] std::string name() const override { return "search_web"; }
    [[nodiscard]] std::string description() const override {
        return "Search DuckDuckGo and return the top result URLs + snippets";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

// Explicit initialization anchor
void initShellTools();

#endif // WINBOT_TOOLS_SHELLTOOLS_H
