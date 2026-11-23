#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace sidera {

enum class GPUMemorySubsystem : uint32_t {
    CoreRenderer = 0,
    GBuffer = 1,
    Shadows = 2,
    Terrain = 3,
    Vegetation = 4,
    VirtualTexturing = 5,
    PostProcessing = 6,
    Volumetrics = 7,
    GlobalIllumination = 8,
    Reflections = 9,
    SceneResources = 10,
    Streaming = 11
};

inline const char* gpuMemorySubsystemName(GPUMemorySubsystem s) noexcept {
    switch (s) {
        case GPUMemorySubsystem::CoreRenderer:       return "CoreRenderer";
        case GPUMemorySubsystem::GBuffer:            return "GBuffer";
        case GPUMemorySubsystem::Shadows:            return "Shadows";
        case GPUMemorySubsystem::Terrain:            return "Terrain";
        case GPUMemorySubsystem::Vegetation:         return "Vegetation";
        case GPUMemorySubsystem::VirtualTexturing:   return "VirtualTexturing";
        case GPUMemorySubsystem::PostProcessing:     return "PostProcessing";
        case GPUMemorySubsystem::Volumetrics:        return "Volumetrics";
        case GPUMemorySubsystem::GlobalIllumination: return "GlobalIllumination";
        case GPUMemorySubsystem::Reflections:        return "Reflections";
        case GPUMemorySubsystem::SceneResources:     return "SceneResources";
        case GPUMemorySubsystem::Streaming:          return "Streaming";
    }
    return "Unknown";
}

struct GPUProfileSample {
    std::string name;
    double durationMs = 0.0;
    bool valid = false;
};

struct CPUProfileSample {
    std::string name;
    double durationMs = 0.0;
    bool valid = false;
};

struct GPUMemoryRecord {
    std::string name;
    GPUMemorySubsystem subsystem = GPUMemorySubsystem::CoreRenderer;
    uint64_t estimatedBytes = 0;
    uint64_t actualAllocatedBytes = 0;
    bool persistent = true;
};

struct GPUMemoryStatistics {
    std::vector<GPUMemoryRecord> records;
    uint64_t totalEstimatedBytes = 0;
    uint64_t totalActualBytes = 0;

    void clear() noexcept {
        records.clear();
        totalEstimatedBytes = 0;
        totalActualBytes = 0;
    }

    void addRecord(const GPUMemoryRecord& r) {
        records.push_back(r);
        totalEstimatedBytes += r.estimatedBytes;
        totalActualBytes += r.actualAllocatedBytes;
    }

    [[nodiscard]] uint64_t bytesForSubsystem(GPUMemorySubsystem s) const noexcept {
        uint64_t sum = 0;
        for (const auto& r : records) {
            if (r.subsystem == s) {
                sum += (r.actualAllocatedBytes > 0) ? r.actualAllocatedBytes : r.estimatedBytes;
            }
        }
        return sum;
    }
};

struct TimingSummary {
    double avgMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    double medianMs = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    uint32_t sampleCount = 0;
};

inline TimingSummary computeTimingSummary(std::vector<double> samples) {
    TimingSummary s{};
    if (samples.empty()) return s;
    std::sort(samples.begin(), samples.end());
    s.sampleCount = static_cast<uint32_t>(samples.size());
    s.minMs = samples.front();
    s.maxMs = samples.back();
    double sum = 0.0;
    for (double v : samples) sum += v;
    s.avgMs = sum / static_cast<double>(samples.size());
    auto pct = [&](double p) -> double {
        const size_t idx = static_cast<size_t>(std::clamp(p * static_cast<double>(samples.size() - 1u), 0.0, static_cast<double>(samples.size() - 1u)));
        return samples[idx];
    };
    s.medianMs = pct(0.50);
    s.p95Ms = pct(0.95);
    s.p99Ms = pct(0.99);
    return s;
}

struct GPUDeviceProfile {
    std::string deviceName = "Unknown";
    uint32_t apiVersion = 0;
    uint32_t driverVersion = 0;
    float timestampPeriodNs = 1.0f;
    uint32_t timestampValidBits = 64;
    bool timestampsSupported = false;
};

struct GPUFrameStatistics {
    uint64_t frameIndex = 0;
    double totalGpuMs = 0.0;
    std::vector<GPUProfileSample> passes;
    bool valid = false;
};

struct CPUFrameStatistics {
    uint64_t frameIndex = 0;
    double totalCpuMs = 0.0;
    std::vector<CPUProfileSample> stages;
    bool valid = false;
};

enum class BenchmarkScenario : uint32_t {
    BasicScene = 0,
    VegetationTerrain = 1,
    LightingStress = 2,
    AdvancedRendering = 3,
    FullCombined = 4
};

inline const char* benchmarkScenarioName(BenchmarkScenario s) noexcept {
    switch (s) {
        case BenchmarkScenario::BasicScene:        return "BasicScene";
        case BenchmarkScenario::VegetationTerrain: return "VegetationTerrain";
        case BenchmarkScenario::LightingStress:    return "LightingStress";
        case BenchmarkScenario::AdvancedRendering: return "AdvancedRendering";
        case BenchmarkScenario::FullCombined:      return "FullCombined";
    }
    return "FullCombined";
}

struct BenchmarkConfig {
    bool enabled = false;
    BenchmarkScenario scenario = BenchmarkScenario::FullCombined;
    uint32_t warmupFrames = 5;
    uint32_t measuredFrames = 20;
    std::string outputDir = "benchmark_results";
    bool profilingEnabled = true;

    void validate() noexcept {
        warmupFrames = std::clamp(warmupFrames, 0u, 200u);
        measuredFrames = std::clamp(measuredFrames, 1u, 1000u);
        if (outputDir.empty()) outputDir = "benchmark_results";
    }
};

struct ImageComparisonResult {
    uint32_t width = 0;
    uint32_t height = 0;
    bool compatible = false;
    double meanAbsDiff = 0.0;
    double maxAbsDiff = 0.0;
    double mse = 0.0;
    double psnrDb = 0.0;
    uint64_t differingPixels = 0;
};

inline ImageComparisonResult compareRGBABuffers(const uint8_t* imgA, uint32_t wA, uint32_t hA,
                                                const uint8_t* imgB, uint32_t wB, uint32_t hB) noexcept {
    ImageComparisonResult res{};
    if (!imgA || !imgB || wA == 0 || hA == 0 || wA != wB || hA != hB) {
        res.compatible = false;
        return res;
    }
    res.compatible = true;
    res.width = wA;
    res.height = hA;
    const uint64_t pixelCount = static_cast<uint64_t>(wA) * hA;
    double absSum = 0.0;
    double sqSum = 0.0;
    for (uint64_t i = 0; i < pixelCount; ++i) {
        bool diffPix = false;
        for (uint32_t c = 0; c < 3; ++c) {
            const double d = std::abs(static_cast<double>(imgA[i * 4 + c]) - static_cast<double>(imgB[i * 4 + c]));
            if (d > 0.0) diffPix = true;
            absSum += d;
            sqSum += d * d;
            if (d > res.maxAbsDiff) res.maxAbsDiff = d;
        }
        if (diffPix) ++res.differingPixels;
    }
    const double channelCount = static_cast<double>(pixelCount * 3ull);
    res.meanAbsDiff = absSum / channelCount;
    res.mse = sqSum / channelCount;
    res.psnrDb = (res.mse > 1e-9) ? (10.0 * std::log10((255.0 * 255.0) / res.mse)) : 99.0;
    return res;
}

inline bool writeBenchmarkReportFiles(const std::string& outputDir,
                                      const GPUDeviceProfile& device,
                                      const BenchmarkConfig& cfg,
                                      const std::string& renderPathName,
                                      uint32_t width, uint32_t height,
                                      const TimingSummary& cpuSummary,
                                      const TimingSummary& gpuSummary,
                                      const std::vector<GPUFrameStatistics>& gpuFrames,
                                      const GPUMemoryStatistics& memStats) {
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);

    // 1. run_metadata.json
    {
        std::ofstream f(std::filesystem::path(outputDir) / "run_metadata.json");
        if (!f.is_open()) return false;
        f << std::fixed << std::setprecision(4)
          << "{\n"
          << "  \"gpu_device\": \"" << device.deviceName << "\",\n"
          << "  \"timestamps_supported\": " << (device.timestampsSupported ? "true" : "false") << ",\n"
          << "  \"scenario\": \"" << benchmarkScenarioName(cfg.scenario) << "\",\n"
          << "  \"render_path\": \"" << renderPathName << "\",\n"
          << "  \"resolution\": \"" << width << "x" << height << "\",\n"
          << "  \"warmup_frames\": " << cfg.warmupFrames << ",\n"
          << "  \"measured_frames\": " << cfg.measuredFrames << ",\n"
          << "  \"cpu_avg_ms\": " << cpuSummary.avgMs << ",\n"
          << "  \"cpu_p95_ms\": " << cpuSummary.p95Ms << ",\n"
          << "  \"gpu_avg_ms\": " << gpuSummary.avgMs << ",\n"
          << "  \"gpu_p95_ms\": " << gpuSummary.p95Ms << ",\n"
          << "  \"tracked_gpu_memory_bytes\": " << memStats.totalEstimatedBytes << "\n"
          << "}\n";
    }

    // 2. gpu_pass_timings.csv
    {
        std::ofstream f(std::filesystem::path(outputDir) / "gpu_pass_timings.csv");
        if (!f.is_open()) return false;
        f << "frame_index,pass_name,duration_ms,valid\n";
        for (const auto& gf : gpuFrames) {
            for (const auto& p : gf.passes) {
                f << gf.frameIndex << "," << p.name << "," << std::fixed << std::setprecision(4)
                  << p.durationMs << "," << (p.valid ? 1 : 0) << "\n";
            }
        }
    }

    return true;
}

} // namespace sidera
