# WinBot

WinBot is an autonomous Windows AI Agent built entirely in modern C++ (C++23/26) that perceives your screen and acts on your behalf. Designed to be highly performant and secure, it leverages on-device local inferencing with [llama.cpp](https://github.com/ggerganov/llama.cpp) to run text and multimodal vision models completely offline.

## Features

- **Local AI Engine**: Powered by `llama.cpp` using HuggingFace models for both text and vision actions.
- **Multimodal Perception**:
  - **Vision Fallback**: Analyzes `Desktop Windows Manager (DWM)` visual screenshots whenever the UI tree is unreadable. 
  - **UI Automation (UIA)**: Traverses Windows UI structures to detect specific interactive elements.
- **Deep Integration Tools**: Contains system tools for executing commands via PowerShell (`run_command`), managing the clipboard, navigating the web (CDP + Chrome/Edge), and reading/writing local files.
- **Task Planning & Reflection**: Automatically delegates complicated instructions into step-by-step executions (Planning) and validates the result of every step (Reflection). 
- **Persisted Memory System**: Context stays active across usages; it natively utilizes AES-encrypted SQLite (`data/memory.db`) storage limits bounded by an auto-summarizing LRU cache context manager.
- **Uncompromised Security**: Integrates a "Kill-Switch" globally monitored over `WH_KEYBOARD_LL` (Default: `Ctrl+Alt+X`) and an explicit runtime permission structure blocking out-of-scope system interactions.

## Getting Started

### Prerequisites

You need the following installed:
- Active Windows 10/11 instance with Desktop UI.
- Visual Studio 2022+ with MSVC Desktop C++ Workload and Windows SDK.
- CMake (3.25+ recommended).

### Building

You can build WinBot entirely through CMake. The pipeline will automatically handle fetching missing libraries, `nlohmann-json`, `sqlite3`, and `llama.cpp` (including `mtmd`).

1. Configure the project:
   ```cmd
   cmake -S . -B build -G Ninja -DCMAKE_CXX_STANDARD=23
   ```
2. Build the executable:
   ```cmd
   cmake --build build --config Release
   ```

## Running an Example: "Use the Calculator App"

WinBot starts with a configuration parameter stored in `config.json` inside its base execution directory. Wait for it to pull down its core target model directly through the `llama.cpp` pipeline:

1. **Start the Agent** 
   From the command prompt, launch the bot:
   ```cmd
   cd build\bin\Release
   .\WinBot.exe
   ```

2. **Supply the Task**
   The agent will show a prompt. Type the following requirement:
   ```text
   >>> Open the calculator application and add 1500 and 3200, then read out the total result.
   ```

3. **Observe the Actions**  
   WinBot will parse the request, utilizing a subset of internal steps. You will notice its outputs inside the console:
   - **Plan**: `1. Open 'calc.exe'`, `2. Scan for Calculator Interface or click numbers visually`, `3. Retrieve outcome.`
   - **Actions**: Will utilize its process tools context window to call `run_command` to execute `calc.exe`.
   - **Perception**: Utilizing the `UIAutomationScanner`, WinBot locates "1", "5", "0", "0", "Add", "3", "2", "0", "0", "Equals" inside the UIA structure without using coordinate grids.
   - **Verification**: It reflects on the visible display pane in the Calculator and provides the user feedback on the sum.

## Safety and Security Operations

You are deeply protected through layered security. Before attempting file deletion, heavy `PowerShell` modifications, or arbitrary process termination:

- A Windows interactive console popup bounds WinBot actioning to explicitly require your (USER) confirmation.
- WinBot operates completely bound by `config.json` limits, specifically isolating directories out of bounding like `C:\Windows\System32`.
- **Global Kill Switch Mode**: Hit **`Ctrl` + `Alt` + `X`** at any time. WinBot will forcefully abort in-flight commands and exit safely.

## Dependencies

- **llama.cpp / mtmd** - Inference and LLM routing.
- **stb_image / stb_image_write** - Raw RGB pixel decoding for vision.
- **SQLite 3** - Persisted agent context engine.
- **Nlohmann JSON** - Structural memory configurations.
