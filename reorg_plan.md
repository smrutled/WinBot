Here is a detailed, phased plan to reorganize the **WinBot** source tree, establish clean architectural boundaries, and make adding or updating tools straightforward.

### **1\. Target Directory Blueprint**

Plaintext  
WinBot/  
├── .clangd  
├── .gitignore  
├── .vscode/  
│   └── extensions.json  
├── CMakeLists.txt  
├── CMakePresets.json  
├── config.json  
├── launch-antigravity.bat  
├── BENCHMARKS.md  
├── README.md  
│  
├── src/  
│   ├── main.cpp  
│   ├── Common.h  
│   ├── Protocol.h  
│   │  
│   ├── core/                           \# Server, RPC protocol & tool dispatch  
│   │   ├── ITool.h  
│   │   ├── ToolRegistry.h / .cpp  
│   │   ├── ToolServer.h / .cpp  
│   │   └── McpServer.h / .cpp  
│   │  
│   ├── security/                       \# Guardrails, access controls & auditing  
│   │   ├── AuditLog.h / .cpp  
│   │   ├── KillSwitch.h / .cpp  
│   │   └── PermissionSystem.h / .cpp  
│   │  
│   ├── platform/                       \# OS engines & external automation drivers  
│   │   ├── uia/                        \# Windows UI Automation COM subsystem  
│   │   │   ├── UIAutomationScanner.h / .cpp  
│   │   │   ├── UIHandle.h / .cpp  
│   │   │   └── UIADebugger.h / .cpp  
│   │   ├── screen/                     \# GDI / DirectX capture subsystem  
│   │   │   └── ScreenCapture.h / .cpp  
│   │   └── browser/                    \# Browser automation driver (CDP / WebView2)  
│   │       └── BrowserAutomation.h / .cpp  
│   │  
│   ├── services/                       \# Background orchestration & local state  
│   │   ├── LuaRuntime.h / .cpp  
│   │   ├── Scheduler.h / .cpp  
│   │   └── SiteProfileRegistry.h / .cpp  
│   │  
│   └── tools/                          \# Pure ITool endpoints only  
│       ├── BrowserTools.h / .cpp  
│       ├── BuiltinTools.h / .cpp  
│       ├── FileTools.h / .cpp  
│       ├── InputTools.h / .cpp  
│       ├── ScreenTools.h / .cpp  
│       ├── ShellTools.h / .cpp  
│       └── WindowTools.h / .cpp  
│  
└── tests/  
    ├── TestHelpers.h  
    ├── unit/  
    │   ├── core/  
    │   │   └── ToolDecouplingTest.cpp  
    │   └── platform/  
    │       ├── UIAutomationScannerTest.cpp  
    │       ├── UIHandleTest.cpp  
    │       └── UIADebuggerTest.cpp  
    └── integration/  
        ├── LiveGUIIntegrationTest.cpp  
        └── UIAutomationBenchmark.cpp

### **2\. Comprehensive File Migration Mapping**

| Current Location | Target Location | Category | Primary Dependency Invariant |
| :---- | :---- | :---- | :---- |
| src/main.cpp | src/main.cpp | Root | Application entry point |
| src/Common.h | src/Common.h | Shared | Fundamental types, logging, constants |
| src/Protocol.h | src/Protocol.h | Shared | MCP / JSON-RPC protocol schemas |
| src/ITool.h | src/core/ITool.h | Core Interface | Base contract for all tools |
| src/ToolRegistry.\* | src/core/ToolRegistry.\* | Core Dispatch | Tool registration & discovery |
| src/ToolServer.\* | src/core/ToolServer.\* | Core Server | Internal tool execution runtime |
| src/McpServer.\* | src/core/McpServer.\* | Core Server | MCP transport & protocol handling |
| src/PermissionSystem.\* | src/security/PermissionSystem.\* | Security | Tool permission & scope gating |
| src/KillSwitch.\* | src/security/KillSwitch.\* | Security | Emergency halt & action revocation |
| src/AuditLog.\* | src/security/AuditLog.\* | Security | Action audit trail & logging |
| src/tools/UIAutomationScanner.\* | src/platform/uia/UIAutomationScanner.\* | Platform Driver | Pure UIA tree navigation |
| src/tools/UIHandle.\* | src/platform/uia/UIHandle.\* | Platform Driver | UIA element abstraction |
| src/UIADebugger.\* | src/platform/uia/UIADebugger.\* | Platform Driver | Tree inspection & diagnostic utilities |
| src/tools/ScreenCapture.\* | src/platform/screen/ScreenCapture.\* | Platform Driver | Desktop frame buffer acquisition |
| src/tools/BrowserAutomation.\* | src/platform/browser/BrowserAutomation.\* | Platform Driver | CDP/browser connection controller |
| src/LuaRuntime.\* | src/services/LuaRuntime.\* | Services | Scripting engine |
| src/Scheduler.\* | src/services/Scheduler.\* | Services | Periodic task scheduling |
| src/SiteProfileRegistry.\* | src/services/SiteProfileRegistry.\* | Services | Site-specific configuration store |
| src/tools/BrowserTools.\* | src/tools/BrowserTools.\* | Tool Endpoint | ITool wrapper over BrowserAutomation |
| src/tools/BuiltinTools.\* | src/tools/BuiltinTools.\* | Tool Endpoint | Built-in utilities (version, help, ping) |
| src/tools/FileTools.\* | src/tools/FileTools.\* | Tool Endpoint | File system tool commands |
| src/tools/InputTools.\* | src/tools/InputTools.\* | Tool Endpoint | Keyboard/mouse input tool commands |
| src/tools/ScreenTools.\* | src/tools/ScreenTools.\* | Tool Endpoint | ITool wrapper over ScreenCapture |
| src/tools/ShellTools.\* | src/tools/ShellTools.\* | Tool Endpoint | Shell execution tool commands |
| src/tools/WindowTools.\* | src/tools/WindowTools.\* | Tool Endpoint | Window management tool commands |

### **3\. Phased Execution Plan**

#### **Phase 1: Directory Setup and File Relocation**

> 1. Create the new subfolder structure:  
   * src/core/  
   * src/security/  
   * src/services/  
   * src/platform/uia/  
   * src/platform/screen/  
   * src/platform/browser/  
   * tests/unit/core/  
   * tests/unit/platform/  
   * tests/integration/  
> 2. Move files using git (git mv) to preserve commit history.

#### **Phase 2: Include Path Normalization & Architectural Invariants**

> 1. **Rule of Dependencies:**  
   * **src/platform/\*** must **never** include ITool.h, ToolServer.h, or any file from src/tools/. Platform subsystems are standalone C++ libraries that know nothing about MCP.  
   * **src/tools/\*** consumes src/core/ITool.h and platform drivers (e.g., \#include "platform/screen/ScreenCapture.h").  
   * **src/security/\*** remains independent of concrete tools, operating on tool metadata (e.g., tool names, risk levels).  
> 2. Update include statements across your headers and translation units to root-relative paths:  
>    C++  
>    // Before (scattered relative includes):  
>    \#**include** "../ToolServer.h"  
>    \#**include** "ScreenCapture.h"  
>    \#**include** "../UIADebugger.h"

>    // After (clean root-relative includes):  
>    \#**include** "core/ITool.h"  
>    \#**include** "platform/screen/ScreenCapture.h"  
>    \#**include** "platform/uia/UIADebugger.h"  
>    \#**include** "security/PermissionSystem.h"

#### **Phase 3: CMake Configuration Update**

Update CMakeLists.txt to reflect the new paths and set the root include directory:

CMake  
\# Target include directories allows clean "\#include \<folder\>/\<file\>.h" syntax  
target\_include\_directories(winbot PRIVATE  
    \${CMAKE\_CURRENT\_SOURCE\_DIR}/src  
)

\# Explicit source grouping (modular or unified)  
set(CORE\_SOURCES  
    src/core/ToolRegistry.cpp  
    src/core/ToolServer.cpp  
    src/core/McpServer.cpp  
)

set(SECURITY\_SOURCES  
    src/security/AuditLog.cpp  
    src/security/KillSwitch.cpp  
    src/security/PermissionSystem.cpp  
)

set(PLATFORM\_SOURCES  
    src/platform/uia/UIAutomationScanner.cpp  
    src/platform/uia/UIHandle.cpp  
    src/platform/uia/UIADebugger.cpp  
    src/platform/screen/ScreenCapture.cpp  
    src/platform/browser/BrowserAutomation.cpp  
)

set(SERVICES\_SOURCES  
    src/services/LuaRuntime.cpp  
    src/services/Scheduler.cpp  
    src/services/SiteProfileRegistry.cpp  
)

set(TOOL\_SOURCES  
    src/tools/BrowserTools.cpp  
    src/tools/BuiltinTools.cpp  
    src/tools/FileTools.cpp  
    src/tools/InputTools.cpp  
    src/tools/ScreenTools.cpp  
    src/tools/ShellTools.cpp  
    src/tools/WindowTools.cpp  
)

add\_executable(winbot  
    src/main.cpp  
    \${CORE\_SOURCES}  
    \${SECURITY\_SOURCES}  
    \${PLATFORM\_SOURCES}  
    \${SERVICES\_SOURCES}  
    \${TOOL\_SOURCES}  
)

*(Optional Enhancement)*: As the codebase expands, you can split these into static libraries (winbot\_platform, winbot\_security, winbot\_tools) so compiling changes in one tool does not re-link or re-check the entire platform layer.

#### **Phase 4: Self-Registering Tool Mechanism (Zero-Touch Updates)**

To make adding and modifying tools seamless without editing ToolServer or ToolRegistry every time, introduce a registration pattern in src/core/ITool.h:

C++  
// In src/core/ITool.h  
\#**include** \<string\>  
\#**include** \<functional\>  
\#**include** \<memory\>  
\#**include** \<nlohmann/json.hpp\>

class ITool {  
public:  
    virtual \~ITool() \= default;  
    virtual std::string name() const \= 0;  
    virtual std::string description() const \= 0;  
    virtual nlohmann::json schema() const \= 0;  
    virtual nlohmann::json execute(const nlohmann::json& params) \= 0;  
};

// Self-registration helper  
class ToolRegistrar {  
public:  
    using FactoryFunc \= std::function\<std::unique\_ptr\<ITool\>()\>;  
    ToolRegistrar(const std::string& name, FactoryFunc factory);  
};

\#**define** REGISTER\_TOOL(ToolClass) \\  
    static ToolRegistrar s\_registrar\_\#\#ToolClass( \\  
        \#ToolClass, \[\]() \-\> std::unique\_ptr\<ITool\> { return std::make\_unique\<ToolClass\>(); } \\  
    )

With this pattern, adding a tool requires only:

> 1. Implementing the ITool interface in src/tools/NewTool.h and NewTool.cpp.  
> 2. Calling REGISTER\_TOOL(NewTool); inside NewTool.cpp.  
> 3. Adding src/tools/NewTool.cpp to CMakeLists.txt.

Neither ToolServer.cpp nor McpServer.cpp needs to be edited.

#### **Phase 5: Test Suite Realignment**

> 1. Move tests into their categorized locations under tests/unit/ and tests/integration/.  
> 2. Update the test target includes in CMakeLists.txt:  
   * Unit tests for platform components (UIAutomationScannerTest.cpp, UIHandleTest.cpp, UIADebuggerTest.cpp) link against src/platform/uia/ without touching ToolServer.  
   * ToolDecouplingTest.cpp tests mock ITool instances registered with ToolRegistry.  
   * LiveGUIIntegrationTest.cpp exercises end-to-end execution.

### **4\. Step-by-Step Workflow for Future Updates**

| Scenario | Where to Work | Impact |
| :---- | :---- | :---- |
| **Add a new tool (e.g., ClipboardTools)** | Add ClipboardTools.h / .cpp to src/tools/ | Isolated to src/tools/. No platform or server files changed. |
| **Improve screen capture performance (e.g., DirectX/WGC)** | Update src/platform/screen/ScreenCapture.\* | Isolated to platform engine. ScreenTools interface stays unchanged. |
| **Upgrade Windows UIA heuristics or COM cache** | Update src/platform/uia/UIAutomationScanner.\* | Isolated to UIA subsystem. |
| **Adjust security permissions / approval prompts** | Update src/security/PermissionSystem.\* | Centralized security change. |

### **5\. Verification Checklist**

* [x] All files moved via git mv and tracked properly.  
* [x] No files in src/platform/ include src/core/ITool.h or anything from src/tools/.  
* [x] src/platform/uia/ contains all three related components: UIAutomationScanner, UIHandle, and UIADebugger.  
* [x] CMakeLists.txt builds cleanly with Ninja/MSVC preset using ${CMAKE_CURRENT_SOURCE_DIR}/src in include paths.  
* [x] All unit and decoupling tests pass without requiring UI Automation COM initialized unless explicitly testing UIA.