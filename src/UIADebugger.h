#ifndef WINBOT_UIADEBUGGER_H
#define WINBOT_UIADEBUGGER_H
#include "UIAutomationScanner.h"
#include "UIHandle.h"
#include <string>
#include <unordered_map>
#include <optional>

// ── UIADebugger ───────────────────────────────────────────────────────────────
// Interactive REPL for testing UI Automation commands. Supports:
//   - Semicolon-separated statements:  Click("Start") ; wait 1000
//   - Dot-notation chaining:           Select("Calc", 3000).Click("One")
//   - Variable assignment:             $calc = Select("Calculator", 3000)
//   - Scoped search:                   $calc.Click("One")
class UIADebugger {
public:
    struct CommandArgs {
        std::string type;
        std::string name;
        int timeoutMs = 0;
    };

    explicit UIADebugger(UIAutomationScanner& scanner);

    // Run the interactive REPL loop. Returns when user types 'exit'
    // or KillSwitch fires.
    void run();

    // Strip surrounding double-quotes from a string, unescaping any embedded quotes.
    static std::string stripQuotes(std::string s);

    // Parse arguments like ("Name"), ("Name", 5000), or ("Type", "Name", 5000)
    static CommandArgs parseCommandArgs(std::string_view argStr);

    // Execute one semicolon-delimited segment (may contain dot-chain).
    // Returns false if the REPL should exit.
    bool execSegment(std::string_view seg);

    // Execute a full semicolon-separated command line string.
    // Returns false if the REPL should exit.
    bool execute(std::string_view line);

    // Session state accessors (useful for testing and inspection)
    [[nodiscard]] const std::unordered_map<std::string, UIHandle>& getVars() const noexcept { return m_vars; }
    [[nodiscard]] const std::optional<UIHandle>& getLastResult() const noexcept { return m_lastResult; }
    void setVar(const std::string& name, UIHandle handle) { m_vars.insert_or_assign(name, std::move(handle)); }
    void clearVars() noexcept { m_vars.clear(); m_lastResult.reset(); }

private:
    UIAutomationScanner& m_uia;

    // Session state
    std::unordered_map<std::string, UIHandle> m_vars;
    std::optional<UIHandle> m_lastResult;  // $_

    // Dispatch a single .Method(args) call on an existing UIHandle.
    // Returns false to abort the chain.
    bool dispatchOnHandle(UIHandle& h, std::string_view methodToken);
};

#endif // WINBOT_UIADEBUGGER_H
