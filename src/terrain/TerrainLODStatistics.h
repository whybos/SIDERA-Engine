#pragma once

#include <array>
#include <cstdint>

namespace sidera {

struct TerrainLODStatistics {
    static constexpr uint32_t kMaxTrackedLods = 8;

    uint32_t totalQuadtreeNodes = 0;
    uint32_t activeLeafNodes = 0;
    uint32_t visibleTerrainPatches = 0;
    uint32_t culledTerrainPatches = 0;

    // LOD 0 = finest detail (maxDepth), LOD 1 = maxDepth - 1, ..., LOD 4+ = coarsest
    std::array<uint32_t, kMaxTrackedLods> patchCountPerLod{};

    uint64_t totalTerrainVertices = 0;
    uint64_t totalTerrainTriangles = 0;

    float maxScreenSpaceError = 0.0f;
    float averageScreenSpaceError = 0.0f;
    float averageMorphFactor = 0.0f;

    uint32_t stitchingPatchCount = 0;
    uint32_t lodTransitionCount = 0;
    uint32_t cachedPatchCount = 0;

    float quadtreeUpdateTimeMs = 0.0f;
    float patchSelectionTimeMs = 0.0f;
    float terrainGpuFrameTimeMs = 0.0f;
    uint64_t terrainGpuMemoryBytes = 0;

    void reset() noexcept {
        totalQuadtreeNodes = 0;
        activeLeafNodes = 0;
        visibleTerrainPatches = 0;
        culledTerrainPatches = 0;
        patchCountPerLod.fill(0);
        totalTerrainVertices = 0;
        totalTerrainTriangles = 0;
        maxScreenSpaceError = 0.0f;
        averageScreenSpaceError = 0.0f;
        averageMorphFactor = 0.0f;
        stitchingPatchCount = 0;
        lodTransitionCount = 0;
        quadtreeUpdateTimeMs = 0.0f;
        patchSelectionTimeMs = 0.0f;
    }
};

} // namespace sidera
