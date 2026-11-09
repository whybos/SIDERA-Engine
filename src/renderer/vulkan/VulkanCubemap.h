#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace sidera {

class VulkanDevice;

class VulkanCubemap {
public:
    VulkanCubemap(const VulkanDevice& device,
                  uint32_t width,
                  uint32_t height,
                  uint32_t mipLevels = 1,
                  VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT,
                  std::string_view name = "Cubemap");
    ~VulkanCubemap();

    VulkanCubemap(const VulkanCubemap&) = delete;
    VulkanCubemap& operator=(const VulkanCubemap&) = delete;
    VulkanCubemap(VulkanCubemap&&) = delete;
    VulkanCubemap& operator=(VulkanCubemap&&) = delete;

    [[nodiscard]] VkImage image() const noexcept { return m_image; }
    [[nodiscard]] VkImageView imageView() const noexcept { return m_imageView; }
    [[nodiscard]] VkImageView createMipView(uint32_t mipLevel) const;
    [[nodiscard]] VkSampler sampler() const noexcept { return m_sampler; }
    [[nodiscard]] uint32_t width() const noexcept { return m_width; }
    [[nodiscard]] uint32_t height() const noexcept { return m_height; }
    [[nodiscard]] uint32_t mipLevels() const noexcept { return m_mipLevels; }
    [[nodiscard]] VkFormat format() const noexcept { return m_format; }
    [[nodiscard]] std::string_view name() const noexcept { return m_name; }

private:
    void createImageAndMemory();
    void createImageView();
    void createSampler();
    void cleanup() noexcept;

    const VulkanDevice& m_device;
    std::string m_name;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_mipLevels = 1;
    VkFormat m_format = VK_FORMAT_R16G16B16A16_SFLOAT;

    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
};

} // namespace sidera
