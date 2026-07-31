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
    explicit UIADebugger(UIAutomationScanner& scanner);

    // Run the interactive REPL loop. Returns when user types 'exit'
    // or KillSwitch fires.
    void run();

private:
    UIAutomationScanner& m_uia;

    // Session state
    std::unordered_map<std::string, UIHandle> m_vars;
    std::optional<UIHandle> m_lastResult;  // $_

    struct CommandArgs {
        std::string type;
        std::string name;
        int timeoutMs = 0;
    };

    // Strip surrounding double-quotes from a string.
    static std::string stripQuotes(std::string s);

    // Parse arguments like ("Name"), ("Name", 5000), or ("Type", "Name", 5000)
    CommandArgs parseCommandArgs(std::string_view argStr);

    // Dispatch a single .Method(args) call on an existing UIHandle.
    // Returns false to abort the chain.
    bool dispatchOnHandle(UIHandle& h, std::string_view methodToken);

    // Execute one semicolon-delimited segment (may contain dot-chain).
    // Returns false if the REPL should exit.
    bool execSegment(std::string_view seg);
};

#endif // WINBOT_UIADEBUGGER_H
