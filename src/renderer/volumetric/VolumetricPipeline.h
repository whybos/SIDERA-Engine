#pragma once

#include "renderer/Camera.h"
#include "renderer/lighting/LightTypes.h"
#include "renderer/postprocess/PostProcessPipeline.h"
#include "renderer/volumetric/VolumetricSettings.h"
#include "renderer/vulkan/VulkanBuffer.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace sidera {

class VulkanDevice;

// std140 UBO matching shaders/volumetric_*.comp
struct alignas(16) VolumetricUniformData {
    glm::mat4 invViewProj{1.0f};
    glm::mat4 prevViewProj{1.0f};
    std::array<glm::mat4, 4> cascadeMatrices{
        glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)
    };
    glm::vec4 cascadeSplits{15.0f, 45.0f, 100.0f, 200.0f};
    glm::vec4 cameraPosNear{0.0f, 0.0f, 0.0f, 0.1f};
    glm::vec4 fogParams{0.015f, 12.0f, 0.08f, 0.25f};
    glm::vec4 scatterParams{0.85f, 1.0f, 0.65f, 1.0f};
    glm::vec4 fogColorMaxDist{0.62f, 0.74f, 0.90f, 160.0f};
    glm::vec4 scatterColorFlags{1.0f, 0.96f, 0.88f, 31.0f};
    glm::uvec4 gridCounts{120u, 68u, 64u, 0u};
    glm::uvec4 extraCounts{0u, 0u, 0u, 0u};
    glm::vec4 temporalParams{0.85f, 200.0f, 0.002f, 4.0f};
};

// Per-viewport independent temporal state (supports Single / Split / Quad viewports without contamination)
struct ViewportVolumetricHistory {
    glm::mat4 prevViewProj{1.0f};
    glm::vec3 prevCameraPos{0.0f};
    uint32_t pingPongIndex = 0;
    bool valid = false;
};

// GPU Froxel-Based Volumetric Rendering Pipeline (Milestone 30):
// Stages: Density & Light Injection -> Temporal Reprojection -> Front-to-Back Integration -> Linear HDR Composition
class VolumetricPipeline {
public:
    static constexpr uint32_t kMaxLocalVolumes = 64;

    struct VolumeTarget3D {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 0;
    };

    VolumetricPipeline() = default;
    ~VolumetricPipeline();

    VolumetricPipeline(const VolumetricPipeline&) = delete;
    VolumetricPipeline& operator=(const VolumetricPipeline&) = delete;

    bool init(uint32_t screenWidth, uint32_t screenHeight, const VulkanDevice* device = nullptr);
    bool resize(uint32_t screenWidth, uint32_t screenHeight);
    void destroy() noexcept;

    // Local fog volume management
    uint32_t addLocalVolume(const LocalFogVolume& vol);
    void clearLocalVolumes() noexcept { m_localVolumes.clear(); }
    [[nodiscard]] const std::vector<LocalFogVolume>& getLocalVolumes() const noexcept { return m_localVolumes; }

    // Explicit temporal history invalidation (e.g., on camera cut or fog setting change)
    void invalidateHistory(uint32_t viewportId = 0xFFFFFFFFu) noexcept;
    [[nodiscard]] bool checkAndUpdateViewportHistory(uint32_t viewportId, const Camera& camera) noexcept;

    // Record GPU volumetric passes (Inject -> Temporal -> Integrate -> HDR Composite)
    void recordPasses(VkCommandBuffer cmd,
                      const PostProcessPipeline::ImageTarget& hdrTarget,
                      VkImageView depthImageView,
                      VkImageView csmShadowView,
                      VkSampler csmShadowSampler,
                      const std::array<glm::mat4, 4>& cascadeMatrices,
                      const std::array<float, 4>& cascadeSplits,
                      uint32_t cascadeCount,
                      const Camera& camera,
                      const LightManager& lightManager,
                      uint32_t viewportId = 0u);

    [[nodiscard]] static VkFormat selectBestVolumeFormat(VkPhysicalDevice physicalDevice) noexcept;

    // Settings & Accessors
    [[nodiscard]] VolumetricSettings& getSettings() noexcept { return m_settings; }
    [[nodiscard]] const VolumetricSettings& getSettings() const noexcept { return m_settings; }
    void setSettings(const VolumetricSettings& s) noexcept { m_settings = s; m_settings.validate(); }

    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] bool isGpuActive() const noexcept { return m_device != nullptr && m_compositePipeline != VK_NULL_HANDLE; }
    [[nodiscard]] const VolumetricStatistics& getStats() const noexcept { return m_stats; }
    [[nodiscard]] uint32_t getMaxDim3D() const noexcept { return m_maxDim3D; }

private:
    bool createVolume3D(VolumeTarget3D& out, VkFormat format, uint32_t w, uint32_t h, uint32_t d);
    void destroyVolume3D(VolumeTarget3D& v) noexcept;
    bool createPipelines();
    void allocateBuffers();
    static void transitionImage(VkCommandBuffer cmd, VkImage img, VkImageLayout oldLayout, VkImageLayout newLayout,
                                VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) noexcept;

    const VulkanDevice* m_device = nullptr;
    VolumetricSettings m_settings{};
    VolumetricStatistics m_stats{};
    bool m_initialized = false;
    uint32_t m_screenWidth = 0;
    uint32_t m_screenHeight = 0;
    uint32_t m_maxDim3D = 2048;
    uint32_t m_frameCounter = 0;

    std::vector<LocalFogVolume> m_localVolumes;
    std::unordered_map<uint32_t, ViewportVolumetricHistory> m_viewportHistories;

    VkSampler m_linearSampler = VK_NULL_HANDLE;
    VolumeTarget3D m_volumeInject{};
    std::array<VolumeTarget3D, 2> m_volumeHistory{};
    VolumeTarget3D m_volumeIntegrated{};

    std::unique_ptr<VulkanBuffer> m_uboBuffer;
    std::unique_ptr<VulkanBuffer> m_lightBuffer;
    std::unique_ptr<VulkanBuffer> m_localVolumeBuffer;

    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_injectDSL = VK_NULL_HANDLE;
    VkPipelineLayout m_injectLayout = VK_NULL_HANDLE;
    VkPipeline m_injectPipeline = VK_NULL_HANDLE;
    VkDescriptorSet m_injectSet = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_temporalDSL = VK_NULL_HANDLE;
    VkPipelineLayout m_temporalLayout = VK_NULL_HANDLE;
    VkPipeline m_temporalPipeline = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> m_temporalSets{};

    VkDescriptorSetLayout m_integrateDSL = VK_NULL_HANDLE;
    VkPipelineLayout m_integrateLayout = VK_NULL_HANDLE;
    VkPipeline m_integratePipeline = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> m_integrateSets{};

    VkDescriptorSetLayout m_compositeDSL = VK_NULL_HANDLE;
    VkPipelineLayout m_compositeLayout = VK_NULL_HANDLE;
    VkPipeline m_compositePipeline = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> m_compositeSets{};
};

} // namespace sidera
