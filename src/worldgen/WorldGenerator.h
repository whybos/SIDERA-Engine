#pragma once

#include "terrain/HeightmapData.h"
#include "worldgen/BiomeGenerator.h"
#include "worldgen/TerrainGenerationPipeline.h"
#include "worldgen/WorldGenerationConfig.h"
#include "worldgen/WorldGenerationStatistics.h"
#include "worldgen/WorldRegionGenerator.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace sidera {

class WorldPartitionManager;
class WorldStreamingManager;

// Central Procedural World Generation coordinator (Milestone 24).
class WorldGenerator {
public:
    explicit WorldGenerator(const WorldGenerationConfig& config = {});
    ~WorldGenerator() = default;

    // Configuration & Presets
    void setConfig(const WorldGenerationConfig& config);
    [[nodiscard]] const WorldGenerationConfig& getConfig() const noexcept { return m_config; }
    void setWorldSeed(uint64_t seed);
    void setWorldScale(float scale);
    void setPreset(WorldPreset preset);

    // Generate full global heightmap & clear stale region cache
    bool generateWorld();

    // Deterministic region generation with caching (thread-safe)
    const GeneratedRegionData& generateRegion(int32_t regionX, int32_t regionZ);
    const GeneratedRegionData& regenerateRegion(int32_t regionX, int32_t regionZ);
    [[nodiscard]] bool hasRegion(int32_t regionX, int32_t regionZ) const;
    void clearRegionCache();

    // Direct world-space queries (safe even for ungenerated regions)
    [[nodiscard]] WorldPointSample samplePoint(float worldX, float worldZ) const noexcept;
    [[nodiscard]] BiomeType getBiomeAt(float worldX, float worldZ) const noexcept;
    [[nodiscard]] float getTemperatureAt(float worldX, float worldZ) const noexcept;
    [[nodiscard]] float getMoistureAt(float worldX, float worldZ) const noexcept;
    [[nodiscard]] float getHeightAt(float worldX, float worldZ) const noexcept;
    [[nodiscard]] float getContinentalnessAt(float worldX, float worldZ) const noexcept;
    [[nodiscard]] float getRiverMaskAt(float worldX, float worldZ) const noexcept;
    [[nodiscard]] float getLakeMaskAt(float worldX, float worldZ) const noexcept;

    // Access generated global heightmap for Terrain / Advanced Terrain LOD / World Streaming
    [[nodiscard]] const HeightmapData& getGlobalHeightmap() const noexcept { return m_globalHeightmap; }
    [[nodiscard]] bool isWorldGenerated() const noexcept { return m_worldGenerated; }

    // Subsystem links (World Partition & World Streaming)
    void setWorldPartitionManager(WorldPartitionManager* partitionMgr) noexcept { m_partitionManager = partitionMgr; }
    void setWorldStreamingManager(WorldStreamingManager* streamingMgr) noexcept { m_streamingManager = streamingMgr; }

    // Telemetry
    [[nodiscard]] const WorldGenerationStatistics& getStats() const noexcept { return m_stats; }

private:
    [[nodiscard]] static uint64_t packRegionKey(int32_t rx, int32_t rz) noexcept {
        return (static_cast<uint64_t>(static_cast<uint32_t>(rx)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(rz));
    }

    WorldGenerationConfig m_config{};
    TerrainGenerationPipeline m_pipeline{};
    WorldRegionGenerator m_regionGenerator{};
    HeightmapData m_globalHeightmap{};

    mutable std::mutex m_cacheMutex;
    std::unordered_map<uint64_t, std::unique_ptr<GeneratedRegionData>> m_regionCache;

    WorldPartitionManager* m_partitionManager = nullptr;
    WorldStreamingManager* m_streamingManager = nullptr;

    WorldGenerationStatistics m_stats{};
    bool m_worldGenerated = false;
};

} // namespace sidera
