#pragma once

#include <cstdint>

namespace sidera {

struct StreamingStats {
    uint32_t totalRegions = 0;
    uint32_t unloadedCount = 0;
    uint32_t queuedLoadCount = 0;
    uint32_t loadingCount = 0;
    uint32_t cpuReadyCount = 0;
    uint32_t uploadQueuedCount = 0;
    uint32_t residentCount = 0;
    uint32_t activeCount = 0;
    uint32_t queuedUnloadCount = 0;
    uint32_t unloadingCount = 0;
    uint32_t failedCount = 0;

    // Queue depths
    uint32_t cpuQueueLength = 0;
    uint32_t uploadQueueLength = 0;

    // Memory usage
    uint64_t totalCpuMemoryBytes = 0;
    uint64_t totalGpuMemoryBytes = 0;
    uint64_t uploadedBytesLastFrame = 0;
    uint32_t uploadOpsLastFrame = 0;

    // Active rendered content
    uint32_t activeTerrainChunks = 0;
    uint32_t activeFoliageInstances = 0;
    uint64_t activeTriangles = 0;

    // Cumulative lifetime counters
    uint64_t totalLoadsCompleted = 0;
    uint64_t totalUnloadsCompleted = 0;
    uint32_t activeStreamingSources = 0;
};

} // namespace sidera
