#pragma once

#include <cstdint>

namespace sidera {

// Real-time telemetry and spatial statistics for World Partition
struct WorldPartitionStats {
    uint32_t totalCells = 0;
    uint32_t loadedCells = 0;
    uint32_t activeCells = 0;
    uint32_t inactiveCells = 0;
    uint32_t pendingLoadingCells = 0;
    uint32_t pendingUnloadingCells = 0;

    uint32_t totalObjects = 0;
    uint32_t totalTerrainChunks = 0;
    uint32_t totalFoliageGroups = 0;

    uint64_t cellMemoryUsageBytes = 0;
    float avgObjectsPerCell = 0.0f;

    float cellLookupTimeUs = 0.0f;
    float spatialQueryTimeUs = 0.0f;
};

} // namespace sidera
