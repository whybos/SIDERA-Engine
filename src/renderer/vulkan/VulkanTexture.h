#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace sidera {

class VulkanDevice;

class VulkanTexture {
public:
    VulkanTexture(const VulkanDevice& device,
                  VkCommandPool transferCommandPool,
                  const void* rawPixels,
                  uint32_t width,
                  uint32_t height,
                  VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
                  VkImageUsageFlags additionalUsage = 0,
                  std::string_view name = "Texture2D",
                  bool generateMips = true,
                  VkSampler externalSampler = VK_NULL_HANDLE);

    // Compressed texture constructor (BC1, BC3, BC5, BC7)
    VulkanTexture(const VulkanDevice& device,
                  VkCommandPool transferCommandPool,
                  const void* compressedData,
                  size_t dataSize,
                  uint32_t width,
                  uint32_t height,
                  VkFormat format,
                  std::string_view name = "CompressedTexture2D",
                  VkSampler externalSampler = VK_NULL_HANDLE);

    // Empty storage texture constructor (e.g. for compute writes like BRDF LUT)
    VulkanTexture(const VulkanDevice& device,
                  uint32_t width,
                  uint32_t height,
                  VkFormat format,
                  VkImageUsageFlags usage,
                  std::string_view name = "Texture2D",
                  VkSampler externalSampler = VK_NULL_HANDLE);

    ~VulkanTexture();

    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;
    VulkanTexture(VulkanTexture&&) = delete;
    VulkanTexture& operator=(VulkanTexture&&) = delete;

    [[nodiscard]] static std::unique_ptr<VulkanTexture> createProceduralTestTexture(
        const VulkanDevice& device,
        VkCommandPool transferCommandPool,
        uint32_t width = 256,
        uint32_t height = 256,
        bool generateMips = true);

    [[nodiscard]] static uint32_t calculateMipLevels(uint32_t width, uint32_t height) noexcept;
    [[nodiscard]] static bool isCompressedFormat(VkFormat format) noexcept;
    [[nodiscard]] static size_t getFormatBytesPerPixel(VkFormat format) noexcept;
    [[nodiscard]] static uint32_t getFormatBlockSize(VkFormat format) noexcept;

    [[nodiscard]] VkImage image() const noexcept { return m_image; }
    [[nodiscard]] VkImageView imageView() const noexcept { return m_imageView; }
    [[nodiscard]] VkSampler sampler() const noexcept { return m_sampler; }
    [[nodiscard]] uint32_t width() const noexcept { return m_width; }
    [[nodiscard]] uint32_t height() const noexcept { return m_height; }
    [[nodiscard]] uint32_t mipLevels() const noexcept { return m_mipLevels; }
    [[nodiscard]] VkFormat format() const noexcept { return m_format; }
    [[nodiscard]] VkDeviceSize memorySize() const noexcept { return m_memorySize; }
    [[nodiscard]] bool isCompressed() const noexcept { return m_isCompressed; }
    [[nodiscard]] std::string_view name() const noexcept { return m_name; }

private:
    void createImageAndMemory();
    void uploadPixelsToImage(VkCommandPool transferCommandPool, const void* pixels, size_t explicitByteSize = 0);
    void generateMipmaps(VkCommandBuffer cmd);
    void createImageView();
    void createSampler();
    void cleanup() noexcept;

    const VulkanDevice& m_device;
    std::string m_name;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_mipLevels = 1;
    VkFormat m_format = VK_FORMAT_R8G8B8A8_SRGB;
    VkImageUsageFlags m_usage = 0;
    bool m_generateMips = false;
    bool m_isCompressed = false;
    bool m_ownsSampler = true;
    VkDeviceSize m_memorySize = 0;

    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
};

} // namespace sidera
