#ifndef WINBOT_PROTOCOL_H
#define WINBOT_PROTOCOL_H
#include "Common.h"

// ── WinBot Stdio Protocol ──────────────────────────────────────────────────────
//
// Newline-delimited JSON — one JSON object per line in each direction.
//
// Request  (agent → WinBot):
//   {"id": <int>, "tool": "<name>", "args": {<params>}}
//
// Response (WinBot → agent):
//   {"id": <int>, "ok": true,  "result": "<string>"}
//   {"id": <int>, "ok": false, "error":  "<string>"}
//
// Special tool "list_tools" returns "result" as a JSON array string (not a
// human string) so the agent can self-discover the tool schema.

namespace protocol {

struct Request {
    int         id{ 0 };
    std::string tool;
    json        args{ json::object() };
};

// Parse one newline-delimited JSON line.
// Returns std::unexpected if the line is empty, a comment (#...), or malformed.
inline std::expected<Request, std::string> parseRequest(std::string_view line) {
    // Skip blank lines and comment lines
    size_t first = line.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos || line[first] == '#')
        return std::unexpected(std::string{});

    try {
        json j = json::parse(line);
        Request r;
        r.id   = j.value("id",   0);
        r.tool = j.value("tool", "");
        r.args = j.contains("args") ? j["args"] : json::object();
        if (r.tool.empty())
            return std::unexpected("Request missing 'tool' field");
        return r;
    } catch (const json::exception& e) {
        return std::unexpected(std::format("JSON parse error: {}", e.what()));
    }
}

// Serialise a successful response to one line (no trailing newline).
inline std::string makeOk(int id, std::string result) {
    return json{ {"id", id}, {"ok", true}, {"result", std::move(result)} }.dump();
}

// Serialise an error response to one line (no trailing newline).
inline std::string makeErr(int id, std::string error) {
    return json{ {"id", id}, {"ok", false}, {"error", std::move(error)} }.dump();
}

} // namespace protocol

#endif // WINBOT_PROTOCOL_H
