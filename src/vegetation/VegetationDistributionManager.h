#pragma once

#include "vegetation/VegetationDensityMap.h"
#include "vegetation/VegetationExclusionZone.h"
#include "vegetation/VegetationSpecies.h"
#include "vegetation/VegetationTypes.h"
#include "worldgen/WorldGenerator.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace sidera {

class Terrain;
class FoliageSystem;
class WorldPartitionManager;
class WorldStreamingManager;

// Central coordinator for biome-aware, deterministic procedural vegetation distribution (Milestone 25).
class VegetationDistributionManager {
public:
    VegetationDistributionManager() = default;
    ~VegetationDistributionManager() = default;

    void init(const VegetationDistributionConfig& config,
              const WorldGenerator* worldGen,
              const Terrain* terrain = nullptr);

    // Register default biome-aware species catalog (Trees, Bushes, Grass, Small Vegetation)
    void registerDefaultSpecies();
    uint32_t registerSpecies(const VegetationSpecies& species);
    void clearSpecies();
    [[nodiscard]] const std::vector<VegetationSpecies>& getSpecies() const noexcept { return m_species; }

    // Exclusion zones (lakes, rivers, roads, buildings, user-defined circles/boxes)
    void addExclusionZone(const VegetationExclusionZone& zone);
    void clearExclusionZones();
    [[nodiscard]] bool isExcluded(float worldX, float worldZ) const noexcept;

    // 6-factor normalized suitability evaluation [0..1]
    [[nodiscard]] static float evaluateSuitability(const VegetationSpecies& species,
                                                   const WorldPointSample& pt) noexcept;

    // Deterministic, order-independent, thread-safe region vegetation distribution
    const RegionVegetationResult& generateRegion(int32_t regionX, int32_t regionZ);
    const RegionVegetationResult& regenerateRegion(int32_t regionX, int32_t regionZ);
    void clearCache();

    // Generate across a grid of regions and push instances into existing FoliageSystem (Milestone 20)
    uint64_t distributeWorld(int32_t minRegionX, int32_t maxRegionX,
                             int32_t minRegionZ, int32_t maxRegionZ,
                             FoliageSystem* foliageSystem = nullptr);

    // Subsystem links
    void setWorldPartitionManager(WorldPartitionManager* partitionMgr) noexcept { m_partitionManager = partitionMgr; }
    void setWorldStreamingManager(WorldStreamingManager* streamingMgr) noexcept { m_streamingManager = streamingMgr; }

    // Accessors & Telemetry
    [[nodiscard]] const VegetationDistributionConfig& getConfig() const noexcept { return m_config; }
    void setConfig(const VegetationDistributionConfig& cfg) noexcept { m_config = cfg; }
    [[nodiscard]] const VegetationDensityMap& getDensityMap() const noexcept { return m_densityMap; }
    [[nodiscard]] const VegetationStatistics& getStats() const noexcept { return m_stats; }

private:
    [[nodiscard]] static uint64_t packRegionKey(int32_t rx, int32_t rz) noexcept {
        return (static_cast<uint64_t>(static_cast<uint32_t>(rx)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(rz));
    }

    RegionVegetationResult computeRegion(int32_t regionX, int32_t regionZ) const;

    VegetationDistributionConfig m_config{};
    const WorldGenerator* m_worldGen = nullptr;
    const Terrain* m_terrain = nullptr;
    WorldPartitionManager* m_partitionManager = nullptr;
    WorldStreamingManager* m_streamingManager = nullptr;

    std::vector<VegetationSpecies> m_species;
    std::vector<VegetationExclusionZone> m_exclusionZones;
    VegetationDensityMap m_densityMap;

    mutable std::mutex m_cacheMutex;
    std::unordered_map<uint64_t, std::unique_ptr<RegionVegetationResult>> m_regionCache;
    VegetationStatistics m_stats{};
};

} // namespace sidera
