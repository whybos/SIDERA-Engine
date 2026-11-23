#pragma once

#include "terrain/HeightmapData.h"
#include "worldgen/BiomeGenerator.h"
#include "worldgen/WorldGenerationConfig.h"

#include <glm/vec2.hpp>
#include <cstdint>
#include <vector>

namespace sidera {

struct WorldPointSample {
    float normalizedHeight = 0.0f;  // [0..1]
    float worldHeight = 0.0f;       // heightOffset + normalizedHeight * maxElevation
    float continentalness = 0.0f;   // [-1..1]
    float mountainMask = 0.0f;      // [0..1]
    float valleyMask = 0.0f;        // [0..1]
    float riverMask = 0.0f;         // [0..1]
    float lakeMask = 0.0f;          // [0..1]
    glm::vec2 flowDirection{0.0f, 0.0f}; // downhill water flow vector
    BiomeSample biome{};
};

class TerrainGenerationPipeline {
public:
    explicit TerrainGenerationPipeline(const WorldGenerationConfig& config = {}) noexcept;

    void setConfig(const WorldGenerationConfig& config) noexcept;
    [[nodiscard]] const WorldGenerationConfig& getConfig() const noexcept { return m_config; }

    // Evaluate a single world coordinate (worldX, worldZ) deterministically
    [[nodiscard]] float evaluateContinentalness(float worldX, float worldZ) const noexcept;
    [[nodiscard]] float evaluateRawElevation(float worldX, float worldZ,
                                             float& outContinentalness,
                                             float& outMountainMask,
                                             float& outValleyMask) const noexcept;
    [[nodiscard]] float evaluateRiverMask(float worldX, float worldZ, float continentalness, float rawElev) const noexcept;
    [[nodiscard]] float evaluateLakeMask(float continentalness, float rawElev) const noexcept;
    [[nodiscard]] glm::vec2 evaluateFlowDirection(float worldX, float worldZ) const noexcept;

    [[nodiscard]] WorldPointSample evaluatePoint(float worldX, float worldZ) const noexcept;

    // Generate a full global HeightmapData compatible with Terrain & Advanced Terrain LOD
    bool generateHeightmap(HeightmapData& outHeightmap, float* outTimeMs = nullptr) const;

private:
    WorldGenerationConfig m_config{};
    BiomeGenerator m_biomeGenerator{};
};

} // namespace sidera
