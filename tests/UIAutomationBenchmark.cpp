#include <gtest/gtest.h>
#include "TestHelpers.h"
#include "UIAutomationScanner.h"
#include "UIADebugger.h"

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

void printBenchmarkReport(const std::vector<BenchmarkResult>& results) {
    std::print("\n========================================================================================\n");
    std::print("                              UI AUTOMATION BENCHMARK REPORT                            \n");
    std::print("========================================================================================\n");
    std::print("{:<38} | {:>9} | {:>7} | {:>10} | {:>12}\n",
               "Benchmark Name", "Tree Size", "Iters", "Avg Latency", "Throughput");
    std::print("---------------------------------------+-----------+---------+------------+-------------\n");

    for (const auto& r : results) {
        std::string latencyStr = std::format("{:.2f} us", r.avgUs);
        std::string throughputStr = std::format("{:.0f} ops/s", r.opsPerSec);
        std::print("{:<38} | {:>9} | {:>7} | {:>10} | {:>12}\n",
                   r.name, r.treeSize, r.iterations, latencyStr, throughputStr);
    }
    std::print("========================================================================================\n\n");
}

template <typename Func>
BenchmarkResult runBenchmark(const std::string& name, int treeSize, int iterations, Func&& fn) {
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
    double totalMs = std::chrono::duration<double, std::milli>(endTotal - startTotal).count();

    double sumUs = std::accumulate(runTimesUs.begin(), runTimesUs.end(), 0.0);
    double avgUs = sumUs / iterations;
    double minUs = *std::min_element(runTimesUs.begin(), runTimesUs.end());
    double maxUs = *std::max_element(runTimesUs.begin(), runTimesUs.end());
    double opsPerSec = (totalMs > 0.0) ? (iterations / (totalMs / 1000.0)) : 0.0;

    return BenchmarkResult{
        .name = name,
        .treeSize = treeSize,
        .iterations = iterations,
        .totalMs = totalMs,
        .avgUs = avgUs,
        .minUs = minUs,
        .maxUs = maxUs,
        .opsPerSec = opsPerSec
    };
}

template <typename T>
inline void doNotOptimize(T const& value) {
    volatile const void* p = static_cast<const void*>(&value);
    (void)p;
}

} // namespace

TEST(UIAutomationBenchmarkSuite, DISABLED_Benchmark_FindBestMatch_Exact) {
    std::vector<BenchmarkResult> report;

    // Small tree (Calculator, ~10 elements)
    UIElement calc = createCalculatorTree();
    report.push_back(runBenchmark("FindBestMatch (Exact - Small)", 10, 1000, [&]() {
        const UIElement* res = calc.findBestMatch("Plus");
        doNotOptimize(res);
    }));

    // Medium tree (500 elements)
    UIElement tree500 = createLargeTree(500);
    report.push_back(runBenchmark("FindBestMatch (Exact - Medium)", 500, 200, [&]() {
        const UIElement* res = tree500.findBestMatch("Element_250");
        doNotOptimize(res);
    }));

    // Large tree (2,000 elements)
    UIElement tree2000 = createLargeTree(2000);
    report.push_back(runBenchmark("FindBestMatch (Exact - Large)", 2000, 50, [&]() {
        const UIElement* res = tree2000.findBestMatch("Element_1500");
        doNotOptimize(res);
    }));

    // Very Large tree (5,000 elements)
    UIElement tree5000 = createLargeTree(5000);
    report.push_back(runBenchmark("FindBestMatch (Exact - 5K Nodes)", 5000, 20, [&]() {
        const UIElement* res = tree5000.findBestMatch("Element_4000");
        doNotOptimize(res);
    }));

    printBenchmarkReport(report);
}

TEST(UIAutomationBenchmarkSuite, DISABLED_Benchmark_FindBestMatch_Partial) {
    std::vector<BenchmarkResult> report;

    UIElement tree500 = createLargeTree(500);
    report.push_back(runBenchmark("FindBestMatch (Substring - Medium)", 500, 100, [&]() {
        const UIElement* res = tree500.findBestMatch("ment_25");
        doNotOptimize(res);
    }));

    UIElement tree2000 = createLargeTree(2000);
    report.push_back(runBenchmark("FindBestMatch (Substring - Large)", 2000, 30, [&]() {
        const UIElement* res = tree2000.findBestMatch("ment_15");
        doNotOptimize(res);
    }));

    printBenchmarkReport(report);
}

TEST(UIAutomationBenchmarkSuite, DISABLED_Benchmark_FindBestMatch_WorstCase) {
    std::vector<BenchmarkResult> report;

    // Element not found - traverses entire tree
    UIElement tree500 = createLargeTree(500);
    report.push_back(runBenchmark("WorstCase Search (500 Nodes)", 500, 100, [&]() {
        const UIElement* res = tree500.findBestMatch("NonExistentQuery_XYZ");
        doNotOptimize(res);
    }));

    UIElement tree2000 = createLargeTree(2000);
    report.push_back(runBenchmark("WorstCase Search (2000 Nodes)", 2000, 30, [&]() {
        const UIElement* res = tree2000.findBestMatch("NonExistentQuery_XYZ");
        doNotOptimize(res);
    }));

    printBenchmarkReport(report);
}

TEST(UIAutomationBenchmarkSuite, DISABLED_Benchmark_FindParent_DeepHierarchy) {
    std::vector<BenchmarkResult> report;

    UIElement deepTree = createDeepTree(50, "DeepTarget");
    std::vector<int> targetRId = {1, 50};

    report.push_back(runBenchmark("FindParent (50-Level Nesting)", 50, 1000, [&]() {
        const UIElement* p = deepTree.findParent(targetRId);
        doNotOptimize(p);
    }));

    printBenchmarkReport(report);
}

TEST(UIAutomationBenchmarkSuite, DISABLED_Benchmark_Serialize_Tree) {
    std::vector<BenchmarkResult> report;

    UIElement calc = createCalculatorTree();
    report.push_back(runBenchmark("Serialize Tree (Small - 10 Nodes)", 10, 1000, [&]() {
        std::string s = UIAutomationScanner::serialize(calc);
        doNotOptimize(s.size());
    }));

    UIElement tree500 = createLargeTree(500);
    report.push_back(runBenchmark("Serialize Tree (Medium - 500 Nodes)", 500, 50, [&]() {
        std::string s = UIAutomationScanner::serialize(tree500);
        doNotOptimize(s.size());
    }));

    printBenchmarkReport(report);
}

TEST(UIAutomationBenchmarkSuite, DISABLED_Benchmark_CommandParser_Throughput) {
    std::vector<BenchmarkResult> report;

    report.push_back(runBenchmark("Parser: parseCommandArgs (3 args)", 0, 5000, [&]() {
        auto args = UIADebugger::parseCommandArgs("(\"Button\", \"Submit Form\", 5000)");
        doNotOptimize(args.timeoutMs);
    }));

    UIElement calc = createCalculatorTree();
    UIHandle handle(calc, nullptr);

    report.push_back(runBenchmark("Scoped Handle Search: select('Plus')", 10, 2000, [&]() {
        UIHandle child = handle.select("Plus");
        doNotOptimize(child.element().bounds.left);
    }));

    printBenchmarkReport(report);
}
