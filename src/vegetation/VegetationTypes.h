#pragma once

#include "foliage/FoliageInstance.h"
#include "vegetation/VegetationSpecies.h"
#include "worldgen/BiomeGenerator.h"

#include <glm/vec2.hpp>
#include <array>
#include <cstdint>
#include <vector>

namespace sidera {

struct VegetationCandidate {
    float worldX = 0.0f;
    float worldZ = 0.0f;
    uint32_t speciesId = 0;
    float suitability = 0.0f;
    bool accepted = false;
};

struct VegetationCluster {
    glm::vec2 center{0.0f, 0.0f};
    float radius = 8.0f;
    float densityMultiplier = 1.0f;
    uint32_t speciesId = 0;
};

struct VegetationPlacement {
    FoliageInstance foliageInstance{};
    uint32_t speciesId = 0;
    VegetationCategory category = VegetationCategory::Tree;
    BiomeType biome = BiomeType::Grassland;
    float suitabilityScore = 0.0f;
    uint64_t ownerCellId = 0;
};

struct VegetationDistributionConfig {
    uint64_t worldSeed = 424242ULL;
    float densityMultiplier = 1.0f;
    float regionSize = 30.0f;
    float partitionCellSize = 30.0f;
    uint32_t densityMapResolution = 64;
    uint32_t maxInstancesPerRegion = 400;
    bool enableClustering = true;
    bool enableExclusionZones = true;
    bool debugVisualization = false;
};

struct VegetationStatistics {
    uint32_t totalSpeciesCount = 0;
    uint64_t totalCandidates = 0;
    uint64_t acceptedPlacements = 0;
    uint64_t rejectedPlacements = 0;

    uint64_t totalTreeInstances = 0;
    uint64_t totalBushInstances = 0;
    uint64_t totalGrassInstances = 0;
    uint64_t totalSmallVegetationInstances = 0;
    uint64_t totalClusters = 0;
    uint64_t totalVegetationInstances = 0;

    float averageSuitabilityScore = 0.0f;
    float distributionGenerationTimeMs = 0.0f;
    float lastRegionGenerationTimeMs = 0.0f;
    uint64_t cpuDistributionMemoryBytes = 0;
    float placementThroughputPerSec = 0.0f;

    std::array<uint64_t, static_cast<size_t>(BiomeType::Count)> instanceCountPerBiome{};

    void reset() noexcept {
        totalCandidates = 0;
        acceptedPlacements = 0;
        rejectedPlacements = 0;
        totalTreeInstances = 0;
        totalBushInstances = 0;
        totalGrassInstances = 0;
        totalSmallVegetationInstances = 0;
        totalClusters = 0;
        totalVegetationInstances = 0;
        averageSuitabilityScore = 0.0f;
        distributionGenerationTimeMs = 0.0f;
        lastRegionGenerationTimeMs = 0.0f;
        cpuDistributionMemoryBytes = 0;
        placementThroughputPerSec = 0.0f;
        instanceCountPerBiome.fill(0);
    }
};

struct RegionVegetationResult {
    int32_t regionX = 0;
    int32_t regionZ = 0;
    std::vector<VegetationPlacement> placements;
    std::vector<VegetationCluster> clusters;
    uint64_t candidatesEvaluated = 0;
    float generationTimeMs = 0.0f;
};

} // namespace sidera
