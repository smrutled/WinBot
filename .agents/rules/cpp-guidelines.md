# C++ Coding and Clang-Tidy Guidelines for WinBot

These guidelines ensure high code quality, memory safety, and seamless static analysis compliance across WinBot.

## Static Analysis Verification
- Clang-tidy runs during compilation with `WarningsAsErrors: '*'`.
- After modifying C++ files, verify changes by running:
  ```powershell
  cmake --build build --config Debug
  ```
- If running standalone `clang-tidy`, pass the build compilation database:
  ```powershell
  clang-tidy -p build src/path/to/File.cpp
  ```
- Note: Warnings regarding `/analyze:external-` and `/analyze:quiet` from the MSVC compiler driver can be safely ignored when invoking clang-tidy.

## Memory & Bounds Safety
1. **Arrays and Containers**:
   - Prefer structured bindings with `std::array` (e.g. `const auto& [x, y, z] = arr;`) over `.at()` or raw `[]` indexing when boundaries are fixed and known.
   - For variable-length data, use range-based `for` loops or bounds-checked containers.
   - Prefer `std::span` over raw pointer arithmetic when indexing command-line arguments (`argv`) or buffers.
2. **String Views in noexcept Functions**:
   - Never return `std::string` by value in a function marked `noexcept` unless it is guaranteed not to allocate. Constructing `std::string` can throw `std::bad_alloc`.
   - Prefer `std::string_view` or `const char*` for lookup tables or literal mappings (e.g. `controlTypeToString`).

## Exception Safety & bugprone Rules
1. **No-Throw Contracts (`bugprone-exception-escape`)**:
   - Functions marked `noexcept`, move constructors, destructors, `main()`, and thread pool task lambdas must not allow unhandled exceptions to escape.
   - Always enclose operations that can allocate (`std::string`, `std::istringstream`, JSON operations) within an internal `try/catch` guard when implementing `noexcept` functions or thread tasks.
   - For `main()`, wrap the entire body in a top-level `try/catch` handler, and use non-throwing functions like `std::fputs(..., stderr)` for fatal exit logging.
2. **Exception Handling (`bugprone-empty-catch`)**:
   - Never leave empty catch blocks (`catch (...) {}`).
   - Always log caught exceptions (`WINBOT_WARN`, `WINBOT_ERROR`) or set appropriate fallback state with an explicit explanatory comment.
3. **Multiplication Widening (`bugprone-implicit-widening-of-multiplication-result`)**:
   - When calculating byte offsets or buffer sizes for 64-bit values (`size_t`, `DWORDLONG`, `uint64_t`), write literal multipliers with explicit 64-bit suffixes (e.g., `1024ULL * 1024ULL` instead of `1024 * 1024`).

## Clang-Tidy Suppressions (NOLINT)
- Use `// NOLINTNEXTLINE(<rule>)` or `// NOLINTBEGIN(<rule>)` sparingly.
- **Every suppression MUST include a brief justification** explaining the technical rationale (e.g. `// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast) - Win32 EnumWindows LPARAM callback context`).
- Common valid suppressions include:
  - Win32 API callbacks passing pointer context via `LPARAM` (`LONG_PTR`).
  - Win32 structs with fixed C arrays decaying to pointers (e.g. `PROCESSENTRY32W::szExeFile`).
  - GoogleTest environment registration where ownership is transferred to the test framework.
