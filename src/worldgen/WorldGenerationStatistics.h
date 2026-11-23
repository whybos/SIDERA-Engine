#pragma once

#include <cstdint>

namespace sidera {

struct WorldGenerationStatistics {
    uint64_t worldSeed = 0;
    float worldScale = 1.0f;

    uint32_t generatedRegionCount = 0;
    uint32_t pendingGenerationCount = 0;
    uint32_t completedGenerationCount = 0;
    uint32_t failedGenerationCount = 0;
    uint32_t generationQueueLength = 0;

    float totalGenerationTimeMs = 0.0f;
    float heightmapGenerationTimeMs = 0.0f;
    float biomeGenerationTimeMs = 0.0f;

    uint64_t noiseEvaluationCount = 0;
    uint64_t heightmapMemoryBytes = 0;
    uint64_t biomeDataMemoryBytes = 0;
    uint64_t generatedTerrainTriangles = 0;

    void reset() noexcept {
        generatedRegionCount = 0;
        pendingGenerationCount = 0;
        completedGenerationCount = 0;
        failedGenerationCount = 0;
        generationQueueLength = 0;
        totalGenerationTimeMs = 0.0f;
        heightmapGenerationTimeMs = 0.0f;
        biomeGenerationTimeMs = 0.0f;
        noiseEvaluationCount = 0;
        heightmapMemoryBytes = 0;
        biomeDataMemoryBytes = 0;
        generatedTerrainTriangles = 0;
    }
};

} // namespace sidera
