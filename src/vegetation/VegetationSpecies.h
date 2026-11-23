#pragma once

#include "worldgen/BiomeGenerator.h"

#include <glm/vec4.hpp>
#include <cstdint>
#include <string>

namespace sidera {

enum class VegetationCategory : uint8_t {
    Tree = 0,
    Bush,
    Grass,
    SmallVegetation
};

enum class DistributionAlgorithm : uint8_t {
    JitteredGrid = 0,
    PoissonDisk,
    Clustered,
    WeightedRandom,
    Hybrid
};

[[nodiscard]] constexpr uint32_t biomeBit(BiomeType b) noexcept {
    return 1u << static_cast<uint32_t>(b);
}

struct VegetationRule {
    uint32_t allowedBiomeMask = 0xFFFFFFFFu;

    // Normalized altitude [0..1]
    float minAltitude = 0.22f;
    float maxAltitude = 0.78f;

    // Terrain slope [0..1] (0=flat, 1=vertical)
    float minSlope = 0.0f;
    float maxSlope = 0.45f;

    // Preferred climate [0..1]
    float preferredTemperature = 0.5f;
    float temperatureTolerance = 0.35f;
    float preferredMoisture = 0.5f;
    float moistureTolerance = 0.35f;

    // Surface material affinity
    float grassAffinity = 1.0f;
    float rockPenalty = 0.85f;
    float sandAffinity = 0.15f;
    float snowPenalty = 0.80f;

    // Water & spacing constraints
    bool avoidWater = true;
    float minSpacing = 2.0f;
    float treeAvoidanceRadius = 0.0f; // optional grass/bush reduction under tree canopies
};

struct VegetationSpecies {
    uint32_t speciesId = 0;
    std::string name = "Species";
    VegetationCategory category = VegetationCategory::Tree;
    uint32_t foliageTypeId = 1; // Maps to FoliageSystem::FoliageType ID (1=Tree, 2=Bush, 3=Grass, 4=SmallVeg)

    VegetationRule rule{};
    DistributionAlgorithm algorithm = DistributionAlgorithm::Hybrid;

    float baseDensity = 0.5f;         // Base placement probability / density weight
    float minScale = 0.75f;
    float maxScale = 1.35f;
    bool randomRotation = true;
    float alignToTerrainNormal = 0.15f; // 0 = upright (trees), 1 = full normal alignment (grass)

    // Clustering parameters
    float clusterRadius = 8.0f;
    float clusterStrength = 0.45f;

    // Higher priority species (e.g. trees) are placed before lower priority (e.g. grass)
    uint32_t placementPriority = 10;

    // Debug visualization color
    glm::vec4 debugColor{0.2f, 0.8f, 0.2f, 1.0f};
};

} // namespace sidera
