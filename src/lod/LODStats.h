#pragma once

#include <cstdint>

namespace sidera {

// Aggregated real-time metrics for LOD system profiling and diagnostics
struct LODStatistics {
    uint32_t totalLODObjects = 0;           // Total renderable objects backed by a LODGroup
    uint32_t visibleLODObjects = 0;         // LOD objects that passed frustum & occlusion culling
    uint32_t lod0Count = 0;                 // Instances currently rendering at LOD 0
    uint32_t lod1Count = 0;                 // Instances currently rendering at LOD 1
    uint32_t lod2Count = 0;                 // Instances currently rendering at LOD 2
    uint32_t lod3Count = 0;                 // Instances currently rendering at LOD 3
    uint32_t totalLODGroups = 0;            // Distinct registered LODGroup definitions
    uint32_t totalLODDrawCalls = 0;         // Draw calls generated for LOD instances
    uint64_t totalLODTriangles = 0;         // Actual triangles rendered across all active LOD instances
    uint64_t originalTriangles = 0;         // Theoretical triangles if all objects rendered at LOD 0
    double triangleReductionPercent = 0.0;  // Percentage of triangles culled/reduced by LOD
    uint32_t lodTransitionsThisFrame = 0;   // Count of LOD level transitions that occurred this frame
    float selectionTimeMs = 0.0f;           // CPU execution time spent evaluating LOD selection (ms)
};

} // namespace sidera
