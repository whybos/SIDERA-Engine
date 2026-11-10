#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sidera {

class VulkanDevice;

class VulkanTextureArray {
public:
    VulkanTextureArray(const VulkanDevice& device,
                       VkCommandPool transferCommandPool,
                       const std::vector<const void*>& layerPixels,
                       uint32_t width,
                       uint32_t height,
                       VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
                       bool generateMips = true,
                       VkSampler externalSampler = VK_NULL_HANDLE,
                       std::string_view name = "Texture2DArray");

    ~VulkanTextureArray();

    VulkanTextureArray(const VulkanTextureArray&) = delete;
    VulkanTextureArray& operator=(const VulkanTextureArray&) = delete;
    VulkanTextureArray(VulkanTextureArray&&) = delete;
    VulkanTextureArray& operator=(VulkanTextureArray&&) = delete;

    [[nodiscard]] VkImage image() const noexcept { return m_image; }
    [[nodiscard]] VkImageView imageView() const noexcept { return m_imageView; }
    [[nodiscard]] VkSampler sampler() const noexcept { return m_sampler; }
    [[nodiscard]] uint32_t width() const noexcept { return m_width; }
    [[nodiscard]] uint32_t height() const noexcept { return m_height; }
    [[nodiscard]] uint32_t layerCount() const noexcept { return m_layerCount; }
    [[nodiscard]] uint32_t mipLevels() const noexcept { return m_mipLevels; }
    [[nodiscard]] VkFormat format() const noexcept { return m_format; }
    [[nodiscard]] VkDeviceSize memorySize() const noexcept { return m_memorySize; }
    [[nodiscard]] std::string_view name() const noexcept { return m_name; }

private:
    void createImageAndMemory();
    void uploadLayers(VkCommandPool transferCommandPool, const std::vector<const void*>& layerPixels);
    void generateMipmaps(VkCommandBuffer cmd);
    void createImageView();
    void createSampler();
    void cleanup() noexcept;

    const VulkanDevice& m_device;
    std::string m_name;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_layerCount = 0;
    uint32_t m_mipLevels = 1;
    VkFormat m_format = VK_FORMAT_R8G8B8A8_SRGB;
    VkImageUsageFlags m_usage = 0;
    bool m_generateMips = false;
    bool m_ownsSampler = true;
    VkDeviceSize m_memorySize = 0;

    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
};

} // namespace sidera
