#ifndef WINBOT_TOOLS_INPUTTOOLS_H
#define WINBOT_TOOLS_INPUTTOOLS_H

#include "Common.h"
#include "core/ITool.h"

namespace tools {
// Mouse and keyboard control via Win32 SendInput

ToolResult click(int x, int y, std::string_view button = "left", bool humanMove = false);
ToolResult doubleClick(int x, int y, bool humanMove = false);
ToolResult drag(int x1, int y1, int x2, int y2, bool humanMove = false);
ToolResult scroll(int x, int y, int delta, bool humanMove = false);
ToolResult moveMouse(int x, int y, bool humanMove = false);
ToolResult humanMouseMove(int toX, int toY);
ToolResult typeText(std::string_view text);
ToolResult keyPress(std::string_view combo); // e.g. "Ctrl+C", "Enter", "F5"

} // namespace tools

// ── Input ITool Endpoints ────────────────────────────────────────────────────

class ClickTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "click"; }
    [[nodiscard]] std::string description() const override {
        return "Click the mouse at (x, y)";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class DoubleClickTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "double_click"; }
    [[nodiscard]] std::string description() const override {
        return "Double-click at (x, y)";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class MouseDragTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "drag"; }
    [[nodiscard]] std::string description() const override {
        return "Click-drag from (x1,y1) to (x2,y2)";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class ScrollTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "scroll"; }
    [[nodiscard]] std::string description() const override {
        return "Scroll the mouse wheel at (x, y)";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class TypeTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "type"; }
    [[nodiscard]] std::string description() const override {
        return "Type text using the keyboard";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class KeyTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "key"; }
    [[nodiscard]] std::string description() const override {
        return "Press a key or key combination (e.g. Ctrl+C, Enter, F5)";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

class HumanMoveTool : public ITool {
public:
    [[nodiscard]] std::string name() const override { return "input_human_move"; }
    [[nodiscard]] std::string description() const override {
        return "Move the mouse to (x, y) using a human-like path (cubic Bézier curve with ease-in/out and jitter)";
    }
    [[nodiscard]] json parametersSchema() const override;
    [[nodiscard]] ToolResult execute(const json& args) override;
};

// Explicit initialization anchor
void initInputTools();

#endif // WINBOT_TOOLS_INPUTTOOLS_H
