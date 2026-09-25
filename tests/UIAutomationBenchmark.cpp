#include "TestHelpers.h"
#include "UIADebugger.h"
#include "UIAutomationScanner.h"
#include <gtest/gtest.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <print>
#include <string>
#include <vector>

namespace {

struct BenchmarkResult {
  std::string name;
  int treeSize;
  int iterations;
  double totalMs;
  double avgUs;
  double minUs;
  double maxUs;
  double opsPerSec;
};

void printBenchmarkReport(const std::vector<BenchmarkResult> &results) {
  std::print("\n==============================================================="
             "=========================\n");
  std::print("                              UI AUTOMATION BENCHMARK REPORT     "
             "                       \n");
  std::print("================================================================="
             "=======================\n");
  std::print("{:<38} | {:>9} | {:>7} | {:>10} | {:>12}\n", "Benchmark Name",
             "Tree Size", "Iters", "Avg Latency", "Throughput");
  std::print("---------------------------------------+-----------+---------+---"
             "---------+-------------\n");

  for (const auto &r : results) {
    std::string latencyStr = std::format("{:.2f} us", r.avgUs);
    std::string throughputStr = std::format("{:.0f} ops/s", r.opsPerSec);
    std::print("{:<38} | {:>9} | {:>7} | {:>10} | {:>12}\n", r.name, r.treeSize,
               r.iterations, latencyStr, throughputStr);
  }
  std::print("================================================================="
             "=======================\n\n");
}

template <typename Func>
BenchmarkResult runBenchmark(const std::string &name, int treeSize,
                             int iterations, Func &&fn) {
  // Warm-up iteration
  fn();

  std::vector<double> runTimesUs;
  runTimesUs.reserve(iterations);

  auto startTotal = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iterations; ++i) {
    auto t0 = std::chrono::high_resolution_clock::now();
    fn();
    auto t1 = std::chrono::high_resolution_clock::now();
    double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    runTimesUs.push_back(us);
  }
  auto endTotal = std::chrono::high_resolution_clock::now();
  double totalMs =
      std::chrono::duration<double, std::milli>(endTotal - startTotal).count();

  double sumUs = std::accumulate(runTimesUs.begin(), runTimesUs.end(), 0.0);
  double avgUs = sumUs / iterations;
  double minUs = *std::min_element(runTimesUs.begin(), runTimesUs.end());
  double maxUs = *std::max_element(runTimesUs.begin(), runTimesUs.end());
  double opsPerSec = (totalMs > 0.0) ? (iterations / (totalMs / 1000.0)) : 0.0;

  return BenchmarkResult{.name = name,
                         .treeSize = treeSize,
                         .iterations = iterations,
                         .totalMs = totalMs,
                         .avgUs = avgUs,
                         .minUs = minUs,
                         .maxUs = maxUs,
                         .opsPerSec = opsPerSec};
}

template <typename T> inline void doNotOptimize(T const &value) {
  volatile const void *p = static_cast<const void *>(&value);
  (void)p;
}

} // namespace

TEST(UIAutomationBenchmarkSuite, Benchmark_FindBestMatch_Exact) {
  std::vector<BenchmarkResult> report;

  // Small tree (Calculator, ~10 elements)
  UIElement calc = createCalculatorTree();
  report.push_back(
      runBenchmark("FindBestMatch (Exact - Small)", 10, 1000, [&]() {
        const UIElement *res = calc.findBestMatch("Plus");
        doNotOptimize(res);
      }));

  // Medium tree (500 elements)
  UIElement tree500 = createLargeTree(500);
  report.push_back(
      runBenchmark("FindBestMatch (Exact - Medium)", 500, 200, [&]() {
        const UIElement *res = tree500.findBestMatch("Element_250");
        doNotOptimize(res);
      }));

  // Large tree (2,000 elements)
  UIElement tree2000 = createLargeTree(2000);
  report.push_back(
      runBenchmark("FindBestMatch (Exact - Large)", 2000, 50, [&]() {
        const UIElement *res = tree2000.findBestMatch("Element_1500");
        doNotOptimize(res);
      }));

  // Very Large tree (5,000 elements)
  UIElement tree5000 = createLargeTree(5000);
  report.push_back(
      runBenchmark("FindBestMatch (Exact - 5K Nodes)", 5000, 20, [&]() {
        const UIElement *res = tree5000.findBestMatch("Element_4000");
        doNotOptimize(res);
      }));

  printBenchmarkReport(report);
}

TEST(UIAutomationBenchmarkSuite, Benchmark_FindBestMatch_Partial) {
  std::vector<BenchmarkResult> report;

  UIElement tree500 = createLargeTree(500);
  report.push_back(
      runBenchmark("FindBestMatch (Substring - Medium)", 500, 100, [&]() {
        const UIElement *res = tree500.findBestMatch("ment_25");
        doNotOptimize(res);
      }));

  UIElement tree2000 = createLargeTree(2000);
  report.push_back(
      runBenchmark("FindBestMatch (Substring - Large)", 2000, 30, [&]() {
        const UIElement *res = tree2000.findBestMatch("ment_15");
        doNotOptimize(res);
      }));

  printBenchmarkReport(report);
}

TEST(UIAutomationBenchmarkSuite, Benchmark_FindBestMatch_WorstCase) {
  std::vector<BenchmarkResult> report;

  // Element not found - traverses entire tree
  UIElement tree500 = createLargeTree(500);
  report.push_back(
      runBenchmark("WorstCase Search (500 Nodes)", 500, 100, [&]() {
        const UIElement *res = tree500.findBestMatch("NonExistentQuery_XYZ");
        doNotOptimize(res);
      }));

  UIElement tree2000 = createLargeTree(2000);
  report.push_back(
      runBenchmark("WorstCase Search (2000 Nodes)", 2000, 30, [&]() {
        const UIElement *res = tree2000.findBestMatch("NonExistentQuery_XYZ");
        doNotOptimize(res);
      }));

  printBenchmarkReport(report);
}

TEST(UIAutomationBenchmarkSuite, Benchmark_FindParent_DeepHierarchy) {
  std::vector<BenchmarkResult> report;

  UIElement deepTree = createDeepTree(50, "DeepTarget");
  std::vector<int> targetRId = {1, 50};

  report.push_back(
      runBenchmark("FindParent (50-Level Nesting)", 50, 1000, [&]() {
        const UIElement *p = deepTree.findParent(targetRId);
        doNotOptimize(p);
      }));

  printBenchmarkReport(report);
}

TEST(UIAutomationBenchmarkSuite, Benchmark_Serialize_Tree) {
  std::vector<BenchmarkResult> report;

  UIElement calc = createCalculatorTree();
  report.push_back(
      runBenchmark("Serialize Tree (Small - 10 Nodes)", 10, 1000, [&]() {
        std::string s = UIAutomationScanner::serialize(calc);
        doNotOptimize(s.size());
      }));

  UIElement tree500 = createLargeTree(500);
  report.push_back(
      runBenchmark("Serialize Tree (Medium - 500 Nodes)", 500, 50, [&]() {
        std::string s = UIAutomationScanner::serialize(tree500);
        doNotOptimize(s.size());
      }));

  printBenchmarkReport(report);
}

TEST(UIAutomationBenchmarkSuite, Benchmark_CommandParser_Throughput) {
  std::vector<BenchmarkResult> report;

  report.push_back(
      runBenchmark("Parser: parseCommandArgs (3 args)", 0, 5000, [&]() {
        auto args = UIADebugger::parseCommandArgs(
            "(\"Button\", \"Submit Form\", 5000)");
        doNotOptimize(args.timeoutMs);
      }));

  UIElement calc = createCalculatorTree();
  UIHandle handle(calc, nullptr);

  report.push_back(
      runBenchmark("Scoped Handle Search: select('Plus')", 10, 2000, [&]() {
        UIHandle child = handle.select("Plus");
        doNotOptimize(child.element().bounds.left);
      }));

  printBenchmarkReport(report);
}

TEST(UIAutomationBenchmarkSuite, Benchmark_Live_ScanDesktop_ColdVsWarm) {
  UIAutomationScanner scanner;

  auto countElements = [](auto& self, const UIElement& el) -> int {
    int count = 1;
    for (const auto& child : el.children) count += self(self, child);
    return count;
  };

  // ── 1. Cold Scan (First Pass) ─────────────────────────────────────────────
  // Initial traversal primes target processes (Chromium accessibility tree,
  // WPF/WinUI AutomationPeers) and establishes cross-process ALPC/RPC channels.
  auto t0 = std::chrono::high_resolution_clock::now();
  auto coldRes = scanner.scanDesktop();
  auto t1 = std::chrono::high_resolution_clock::now();

  ASSERT_TRUE(coldRes.has_value()) << "Cold scanDesktop failed: " << coldRes.error();
  double coldMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
  int coldCount = countElements(countElements, *coldRes);
  EXPECT_GT(coldCount, 0);

  // ── 2. Warm Scan (Second Pass) ────────────────────────────────────────────
  // Providers are already instantiated and RPC channels are warm.
  auto t2 = std::chrono::high_resolution_clock::now();
  auto warmRes = scanner.scanDesktop();
  auto t3 = std::chrono::high_resolution_clock::now();

  ASSERT_TRUE(warmRes.has_value()) << "Warm scanDesktop failed: " << warmRes.error();
  double warmMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
  int warmCount = countElements(countElements, *warmRes);
  EXPECT_GT(warmCount, 0);

  // ── 3. Warm Scan (Third Pass - Confirmation) ──────────────────────────────
  auto t4 = std::chrono::high_resolution_clock::now();
  auto warmRes2 = scanner.scanDesktop();
  auto t5 = std::chrono::high_resolution_clock::now();

  ASSERT_TRUE(warmRes2.has_value()) << "Second warm scanDesktop failed: " << warmRes2.error();
  double warmMs2 = std::chrono::duration<double, std::milli>(t5 - t4).count();

  double bestWarmMs = std::min(warmMs, warmMs2);
  double speedup = (bestWarmMs > 0.0) ? (coldMs / bestWarmMs) : 1.0;

  std::print("\n========================================================================================\n");
  std::print("                    LIVE DESKTOP SCAN: COLD VS. WARM BENCHMARK                          \n");
  std::print("========================================================================================\n");
  std::print("Pass 1 (Cold Scan)       : {:>8.2f} ms | {:>5} elements | {:>8.2f} us/node\n",
             coldMs, coldCount, (coldCount > 0 ? (coldMs * 1000.0 / coldCount) : 0.0));
  std::print("Pass 2 (Warm Scan)       : {:>8.2f} ms | {:>5} elements | {:>8.2f} us/node\n",
             warmMs, warmCount, (warmCount > 0 ? (warmMs * 1000.0 / warmCount) : 0.0));
  std::print("Pass 3 (Warm Scan 2)     : {:>8.2f} ms | {:>5} elements | {:>8.2f} us/node\n",
             warmMs2, warmCount, (warmCount > 0 ? (warmMs2 * 1000.0 / warmCount) : 0.0));
  std::print("----------------------------------------------------------------------------------------\n");
  std::print("Warm Execution Speedup   : {:>8.2f}x faster\n", speedup);
  std::print("========================================================================================\n\n");

  // Verify that the warmed-up scan runs faster than the cold scan
  EXPECT_LT(bestWarmMs, coldMs)
      << std::format("Expected warm scan ({:.2f} ms) to be faster than cold scan ({:.2f} ms)",
                     bestWarmMs, coldMs);
}

