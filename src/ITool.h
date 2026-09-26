#ifndef WINBOT_ITOOL_H
#define WINBOT_ITOOL_H

#include "Common.h"

// ── ITool ─────────────────────────────────────────────────────────────────────
// Abstract interface that all modular tools inherit from.
// Encapsulates tool identity, schema metadata, and argument execution logic.
class ITool {
public:
    virtual ~ITool() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::string description() const = 0;
    [[nodiscard]] virtual json parametersSchema() const = 0;
    [[nodiscard]] virtual ToolResult execute(const json& args) = 0;
};

#endif // WINBOT_ITOOL_H
