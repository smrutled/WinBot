# WinBot Performance Benchmarks & Baseline

This document records the baseline performance metrics for WinBot's core algorithms, synthetic UI tree traversal, scoring, serialization, and command parsing.

---

## 1. Test Environment & Configuration

- **Date Recorded**: September 24, 2026
- **Architecture**: x64 (`/std:c++latest`)
- **Compiler**: MSVC 19.51 (Visual Studio 2026 / 2022 v18/v17 toolset)
- **Build Mode**: Debug with Inlining disabled (`/Ob0 /Od /RTC1`)
- **Test Binary**: `build/bin/WinBot_Tests.exe`
- **Benchmark Suite**: `UIAutomationBenchmarkSuite`

---

## 2. Baseline Measurements

All benchmarks run warm-up passes before timing loops and collect microsecond-level timing using `std::chrono::high_resolution_clock`.

| Benchmark Operation | Dataset / Tree Size | Iterations | Avg Latency | Baseline Throughput | Regression Threshold (+20% Latency) |
|:---|:---:|:---:|:---:|:---:|:---:|
| **FindBestMatch (Exact - Small)** | 10 nodes (Calculator) | 1,000 | **16.03 µs** | 62,046 ops/s | > 19.24 µs |
| **FindBestMatch (Exact - Medium)** | 500 nodes | 200 | **761.35 µs** | 1,313 ops/s | > 913.62 µs |
| **FindBestMatch (Exact - Large)** | 2,000 nodes | 50 | **3,049.86 µs** | 328 ops/s | > 3,659.83 µs |
| **FindBestMatch (Exact - 5K Nodes)** | 5,000 nodes | 20 | **7,931.77 µs** | 126 ops/s | > 9,518.12 µs |
| **FindBestMatch (Substring - Medium)** | 500 nodes | 100 | **784.61 µs** | 1,274 ops/s | > 941.53 µs |
| **FindBestMatch (Substring - Large)** | 2,000 nodes | 30 | **3,130.67 µs** | 319 ops/s | > 3,756.80 µs |
| **WorstCase Search (500 Nodes)** | 500 nodes (full scan miss) | 100 | **801.90 µs** | 1,246 ops/s | > 962.28 µs |
| **WorstCase Search (2,000 Nodes)** | 2,000 nodes (full scan miss) | 30 | **3,204.07 µs** | 312 ops/s | > 3,844.88 µs |
| **FindParent (50-Level Nesting)** | 50 levels deep | 1,000 | **1.57 µs** | 615,650 ops/s | > 1.88 µs |
| **Serialize Tree (Small - 10 Nodes)** | 10 nodes | 1,000 | **60.63 µs** | 16,457 ops/s | > 72.76 µs |
| **Serialize Tree (Medium - 500 Nodes)** | 500 nodes | 50 | **3,249.88 µs** | 308 ops/s | > 3,899.86 µs |
| **Parser: parseCommandArgs (3 args)** | Parameter string | 5,000 | **7.26 µs** | 136,454 ops/s | > 8.71 µs |
| **Scoped Handle Search: select('Plus')**| 10 nodes | 2,000 | **18.82 µs** | 52,866 ops/s | > 22.58 µs |
| **ScanDesktop (Cold Scan)** | Live Desktop | 1 | **~9.5 ms** | 105 ops/s | > 12.0 ms |
| **ScanDesktop (Warm Scan)** | Live Desktop | 1 | **~0.6 ms** | 1,600 ops/s | > 1.0 ms |

---

## 3. Performance Characteristics

1. **Tree Matching Complexity**:
   - `UIElement::findBestMatch` scales linearly ($O(N)$) with tree node count.
   - 10-node calculator controls resolve in **~16 µs**.
   - 500-node trees resolve in **~0.76 ms**.
   - 5,000-node massive trees resolve in **~7.93 ms**, staying well within real-time interactive thresholds (< 16 ms / 60 FPS frame time).
2. **Parent Resolution**:
   - RuntimeId vector matching executes in **1.57 µs** even at 50-level tree depth.
3. **Debugger Parsing**:
   - Lexing and argument splitting via `parseCommandArgs` achieves over **136,000 ops/second** (7.26 µs/call).

---

## 4. How to Run Benchmarks

To execute the benchmark suite and output the formatted performance report:

```powershell
# Run benchmark tests specifically:
.\build\bin\WinBot_Tests.exe --gtest_filter="*Benchmark*"

# Run all tests (unit + benchmarks + live GUI):
ctest --test-dir build -C Debug --output-on-failure
```

### Regression Guidelines
- **Warning Threshold**: Any change that increases average latency by more than **+15%** should be profiled.
- **Regression Threshold**: Any change that increases average latency by more than **+20%** or drops throughput below the threshold in the table above is considered a performance regression and must be investigated.
