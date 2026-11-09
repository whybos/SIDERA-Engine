#include "renderer/vulkan/VulkanCubemap.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanDevice.h"

#include <stdexcept>

namespace sidera {

VulkanCubemap::VulkanCubemap(const VulkanDevice& device,
                             uint32_t width,
                             uint32_t height,
                             uint32_t mipLevels,
                             VkFormat format,
                             std::string_view name)
    : m_device(device)
    , m_name(name)
    , m_width(width)
    , m_height(height)
    , m_mipLevels(mipLevels)
    , m_format(format) {
    if (width == 0 || height == 0 || mipLevels == 0) {
        throw std::invalid_argument("VulkanCubemap dimensions and mipLevels must be > 0.");
    }

    try {
        createImageAndMemory();
        createImageView();
        createSampler();
    } catch (...) {
        cleanup();
        throw;
    }

    SIDERA_LOG_INFO(
        "Cubemap",
        "Created VulkanCubemap '{}' ({}x{} px, mips: {}, format: 0x{:X}).",
        m_name,
        m_width,
        m_height,
        m_mipLevels,
        static_cast<uint32_t>(m_format)
    );
}

VulkanCubemap::~VulkanCubemap() {
    SIDERA_LOG_INFO("Cubemap", "Destroying VulkanCubemap '{}'...", m_name);
    cleanup();
}

void VulkanCubemap::createImageAndMemory() {
    const VkDevice dev = m_device.getLogicalDevice();

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = {m_width, m_height, 1};
    info.mipLevels = m_mipLevels;
    info.arrayLayers = 6;
    info.format = m_format;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateImage(dev, &info, nullptr, &m_image), "Failed to create cubemap VkImage");

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(dev, m_image, &req);

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = VulkanBuffer::findMemoryType(
        m_device.getPhysicalDevice(),
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    VK_CHECK(vkAllocateMemory(dev, &alloc, nullptr, &m_memory), "Failed to allocate cubemap memory");
    VK_CHECK(vkBindImageMemory(dev, m_image, m_memory, 0), "Failed to bind cubemap image memory");
}

void VulkanCubemap::createImageView() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = m_format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    VK_CHECK(
        vkCreateImageView(m_device.getLogicalDevice(), &viewInfo, nullptr, &m_imageView),
        "Failed to create cubemap VkImageView"
    );
}

VkImageView VulkanCubemap::createMipView(uint32_t mipLevel) const {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = m_format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = mipLevel;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    VkImageView view = VK_NULL_HANDLE;
    VK_CHECK(
        vkCreateImageView(m_device.getLogicalDevice(), &viewInfo, nullptr, &view),
        "Failed to create cubemap mip layer VkImageView"
    );
    return view;
}

void VulkanCubemap::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(m_mipLevels);
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    VK_CHECK(
        vkCreateSampler(m_device.getLogicalDevice(), &samplerInfo, nullptr, &m_sampler),
        "Failed to create cubemap VkSampler"
    );
}

void VulkanCubemap::cleanup() noexcept {
    const VkDevice dev = m_device.getLogicalDevice();
    if (dev == VK_NULL_HANDLE) {
        return;
    }

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(dev, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
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
