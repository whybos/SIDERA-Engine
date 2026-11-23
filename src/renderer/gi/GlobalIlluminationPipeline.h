#pragma once

#include "renderer/Camera.h"
#include "renderer/gi/GlobalIlluminationSettings.h"
#include "renderer/postprocess/PostProcessPipeline.h"
#include "renderer/vulkan/VulkanBuffer.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace sidera {

class VulkanDevice;

// std140 UBO matching shaders/ssgi_*.comp
struct alignas(16) GIUniformData {
    glm::mat4 viewProj{1.0f};
    glm::mat4 invViewProj{1.0f};
    glm::mat4 prevViewProj{1.0f};
    glm::vec4 cameraPosNear{0.0f, 0.0f, 0.0f, 0.1f};
    glm::vec4 rayParams{24.0f, 0.75f, 0.85f, 200.0f};
    glm::vec4 temporalParams{0.88f, 0.05f, 0.85f, 1.0f};
    glm::uvec4 dims{960u, 540u, 1920u, 1080u};
    glm::uvec4 counts{4u, 16u, 0u, 0u};
    glm::uvec4 debugParams{0u, 2u, 10u, 1u};
};

// Per-viewport independent temporal GI history (prevents cross-viewport contamination)
struct ViewportGIHistory {
    glm::mat4 prevViewProj{1.0f};
    glm::vec3 prevCameraPos{0.0f};
    uint32_t pingPongIndex = 0;
    bool valid = false;
};

// GPU Screen-Space Global Illumination (SSGI) Pipeline (Milestone 31):
// Stages: Pre-GI HDR Snapshot -> Hi-Z Stochastic Ray Trace -> Temporal Accumulation -> Edge-Aware Denoise -> Linear HDR Composition
class GlobalIlluminationPipeline {
public:
    struct ImageTarget2D {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    GlobalIlluminationPipeline() = default;
    ~GlobalIlluminationPipeline();

    GlobalIlluminationPipeline(const GlobalIlluminationPipeline&) = delete;
    GlobalIlluminationPipeline& operator=(const GlobalIlluminationPipeline&) = delete;

    bool init(uint32_t screenWidth, uint32_t screenHeight, const VulkanDevice* device = nullptr);
    bool resize(uint32_t screenWidth, uint32_t screenHeight);
    void destroy() noexcept;

    void invalidateHistory(uint32_t viewportId = 0xFFFFFFFFu) noexcept;
    [[nodiscard]] bool checkAndUpdateViewportHistory(uint32_t viewportId, const Camera& camera) noexcept;

    // Record GPU SSGI passes (Copy Pre-GI HDR -> Trace -> Temporal -> Denoise -> Composite)
    void recordPasses(VkCommandBuffer cmd,
                      const PostProcessPipeline::ImageTarget& hdrTarget,
                      VkImageView depthImageView,
                      VkImageView hiZImageView,
                      VkSampler hiZSampler,
                      uint32_t hiZMipLevels,
                      const Camera& camera,
                      uint32_t viewportId = 0u);

    [[nodiscard]] static GICapabilities queryDeviceCapabilities(VkPhysicalDevice physicalDevice) noexcept;

    // Settings & Accessors
    [[nodiscard]] GlobalIlluminationSettings& getSettings() noexcept { return m_settings; }
    [[nodiscard]] const GlobalIlluminationSettings& getSettings() const noexcept { return m_settings; }
    void setSettings(const GlobalIlluminationSettings& s) noexcept { m_settings = s; m_settings.validate(); }

    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] bool isGpuActive() const noexcept { return m_device != nullptr && m_compositePipeline != VK_NULL_HANDLE; }
    [[nodiscard]] const GlobalIlluminationStatistics& getStats() const noexcept { return m_stats; }
    [[nodiscard]] const GICapabilities& getCapabilities() const noexcept { return m_capabilities; }

private:
    bool createTarget2D(ImageTarget2D& out, VkFormat format, uint32_t w, uint32_t h, VkImageUsageFlags usage);
    void destroyTarget2D(ImageTarget2D& t) noexcept;
    bool createPipelines();
    static void transitionImage(VkCommandBuffer cmd, VkImage img, VkImageLayout oldLayout, VkImageLayout newLayout,
                                VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) noexcept;

    const VulkanDevice* m_device = nullptr;
    GlobalIlluminationSettings m_settings{};
    GlobalIlluminationStatistics m_stats{};
    GICapabilities m_capabilities{};
    bool m_initialized = false;
    uint32_t m_screenWidth = 0;
    uint32_t m_screenHeight = 0;
    uint32_t m_frameCounter = 0;

    std::unordered_map<uint32_t, ViewportGIHistory> m_viewportHistories;

    VkSampler m_linearSampler = VK_NULL_HANDLE;
    ImageTarget2D m_preGiHdrCopy{};
    ImageTarget2D m_rawGI{};
    std::array<ImageTarget2D, 2> m_history{};
    ImageTarget2D m_denoisedGI{};

    std::unique_ptr<VulkanBuffer> m_uboBuffer;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_traceDSL = VK_NULL_HANDLE;
    VkPipelineLayout m_traceLayout = VK_NULL_HANDLE;
    VkPipeline m_tracePipeline = VK_NULL_HANDLE;
    VkDescriptorSet m_traceSet = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_temporalDSL = VK_NULL_HANDLE;
    VkPipelineLayout m_temporalLayout = VK_NULL_HANDLE;
    VkPipeline m_temporalPipeline = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> m_temporalSets{};

    VkDescriptorSetLayout m_denoiseDSL = VK_NULL_HANDLE;
    VkPipelineLayout m_denoiseLayout = VK_NULL_HANDLE;
    VkPipeline m_denoisePipeline = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> m_denoiseSets{};

    VkDescriptorSetLayout m_compositeDSL = VK_NULL_HANDLE;
    VkPipelineLayout m_compositeLayout = VK_NULL_HANDLE;
    VkPipeline m_compositePipeline = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> m_compositeSets{};
};

} // namespace sidera
