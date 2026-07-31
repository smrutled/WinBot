#include "FileTools.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace tools {

ToolResult readFile(std::string_view path) {
    std::ifstream file{ utf8_to_wide(path), std::ios::binary };
    if (!file) return err(std::format("Cannot open file: {}", path));
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
    if (!file) return err(std::format("Cannot write file: {}", path));
    file << content;
    return ok(std::format("Wrote {} bytes to {}", content.size(), path));
}

ToolResult appendFile(std::string_view path, std::string_view content) {
    std::ofstream file{ utf8_to_wide(path), std::ios::app | std::ios::binary };
    if (!file) return err(std::format("Cannot append to file: {}", path));
    file << content;
    return ok(std::format("Appended {} bytes to {}", content.size(), path));
}

ToolResult listDirectory(std::string_view path) {
    std::filesystem::path dir = utf8_to_wide(path);
    if (!std::filesystem::exists(dir)) return err(std::format("Path not found: {}", path));

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
    if (!removed || ec) return err(std::format("Failed to delete '{}': {}", path, ec.message()));
    return ok(std::format("Deleted: {}", path));
}

ToolResult copyFile(std::string_view src, std::string_view dst) {
    std::error_code ec;
    std::filesystem::copy(utf8_to_wide(src), utf8_to_wide(dst),
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return err(std::format("Copy failed: {}", ec.message()));
    return ok(std::format("Copied {} → {}", src, dst));
}

} // namespace tools
