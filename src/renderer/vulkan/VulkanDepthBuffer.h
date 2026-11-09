#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace sidera {

class VulkanDevice;

class VulkanDepthBuffer {
public:
    VulkanDepthBuffer(const VulkanDevice& device, uint32_t width, uint32_t height);
    ~VulkanDepthBuffer();

    VulkanDepthBuffer(const VulkanDepthBuffer&) = delete;
    VulkanDepthBuffer& operator=(const VulkanDepthBuffer&) = delete;
    VulkanDepthBuffer(VulkanDepthBuffer&&) = delete;
    VulkanDepthBuffer& operator=(VulkanDepthBuffer&&) = delete;

    void recreate(uint32_t width, uint32_t height);

    [[nodiscard]] VkImage image() const noexcept { return m_image; }
    [[nodiscard]] VkImageView imageView() const noexcept { return m_imageView; }
    [[nodiscard]] VkFormat format() const noexcept { return m_format; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return m_extent; }

    static VkFormat findDepthFormat(VkPhysicalDevice physicalDevice);

private:
    void createResources(uint32_t width, uint32_t height);
    void cleanup() noexcept;

    const VulkanDevice& m_device;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{0, 0};
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
};

} // namespace sidera
