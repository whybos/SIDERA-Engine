#pragma once

#include "renderer/Camera.h"
#include "renderer/postprocess/PostProcessPipeline.h"
#include "renderer/reflection/ReflectionSettings.h"
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

// std140 UBO matching shaders/ssr_*.comp & reflection_composite.comp
struct alignas(16) ReflectionUniformData {
    glm::mat4 viewProj{1.0f};
    glm::mat4 invViewProj{1.0f};
    glm::mat4 prevViewProj{1.0f};
    glm::vec4 cameraPosNear{0.0f, 0.0f, 0.0f, 0.1f};
    glm::vec4 rayParams{48.0f, 0.65f, 0.65f, 200.0f};
    glm::vec4 blendParams{0.85f, 1.0f, 1.0f, 0.1f};
    glm::uvec4 dims{960u, 540u, 1920u, 1080u};
    glm::uvec4 counts{24u, 4u, 0u, 0u};
    glm::uvec4 debugParams{0u, 2u, 5u, 0u};
};

// Per-viewport independent temporal reflection history
struct ViewportReflectionHistory {
    glm::mat4 prevViewProj{1.0f};
    glm::vec3 prevCameraPos{0.0f};
    uint32_t pingPongIndex = 0;
    bool valid = false;
};

// GPU Hybrid Real-Time Reflection Pipeline (Milestone 32):
// Combines Hi-Z SSR + Local Reflection Probes (Box-Projected) + Global Prefiltered IBL Fallback
class ReflectionPipeline {
public:
    static constexpr uint32_t kMaxProbes = 64;

    struct ImageTarget2D {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    ReflectionPipeline() = default;
    ~ReflectionPipeline();

    ReflectionPipeline(const ReflectionPipeline&) = delete;
    ReflectionPipeline& operator=(const ReflectionPipeline&) = delete;

    bool init(uint32_t screenWidth, uint32_t screenHeight, const VulkanDevice* device = nullptr);
    bool resize(uint32_t screenWidth, uint32_t screenHeight);
    void destroy() noexcept;

    // Reflection probe management
    uint32_t registerProbe(const ReflectionProbe& probe);
    void removeProbe(uint32_t index);
    void clearProbes() noexcept { m_probes.clear(); }
    [[nodiscard]] const std::vector<ReflectionProbe>& getProbes() const noexcept { return m_probes; }

    void invalidateHistory(uint32_t viewportId = 0xFFFFFFFFu) noexcept;
    [[nodiscard]] bool checkAndUpdateViewportHistory(uint32_t viewportId, const Camera& camera) noexcept;

    // Record GPU hybrid reflection passes (Pre-Refl Snapshot -> Hi-Z SSR -> Temporal -> Denoise -> Hybrid Composite)
    void recordPasses(VkCommandBuffer cmd,
                      const PostProcessPipeline::ImageTarget& hdrTarget,
                      VkImageView depthImageView,
                      VkImageView hiZImageView,
                      VkSampler hiZSampler,
                      VkImageView envPrefilteredView,
                      VkSampler envPrefilteredSampler,
                      uint32_t envMipLevels,
                      const Camera& camera,
                      uint32_t viewportId = 0u);

    [[nodiscard]] ReflectionSettings& getSettings() noexcept { return m_settings; }
    [[nodiscard]] const ReflectionSettings& getSettings() const noexcept { return m_settings; }
    void setSettings(const ReflectionSettings& s) noexcept { m_settings = s; m_settings.validate(); }

    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] bool isGpuActive() const noexcept { return m_device != nullptr && m_compositePipeline != VK_NULL_HANDLE; }
    [[nodiscard]] const ReflectionStatistics& getStats() const noexcept { return m_stats; }

private:
    bool createTarget2D(ImageTarget2D& out, VkFormat format, uint32_t w, uint32_t h, VkImageUsageFlags usage);
    void destroyTarget2D(ImageTarget2D& t) noexcept;
    bool createPipelines();
    static void transitionImage(VkCommandBuffer cmd, VkImage img, VkImageLayout oldLayout, VkImageLayout newLayout,
                                VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) noexcept;

    const VulkanDevice* m_device = nullptr;
    ReflectionSettings m_settings{};
    ReflectionStatistics m_stats{};
    bool m_initialized = false;
    uint32_t m_screenWidth = 0;
    uint32_t m_screenHeight = 0;
    uint32_t m_frameCounter = 0;

    std::vector<ReflectionProbe> m_probes;
    std::unordered_map<uint32_t, ViewportReflectionHistory> m_viewportHistories;

    VkSampler m_linearSampler = VK_NULL_HANDLE;
    ImageTarget2D m_preReflHdrCopy{};
    ImageTarget2D m_rawSSR{};
    std::array<ImageTarget2D, 2> m_history{};
    ImageTarget2D m_denoisedSSR{};

    std::unique_ptr<VulkanBuffer> m_uboBuffer;
    std::unique_ptr<VulkanBuffer> m_probeBuffer;
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
