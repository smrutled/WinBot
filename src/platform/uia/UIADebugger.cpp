#include "platform/uia/UIADebugger.h"
#include "security/KillSwitch.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <iostream>
#include <print>
#include <sstream>
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
    if (s.at(i) == '\\' && i + 1 < s.size() && s.at(i + 1) == '"') {
      unescaped += '"';
      ++i;
    } else {
      unescaped += s.at(i);
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
      char c = inner.at(i);
      if (c == '\\' && i + 1 < inner.size() && inner.at(i + 1) == '"') {
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
    result.name = parts.at(0);
  } else if (parts.size() == 2) {
    // (Name, Timeout) OR (Type, Name)
    // If the second part is numeric, it's a timeout.
    bool isNumeric = !parts.at(1).empty() &&
                     std::all_of(parts.at(1).begin(), parts.at(1).end(), ::isdigit);
    if (isNumeric) {
      result.name = parts.at(0);
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      std::from_chars(parts.at(1).data(), parts.at(1).data() + parts.at(1).size(), result.timeoutMs);
    } else {
      result.type = parts.at(0);
      result.name = parts.at(1);
    }
  } else if (parts.size() >= 3) {
    // (Type, Name, Timeout)
    result.type = parts.at(0);
    result.name = parts.at(1);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    std::from_chars(parts.at(2).data(), parts.at(2).data() + parts.at(2).size(), result.timeoutMs);
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
    else if (method == "text" || method == "gettext")
      method = "Text";
    else if (method == "value" || method == "getvalue")
      method = "Value";
    else if (method == "info")
      method = "Info";
  }

  std::string argStr(methodToken.substr(paren + 1));
  if (!argStr.empty() && argStr.back() == ')')
    argStr.pop_back();
  std::string arg = stripQuotes(argStr);

  if (method == "Click") {
    if (arg.empty()) {
      emit("Clicking [{}] '{}'...\n", h.element().controlType,
           h.element().name);
      try {
        h.click();
        emit("Invoked [{}] '{}'.\n", h.element().controlType, h.element().name);
      } catch (const std::exception &e) {
        emitError("Click failed: {}", e.what());
        return false;
      }
    } else {
      auto args = parseCommandArgs("(" + argStr + ")");
      if (!args.type.empty()) {
        emit("Clicking {} '{}' within [{}] '{}'...\n", args.type,
             args.name, h.element().controlType, h.element().name);
        try {
          h.click(args.name, args.type);
          emit("Invoked {} '{}'.\n", args.type, args.name);
        } catch (const std::exception &e) {
          emitError("Click failed: {}", e.what());
          return false;
        }
      } else {
        emit("Clicking '{}' within [{}] '{}'...\n", args.name,
             h.element().controlType, h.element().name);
        try {
          h.click(args.name);
          emit("Invoked '{}'.\n", args.name);
        } catch (const std::exception &e) {
          emitError("Click failed: {}", e.what());
          return false;
        }
      }
    }
  } else if (method == "WaitClick") {
    auto args = parseCommandArgs("(" + argStr + ")");
    if (args.timeoutMs == 0)
      args.timeoutMs = 5000;
    emit("Waiting to click {} '{}' inside '{}' (timeout {}ms)...\n",
         args.type.empty() ? "element" : args.type, args.name,
         h.element().name, args.timeoutMs);
    try {
      h.waitClick(args.name, args.timeoutMs, args.type);
      emit("Invoked {} '{}'.\n", args.type.empty() ? "element" : args.type, args.name);
    } catch (const std::exception &e) {
      emitError("WaitClick failed: {}", e.what());
      return false;
    }
  } else if (method == "Type") {
    emit("Typing '{}' into [{}] '{}'...\n", arg, h.element().controlType,
         h.element().name);
    h.type(arg);
  } else if (method == "Key") {
    emit("Pressing keys '{}'...\n", arg);
    h.key(arg);
  } else if (method == "Wait") {
    int ms = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    std::from_chars(arg.data(), arg.data() + arg.size(), ms);
    emit("Waiting {}ms...\n", ms);
    h.wait(ms);
  } else if (method == "Select" || method == "WaitSelect") {
    auto args = parseCommandArgs("(" + argStr + ")");
    emit(
        "Selecting {} '{}' inside '{}'{}...\n",
        args.type.empty() ? "element" : args.type, args.name, h.element().name,
        args.timeoutMs > 0 ? std::format(" (timeout {}ms)", args.timeoutMs)
                           : "");
    try {
      UIHandle next = h.select(args.name, args.timeoutMs, args.type);
      h = std::move(next);
      m_lastResult = h;
      emit("Selected: [{}] '{}'\n", h.element().controlType,
           h.element().name);
    } catch (const std::exception &e) {
      emitError("{}", e.what());
      return false;
    }
  } else if (method == "Text" || method == "GetText") {
    if (!arg.empty()) {
      auto args = parseCommandArgs("(" + argStr + ")");
      try {
        UIHandle child = h.select(args.name, args.timeoutMs, args.type);
        std::string txt = child.element().value.empty() ? child.element().name : child.element().value;
        emit("Text of [{}] '{}': {}\n", child.element().controlType, child.element().name, txt);
      } catch (const std::exception &e) {
        emitError("{}", e.what());
        return false;
      }
    } else {
      std::string txt = h.element().value.empty() ? h.element().name : h.element().value;
      emit("Text of [{}] '{}': {}\n", h.element().controlType, h.element().name, txt);
    }
  } else if (method == "Value" || method == "GetValue") {
    if (!arg.empty()) {
      auto args = parseCommandArgs("(" + argStr + ")");
      try {
        UIHandle child = h.select(args.name, args.timeoutMs, args.type);
        emit("Value of [{}] '{}': {}\n", child.element().controlType, child.element().name, child.element().value);
      } catch (const std::exception &e) {
        emitError("{}", e.what());
        return false;
      }
    } else {
      emit("Value of [{}] '{}': {}\n", h.element().controlType, h.element().name, h.element().value);
    }
  } else if (method == "Info") {
    const auto &el = h.element();
    emit("[{}] '{}' id='{}' val='{}' at ({},{},{},{}) enabled={} focused={}\n",
         el.controlType, el.name, el.automationId, el.value,
         el.bounds.left, el.bounds.top, el.bounds.right, el.bounds.bottom,
         el.isEnabled, el.isFocused);
  } else if (method == "Refresh") {
    emit("Refreshing state for '{}'...\n", h.element().name);
    if (h.refresh()) {
      emit("Refreshed: [{}] '{}'\n", h.element().controlType,
           h.element().name);
      m_lastResult = h;
    } else {
      emitError("Refresh failed for '{}'", h.element().name);
      return false;
    }
  } else if (method == "Parent") {
    try {
      UIHandle next = h.parent();
      h = std::move(next);
      m_lastResult = h;
      emit("Moved to Parent: [{}] '{}'\n", h.element().controlType,
           h.element().name);
    } catch (const std::exception &e) {
      emitError("Parent failed: {}", e.what());
      return false;
    }
  } else if (method == "Window") {
    try {
      UIHandle next = h.window();
      h = std::move(next);
      m_lastResult = h;
      emit("Moved to Window: [{}] '{}'\n", h.element().controlType,
           h.element().name);
    } catch (const std::exception &e) {
      emitError("Window failed: {}", e.what());
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
      emit("Subtree of [{}] '{}':\n", h.element().controlType,
           h.element().name);
      emit("{}\n", UIAutomationScanner::serialize(h.element(), 1));
    } else {
      if (const UIElement *best = h.element().findBestMatch(search)) {
        emit("Match '{}' within [{}]:\n", search, h.element().name);
        emit("{}\n", UIAutomationScanner::serialize(*best, 1));
      } else {
        emitError("Element matching '{}' not found within '{}'", search,
                  h.element().name);
      }
    }
  } else {
    emit("Unknown method: {}\n", method);
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
      char c = cmd.at(i);
      if (c == '\\' && i + 1 < cmd.size() && cmd.at(i + 1) == '"') {
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
      emitError("Syntax error: invalid or dangling dot in '{}'", cmd);
      return false;
    }
    auto trimmed = cur;
    auto sPos = trimmed.find_first_not_of(" \t");
    if (sPos != std::string::npos) {
      tokens.push_back(trimmed.substr(sPos, trimmed.find_last_not_of(" \t") - sPos + 1));
    } else if (!tokens.empty() && cmd.find_last_not_of(" \t") != std::string::npos && cmd.at(cmd.find_last_not_of(" \t")) == '.') {
      emitError("Syntax error: trailing dot in '{}'", cmd);
      return false;
    }
  }
  if (tokens.empty())
    return true;

  // ── Resolve the first token ───────────────────────────────────────────
  std::string first = tokens.at(0);
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
    else if (method == "text" || method == "gettext")
      method = "Text";
    else if (method == "value" || method == "getvalue")
      method = "Value";
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
      emitError("Variable '{}' not defined", first);
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
        emit("{}\n", UIAutomationScanner::serialize(*res));
      else
        emitError("Global scan failed: {}", res.error());
    } else {
      // Use findBestMatch to ensure the most relevant element
      // (like a Window) is picked if multiple objects match the name.
      if (auto tree = m_uia.scanDesktop(); tree) {
        if (const UIElement *best = tree->findBestMatch(search)) {
          emit("{}\n", UIAutomationScanner::serialize(*best));
        } else {
          emitError("Element matching '{}' not found in global search",
                       search);
        }
      } else {
        emitError("Global desktop scan failed before searching.");
      }
    }
    return true;
  } else if (method == "ScanFocus") {
    auto res = m_uia.scanFocusedWindow();
    if (res)
      emit("{}\n", UIAutomationScanner::serialize(*res));
    else
      emitError("Scan failed: {}", res.error());
    return true;
  } else if (method == "Wait") {
    int ms = 0;
    auto stripped = stripQuotes(arg);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    std::from_chars(stripped.data(), stripped.data() + stripped.size(), ms);
    if (ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(ms));
      emit("Waited {} ms\n", ms);
    } else {
      emit("Usage: Wait(ms)\n");
    }
    return true;
  } else if (method == "Type") {
    auto text = stripQuotes(arg);
    if (auto res = UIHandle::sendTypeText(text); !res)
      emitError("Type failed: {}", res.error());
    else
      emit("Typed: {}\n", text);
    return true;
  } else if (method == "Key") {
    auto combo = stripQuotes(arg);
    if (auto res = UIHandle::sendKeyPress(combo); !res)
      emitError("Key failed: {}", res.error());
    else
      emit("Pressed key: {}\n", combo);
    return true;
  } else if (method == "Focus") {
    auto title = stripQuotes(arg);
    if (auto res = m_uia.waitForWindow(title, 2000); res) {
      if (res->element().ownerHwnd) {
        ::SetForegroundWindow(res->element().ownerHwnd);
        emit("Focused window: '{}'\n", title);
      }
    } else {
      emitError("Focus failed: {}", res.error());
    }
    return true;
  } else if (method == "Clear") {
    std::string sub = stripQuotes(arg);
    if (sub == "All") {
      m_vars.clear();
      m_lastResult.reset();
      emit("All variables cleared.\n");
    } else if (!sub.empty() && sub.front() == '$') {
      m_vars.erase(sub);
      emit("Cleared {}.\n", sub);
    } else {
      emit("Usage: Clear(All) | Clear($var)\n");
    }
    return true;
  } else if (method == "Vars") {
    emit("Defined variables:\n");
    for (const auto &[k, v] : m_vars)
      emit("  {} = [{}] '{}'\n", k, v.element().controlType,
                 v.element().name);
    if (m_vars.empty())
      emit("  (none)\n");
    return true;
  }
  // ── Select / WaitSelect (global scope — produces a handle) ────────────
  else if (method == "Select" || method == "WaitSelect") {
    auto args = parseCommandArgs(
        paren != std::string::npos ? first.substr(paren) : "(" + arg + ")");
    emit("Selecting {} '{}'{}...\n",
               args.type.empty() ? "element" : args.type, args.name,
               args.timeoutMs > 0
                   ? std::format(" (timeout {}ms)", args.timeoutMs)
                   : "");
    auto res = m_uia.select(args.name, args.timeoutMs, args.type);
    if (!res) {
      emitError("{}", res.error());
      return true;
    }
    handle = std::move(*res);
    m_lastResult = handle;
    emit("Selected: [{}] '{}'\n", handle->element().controlType,
               handle->element().name);
  }
  // ── WaitWindow / SelectWindow (global scope) ─────────────────────────
  else if (method == "WaitWindow" || method == "SelectWindow") {
    auto args = parseCommandArgs(
        paren != std::string::npos ? first.substr(paren) : "(" + arg + ")");
    if (args.timeoutMs == 0 && method == "WaitWindow")
      args.timeoutMs = 5000; // Default timeout

    if (args.timeoutMs > 0)
      emit("Waiting for window '{}' (timeout {}ms)...\n", args.name,
                 args.timeoutMs);
    else
      emit("Selecting window '{}'...\n", args.name);

    auto res = m_uia.waitForWindow(args.name, args.timeoutMs);
    if (!res) {
      emitError("{}", res.error());
      return true;
    }
    handle = std::move(*res);
    m_lastResult = handle;
    emit("Window Found: [{}] '{}'\n", handle->element().controlType,
               handle->element().name);
  }
  // ── WaitClick (global scope) ──────────────────────────────────────────
  else if (method == "WaitClick") {
    auto args = parseCommandArgs(
        paren != std::string::npos ? first.substr(paren) : "(" + arg + ")");
    if (args.timeoutMs == 0)
      args.timeoutMs = 5000; // Default timeout
    emit("Waiting to click {} '{}' (timeout {}ms)...\n",
               args.type.empty() ? "element" : args.type, args.name,
               args.timeoutMs);
    auto res = m_uia.select(args.name, args.timeoutMs, args.type);
    if (!res) {
      emitError("{}", res.error());
      return true;
    }
    try {
      res->click();
      emit("Invoked {} '{}'.\n", args.type.empty() ? "element" : args.type, args.name);
      handle = std::move(*res);
      m_lastResult = handle;
    } catch (const std::exception &e) {
      emitError("Click failed: {}", e.what());
    }
  }
  // ── Click (global scope) ──────────────────────────────────────────────
  else if (method == "Click") {
    std::string clickArg = stripQuotes(arg);
    int x = 0;
    int y = 0;
    std::istringstream iss(clickArg);
    if (!clickArg.empty() && (iss >> x >> y)) {
      auto res = UIHandle::clickAt(x, y);
      if (!res)
        emitError("Click failed: {}", res.error());
      else
        emit("Clicked at ({}, {}).\n", x, y);
    } else if (!clickArg.empty()) {
      auto args = parseCommandArgs(
          paren != std::string::npos ? first.substr(paren) : "(" + arg + ")");
      auto res = m_uia.select(args.name, 0, args.type);
      if (res) {
        if (!args.type.empty())
          emit("Clicking {} '{}'...\n", args.type, args.name);
        else
          emit("Clicking '{}'...\n", args.name);
        try {
          res->click();
          emit("Invoked {} '{}'.\n", args.type.empty() ? "element" : args.type, args.name);
          handle = std::move(*res);
          m_lastResult = handle;
        } catch (const std::exception &e) {
          emitError("Click failed: {}", e.what());
        }
      } else {
        emitError("{}", res.error());
      }
    } else {
      emit("Usage: Click(\"name\") or Click(x, y)\n");
    }
    return true;
  }
  // ── Text / GetText (global scope) ─────────────────────────────────────
  else if (method == "Text" || method == "GetText") {
    auto args = parseCommandArgs(
        paren != std::string::npos ? first.substr(paren) : "(" + arg + ")");
    auto res = m_uia.select(args.name, args.timeoutMs, args.type);
    if (!res) {
      emitError("{}", res.error());
      return true;
    }
    std::string txt = res->element().value.empty() ? res->element().name : res->element().value;
    emit("Text of [{}] '{}': {}\n", res->element().controlType, res->element().name, txt);
    handle = std::move(*res);
    m_lastResult = handle;
  } else {
    emit("Unknown command: {}\n", method);
    emit("Commands: Scan(name?), Wait(ms), Type(text), Key(combo), "
               "Focus(title), SelectWindow(title), WaitWindow(title, ms?), "
               "WaitClick(name, ms?), Select(name, ms?), Click(name), "
               "Text(name?), Value(name?), "
               "$var = Select(...), $var.Click(...), Clear(All|$var), Vars(), "
               "Exit()\n");
    return true;
  }

  // ── Execute dot-chain on the resolved handle ──────────────────────────
  if (handle.has_value()) {
    for (size_t i = 1; i < tokens.size(); ++i) {
      if (!dispatchOnHandle(*handle, tokens.at(i)))
        break;
    }
    m_lastResult = *handle;
    if (!varName.empty()) {
      m_vars.insert_or_assign(varName, *handle);
      emit("Saved {} = [{}] '{}'\n", varName,
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
  emit(
      ">>> UIA Debugger Ready.\n"
      ">>> Commands: SelectWindow(title), WaitWindow(title), WaitClick(name), "
      "Select(name),\n"
      ">>>           $var=SelectWindow(...), $var.Click(One), Scan(name?),\n"
      ">>>           Wait(ms), Type(text), Key(combo), Focus(title),\n"
      ">>>           Text(name?), Value(name?), Clear(All|$var), Vars(), Exit()\n");

  std::string line;
  while (!KillSwitch::isTriggered()) {
    emit(">>> ");
    std::cout.flush();
    if (!std::getline(std::cin, line))
      break;
    if (line.empty())
      continue;

    if (!execute(line))
      break;
  }
}
