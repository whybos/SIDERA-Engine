#include "renderer/vulkan/VulkanShadowMap.h"
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
        case VK_FORMAT_D16_UNORM:           return "VK_FORMAT_D16_UNORM";
        default:                            return "UNKNOWN_DEPTH_FORMAT";
    }
}

} // namespace

VkFormat VulkanShadowMap::findShadowDepthFormat(VkPhysicalDevice physicalDevice) {
    const std::vector<VkFormat> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };

    const VkFormatFeatureFlags requiredFeatures =
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

    for (VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if ((props.optimalTilingFeatures & requiredFeatures) == requiredFeatures) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find a supported sampled Vulkan depth format for shadow mapping.");
}

VulkanShadowMap::VulkanShadowMap(const VulkanDevice& device, uint32_t resolution)
    : m_device(device)
    , m_resolution(resolution)
    , m_format(findShadowDepthFormat(device.getPhysicalDevice())) {
    createResources();
    SIDERA_LOG_INFO(
        "ShadowMap",
        "Created directional shadow map ({}x{} px, format: {}).",
        m_resolution,
        m_resolution,
        depthFormatToString(m_format)
    );
}

VulkanShadowMap::~VulkanShadowMap() {
    SIDERA_LOG_INFO(
        "ShadowMap",
        "Destroying directional shadow map ({}x{} px)...",
        m_resolution,
        m_resolution
    );
    cleanup();
}

void VulkanShadowMap::recreate(uint32_t resolution) {
    cleanup();
    m_resolution = resolution;
    m_format = findShadowDepthFormat(m_device.getPhysicalDevice());
    createResources();
    SIDERA_LOG_INFO(
        "ShadowMap",
        "Recreated directional shadow map ({}x{} px, format: {}).",
        m_resolution,
        m_resolution,
        depthFormatToString(m_format)
    );
}

void VulkanShadowMap::createResources() {
    const VkDevice logicalDevice = m_device.getLogicalDevice();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_resolution;
    imageInfo.extent.height = m_resolution;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(
        vkCreateImage(logicalDevice, &imageInfo, nullptr, &m_image),
        "Failed to create shadow map VkImage"
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
            "Failed to allocate shadow map device memory"
        );

        VK_CHECK(
            vkBindImageMemory(logicalDevice, m_image, m_memory, 0),
            "Failed to bind shadow map image memory"
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
            "Failed to create shadow map VkImageView"
        );

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 1.0f;

        VK_CHECK(
            vkCreateSampler(logicalDevice, &samplerInfo, nullptr, &m_sampler),
            "Failed to create shadow map VkSampler"
        );

        m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    } catch (...) {
        cleanup();
        throw;
    }
}

void VulkanShadowMap::transitionToAttachment(VkCommandBuffer cmd) const noexcept {
    const VkPipelineStageFlags2 srcStage = (m_currentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        ? VK_PIPELINE_STAGE_2_NONE
        : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    const VkAccessFlags2 srcAccess = (m_currentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        ? VK_ACCESS_2_NONE
        : VK_ACCESS_2_SHADER_READ_BIT;

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = m_currentLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    m_currentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
}

void VulkanShadowMap::transitionToShaderRead(VkCommandBuffer cmd) const noexcept {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    m_currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void VulkanShadowMap::cleanup() noexcept {
    const VkDevice logicalDevice = m_device.getLogicalDevice();
    if (logicalDevice == VK_NULL_HANDLE) {
        return;
    }

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(logicalDevice, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }

    if (m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(logicalDevice, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }

    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(logicalDevice, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }

    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(logicalDevice, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }

    m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

} // namespace sidera
