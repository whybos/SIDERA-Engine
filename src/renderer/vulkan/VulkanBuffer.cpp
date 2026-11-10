#include "renderer/vulkan/VulkanBuffer.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanDevice.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sidera {

VulkanBuffer::VulkanBuffer(const VulkanDevice& device,
                           VkDeviceSize size,
                           VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags requiredProperties,
                           VkMemoryPropertyFlags preferredProperties)
    : m_device(&device)
    , m_size(size)
    , m_usage(usage) {
    if (size == 0) {
        SIDERA_LOG_ERROR("Buffer", "Cannot create VulkanBuffer with size 0.");
        throw std::invalid_argument("VulkanBuffer size must be greater than 0.");
    }

    const VkDevice logicalDevice = m_device->getLogicalDevice();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(
        vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &m_buffer),
        "Failed to create Vulkan buffer"
    );

    try {
        VkMemoryRequirements memRequirements{};
        vkGetBufferMemoryRequirements(logicalDevice, m_buffer, &memRequirements);
        m_allocatedSize = memRequirements.size;

        const uint32_t memoryTypeIndex = findMemoryType(
            m_device->getPhysicalDevice(),
            memRequirements.memoryTypeBits,
            requiredProperties,
            preferredProperties,
            &m_memoryPropertyFlags
        );

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        VK_CHECK(
            vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &m_memory),
            "Failed to allocate Vulkan buffer device memory"
        );

        VK_CHECK(
            vkBindBufferMemory(logicalDevice, m_buffer, m_memory, 0),
            "Failed to bind Vulkan buffer device memory"
        );
    } catch (...) {
        release();
        throw;
    }
}

VulkanBuffer::~VulkanBuffer() {
    release();
}

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
    : m_device(std::exchange(other.m_device, nullptr))
    , m_buffer(std::exchange(other.m_buffer, VK_NULL_HANDLE))
    , m_memory(std::exchange(other.m_memory, VK_NULL_HANDLE))
    , m_size(std::exchange(other.m_size, 0))
    , m_allocatedSize(std::exchange(other.m_allocatedSize, 0))
    , m_usage(std::exchange(other.m_usage, 0))
    , m_memoryPropertyFlags(std::exchange(other.m_memoryPropertyFlags, 0)) {}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
    if (this != &other) {
        release();
        m_device = std::exchange(other.m_device, nullptr);
        m_buffer = std::exchange(other.m_buffer, VK_NULL_HANDLE);
        m_memory = std::exchange(other.m_memory, VK_NULL_HANDLE);
        m_size = std::exchange(other.m_size, 0);
        m_allocatedSize = std::exchange(other.m_allocatedSize, 0);
        m_usage = std::exchange(other.m_usage, 0);
        m_memoryPropertyFlags = std::exchange(other.m_memoryPropertyFlags, 0);
    }
    return *this;
}

void VulkanBuffer::uploadData(const void* srcData, VkDeviceSize size, VkDeviceSize offset) {
    if (!srcData) {
        SIDERA_LOG_ERROR("Buffer", "Cannot upload null source pointer into VulkanBuffer.");
        throw std::invalid_argument("VulkanBuffer::uploadData source pointer is null.");
    }
    if (size == 0 || (offset + size) > m_size) {
        SIDERA_LOG_ERROR(
            "Buffer",
            "Invalid upload range (offset={}, size={}) for buffer of size {}.",
            offset, size, m_size
        );
        throw std::out_of_range("VulkanBuffer::uploadData range exceeds buffer size.");
    }
    if ((m_memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
        SIDERA_LOG_ERROR("Buffer", "Cannot map a non-HOST_VISIBLE VulkanBuffer directly.");
        throw std::runtime_error("VulkanBuffer is not host-visible and cannot be mapped directly.");
    }

    const VkDevice logicalDevice = m_device->getLogicalDevice();
    void* mappedData = nullptr;

    VK_CHECK(
        vkMapMemory(logicalDevice, m_memory, offset, size, 0, &mappedData),
        "Failed to map Vulkan buffer memory"
    );

    std::memcpy(mappedData, srcData, static_cast<size_t>(size));

    // If the selected memory type is not host-coherent, explicitly flush the mapped range
    if ((m_memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        const VkDeviceSize atomSize = std::max<VkDeviceSize>(
            1, m_device->getProperties().limits.nonCoherentAtomSize
        );
        const VkDeviceSize alignedOffset = (offset / atomSize) * atomSize;
        const VkDeviceSize unalignedEnd = offset + size;
        const VkDeviceSize alignedEnd = std::min(
            m_allocatedSize,
            ((unalignedEnd + atomSize - 1) / atomSize) * atomSize
        );

        VkMappedMemoryRange flushRange{};
        flushRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        flushRange.memory = m_memory;
        flushRange.offset = alignedOffset;
        flushRange.size = (alignedEnd == m_allocatedSize) ? VK_WHOLE_SIZE : (alignedEnd - alignedOffset);

        VK_CHECK(
            vkFlushMappedMemoryRanges(logicalDevice, 1, &flushRange),
            "Failed to flush non-coherent mapped buffer memory range"
        );
    }

    vkUnmapMemory(logicalDevice, m_memory);
}

void VulkanBuffer::downloadData(void* dstData, VkDeviceSize size, VkDeviceSize offset) {
    if (!dstData) {
        SIDERA_LOG_ERROR("Buffer", "Cannot download into null destination pointer in VulkanBuffer.");
        throw std::invalid_argument("VulkanBuffer::downloadData destination pointer is null.");
    }
    if (size == 0 || (offset + size) > m_size) {
        SIDERA_LOG_ERROR(
            "Buffer",
            "Invalid download range (offset={}, size={}) for buffer of size {}.",
            offset, size, m_size
        );
        throw std::out_of_range("VulkanBuffer::downloadData range exceeds buffer size.");
    }
    if ((m_memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
        SIDERA_LOG_ERROR("Buffer", "Cannot map a non-HOST_VISIBLE VulkanBuffer directly.");
        throw std::runtime_error("VulkanBuffer is not host-visible and cannot be mapped directly.");
    }

    const VkDevice logicalDevice = m_device->getLogicalDevice();
    void* mappedData = nullptr;

    VK_CHECK(
        vkMapMemory(logicalDevice, m_memory, offset, size, 0, &mappedData),
        "Failed to map Vulkan buffer memory for download"
    );

    if ((m_memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        VkMappedMemoryRange invalidateRange{};
        invalidateRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        invalidateRange.memory = m_memory;
        invalidateRange.offset = offset;
        invalidateRange.size = size;
        vkInvalidateMappedMemoryRanges(logicalDevice, 1, &invalidateRange);
    }

    std::memcpy(dstData, mappedData, static_cast<size_t>(size));
    vkUnmapMemory(logicalDevice, m_memory);
}

std::unique_ptr<VulkanBuffer> VulkanBuffer::createDeviceLocalBuffer(
    const VulkanDevice& device,
    VkCommandPool commandPool,
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage) {
    if (!data || size == 0) {
        SIDERA_LOG_ERROR("Buffer", "Invalid data or size passed to createDeviceLocalBuffer.");
        throw std::invalid_argument("createDeviceLocalBuffer requires non-null data and size > 0.");
    }

    // 1. Create temporary HOST_VISIBLE (preferably HOST_COHERENT) staging buffer
    VulkanBuffer stagingBuffer(
        device,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer.uploadData(data, size, 0);

    // 2. Create target DEVICE_LOCAL buffer with TRANSFER_DST_BIT enabled
    auto deviceBuffer = std::make_unique<VulkanBuffer>(
        device,
        size,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    // 3. Copy from staging buffer to device-local buffer via GPU command buffer
    copyBuffer(device, commandPool, stagingBuffer.getHandle(), deviceBuffer->getHandle(), size);

    return deviceBuffer;
}

void VulkanBuffer::copyBuffer(const VulkanDevice& device,
                              VkCommandPool commandPool,
                              VkBuffer srcBuffer,
                              VkBuffer dstBuffer,
                              VkDeviceSize size) {
    const VkDevice logicalDevice = device.getLogicalDevice();

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer copyCmd = VK_NULL_HANDLE;
    VK_CHECK(
        vkAllocateCommandBuffers(logicalDevice, &allocInfo, &copyCmd),
        "Failed to allocate buffer transfer command buffer"
    );

    VkFence transferFence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    struct ScopedTransferCleanup {
        VkDevice dev;
        VkCommandPool pool;
        VkCommandBuffer cmd;
        VkFence& fence;

        ~ScopedTransferCleanup() {
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(dev, fence, nullptr);
            }
            if (cmd != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(dev, pool, 1, &cmd);
            }
        }
    } cleanup{logicalDevice, commandPool, copyCmd, transferFence};

    VK_CHECK(
        vkCreateFence(logicalDevice, &fenceInfo, nullptr, &transferFence),
        "Failed to create buffer transfer synchronization fence"
    );

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(
        vkBeginCommandBuffer(copyCmd, &beginInfo),
        "Failed to begin buffer transfer command buffer"
    );

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    vkCmdCopyBuffer(copyCmd, srcBuffer, dstBuffer, 1, &copyRegion);

    VK_CHECK(
        vkEndCommandBuffer(copyCmd),
        "Failed to end buffer transfer command buffer"
    );

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = copyCmd;
    cmdSubmitInfo.deviceMask = 0;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;

    VK_CHECK(
        vkQueueSubmit2(device.getGraphicsQueue(), 1, &submitInfo, transferFence),
        "Failed to submit buffer transfer command buffer"
    );

    VK_CHECK(
        vkWaitForFences(
            logicalDevice,
            1,
            &transferFence,
            VK_TRUE,
            std::numeric_limits<uint64_t>::max()
        ),
        "Failed waiting for buffer transfer fence"
    );
}

void VulkanBuffer::release() noexcept {
    if (!m_device) {
        return;
    }

    const VkDevice logicalDevice = m_device->getLogicalDevice();

    if (m_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(logicalDevice, m_buffer, nullptr);
        m_buffer = VK_NULL_HANDLE;
    }

    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(logicalDevice, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
}

uint32_t VulkanBuffer::findMemoryType(VkPhysicalDevice physicalDevice,
                                      uint32_t typeFilter,
                                      VkMemoryPropertyFlags requiredProperties,
                                      VkMemoryPropertyFlags preferredProperties,
                                      VkMemoryPropertyFlags* outSelectedFlags) {
    VkPhysicalDeviceMemoryProperties memProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    // Pass 1: Search for a memory type matching both required and preferred properties
    const VkMemoryPropertyFlags combinedProps = requiredProperties | preferredProperties;
    if (preferredProperties != 0) {
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            const bool isTypeSupported = (typeFilter & (1U << i)) != 0;
            const VkMemoryPropertyFlags flags = memProperties.memoryTypes[i].propertyFlags;
            if (isTypeSupported && (flags & combinedProps) == combinedProps) {
                if (outSelectedFlags) {
                    *outSelectedFlags = flags;
                }
                return i;
            }
        }
    }

    // Pass 2: Fallback to searching for required properties only
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        const bool isTypeSupported = (typeFilter & (1U << i)) != 0;
        const VkMemoryPropertyFlags flags = memProperties.memoryTypes[i].propertyFlags;
        if (isTypeSupported && (flags & requiredProperties) == requiredProperties) {
            if (outSelectedFlags) {
                *outSelectedFlags = flags;
            }
            return i;
        }
    }

    SIDERA_LOG_ERROR(
        "Buffer",
        "Failed to find suitable Vulkan memory type (typeFilter=0x{:X}, requiredProps=0x{:X}).",
        typeFilter,
        requiredProperties
    );
    throw std::runtime_error("Failed to find suitable Vulkan device memory type for buffer.");
}

} // namespace sidera
