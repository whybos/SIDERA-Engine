#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string_view>

namespace sidera {

class VulkanDevice;

class VulkanShadowMap {
public:
    explicit VulkanShadowMap(const VulkanDevice& device, uint32_t resolution = 2048);
    ~VulkanShadowMap();

    VulkanShadowMap(const VulkanShadowMap&) = delete;
    VulkanShadowMap& operator=(const VulkanShadowMap&) = delete;
    VulkanShadowMap(VulkanShadowMap&&) = delete;
    VulkanShadowMap& operator=(VulkanShadowMap&&) = delete;

    void recreate(uint32_t resolution);

    [[nodiscard]] VkImage image() const noexcept { return m_image; }
    [[nodiscard]] VkImageView imageView() const noexcept { return m_imageView; }
    [[nodiscard]] VkSampler sampler() const noexcept { return m_sampler; }
    [[nodiscard]] VkFormat format() const noexcept { return m_format; }
    [[nodiscard]] uint32_t resolution() const noexcept { return m_resolution; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return {m_resolution, m_resolution}; }

    void transitionToAttachment(VkCommandBuffer cmd) const noexcept;
    void transitionToShaderRead(VkCommandBuffer cmd) const noexcept;

    static VkFormat findShadowDepthFormat(VkPhysicalDevice physicalDevice);

private:
    void createResources();
    void cleanup() noexcept;

    const VulkanDevice& m_device;
    uint32_t m_resolution = 2048;
    VkFormat m_format = VK_FORMAT_UNDEFINED;

    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    mutable VkImageLayout m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

} // namespace sidera
