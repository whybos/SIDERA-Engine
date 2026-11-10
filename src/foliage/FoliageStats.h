#pragma once

#include <cstdint>

namespace sidera {

// Real-time runtime statistics for profiling foliage instancing, culling and LOD
struct FoliageStatistics {
    uint32_t totalInstances = 0;        // Total registered foliage instances in the world
    uint32_t visibleInstances = 0;      // Instances that passed frustum & distance culling
    uint32_t culledInstances = 0;       // Instances culled by distance or frustum planes
    uint32_t totalTypes = 0;            // Number of registered FoliageType definitions
    uint32_t totalBatches = 0;          // Number of distinct instanced render batches
    uint32_t totalDrawCalls = 0;        // Indirect/direct draw calls emitted for foliage
    uint64_t totalTriangles = 0;        // Theoretical triangles if all foliage rendered at LOD 0
    uint64_t visibleTriangles = 0;      // Actual triangles submitted to GPU for visible LOD tiers
    uint32_t treeInstanceCount = 0;     // Instances of Tree category
    uint32_t grassInstanceCount = 0;    // Instances of Grass category
    uint32_t bushInstanceCount = 0;     // Instances of Bush category
    uint32_t smallVegetationCount = 0;  // Instances of SmallVegetation category
    uint32_t lod0Count = 0;             // Active foliage instances currently at LOD 0
    uint32_t lod1Count = 0;             // Active foliage instances currently at LOD 1
    uint32_t lod2Count = 0;             // Active foliage instances currently at LOD 2
    uint32_t lod3Count = 0;             // Active foliage instances currently at LOD 3
    uint64_t gpuMemoryUsageBytes = 0;   // GPU buffer memory allocated for foliage meshes & instances
    float windTime = 0.0f;              // Cumulative continuous elapsed wind time
};

} // namespace sidera
