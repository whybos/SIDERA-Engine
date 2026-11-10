#pragma once

#include "renderer/vulkan/GraphicsPipeline.h"
#include "renderer/vulkan/VulkanBuffer.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace sidera {

class VulkanDevice;

class VulkanHiZBuffer {
public:
    static constexpr uint32_t kHiZWidth = 1024;
    static constexpr uint32_t kHiZHeight = 512;

    VulkanHiZBuffer(const VulkanDevice& device, uint32_t width = kHiZWidth, uint32_t height = kHiZHeight);
    ~VulkanHiZBuffer();

    VulkanHiZBuffer(const VulkanHiZBuffer&) = delete;
    VulkanHiZBuffer& operator=(const VulkanHiZBuffer&) = delete;
    VulkanHiZBuffer(VulkanHiZBuffer&&) = delete;
    VulkanHiZBuffer& operator=(VulkanHiZBuffer&&) = delete;

    void recreate(uint32_t width, uint32_t height);

    // Records the compute downsampling passes that build the full Hi-Z depth pyramid from the scene depth buffer.
    void build(VkCommandBuffer cmd, VkImageView sceneDepthView, VkExtent2D sceneExtent);
    void build(VkCommandBuffer cmd, VkImageView sceneDepthView, uint32_t width, uint32_t height) {
        build(cmd, sceneDepthView, VkExtent2D{width, height});
    }

    [[nodiscard]] VkImage getImage() const noexcept { return m_image; }
    [[nodiscard]] VkImageView getFullImageView() const noexcept { return m_fullImageView; }
    [[nodiscard]] VkImageView sampledImageView() const noexcept { return m_fullImageView; }
    [[nodiscard]] VkSampler getSampler() const noexcept { return m_sampler; }
    [[nodiscard]] VkSampler sampler() const noexcept { return m_sampler; }
    [[nodiscard]] VkExtent2D getExtent() const noexcept { return m_extent; }
    [[nodiscard]] uint32_t getMipLevels() const noexcept { return m_mipLevels; }

private:
    void createResources(uint32_t width, uint32_t height);
    void cleanup() noexcept;

    void createComputePipelines();
    void destroyComputePipelines() noexcept;

    const VulkanDevice& m_device;
    VkExtent2D m_extent{0, 0};
    uint32_t m_mipLevels = 1;

    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_fullImageView = VK_NULL_HANDLE;
    std::vector<VkImageView> m_mipImageViews;
    VkSampler m_sampler = VK_NULL_HANDLE;

    // Hi-Z build compute resources
    VkDescriptorSetLayout m_genDescSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_genPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_genPipeline = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_downsampleDescSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_downsamplePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_downsamplePipeline = VK_NULL_HANDLE;

    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
};

} // namespace sidera
