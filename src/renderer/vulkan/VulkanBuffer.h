#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

namespace sidera {

class VulkanDevice;

class VulkanBuffer {
public:
    VulkanBuffer(const VulkanDevice& device,
                 VkDeviceSize size,
                 VkBufferUsageFlags usage,
                 VkMemoryPropertyFlags requiredProperties,
                 VkMemoryPropertyFlags preferredProperties = 0);
    ~VulkanBuffer();

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&& other) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

    void uploadData(const void* srcData, VkDeviceSize size, VkDeviceSize offset = 0);

    [[nodiscard]] static std::unique_ptr<VulkanBuffer> createDeviceLocalBuffer(
        const VulkanDevice& device,
        VkCommandPool commandPool,
        const void* data,
        VkDeviceSize size,
        VkBufferUsageFlags usage
    );

    static void copyBuffer(const VulkanDevice& device,
                           VkCommandPool commandPool,
                           VkBuffer srcBuffer,
                           VkBuffer dstBuffer,
                           VkDeviceSize size);

    [[nodiscard]] VkBuffer getHandle() const noexcept { return m_buffer; }
    [[nodiscard]] VkDeviceMemory getMemory() const noexcept { return m_memory; }
    [[nodiscard]] VkDeviceSize getSize() const noexcept { return m_size; }
    [[nodiscard]] VkBufferUsageFlags getUsage() const noexcept { return m_usage; }
    [[nodiscard]] VkMemoryPropertyFlags getMemoryPropertyFlags() const noexcept { return m_memoryPropertyFlags; }

private:
    void release() noexcept;

    [[nodiscard]] static uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                                                 uint32_t typeFilter,
                                                 VkMemoryPropertyFlags requiredProperties,
                                                 VkMemoryPropertyFlags preferredProperties,
                                                 VkMemoryPropertyFlags* outSelectedFlags);

    const VulkanDevice* m_device = nullptr;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkDeviceSize m_size = 0;
    VkDeviceSize m_allocatedSize = 0;
    VkBufferUsageFlags m_usage = 0;
    VkMemoryPropertyFlags m_memoryPropertyFlags = 0;
};

} // namespace sidera
