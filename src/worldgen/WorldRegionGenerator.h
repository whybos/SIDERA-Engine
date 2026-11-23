#pragma once

#include "worldgen/BiomeGenerator.h"
#include "worldgen/TerrainGenerationPipeline.h"
#include "worldgen/WorldGenerationConfig.h"
#include "worldgen/WorldSeed.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <vector>

namespace sidera {

struct GeneratedRegionData {
    int32_t regionX = 0;
    int32_t regionZ = 0;
    uint64_t regionSeed = 0;
    uint32_t resolution = 65;
    float regionSize = 30.0f;

    std::vector<float> heights;          // normalized [0..1]
    std::vector<BiomeType> biomes;
    std::vector<float> temperatures;
    std::vector<float> moistures;
    std::vector<float> continentalness;
    std::vector<float> riverMasks;
    std::vector<float> lakeMasks;
    std::vector<glm::vec2> flowDirections;
    std::vector<glm::vec4> splatWeights;

    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    float generationTimeMs = 0.0f;

    [[nodiscard]] uint64_t getHeightmapMemoryBytes() const noexcept {
        return heights.size() * sizeof(float);
    }

    [[nodiscard]] uint64_t getBiomeMemoryBytes() const noexcept {
        return biomes.size() * sizeof(BiomeType) +
               (temperatures.size() + moistures.size() + continentalness.size() +
                riverMasks.size() + lakeMasks.size()) * sizeof(float) +
               flowDirections.size() * sizeof(glm::vec2) +
               splatWeights.size() * sizeof(glm::vec4);
    }
};

// Stateless, thread-safe deterministic region generator.
class WorldRegionGenerator {
public:
    explicit WorldRegionGenerator(const WorldGenerationConfig& config = {}) noexcept;

    void setConfig(const WorldGenerationConfig& config) noexcept;

    [[nodiscard]] GeneratedRegionData generateRegion(int32_t regionX, int32_t regionZ) const;

private:
    WorldGenerationConfig m_config{};
    TerrainGenerationPipeline m_pipeline{};
};

} // namespace sidera
