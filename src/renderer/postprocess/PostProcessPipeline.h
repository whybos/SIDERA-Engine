#pragma once

#include "renderer/Camera.h"
#include "renderer/postprocess/PostProcessSettings.h"

#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>

namespace sidera {

class VulkanDevice;

// Modular GPU Advanced Post Processing Pipeline (Milestone 29):
// Owns HDR color target, half-res Bloom pyramid, half-res SSAO raw/blur targets,
// and compute pipelines for Bloom, SSAO, and Tone Mapping (ACES / Reinhard / Linear).
class PostProcessPipeline {
public:
    struct ImageTarget {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    PostProcessPipeline() = default;
    ~PostProcessPipeline();

    PostProcessPipeline(const PostProcessPipeline&) = delete;
    PostProcessPipeline& operator=(const PostProcessPipeline&) = delete;

    bool init(uint32_t width, uint32_t height, const VulkanDevice* device = nullptr);
    bool resize(uint32_t width, uint32_t height);
    void destroy() noexcept;

    // Record GPU post-processing passes (SSAO -> Bloom -> Tone Mapping) with Synchronization2 barriers
    void recordPasses(VkCommandBuffer cmd,
                      VkImageView depthImageView,
                      const Camera& camera);

    // Format selection helper with automatic fallback
    [[nodiscard]] static VkFormat selectBestHDRFormat(VkPhysicalDevice physicalDevice) noexcept;

    // Settings & Accessors
    [[nodiscard]] PostProcessSettings& getSettings() noexcept { return m_settings; }
    [[nodiscard]] const PostProcessSettings& getSettings() const noexcept { return m_settings; }
    void setSettings(const PostProcessSettings& s) noexcept { m_settings = s; m_settings.validate(); }

    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] bool isGpuActive() const noexcept { return m_device != nullptr && m_tonemapPipeline != VK_NULL_HANDLE; }
    [[nodiscard]] const ImageTarget& getHDRTarget() const noexcept { return m_hdrSceneTarget; }
    [[nodiscard]] const ImageTarget& getOutputTarget() const noexcept { return m_tonemapOutputTarget; }
    [[nodiscard]] const PostProcessStatistics& getStats() const noexcept { return m_stats; }

private:
    bool createTarget(ImageTarget& out, VkFormat format, uint32_t w, uint32_t h, VkImageUsageFlags usage);
    void destroyTarget(ImageTarget& t) noexcept;
    bool createPipelines();
    void updateDescriptors(VkImageView depthView);
    static void transitionImage(VkCommandBuffer cmd, VkImage img, VkImageLayout oldLayout, VkImageLayout newLayout,
                                VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) noexcept;

    const VulkanDevice* m_device = nullptr;
    PostProcessSettings m_settings{};
    PostProcessStatistics m_stats{};
    bool m_initialized = false;

    VkSampler m_linearSampler = VK_NULL_HANDLE;
    ImageTarget m_hdrSceneTarget{};
    ImageTarget m_bloomA{};
    ImageTarget m_bloomB{};
    ImageTarget m_ssaoRaw{};
    ImageTarget m_ssaoBlur{};
    ImageTarget m_tonemapOutputTarget{};

    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_bloomDSL = VK_NULL_HANDLE;
    VkPipelineLayout m_bloomLayout = VK_NULL_HANDLE;
    VkPipeline m_bloomPipeline = VK_NULL_HANDLE;
    VkDescriptorSet m_bloomSetBright = VK_NULL_HANDLE;
    VkDescriptorSet m_bloomSetBlurH  = VK_NULL_HANDLE;
    VkDescriptorSet m_bloomSetBlurV  = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_ssaoDSL = VK_NULL_HANDLE;
    VkPipelineLayout m_ssaoLayout = VK_NULL_HANDLE;
    VkPipeline m_ssaoPipeline = VK_NULL_HANDLE;
    VkDescriptorSet m_ssaoSetGen  = VK_NULL_HANDLE;
    VkDescriptorSet m_ssaoSetBlur = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_tonemapDSL = VK_NULL_HANDLE;
    VkPipelineLayout m_tonemapLayout = VK_NULL_HANDLE;
    VkPipeline m_tonemapPipeline = VK_NULL_HANDLE;
    VkDescriptorSet m_tonemapSet = VK_NULL_HANDLE;
};

} // namespace sidera
