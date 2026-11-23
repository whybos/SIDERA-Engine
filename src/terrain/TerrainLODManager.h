#pragma once

#include "renderer/Camera.h"
#include "terrain/TerrainConfig.h"
#include "terrain/TerrainLODConfig.h"
#include "terrain/TerrainLODStatistics.h"
#include "terrain/TerrainPatchMesh.h"
#include "terrain/TerrainQuadtree.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace sidera {

class VulkanDevice;
class HeightmapData;
class WorldPartitionManager;
class WorldStreamingManager;

class TerrainLODManager {
public:
    TerrainLODManager() = default;
    ~TerrainLODManager();

    TerrainLODManager(const TerrainLODManager&) = delete;
    TerrainLODManager& operator=(const TerrainLODManager&) = delete;

    // Initialize with Vulkan device (for live rendering) or nullptr (headless testing)
    bool init(const TerrainConfig& terrainConfig,
              const TerrainLODConfig& lodConfig,
              const HeightmapData& heightmap,
              const VulkanDevice* device = nullptr,
              VkCommandPool transferCommandPool = VK_NULL_HANDLE);

    void destroy() noexcept;

    // Advance frame counter and flush deferred GPU buffer deletions
    void beginFrame() noexcept;

    // Update quadtree and cache patch meshes for a specific viewport
    const TerrainLODSelection& updateViewport(uint32_t viewportId,
                                              float deltaTime,
                                              const Camera& camera,
                                              uint32_t viewportHeight,
                                              bool frustumCull = true);

    // Update shadow quadtree with shadow LOD bias
    const TerrainLODSelection& updateShadow(float deltaTime,
                                            const Camera& camera,
                                            uint32_t viewportHeight);

    // Render selected patches for a specific viewport
    uint32_t renderViewport(VkCommandBuffer cmd,
                            VkPipelineLayout pipelineLayout,
                            uint32_t viewportId,
                            const glm::vec4& vpParams) const noexcept;

    // Render shadow patches into a CSM cascade
    uint32_t renderShadow(VkCommandBuffer cmd,
                          VkPipelineLayout shadowLayout,
                          const glm::mat4& lightSpaceMatrix) const noexcept;

    // Optional integration with World Partition & World Streaming
    void setWorldPartitionManager(WorldPartitionManager* partitionMgr) noexcept { m_partitionManager = partitionMgr; }
    void setWorldStreamingManager(WorldStreamingManager* streamingMgr) noexcept { m_streamingManager = streamingMgr; }

    // Configuration & Quality Presets
    [[nodiscard]] const TerrainLODConfig& getConfig() const noexcept { return m_lodConfig; }
    void setConfig(const TerrainLODConfig& cfg);
    void applyQualityPreset(TerrainQualityPreset preset);
    [[nodiscard]] TerrainQualityPreset getQualityPreset() const noexcept { return m_qualityPreset; }

    void toggleStrategy() noexcept;
    void toggleDebugVisualization() noexcept;
    void cycleDebugOverlayMode() noexcept;
    void setDebugVisualization(bool enabled) noexcept { m_lodConfig.debugVisualization = enabled; }
    [[nodiscard]] bool isDebugVisualizationEnabled() const noexcept { return m_lodConfig.debugVisualization; }

    // Accessors & Telemetry
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] const TerrainLODStatistics& getStats(uint32_t viewportId = 0) const noexcept;
    [[nodiscard]] const TerrainQuadtree* getQuadtree(uint32_t viewportId = 0) const noexcept;
    [[nodiscard]] const TerrainStitchIndexPool& getIndexPool() const noexcept { return m_indexPool; }
    [[nodiscard]] const TerrainPatchMesh* getCachedPatchMesh(uint64_t nodeId) const noexcept;
    [[nodiscard]] std::vector<TerrainDebugBox> getDebugBoxes(uint32_t viewportId = 0) const;

private:
    struct CachedPatch {
        std::unique_ptr<TerrainPatchMesh> mesh;
        uint64_t lastUsedFrame = 0;
    };

    struct DeferredDeletion {
        std::unique_ptr<TerrainPatchMesh> mesh;
        uint64_t retireFrame = 0;
    };

    TerrainPatchMesh* getOrCreatePatchMesh(const TerrainPatchSelectionEntry& entry);
    void evictUnusedPatches();
    void flushDeferredDeletions() noexcept;

    const VulkanDevice* m_device = nullptr;
    VkCommandPool m_transferCommandPool = VK_NULL_HANDLE;
    const HeightmapData* m_heightmap = nullptr;
    WorldPartitionManager* m_partitionManager = nullptr;
    WorldStreamingManager* m_streamingManager = nullptr;

    TerrainConfig m_terrainConfig{};
    TerrainLODConfig m_lodConfig{};
    TerrainQualityPreset m_qualityPreset = TerrainQualityPreset::High;

    TerrainStitchIndexPool m_indexPool;
    std::unordered_map<uint32_t, TerrainQuadtree> m_viewportTrees;
    std::unordered_map<uint32_t, TerrainLODSelection> m_viewportSelections;
    TerrainQuadtree m_shadowTree;
    TerrainLODSelection m_shadowSelection{};

    std::unordered_map<uint64_t, CachedPatch> m_patchCache;
    std::vector<DeferredDeletion> m_deferredDeletions;

    uint64_t m_frameIndex = 0;
    uint64_t m_totalGpuMemoryBytes = 0;
    bool m_initialized = false;
};

} // namespace sidera
