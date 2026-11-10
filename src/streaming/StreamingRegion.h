#pragma once

#include "renderer/BoundingVolume.h"
#include "streaming/StreamingState.h"
#include "terrain/TerrainBounds.h"
#include "terrain/TerrainChunk.h"
#include "foliage/FoliageInstance.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sidera {

class Terrain;
class FoliageSystem;
class VulkanDevice;
struct TerrainConfig;
class HeightmapData;

enum class RegionBiome : uint8_t {
    LushMeadow = 0,
    PineForest,
    AlpineRidge,
    RockyPlateau,
    DenseWoodland,
    ValleyBasin
};

[[nodiscard]] constexpr std::string_view getBiomeName(RegionBiome biome) noexcept {
    switch (biome) {
        case RegionBiome::LushMeadow:    return "Lush Meadow";
        case RegionBiome::PineForest:    return "Pine Forest";
        case RegionBiome::AlpineRidge:   return "Alpine Ridge";
        case RegionBiome::RockyPlateau:  return "Rocky Plateau";
        case RegionBiome::DenseWoodland: return "Dense Woodland";
        case RegionBiome::ValleyBasin:   return "Valley Basin";
    }
    return "Unknown";
}

class StreamingRegion {
public:
    StreamingRegion(uint32_t regionId,
                    uint32_t gridX,
                    uint32_t gridZ,
                    const AABB& worldBounds,
                    RegionBiome biome = RegionBiome::LushMeadow);
    ~StreamingRegion();

    StreamingRegion(const StreamingRegion&) = delete;
    StreamingRegion& operator=(const StreamingRegion&) = delete;
    StreamingRegion(StreamingRegion&&) noexcept = default;
    StreamingRegion& operator=(StreamingRegion&&) noexcept = default;

    // Async CPU generation: builds TerrainChunk and foliage instances in RAM
    bool loadCpu(const TerrainConfig& terrainConfig,
                 const HeightmapData& heightmap,
                 const FoliageSystem* foliageSystem = nullptr);

    // Main render thread: allocates device-local GPU buffers and stages mesh data
    bool uploadGpu(const VulkanDevice& device, VkCommandPool transferCommandPool);

    // Scene integration: adds terrain chunk and registers foliage instances into active render loop
    bool activate(Terrain* terrain, FoliageSystem* foliageSystem);

    // Deactivation: unregisters foliage instances and marks chunk inactive
    void deactivate(Terrain* terrain, FoliageSystem* foliageSystem);

    // GPU release: frees GPU vertex & index buffers
    void releaseGpu(Terrain* terrain, FoliageSystem* foliageSystem);

    // Full CPU unload: releases all payload memory, returning to Unloaded state
    void unloadCpu();

    // Accessors
    [[nodiscard]] uint32_t getId() const noexcept { return m_regionId; }
    [[nodiscard]] uint32_t getGridX() const noexcept { return m_gridX; }
    [[nodiscard]] uint32_t getGridZ() const noexcept { return m_gridZ; }
    [[nodiscard]] const AABB& getBounds() const noexcept { return m_worldBounds; }
    [[nodiscard]] RegionBiome getBiome() const noexcept { return m_biome; }
    [[nodiscard]] StreamingState getState() const noexcept { return m_state; }
    void setState(StreamingState newState) noexcept { m_state = newState; }

    void setLinkedChunk(TerrainChunk* chunk) noexcept { m_linkedChunk = chunk; }
    [[nodiscard]] TerrainChunk* getTerrainChunk() noexcept { return m_linkedChunk ? m_linkedChunk : m_terrainChunk.get(); }
    [[nodiscard]] const TerrainChunk* getTerrainChunk() const noexcept { return m_linkedChunk ? m_linkedChunk : m_terrainChunk.get(); }

    [[nodiscard]] float getPriority() const noexcept { return m_priority; }
    void setPriority(float p) noexcept { m_priority = p; }

    [[nodiscard]] float getDistanceToSource() const noexcept { return m_distanceToNearestSource; }
    void setDistanceToSource(float d) noexcept { m_distanceToNearestSource = d; }

    [[nodiscard]] uint64_t getLastActiveFrame() const noexcept { return m_lastActiveFrame; }
    void setLastActiveFrame(uint64_t frame) noexcept { m_lastActiveFrame = frame; }

    [[nodiscard]] uint64_t getCpuMemoryBytes() const noexcept { return m_cpuMemoryBytes; }
    [[nodiscard]] uint64_t getGpuMemoryBytes() const noexcept { return m_gpuMemoryBytes; }

    [[nodiscard]] const std::vector<FoliageInstance>& getFoliageInstances() const noexcept { return m_foliageInstances; }
    [[nodiscard]] uint32_t getFoliageCount() const noexcept { return static_cast<uint32_t>(m_foliageInstances.size()); }
    [[nodiscard]] uint32_t getTriangleCount() const noexcept;

private:
    void generateFoliage(const HeightmapData& heightmap,
                         const TerrainConfig& terrainConfig,
                         const FoliageSystem* foliageSystem);

    uint32_t m_regionId = 0;
    uint32_t m_gridX = 0;
    uint32_t m_gridZ = 0;
    AABB m_worldBounds{};
    RegionBiome m_biome = RegionBiome::LushMeadow;
    StreamingState m_state = StreamingState::Unloaded;

    float m_priority = 0.0f;
    float m_distanceToNearestSource = 0.0f;
    uint64_t m_lastActiveFrame = 0;

    uint64_t m_cpuMemoryBytes = 0;
    uint64_t m_gpuMemoryBytes = 0;

    TerrainChunk* m_linkedChunk = nullptr;
    std::unique_ptr<TerrainChunk> m_terrainChunk;
    std::vector<FoliageInstance> m_foliageInstances;
    std::vector<uint32_t> m_foliageInstanceIds;
    bool m_isRegisteredInTerrain = false;
    bool m_isRegisteredInFoliage = false;
};

} // namespace sidera
