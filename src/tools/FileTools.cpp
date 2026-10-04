#include "tools/FileTools.h"
#include "security/PermissionSystem.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <format>

namespace tools {

ToolResult readFile(std::string_view path) {
    std::ifstream file{ utf8_to_wide(path), std::ios::binary };
    if (!file) {
        return err(std::format("Cannot open file: {}", path));
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    if (content.size() > 32768) {
        content = content.substr(0, 32768) + "\n... (truncated at 32KB)";
    }
    return ok(content);
}

ToolResult writeFile(std::string_view path, std::string_view content) {
    std::filesystem::path fpath = utf8_to_wide(path);
    std::filesystem::create_directories(fpath.parent_path());
    std::ofstream file{ fpath, std::ios::binary };
    if (!file) {
        return err(std::format("Cannot write file: {}", path));
    }
    file << content;
    return ok(std::format("Wrote {} bytes to {}", content.size(), path));
}

ToolResult appendFile(std::string_view path, std::string_view content) {
    std::ofstream file{ utf8_to_wide(path), std::ios::app | std::ios::binary };
    if (!file) {
        return err(std::format("Cannot append to file: {}", path));
    }
    file << content;
    return ok(std::format("Appended {} bytes to {}", content.size(), path));
}

ToolResult listDirectory(std::string_view path) {
    std::filesystem::path dir = utf8_to_wide(path);
    if (!std::filesystem::exists(dir)) {
        return err(std::format("Path not found: {}", path));
    }

    std::string result;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        bool isDir = entry.is_directory(ec);
        std::string name = wide_to_utf8(entry.path().filename().wstring());
        if (isDir) {
            result += std::format("[DIR]  {}\n", name);
        } else {
            result += std::format("[FILE] {}  ({} bytes)\n", name, entry.file_size(ec));
        }
    }
    return ok(result.empty() ? "(empty directory)" : result);
}

ToolResult deleteFile(std::string_view path) {
    std::filesystem::path fpath = utf8_to_wide(path);
    std::error_code ec;
    bool removed = std::filesystem::remove(fpath, ec);
    if (!removed || ec) {
        return err(std::format("Failed to delete '{}': {}", path, ec.message()));
    }
    return ok(std::format("Deleted: {}", path));
}

ToolResult copyFile(std::string_view src, std::string_view dst) {
    std::error_code ec;
    std::filesystem::copy(utf8_to_wide(src), utf8_to_wide(dst),
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return err(std::format("Copy failed: {}", ec.message()));
    }
    return ok(std::format("Copied {} → {}", src, dst));
}

} // namespace tools

// ── ReadFileTool ─────────────────────────────────────────────────────────────
json ReadFileTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Absolute or relative file path"}}}
        }},
        {"required", {"path"}}
    };
}

ToolResult ReadFileTool::execute(const json& args) {
    auto path = args.value("path", "");
    if (m_perms != nullptr) {
        auto check = m_perms->checkPath(path);
        if (!check) {
            return check;
        }
    }
    return tools::readFile(path);
}

// ── WriteFileTool ────────────────────────────────────────────────────────────
json WriteFileTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path",    {{"type", "string"}}},
            {"content", {{"type", "string"}}}
        }},
        {"required", {"path", "content"}}
    };
}

ToolResult WriteFileTool::execute(const json& args) {
    auto path = args.value("path", "");
    if (m_perms != nullptr) {
        auto check = m_perms->checkPath(path);
        if (!check) {
            return check;
        }
    }
    return tools::writeFile(path, args.value("content", ""));
}

// ── AppendFileTool ───────────────────────────────────────────────────────────
json AppendFileTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path",    {{"type", "string"}}},
            {"content", {{"type", "string"}}}
        }},
        {"required", {"path", "content"}}
    };
}

ToolResult AppendFileTool::execute(const json& args) {
    auto path = args.value("path", "");
    if (m_perms != nullptr) {
        auto check = m_perms->checkPath(path);
        if (!check) {
            return check;
        }
    }
    return tools::appendFile(path, args.value("content", ""));
}

// ── ListDirectoryTool ────────────────────────────────────────────────────────
json ListDirectoryTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}}}
        }},
        {"required", {"path"}}
    };
}

ToolResult ListDirectoryTool::execute(const json& args) {
    auto path = args.value("path", ".");
    if (m_perms != nullptr) {
        auto check = m_perms->checkPath(path);
        if (!check) {
            return check;
        }
    }
    return tools::listDirectory(path);
}

// ── DeleteFileTool ───────────────────────────────────────────────────────────
json DeleteFileTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {{"path", {{"type", "string"}}}}},
        {"required", {"path"}}
    };
}

ToolResult DeleteFileTool::execute(const json& args) {
    auto path = args.value("path", "");
    if (m_perms != nullptr) {
        auto check = m_perms->checkFileDelete(path);
        if (!check) {
            return check;
        }
    }

    return tools::deleteFile(path);
}

// ── CopyFileTool ─────────────────────────────────────────────────────────────
json CopyFileTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"src", {{"type", "string"}}},
            {"dst", {{"type", "string"}}}
        }},
        {"required", {"src", "dst"}}
    };
}

ToolResult CopyFileTool::execute(const json& args) {
    auto src = args.value("src", "");
    auto dst = args.value("dst", "");
    if (m_perms != nullptr) {
        auto checkSrc = m_perms->checkPath(src);
        if (!checkSrc) {
            return checkSrc;
        }
        auto checkDst = m_perms->checkPath(dst);
        if (!checkDst) {
            return checkDst;
        }
    }
    return tools::copyFile(src, dst);
}

// ── Self-Registration ────────────────────────────────────────────────────────
REGISTER_TOOL_WITH_DEPS(ReadFileTool, [](const ToolDependencies& d) {
    return std::make_unique<ReadFileTool>(d.perms);
});
REGISTER_TOOL_WITH_DEPS(WriteFileTool, [](const ToolDependencies& d) {
    return std::make_unique<WriteFileTool>(d.perms);
});
REGISTER_TOOL_WITH_DEPS(AppendFileTool, [](const ToolDependencies& d) {
    return std::make_unique<AppendFileTool>(d.perms);
});
REGISTER_TOOL_WITH_DEPS(ListDirectoryTool, [](const ToolDependencies& d) {
    return std::make_unique<ListDirectoryTool>(d.perms);
});
REGISTER_TOOL_WITH_DEPS(DeleteFileTool, [](const ToolDependencies& d) {
    return std::make_unique<DeleteFileTool>(d.perms);
});
REGISTER_TOOL_WITH_DEPS(CopyFileTool, [](const ToolDependencies& d) {
    return std::make_unique<CopyFileTool>(d.perms);
});

void initFileTools() {}
