#include "UIADebugger.h"
#include "KillSwitch.h"
#include "tools/InputTools.h"
#include "tools/WindowTools.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <print>
#include <sstream>
#include <stdexcept>
#include <thread>


UIADebugger::UIADebugger(UIAutomationScanner &scanner) : m_uia(scanner) {}

// ── Helpers
// ───────────────────────────────────────────────────────────────────

std::string UIADebugger::stripQuotes(std::string s) {
  // Trim whitespace
  auto start = s.find_first_not_of(" \t\n\r");
  if (start == std::string::npos)
    return "";
  auto end = s.find_last_not_of(" \t\n\r");
  s = s.substr(start, end - start + 1);

  if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
    s = s.substr(1, s.size() - 2);

  // Unescape \" -> "
  std::string unescaped;
  unescaped.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '"') {
      unescaped += '"';
      ++i;
    } else {
      unescaped += s[i];
    }
  }
  return unescaped;
}

UIADebugger::CommandArgs
UIADebugger::parseCommandArgs(std::string_view argStr) {
  std::string inner(argStr);
  if (!inner.empty() && inner.front() == '(')
    inner = inner.substr(1);
  if (!inner.empty() && inner.back() == ')')
    inner.pop_back();

  std::vector<std::string> parts;
  {
    std::string cur;
    bool inQuote = false;
    for (size_t i = 0; i < inner.size(); ++i) {
      char c = inner[i];
      if (c == '\\' && i + 1 < inner.size() && inner[i + 1] == '"') {
        cur += "\\\"";
        ++i;
        continue;
      }
      if (c == '"')
        inQuote = !inQuote;
      if (c == ',' && !inQuote) {
        parts.push_back(stripQuotes(cur));
        cur.clear();
      } else {
        cur += c;
      }
    }
    if (!cur.empty())
      parts.push_back(stripQuotes(cur));
  }

  CommandArgs result;
  if (parts.size() == 1) {
    result.name = parts[0];
  } else if (parts.size() == 2) {
    // (Name, Timeout) OR (Type, Name)
    // If the second part is numeric, it's a timeout.
    bool isNumeric = !parts[1].empty() &&
                     std::all_of(parts[1].begin(), parts[1].end(), ::isdigit);
    if (isNumeric) {
      result.name = parts[0];
      try {
        result.timeoutMs = std::stoi(parts[1]);
      } catch (...) {
      }
    } else {
      result.type = parts[0];
      result.name = parts[1];
    }
  } else if (parts.size() >= 3) {
    // (Type, Name, Timeout)
    result.type = parts[0];
    result.name = parts[1];
    try {
      result.timeoutMs = std::stoi(parts[2]);
    } catch (...) {
    }
  }
  return result;
}

// ── dispatchOnHandle
// ──────────────────────────────────────────────────────────
bool UIADebugger::dispatchOnHandle(UIHandle &h, std::string_view methodToken) {
  auto paren = methodToken.find('(');
  if (paren == std::string_view::npos) {
    std::print("[parse] Expected '(' in: {}\n", methodToken);
    return false;
  }
  std::string method(methodToken.substr(0, paren));
  // Normalize method to Pascal Case
  if (!method.empty()) {
    if (method == "click")
      method = "Click";
    else if (method == "type")
      method = "Type";
    else if (method == "key")
      method = "Key";
    else if (method == "wait")
      method = "Wait";
    else if (method == "select")
      method = "Select";
    else if (method == "waitselect")
      method = "WaitSelect";
    else if (method == "refresh")
      method = "Refresh";
    else if (method == "parent")
      method = "Parent";
    else if (method == "window")
      method = "Window";
  }

  std::string argStr(methodToken.substr(paren + 1));
  if (!argStr.empty() && argStr.back() == ')')
    argStr.pop_back();
  std::string arg = stripQuotes(argStr);

  if (method == "Click") {
    if (arg.empty()) {
      std::print("Clicking [{}] '{}'...\n", h.element().controlType,
                 h.element().name);
      h.click();
    } else {
      auto args = parseCommandArgs("(" + argStr + ")");
      if (!args.type.empty()) {
        std::print("Clicking {} '{}' within [{}] '{}'...\n", args.type,
                   args.name, h.element().controlType, h.element().name);
        h.click(args.name, args.type);
      } else {
        std::print("Clicking '{}' within [{}] '{}'...\n", args.name,
                   h.element().controlType, h.element().name);
        h.click(args.name);
      }
    }
  } else if (method == "WaitClick") {
    auto args = parseCommandArgs("(" + argStr + ")");
    if (args.timeoutMs == 0)
      args.timeoutMs = 5000;
    std::print("Waiting to click {} '{}' inside '{}' (timeout {}ms)...\n",
               args.type.empty() ? "element" : args.type, args.name,
               h.element().name, args.timeoutMs);
    h.waitClick(args.name, args.timeoutMs, args.type);
  } else if (method == "Type") {
    std::print("Typing '{}' into [{}] '{}'...\n", arg, h.element().controlType,
               h.element().name);
    h.type(arg);
  } else if (method == "Key") {
    std::print("Pressing keys '{}'...\n", arg);
    h.key(arg);
  } else if (method == "Wait") {
    int ms = 0;
    try {
      ms = std::stoi(arg);
    } catch (...) {
    }
    std::print("Waiting {}ms...\n", ms);
    h.wait(ms);
  } else if (method == "Select" || method == "WaitSelect") {
    auto args = parseCommandArgs("(" + argStr + ")");
    std::print(
        "Selecting {} '{}' inside '{}'{}...\n",
        args.type.empty() ? "element" : args.type, args.name, h.element().name,
        args.timeoutMs > 0 ? std::format(" (timeout {}ms)", args.timeoutMs)
                           : "");
    try {
      UIHandle next = h.select(args.name, args.timeoutMs, args.type);
      h = std::move(next);
      m_lastResult = h;
      std::print("Selected: [{}] '{}'\n", h.element().controlType,
                 h.element().name);
    } catch (const std::exception &e) {
      WINBOT_ERROR("{}", e.what());
      return false;
    }
  } else if (method == "Refresh") {
    std::print("Refreshing state for '{}'...\n", h.element().name);
    if (h.refresh()) {
      std::print("Refreshed: [{}] '{}'\n", h.element().controlType,
                 h.element().name);
      m_lastResult = h;
    } else {
      WINBOT_ERROR("Refresh failed for '{}'", h.element().name);
      return false;
    }
  } else if (method == "Parent") {
    try {
      UIHandle next = h.parent();
      h = std::move(next);
      m_lastResult = h;
      std::print("Moved to Parent: [{}] '{}'\n", h.element().controlType,
                 h.element().name);
    } catch (const std::exception &e) {
      WINBOT_ERROR("Parent failed: {}", e.what());
      return false;
    }
  } else if (method == "Window") {
    try {
      UIHandle next = h.window();
      h = std::move(next);
      m_lastResult = h;
      std::print("Moved to Window: [{}] '{}'\n", h.element().controlType,
                 h.element().name);
    } catch (const std::exception &e) {
      WINBOT_ERROR("Window failed: {}", e.what());
      return false;
    }
  } else if (method == "Scan") {
    std::string search;
    if (paren != std::string_view::npos) {
      std::string argPart(methodToken.substr(paren + 1));
      search = stripQuotes(argPart);
      if (!search.empty() && search.back() == ')')
        search.pop_back();
      search = stripQuotes(search); // Handle double quotes if they exist
    }

    if (search.empty()) {
      std::print("Subtree of [{}] '{}':\n", h.element().controlType,
                 h.element().name);
      std::print("{}\n", UIAutomationScanner::serialize(h.element(), 1));
    } else {
      if (const UIElement *best = h.element().findBestMatch(search)) {
        std::print("Match '{}' within [{}]:\n", search, h.element().name);
        std::print("{}\n", UIAutomationScanner::serialize(*best, 1));
      } else {
        WINBOT_ERROR("Element matching '{}' not found within '{}'", search,
                     h.element().name);
      }
    }
  } else {
    std::print("Unknown method: {}\n", method);
    return false;
  }
  return true;
}

// ── execSegment
// ───────────────────────────────────────────────────────────────
bool UIADebugger::execSegment(std::string_view seg) {
  // Trim whitespace
  auto s = seg.find_first_not_of(" \t");
  auto e = seg.find_last_not_of(" \t");
  if (s == std::string_view::npos)
    return true;
  std::string cmd(seg.substr(s, e - s + 1));

  if (cmd == "exit" || cmd == "quit")
    return false;

  // ── Variable assignment: $var = expr ──────────────────────────────────
  std::string varName;
  if (cmd.front() == '$') {
    auto eq = cmd.find('=');
    if (eq != std::string::npos) {
      varName = cmd.substr(0, eq);
      while (!varName.empty() && varName.back() == ' ')
        varName.pop_back();
      cmd = cmd.substr(eq + 1);
      while (!cmd.empty() && cmd.front() == ' ')
        cmd.erase(cmd.begin());
    }
  }

  // ── Split cmd into dot-chained tokens (respecting quoted strings & parens)
  // ──
  std::vector<std::string> tokens;
  {
    std::string cur;
    bool inQuote = false;
    int parenDepth = 0;
    bool hadDanglingDot = false;
    for (size_t i = 0; i < cmd.size(); ++i) {
      char c = cmd[i];
      if (c == '\\' && i + 1 < cmd.size() && cmd[i + 1] == '"') {
        cur += "\\\"";
        ++i;
        continue;
      }
      if (c == '"')
        inQuote = !inQuote;
      if (!inQuote) {
        if (c == '(')
          parenDepth++;
        else if (c == ')')
          parenDepth--;
      }
      if (c == '.' && !inQuote && parenDepth == 0) {
        auto trimmed = cur;
        auto sPos = trimmed.find_first_not_of(" \t");
        if (sPos == std::string::npos) {
          hadDanglingDot = true;
          break;
        }
        tokens.push_back(trimmed.substr(sPos, trimmed.find_last_not_of(" \t") - sPos + 1));
        cur.clear();
      } else {
        cur += c;
      }
    }
    if (hadDanglingDot) {
      WINBOT_ERROR("Syntax error: invalid or dangling dot in '{}'", cmd);
      return false;
    }
    auto trimmed = cur;
    auto sPos = trimmed.find_first_not_of(" \t");
    if (sPos != std::string::npos) {
      tokens.push_back(trimmed.substr(sPos, trimmed.find_last_not_of(" \t") - sPos + 1));
    } else if (!tokens.empty() && cmd.find_last_not_of(" \t") != std::string::npos && cmd[cmd.find_last_not_of(" \t")] == '.') {
      WINBOT_ERROR("Syntax error: trailing dot in '{}'", cmd);
      return false;
    }
  }
  if (tokens.empty())
    return true;

  // ── Resolve the first token ───────────────────────────────────────────
  std::string first = tokens[0];
  while (!first.empty() && first.back() == ' ')
    first.pop_back();

  // Extract method and argument from the first token
  std::string method = first;
  std::string arg;
  auto paren = first.find('(');
  if (paren != std::string::npos) {
    method = first.substr(0, paren);
    // Trim method name
    auto mEnd = method.find_last_not_of(" \t\n\r");
    if (mEnd != std::string::npos)
      method = method.substr(0, mEnd + 1);

    arg = first.substr(paren + 1);
    // Trim trailing space before popping ')'
    auto aEnd = arg.find_last_not_of(" \t\n\r");
    if (aEnd != std::string::npos) {
      arg = arg.substr(0, aEnd + 1);
      if (!arg.empty() && arg.back() == ')') {
        arg.pop_back();
        // Trim again after popping ')'
        auto aEnd2 = arg.find_last_not_of(" \t\n\r");
        if (aEnd2 != std::string::npos)
          arg = arg.substr(0, aEnd2 + 1);
        else
          arg.clear();
      }
    }
  } else {
    // Fallback for legacy space-separated commands...
    auto space = first.find(' ');
    if (space != std::string::npos) {
      method = first.substr(0, space);
      arg = first.substr(space + 1);
    }
  }

  // Normalize method to Pascal Case for internal matching (while allowing
  // lowercase input)
  if (!method.empty()) {
    if (method == "scan")
      method = "Scan";
    else if (method == "wait")
      method = "Wait";
    else if (method == "type")
      method = "Type";
    else if (method == "key")
      method = "Key";
    else if (method == "focus")
      method = "Focus";
    else if (method == "clear")
      method = "Clear";
    else if (method == "vars" || method == "list")
      method = "Vars";
    else if (method == "select")
      method = "Select";
    else if (method == "selectwindow")
      method = "SelectWindow";
    else if (method == "waitselect")
      method = "WaitSelect";
    else if (method == "waitwindow")
      method = "WaitWindow";
    else if (method == "waitclick")
      method = "WaitClick";
    else if (method == "click")
      method = "Click";
    else if (method == "scanfocus" || method == "scanfocused")
      method = "ScanFocus";
    else if (method == "exit" || method == "quit")
      method = "Exit";
  }

  if (method == "Exit")
    return false;

  std::optional<UIHandle> handle;

  // $var reference (without =, already handled above)
  if (first.front() == '$') {
    auto it = m_vars.find(first);
    if (it == m_vars.end()) {
      WINBOT_ERROR("Variable '{}' not defined", first);
      return true;
    }
    handle = it->second;
  }
  // ── Root Commands (standalone or starting a chain) ────────────────────
  else if (method == "Scan") {
    std::string search = stripQuotes(arg);
    if (search.empty()) {
      auto res = m_uia.scanDesktop();
      if (res)
        std::print("{}\n", UIAutomationScanner::serialize(*res));
      else
        WINBOT_ERROR("Global scan failed: {}", res.error());
    } else {
      // Use findBestMatch to ensure the most relevant element
      // (like a Window) is picked if multiple objects match the name.
      if (auto tree = m_uia.scanDesktop(); tree) {
        if (const UIElement *best = tree->findBestMatch(search)) {
          std::print("{}\n", UIAutomationScanner::serialize(*best));
        } else {
          WINBOT_ERROR("Element matching '{}' not found in global search",
                       search);
        }
      } else {
        WINBOT_ERROR("Global desktop scan failed before searching.");
      }
    }
    return true;
  } else if (method == "ScanFocus") {
    auto res = m_uia.scanFocusedWindow();
    if (res)
      std::print("{}\n", UIAutomationScanner::serialize(*res));
    else
      WINBOT_ERROR("Scan failed: {}", res.error());
    return true;
  } else if (method == "Wait") {
    int ms = 0;
    try {
      ms = std::stoi(stripQuotes(arg));
    } catch (...) {
    }
    if (ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(ms));
      std::print("Waited {} ms\n", ms);
    } else
      std::print("Usage: Wait(ms)\n");
    return true;
  } else if (method == "Type") {
    auto text = stripQuotes(arg);
    if (auto res = tools::typeText(text); !res)
      WINBOT_ERROR("Type failed: {}", res.error());
    return true;
  } else if (method == "Key") {
    auto combo = stripQuotes(arg);
    if (auto res = tools::keyPress(combo); !res)
      WINBOT_ERROR("Key failed: {}", res.error());
    return true;
  } else if (method == "Focus") {
    auto title = stripQuotes(arg);
    if (auto res = tools::focusWindow(title); !res)
      WINBOT_ERROR("Focus failed: {}", res.error());
    return true;
  } else if (method == "Clear") {
    std::string sub = stripQuotes(arg);
    if (sub == "All") {
      m_vars.clear();
      m_lastResult.reset();
      std::print("All variables cleared.\n");
    } else if (!sub.empty() && sub.front() == '$') {
      m_vars.erase(sub);
      std::print("Cleared {}.\n", sub);
    } else {
      std::print("Usage: Clear(All) | Clear($var)\n");
    }
    return true;
  } else if (method == "Vars") {
    std::print("Defined variables:\n");
    for (const auto &[k, v] : m_vars)
      std::print("  {} = [{}] '{}'\n", k, v.element().controlType,
                 v.element().name);
    if (m_vars.empty())
      std::print("  (none)\n");
    return true;
  }
  // ── Select / WaitSelect (global scope — produces a handle) ────────────
  else if (method == "Select" || method == "WaitSelect") {
    auto args = parseCommandArgs(
        paren != std::string::npos ? first.substr(paren) : "(" + arg + ")");
    std::print("Selecting {} '{}'{}...\n",
               args.type.empty() ? "element" : args.type, args.name,
               args.timeoutMs > 0
                   ? std::format(" (timeout {}ms)", args.timeoutMs)
                   : "");
    auto res = m_uia.select(args.name, args.timeoutMs, args.type);
    if (!res) {
      WINBOT_ERROR("{}", res.error());
      return true;
    }
    handle = std::move(*res);
    m_lastResult = handle;
    std::print("Selected: [{}] '{}'\n", handle->element().controlType,
               handle->element().name);
  }
  // ── WaitWindow / SelectWindow (global scope) ─────────────────────────
  else if (method == "WaitWindow" || method == "SelectWindow") {
    auto args = parseCommandArgs(
        paren != std::string::npos ? first.substr(paren) : "(" + arg + ")");
    if (args.timeoutMs == 0 && method == "WaitWindow")
      args.timeoutMs = 5000; // Default timeout

    if (args.timeoutMs > 0)
      std::print("Waiting for window '{}' (timeout {}ms)...\n", args.name,
                 args.timeoutMs);
    else
      std::print("Selecting window '{}'...\n", args.name);

    auto res = m_uia.waitForWindow(args.name, args.timeoutMs);
    if (!res) {
      WINBOT_ERROR("{}", res.error());
      return true;
    }
    handle = std::move(*res);
    m_lastResult = handle;
    std::print("Window Found: [{}] '{}'\n", handle->element().controlType,
               handle->element().name);
  }
  // ── WaitClick (global scope) ──────────────────────────────────────────
  else if (method == "WaitClick") {
    auto args = parseCommandArgs(
        paren != std::string::npos ? first.substr(paren) : "(" + arg + ")");
    if (args.timeoutMs == 0)
      args.timeoutMs = 5000; // Default timeout
    std::print("Waiting to click {} '{}' (timeout {}ms)...\n",
               args.type.empty() ? "element" : args.type, args.name,
               args.timeoutMs);
    auto res = m_uia.select(args.name, args.timeoutMs, args.type);
    if (!res) {
      WINBOT_ERROR("{}", res.error());
      return true;
    }
    res->click();
    handle = std::move(*res);
    m_lastResult = handle;
  }
  // ── Click (global scope) ──────────────────────────────────────────────
  else if (method == "Click") {
    std::string clickArg = stripQuotes(arg);
    int x = 0, y = 0;
    if (!clickArg.empty() && sscanf_s(clickArg.c_str(), "%d %d", &x, &y) == 2) {
      auto res = tools::click(x, y);
      if (!res)
        WINBOT_ERROR("Click failed: {}", res.error());
    } else if (!clickArg.empty()) {
      auto args = parseCommandArgs(
          paren != std::string::npos ? first.substr(paren) : "(" + arg + ")");
      auto res = m_uia.select(args.name, 0, args.type);
      if (res) {
        if (!args.type.empty())
          std::print("Clicking {} '{}'...\n", args.type, args.name);
        else
          std::print("Clicking '{}'...\n", args.name);
        res->click();
      } else
        WINBOT_ERROR("{}", res.error());
    } else {
      std::print("Usage: Click(\"name\") or Click(x, y)\n");
    }
    return true;
  } else {
    std::print("Unknown command: {}\n", method);
    std::print("Commands: Scan(name?), Wait(ms), Type(text), Key(combo), "
               "Focus(title), SelectWindow(title), WaitWindow(title, ms?), "
               "WaitClick(name, ms?), Select(name, ms?), Click(name), "
               "$var = Select(...), $var.Click(...), Clear(All|$var), Vars(), "
               "Exit()\n");
    return true;
  }

  // ── Execute dot-chain on the resolved handle ──────────────────────────
  if (handle.has_value()) {
    for (size_t i = 1; i < tokens.size(); ++i) {
      if (!dispatchOnHandle(*handle, tokens[i]))
        break;
    }
    m_lastResult = *handle;
    if (!varName.empty()) {
      m_vars.insert_or_assign(varName, *handle);
      std::print("Saved {} = [{}] '{}'\n", varName,
                 handle->element().controlType, handle->element().name);
    }
  }
  return true;
}

// ── execute
// ─────────────────────────────────────────────────────────────────
bool UIADebugger::execute(std::string_view line) {
  if (line.empty())
    return true;
  bool keepGoing = true;
  std::string lineStr(line);
  std::stringstream ss(lineStr);
  std::string seg;
  while (std::getline(ss, seg, ';') && keepGoing &&
         !KillSwitch::isTriggered()) {
    keepGoing = execSegment(seg);
  }
  return keepGoing;
}

// ── REPL loop
// ─────────────────────────────────────────────────────────────────
void UIADebugger::run() {
  std::print(
      ">>> UIA Debugger Ready.\n"
      ">>> Commands: SelectWindow(title), WaitWindow(title), WaitClick(name), "
      "Select(name),\n"
      ">>>           $var=SelectWindow(...), $var.Click(One), Scan(name?),\n"
      ">>>           Wait(ms), Type(text), Key(combo), Focus(title),\n"
      ">>>           Clear(All|$var), Vars(), Exit()\n");

  std::string line;
  while (!KillSwitch::isTriggered()) {
    std::print(">>> ");
    std::cout.flush();
    if (!std::getline(std::cin, line))
      break;
    if (line.empty())
      continue;

    if (!execute(line))
      break;
  }
}
