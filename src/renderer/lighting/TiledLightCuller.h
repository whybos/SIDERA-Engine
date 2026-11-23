#pragma once

#include "renderer/Camera.h"
#include "renderer/lighting/LightTypes.h"
#include "renderer/vulkan/VulkanBuffer.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sidera {

class VulkanDevice;

struct alignas(16) LightCullPC {
    glm::mat4 view{1.0f};
    glm::vec4 projParams{1.0f, 1.0f, 0.1f, 200.0f};
    glm::uvec4 tileParams{1920u, 1080u, 120u, 0u}; // width, height, tilesX, lightCount
};
static_assert(sizeof(LightCullPC) == 96, "LightCullPC must be 96 bytes.");

struct TiledLightStats {
    uint32_t tileSize = 16;
    uint32_t tilesX = 0;
    uint32_t tilesY = 0;
    uint32_t totalTiles = 0;
    uint32_t totalLights = 0;
    uint32_t maxLightsInTile = 0;
    float averageLightsPerTile = 0.0f;
    uint32_t overflowCount = 0;
    uint64_t bufferMemoryBytes = 0;
    float cullTimeMs = 0.0f;
};

// GPU 16x16 screen-space tiled light culler shared by Deferred and Forward+ rendering paths (Milestone 28).
class TiledLightCuller {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;
    static constexpr uint32_t kTileSize = 16;
    static constexpr uint32_t kMaxLightsPerTile = 64;

    TiledLightCuller() = default;
    ~TiledLightCuller();

    TiledLightCuller(const TiledLightCuller&) = delete;
    TiledLightCuller& operator=(const TiledLightCuller&) = delete;

    bool init(uint32_t screenWidth, uint32_t screenHeight, const VulkanDevice* device = nullptr);
    bool resize(uint32_t screenWidth, uint32_t screenHeight);
    void destroy() noexcept;

    void uploadLights(uint32_t frameIndex, const LightManager& lightManager);
    void recordCull(VkCommandBuffer cmd, uint32_t frameIndex, const Camera& camera, uint32_t lightCount);

    // Deterministic CPU reference implementation matching shaders/light_culling.comp
    TiledLightStats simulateCullCPU(const Camera& camera,
                                    const LightManager& lightManager,
                                    std::vector<uint32_t>& outTileCounts) const;

    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] bool isGpuActive() const noexcept { return m_device != nullptr && m_computePipeline != VK_NULL_HANDLE; }
    [[nodiscard]] uint32_t getTilesX() const noexcept { return m_tilesX; }
    [[nodiscard]] uint32_t getTilesY() const noexcept { return m_tilesY; }
    [[nodiscard]] const TiledLightStats& getStats() const noexcept { return m_stats; }

private:
    bool createComputePipeline();
    void allocateBuffers();
    void updateDescriptorSets();

    const VulkanDevice* m_device = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_tilesX = 0;
    uint32_t m_tilesY = 0;
    bool m_initialized = false;
    TiledLightStats m_stats{};

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_descriptorSets{};
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_computePipeline = VK_NULL_HANDLE;

    std::array<std::unique_ptr<VulkanBuffer>, kMaxFramesInFlight> m_lightBuffers;
    std::array<std::unique_ptr<VulkanBuffer>, kMaxFramesInFlight> m_tileIndexBuffers;
    std::array<std::unique_ptr<VulkanBuffer>, kMaxFramesInFlight> m_tileCountBuffers;
    std::array<std::unique_ptr<VulkanBuffer>, kMaxFramesInFlight> m_overflowBuffers;
};

} // namespace sidera
