#pragma once

#include <algorithm>
#include <cstdint>

namespace sidera {

struct StreamingConfig {
    bool enabled = true;

    // Distance Hysteresis
    // loadRadius: distance within which an Unloaded region is scheduled to load
    // preloadRadius: distance for speculative preloading
    // unloadRadius: distance beyond which a Resident/Active region is scheduled to unload
    // Invariant: unloadRadius > loadRadius + hysteresisMargin
    float loadRadius = 130.0f;
    float preloadRadius = 180.0f;
    float unloadRadius = 230.0f;
    float hysteresisMargin = 40.0f;

    // World Grid Dimensions
    float regionSize = 60.0f; // Width and length in meters per region
    uint32_t gridDimX = 4;    // 4x4 grid = 16 regions (240m x 240m)
    uint32_t gridDimZ = 4;

    // Worker Threads & Async Tasks
    uint32_t maxConcurrentCpuJobs = 2;

    // GPU Upload Throttling (Render Thread)
    uint64_t maxUploadBytesPerFrame = 16 * 1024 * 1024; // 16 MB max upload per frame
    uint32_t maxUploadOpsPerFrame = 4;                  // Max 4 region uploads per frame

    // Memory Budgets
    uint64_t gpuMemoryBudgetBytes = 256 * 1024 * 1024; // 256 MB streaming GPU budget
    uint64_t cpuMemoryBudgetBytes = 512 * 1024 * 1024; // 512 MB staging CPU budget

    // Priority & Prediction
    float velocityPredictionSeconds = 1.0f; // Lookahead time for camera velocity
    float frustumPriorityBonus = 50.0f;     // Priority bonus for regions inside view frustum

    void validate() noexcept {
        if (unloadRadius <= loadRadius + hysteresisMargin) {
            unloadRadius = loadRadius + std::max(10.0f, hysteresisMargin);
        }
        if (preloadRadius < loadRadius) {
            preloadRadius = loadRadius;
        }
        if (preloadRadius > unloadRadius) {
            preloadRadius = (loadRadius + unloadRadius) * 0.5f;
        }
        gridDimX = std::max(1u, gridDimX);
        gridDimZ = std::max(1u, gridDimZ);
        regionSize = std::max(10.0f, regionSize);
        maxConcurrentCpuJobs = std::clamp(maxConcurrentCpuJobs, 1u, 16u);
        maxUploadOpsPerFrame = std::max(1u, maxUploadOpsPerFrame);
    }
};

} // namespace sidera
