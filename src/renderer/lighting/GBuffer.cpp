#include "renderer/lighting/GBuffer.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanDevice.h"

#include <algorithm>

namespace sidera {

GBuffer::~GBuffer() {
    destroy();
}

bool GBuffer::init(const VulkanDevice& device, uint32_t width, uint32_t height) {
    m_device = &device;
    return resize(width, height);
}

bool GBuffer::resize(uint32_t width, uint32_t height) {
    if (!m_device || width == 0 || height == 0) return false;
    destroy();

    m_width = width;
    m_height = height;

    // 0: Albedo (RGBA8), 1: HDR World Normal (RGBA16F), 2: Material Rough/Metal/AO (RGBA8), 3: HDR Emissive (RGBA16F)
    const std::array<VkFormat, kAttachmentCount> formats = {
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R16G16B16A16_SFLOAT
    };

    m_memoryBytes = 0;
    for (uint32_t i = 0; i < kAttachmentCount; ++i) {
        if (!createAttachment(m_attachments[i], formats[i], width, height)) {
            destroy();
            return false;
        }
        const uint64_t bpp = (formats[i] == VK_FORMAT_R16G16B16A16_SFLOAT) ? 8ULL : 4ULL;
        m_memoryBytes += static_cast<uint64_t>(width) * height * bpp;
    }

    VkSamplerCreateInfo sampInfo{};
    sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampInfo.magFilter = VK_FILTER_NEAREST;
    sampInfo.minFilter = VK_FILTER_NEAREST;
    sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(m_device->getLogicalDevice(), &sampInfo, nullptr, &m_sampler),
             "Failed to create G-Buffer sampler");

    m_initialized = true;
    SIDERA_LOG_INFO("GBuffer", "Created G-Buffer ({}x{}, 4 attachments, {:.2f} MB).",
                    width, height, static_cast<double>(m_memoryBytes) / (1024.0 * 1024.0));
    return true;
}

void GBuffer::destroy() noexcept {
    if (m_device && m_device->getLogicalDevice() != VK_NULL_HANDLE) {
        const VkDevice dev = m_device->getLogicalDevice();
        if (m_sampler != VK_NULL_HANDLE) {
            vkDestroySampler(dev, m_sampler, nullptr);
            m_sampler = VK_NULL_HANDLE;
        }
        for (auto& a : m_attachments) {
            destroyAttachment(a);
        }
    }
    m_memoryBytes = 0;
    m_initialized = false;
}

bool GBuffer::createAttachment(Attachment& out, VkFormat format, uint32_t width, uint32_t height) {
    const VkDevice dev = m_device->getLogicalDevice();
    out.format = format;

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = format;
    imgInfo.extent = {width, height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VK_CHECK(vkCreateImage(dev, &imgInfo, nullptr, &out.image), "Failed to create G-Buffer image");

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(dev, out.image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = VulkanBuffer::findMemoryType(
        m_device->getPhysicalDevice(), memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vkAllocateMemory(dev, &allocInfo, nullptr, &out.memory), "Failed to allocate G-Buffer memory");
    VK_CHECK(vkBindImageMemory(dev, out.image, out.memory, 0), "Failed to bind G-Buffer image memory");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = out.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(dev, &viewInfo, nullptr, &out.view), "Failed to create G-Buffer image view");
    return true;
}

void GBuffer::destroyAttachment(Attachment& a) noexcept {
    const VkDevice dev = m_device->getLogicalDevice();
    if (a.view != VK_NULL_HANDLE) { vkDestroyImageView(dev, a.view, nullptr); a.view = VK_NULL_HANDLE; }
    if (a.image != VK_NULL_HANDLE) { vkDestroyImage(dev, a.image, nullptr); a.image = VK_NULL_HANDLE; }
    if (a.memory != VK_NULL_HANDLE) { vkFreeMemory(dev, a.memory, nullptr); a.memory = VK_NULL_HANDLE; }
}

void GBuffer::transitionToAttachments(VkCommandBuffer cmd) const noexcept {
    std::array<VkImageMemoryBarrier2, kAttachmentCount> barriers{};
    for (uint32_t i = 0; i < kAttachmentCount; ++i) {
        barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barriers[i].srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barriers[i].srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barriers[i].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barriers[i].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[i].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[i].image = m_attachments[i].image;
        barriers[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, kAttachmentCount, barriers.data()};
    vkCmdPipelineBarrier2(cmd, &dep);
}

void GBuffer::transitionToShaderRead(VkCommandBuffer cmd) const noexcept {
    std::array<VkImageMemoryBarrier2, kAttachmentCount> barriers{};
    for (uint32_t i = 0; i < kAttachmentCount; ++i) {
        barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barriers[i].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barriers[i].srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[i].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barriers[i].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barriers[i].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[i].image = m_attachments[i].image;
        barriers[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, kAttachmentCount, barriers.data()};
    vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace sidera
