#include "profiling/GPUProfileTypes.h"
#include "profiling/GPUProfiler.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

using namespace sidera;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { ++g_passed; } \
        else { ++g_failed; std::cerr << "[FAIL] " << msg << "\n"; } \
    } while (0)

static void testTimingSummaryAndConversion() {
    // 1. P50 / P95 / P99 aggregation
    std::vector<double> samples;
    for (int i = 1; i <= 100; ++i) samples.push_back(static_cast<double>(i));
    const TimingSummary s = computeTimingSummary(samples);
    CHECK(s.sampleCount == 100u, "Sample count == 100");
    CHECK(std::abs(s.minMs - 1.0) < 1e-6, "Min == 1.0");
    CHECK(std::abs(s.maxMs - 100.0) < 1e-6, "Max == 100.0");
    CHECK(std::abs(s.avgMs - 50.5) < 1e-6, "Avg == 50.5");
    CHECK(s.medianMs >= 49.0 && s.medianMs <= 51.0, "Median ~50");
    CHECK(s.p95Ms >= 94.0 && s.p95Ms <= 96.0, "P95 ~95");
    CHECK(s.p99Ms >= 98.0 && s.p99Ms <= 100.0, "P99 ~99");

    // 2. Vulkan timestamp tick-to-ms conversion & wraparound with validBits
    const double ms1 = GPUProfiler::ticksToMilliseconds(1000ull, 2001000ull, 1.0f, 64u);
    CHECK(std::abs(ms1 - 2.0) < 1e-5, "2,000,000 ns at 1ns/tick == 2.0 ms");
    // 32-bit wraparound test
    const uint64_t start32 = 0xFFFFFF00ull;
    const uint64_t end32 = 0x00000100ull; // +512 ticks across 32-bit boundary
    const double msWrap = GPUProfiler::ticksToMilliseconds(start32, end32, 1000.0f, 32u);
    CHECK(std::abs(msWrap - 0.512) < 1e-5, "32-bit timestamp wraparound handled accurately");
}

static void testCPUProfilerAndMemoryStats() {
    CPUProfiler cpu;
    cpu.beginFrame(1);
    cpu.beginStage("SceneUpdate");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    cpu.endStage("SceneUpdate");
    cpu.endFrame();

    CHECK(cpu.getLastFrame().valid, "CPU frame stats valid");
    CHECK(cpu.getLastFrame().totalCpuMs > 0.0, "CPU frame time > 0 ms");
    CHECK(cpu.getLastFrame().stages.size() == 1u && cpu.getLastFrame().stages[0].name == "SceneUpdate",
          "CPU stage recorded");

    GPUMemoryStatistics mem;
    mem.addRecord({"GBuffer", GPUMemorySubsystem::GBuffer, 32 * 1024 * 1024ull, 0, true});
    mem.addRecord({"Volumetric3D", GPUMemorySubsystem::Volumetrics, 16 * 1024 * 1024ull, 0, true});
    CHECK(mem.bytesForSubsystem(GPUMemorySubsystem::GBuffer) == 32 * 1024 * 1024ull, "GBuffer memory tracked");
    CHECK(mem.totalEstimatedBytes == 48 * 1024 * 1024ull, "Total estimated GPU memory summed");
}

static void testImageComparisonAndBenchmarkReports() {
    // 1. Identical images -> 0 diff, high PSNR
    std::vector<uint8_t> imgA(4 * 4 * 4, 128);
    std::vector<uint8_t> imgB = imgA;
    const auto eq = compareRGBABuffers(imgA.data(), 4, 4, imgB.data(), 4, 4);
    CHECK(eq.compatible && eq.meanAbsDiff == 0.0 && eq.differingPixels == 0u && eq.psnrDb >= 90.0,
          "Identical RGBA buffers yield 0 diff and high PSNR");

    // 2. Modified pixel -> detected
    imgB[0] = 228; // +100 on R of pixel 0
    const auto diff = compareRGBABuffers(imgA.data(), 4, 4, imgB.data(), 4, 4);
    CHECK(diff.compatible && diff.differingPixels == 1u && std::abs(diff.maxAbsDiff - 100.0) < 1e-6,
          "Modified pixel detected accurately");

    // 3. Incompatible dimensions rejected
    const auto bad = compareRGBABuffers(imgA.data(), 4, 4, imgB.data(), 2, 2);
    CHECK(!bad.compatible, "Mismatched image dimensions marked incompatible");

    // 4. JSON & CSV benchmark report export
    GPUProfiler gpu;
    CHECK(gpu.init(nullptr), "CPU-mode GPUProfiler::init succeeds");
    BenchmarkConfig cfg{};
    cfg.outputDir = "benchmark_results_unit_test";
    cfg.validate();
    std::vector<GPUFrameStatistics> frames;
    frames.push_back({1, 4.5, {{"Volumetrics_Froxel", 1.2, true}, {"GlobalIllumination_SSGI", 1.8, true}}, true});
    const bool wrote = writeBenchmarkReportFiles(
        cfg.outputDir, gpu.getDeviceProfile(), cfg, "Forward+", 1920, 1080,
        computeTimingSummary({2.1, 2.3}), computeTimingSummary({4.5}), frames, gpu.getMemoryStats());
    CHECK(wrote, "Benchmark JSON/CSV report written");
    CHECK(std::filesystem::exists(std::filesystem::path(cfg.outputDir) / "run_metadata.json"), "run_metadata.json exists");
    CHECK(std::filesystem::exists(std::filesystem::path(cfg.outputDir) / "gpu_pass_timings.csv"), "gpu_pass_timings.csv exists");

    std::error_code ec;
    std::filesystem::remove_all(cfg.outputDir, ec);
    gpu.destroy();
    CHECK(!gpu.isInitialized(), "GPUProfiler::destroy clears initialized flag");
}

int main() {
    testTimingSummaryAndConversion();
    testCPUProfilerAndMemoryStats();
    testImageComparisonAndBenchmarkReports();

    std::cout << "test_gpu_profiling: " << g_passed << " passed, " << g_failed << " failed.\n";
    return (g_failed == 0) ? 0 : 1;
}
