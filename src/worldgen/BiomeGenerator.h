#pragma once

#include "worldgen/WorldGenerationConfig.h"

#include <glm/vec4.hpp>
#include <cstdint>
#include <string_view>

namespace sidera {

enum class BiomeType : uint8_t {
    TemperateForest = 0,
    DenseForest,
    Grassland,
    Desert,
    SnowyMountains,
    Alpine,
    Tundra,
    Wetlands,
    Tropical,
    Coastal,
    Count
};

[[nodiscard]] constexpr std::string_view getBiomeTypeName(BiomeType biome) noexcept {
    switch (biome) {
        case BiomeType::TemperateForest: return "Temperate Forest";
        case BiomeType::DenseForest:     return "Dense Forest";
        case BiomeType::Grassland:       return "Grassland";
        case BiomeType::Desert:          return "Desert";
        case BiomeType::SnowyMountains:  return "Snowy Mountains";
        case BiomeType::Alpine:          return "Alpine";
        case BiomeType::Tundra:          return "Tundra";
        case BiomeType::Wetlands:        return "Wetlands";
        case BiomeType::Tropical:        return "Tropical";
        case BiomeType::Coastal:         return "Coastal";
        default:                         return "Unknown";
    }
}

struct BiomeSample {
    BiomeType primaryBiome = BiomeType::Grassland;
    BiomeType secondaryBiome = BiomeType::TemperateForest;
    float blendWeight = 0.0f;      // [0..1] blend toward secondaryBiome

    float temperature = 0.5f;      // [0..1] 0=freezing, 1=hot
    float moisture = 0.5f;         // [0..1] 0=arid, 1=wet
    float altitude = 0.5f;         // normalized [0..1]
    float slope = 0.0f;            // [0..1] 0=flat, 1=vertical
    float continentalness = 0.0f;  // [-1..1]
    float riverProximity = 0.0f;   // [0..1] 1=in river
    float lakeMask = 0.0f;         // [0..1] 1=in lake

    // Normalized RGBA splat weights compatible with TerrainMaterial (R=Grass, G=Rock, B=Dirt, A=Sand)
    glm::vec4 materialWeights{1.0f, 0.0f, 0.0f, 0.0f};
    float snowWeight = 0.0f;
    float mudWeight = 0.0f;
};

class BiomeGenerator {
public:
    explicit BiomeGenerator(const WorldGenerationConfig& config = {}) noexcept;

    void setConfig(const WorldGenerationConfig& config) noexcept { m_config = config; }

    [[nodiscard]] float evaluateTemperature(float worldX, float worldZ, float normalizedAltitude) const noexcept;
    [[nodiscard]] float evaluateMoisture(float worldX, float worldZ, float continentalness, float riverProximity) const noexcept;

    [[nodiscard]] BiomeSample evaluateBiome(float worldX,
                                              float worldZ,
                                              float normalizedAltitude,
                                              float slope,
                                              float continentalness,
                                              float riverProximity,
                                              float lakeMask) const noexcept;

    // Compute normalized RGBA splat weights (R=Grass, G=Rock, B=Dirt, A=Sand) + snow/mud metadata
    static void computeMaterialWeights(BiomeSample& sample) noexcept;

private:
    WorldGenerationConfig m_config{};
};

} // namespace sidera
