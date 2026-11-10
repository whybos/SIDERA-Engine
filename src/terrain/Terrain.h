#pragma once

#include "renderer/Frustum.h"
#include "terrain/HeightmapData.h"
#include "terrain/TerrainBounds.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainConfig.h"
#include "terrain/TerrainMaterial.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace sidera {

class VulkanDevice;
class TextureManager;

struct TerrainStats {
    uint32_t totalChunks = 0;
    uint32_t visibleChunks = 0;
    uint32_t culledChunks = 0;
    uint64_t totalVertices = 0;
    uint64_t totalTriangles = 0;
    uint64_t visibleTriangles = 0;
    uint32_t mainDrawCalls = 0;
    uint32_t shadowDrawCalls = 0;
    uint64_t gpuMemoryUsageBytes = 0;
    uint32_t heightmapResolution = 0;
    float generationTimeMs = 0.0f;
};

class Terrain {
public:
    Terrain();
    ~Terrain();

    Terrain(const Terrain&) = delete;
    Terrain& operator=(const Terrain&) = delete;
    Terrain(Terrain&&) noexcept = default;
    Terrain& operator=(Terrain&&) noexcept = default;

    // Initialization and lifecycle
    bool init(const TerrainConfig& config,
              const VulkanDevice& device,
              VkCommandPool transferCommandPool,
              TextureManager& textureManager);
    bool initHeadless(const TerrainConfig& config);
    void rebuild(const TerrainConfig& newConfig);
    void destroy() noexcept;

    // Visibility determination via Frustum culling
    void updateVisibility(const Frustum& frustum) noexcept;

    // World-space height and surface normal queries
    [[nodiscard]] float getHeightAt(float worldX, float worldZ, bool* outOfBounds = nullptr) const noexcept;
    [[nodiscard]] glm::vec3 getNormalAt(float worldX, float worldZ) const noexcept;
    [[nodiscard]] bool containsWorldPoint(float worldX, float worldZ) const noexcept;

    // Drawing
    uint32_t render(VkCommandBuffer cmd, VkPipelineLayout pipelineLayout, const glm::vec4& vpParams) const noexcept;
    uint32_t renderShadow(VkCommandBuffer cmd, VkPipelineLayout shadowLayout, const glm::mat4& lightSpaceMatrix) const noexcept;

    // Accessors
    [[nodiscard]] const TerrainConfig& getConfig() const noexcept { return m_config; }
    [[nodiscard]] const TerrainBounds& getBounds() const noexcept { return m_bounds; }
    [[nodiscard]] const HeightmapData& getHeightmap() const noexcept { return m_heightmap; }
    [[nodiscard]] const TerrainMaterial* getMaterial() const noexcept { return m_material.get(); }
    [[nodiscard]] const std::vector<std::unique_ptr<TerrainChunk>>& getChunks() const noexcept { return m_chunks; }
    [[nodiscard]] const TerrainStats& getStats() const noexcept { return m_stats; }
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

private:
    void buildChunks();
    void computeBounds();

    const VulkanDevice* m_device = nullptr;
    VkCommandPool m_transferCommandPool = VK_NULL_HANDLE;
    TextureManager* m_textureManager = nullptr;

    TerrainConfig m_config{};
    HeightmapData m_heightmap{};
    TerrainBounds m_bounds{};
    std::unique_ptr<TerrainMaterial> m_material;
    std::vector<std::unique_ptr<TerrainChunk>> m_chunks;
    mutable TerrainStats m_stats{};
    bool m_initialized = false;
};

} // namespace sidera
