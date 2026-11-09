#include "renderer/vulkan/VulkanDepthBuffer.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanDevice.h"

#include <stdexcept>
#include <string_view>
#include <vector>

namespace sidera {

namespace {
constexpr std::string_view depthFormatToString(VkFormat format) noexcept {
    switch (format) {
        case VK_FORMAT_D32_SFLOAT:          return "VK_FORMAT_D32_SFLOAT";
        case VK_FORMAT_D32_SFLOAT_S8_UINT:  return "VK_FORMAT_D32_SFLOAT_S8_UINT";
        case VK_FORMAT_D24_UNORM_S8_UINT:   return "VK_FORMAT_D24_UNORM_S8_UINT";
        default:                            return "UNKNOWN_DEPTH_FORMAT";
    }
}
} // namespace

VkFormat VulkanDepthBuffer::findDepthFormat(VkPhysicalDevice physicalDevice) {
    const std::vector<VkFormat> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };

    for (VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ==
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find a supported Vulkan depth attachment format.");
}

VulkanDepthBuffer::VulkanDepthBuffer(const VulkanDevice& device, uint32_t width, uint32_t height)
    : m_device(device)
    , m_format(findDepthFormat(device.getPhysicalDevice())) {
    createResources(width, height);
    SIDERA_LOG_INFO(
        "DepthBuffer",
        "Created depth buffer ({}x{} px, format: {}).",
        m_extent.width,
        m_extent.height,
        depthFormatToString(m_format)
    );
}

VulkanDepthBuffer::~VulkanDepthBuffer() {
    SIDERA_LOG_INFO(
        "DepthBuffer",
        "Destroying depth buffer ({}x{} px)...",
        m_extent.width,
        m_extent.height
    );
    cleanup();
}

void VulkanDepthBuffer::recreate(uint32_t width, uint32_t height) {
    cleanup();
    createResources(width, height);
    SIDERA_LOG_INFO(
        "DepthBuffer",
        "Recreated depth buffer ({}x{} px, format: {}).",
        m_extent.width,
        m_extent.height,
        depthFormatToString(m_format)
    );
}

void VulkanDepthBuffer::createResources(uint32_t width, uint32_t height) {
    m_extent = {width, height};
    const VkDevice logicalDevice = m_device.getLogicalDevice();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(
        vkCreateImage(logicalDevice, &imageInfo, nullptr, &m_image),
        "Failed to create depth buffer VkImage"
    );

    try {
        VkMemoryRequirements memRequirements{};
        vkGetImageMemoryRequirements(logicalDevice, m_image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = VulkanBuffer::findMemoryType(
            m_device.getPhysicalDevice(),
            memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        VK_CHECK(
            vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &m_memory),
            "Failed to allocate DEVICE_LOCAL memory for depth buffer"
        );

        VK_CHECK(
            vkBindImageMemory(logicalDevice, m_image, m_memory, 0),
            "Failed to bind depth buffer image memory"
        );

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VK_CHECK(
            vkCreateImageView(logicalDevice, &viewInfo, nullptr, &m_imageView),
            "Failed to create depth buffer VkImageView"
        );
    } catch (...) {
        cleanup();
        throw;
    }
}

void VulkanDepthBuffer::cleanup() noexcept {
    const VkDevice dev = m_device.getLogicalDevice();
    if (dev == VK_NULL_HANDLE) {
        return;
    }

    if (m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }

    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(dev, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }

    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
}

} // namespace sidera
