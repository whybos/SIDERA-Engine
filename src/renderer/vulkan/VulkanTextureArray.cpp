#include "renderer/vulkan/VulkanTextureArray.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanDevice.h"
#include "renderer/vulkan/VulkanTexture.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sidera {

namespace {

void arrayImageBarrier(VkCommandBuffer cmd,
                       VkImage image,
                       VkImageLayout oldLayout,
                       VkImageLayout newLayout,
                       VkPipelineStageFlags2 srcStage,
                       VkAccessFlags2 srcAccess,
                       VkPipelineStageFlags2 dstStage,
                       VkAccessFlags2 dstAccess,
                       uint32_t baseMip,
                       uint32_t levelCount,
                       uint32_t baseLayer,
                       uint32_t layerCount) noexcept {
    VkImageMemoryBarrier2 b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, baseMip, levelCount, baseLayer, layerCount};

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace

VulkanTextureArray::VulkanTextureArray(const VulkanDevice& device,
                                       VkCommandPool transferCommandPool,
                                       const std::vector<const void*>& layerPixels,
                                       uint32_t width,
                                       uint32_t height,
                                       VkFormat format,
                                       bool generateMips,
                                       VkSampler externalSampler,
                                       std::string_view name)
    : m_device(device)
    , m_name(name)
    , m_width(width)
    , m_height(height)
    , m_layerCount(static_cast<uint32_t>(layerPixels.size()))
    , m_format(format)
    , m_generateMips(generateMips)
    , m_ownsSampler(externalSampler == VK_NULL_HANDLE)
    , m_sampler(externalSampler) {
    if (layerPixels.empty() || width == 0 || height == 0) {
        throw std::invalid_argument("VulkanTextureArray requires at least 1 layer and non-zero dimensions.");
    }

    if (m_generateMips && m_device.isLinearBlitSupported(m_format)) {
        m_mipLevels = VulkanTexture::calculateMipLevels(m_width, m_height);
    } else {
        m_mipLevels = 1;
    }

    m_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (m_mipLevels > 1) {
        m_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    try {
        createImageAndMemory();
        uploadLayers(transferCommandPool, layerPixels);
        createImageView();
        if (m_ownsSampler) {
            createSampler();
        }
    } catch (...) {
        cleanup();
        throw;
    }

    SIDERA_LOG_INFO(
        "TextureArray",
        "Created 2D Texture Array '{}' ({}x{} px, layers: {}, mips: {}, format: 0x{:X}, memory: {:.2f} KB).",
        m_name, m_width, m_height, m_layerCount, m_mipLevels, static_cast<uint32_t>(m_format),
        static_cast<double>(m_memorySize) / 1024.0
    );
}

VulkanTextureArray::~VulkanTextureArray() {
    SIDERA_LOG_INFO("TextureArray", "Destroying 2D Texture Array '{}'...", m_name);
    cleanup();
}

void VulkanTextureArray::createImageAndMemory() {
    const VkDevice dev = m_device.getLogicalDevice();

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = {m_width, m_height, 1};
    info.mipLevels = m_mipLevels;
    info.arrayLayers = m_layerCount;
    info.format = m_format;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = m_usage;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateImage(dev, &info, nullptr, &m_image), "Failed to create texture array VkImage");

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(dev, m_image, &req);
    m_memorySize = req.size;

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = VulkanBuffer::findMemoryType(
        m_device.getPhysicalDevice(), req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vkAllocateMemory(dev, &alloc, nullptr, &m_memory), "Failed to allocate texture array memory");
    VK_CHECK(vkBindImageMemory(dev, m_image, m_memory, 0), "Failed to bind texture array memory");
}

void VulkanTextureArray::uploadLayers(VkCommandPool pool, const std::vector<const void*>& layerPixels) {
    const size_t bpp = VulkanTexture::getFormatBytesPerPixel(m_format);
    const VkDeviceSize layerSize = static_cast<VkDeviceSize>(m_width) * m_height * bpp;
    const VkDeviceSize totalSize = layerSize * m_layerCount;

    VulkanBuffer staging(m_device, totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    for (uint32_t layer = 0; layer < m_layerCount; ++layer) {
        staging.uploadData(layerPixels[layer], layerSize, layer * layerSize);
    }

    const VkDevice dev = m_device.getLogicalDevice();

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandPool = pool;
    alloc.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(dev, &alloc, &cmd), "Failed to allocate texture array upload cmd buffer");

    VkFence fence = VK_NULL_HANDLE;
    struct Cleanup {
        VkDevice dev;
        VkCommandPool pool;
        VkCommandBuffer& cmd;
        VkFence& fence;
        ~Cleanup() {
            if (fence != VK_NULL_HANDLE) vkDestroyFence(dev, fence, nullptr);
            if (cmd != VK_NULL_HANDLE) vkFreeCommandBuffers(dev, pool, 1, &cmd);
        }
    } cleanup{dev, pool, cmd, fence};

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_CHECK(vkCreateFence(dev, &fenceInfo, nullptr, &fence), "Failed to create texture array upload fence");

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin), "Failed to begin texture array upload cmd buffer");

    // Transition all layers at mip 0 to TRANSFER_DST
    arrayImageBarrier(cmd, m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                      VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      0, 1, 0, m_layerCount);

    std::vector<VkBufferImageCopy> regions(m_layerCount);
    for (uint32_t layer = 0; layer < m_layerCount; ++layer) {
        regions[layer].bufferOffset = layer * layerSize;
        regions[layer].bufferRowLength = 0;
        regions[layer].bufferImageHeight = 0;
        regions[layer].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1};
        regions[layer].imageOffset = {0, 0, 0};
        regions[layer].imageExtent = {m_width, m_height, 1};
    }

    vkCmdCopyBufferToImage(cmd, staging.getHandle(), m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());

    if (m_mipLevels > 1) {
        generateMipmaps(cmd);
    } else {
        arrayImageBarrier(cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                          0, 1, 0, m_layerCount);
    }

    VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end texture array upload cmd buffer");

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmd;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;

    VK_CHECK(vkQueueSubmit2(m_device.getGraphicsQueue(), 1, &submit, fence), "Failed to submit texture array upload");
    VK_CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
             "Failed waiting for texture array upload fence");
}

void VulkanTextureArray::generateMipmaps(VkCommandBuffer cmd) {
    for (uint32_t layer = 0; layer < m_layerCount; ++layer) {
        int32_t mipWidth = static_cast<int32_t>(m_width);
        int32_t mipHeight = static_cast<int32_t>(m_height);

        for (uint32_t i = 1; i < m_mipLevels; ++i) {
            arrayImageBarrier(cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                              VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                              i - 1, 1, layer, 1);

            arrayImageBarrier(cmd, m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                              VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                              i, 1, layer, 1);

            const int32_t nextWidth = std::max(1, mipWidth / 2);
            const int32_t nextHeight = std::max(1, mipHeight / 2);

            VkImageBlit blit{};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, layer, 1};
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {nextWidth, nextHeight, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, layer, 1};

            vkCmdBlitImage(cmd,
                           m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit, VK_FILTER_LINEAR);

            arrayImageBarrier(cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                              i - 1, 1, layer, 1);

            mipWidth = nextWidth;
            mipHeight = nextHeight;
        }

        arrayImageBarrier(cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                          m_mipLevels - 1, 1, layer, 1);
    }
}

void VulkanTextureArray::createImageView() {
    VkImageViewCreateInfo v{};
    v.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    v.image = m_image;
    v.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    v.format = m_format;
    v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, m_mipLevels, 0, m_layerCount};
    VK_CHECK(vkCreateImageView(m_device.getLogicalDevice(), &v, nullptr, &m_imageView),
             "Failed to create texture array VkImageView");
}

void VulkanTextureArray::createSampler() {
    VkSamplerCreateInfo s{};
    s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    s.magFilter = VK_FILTER_LINEAR;
    s.minFilter = VK_FILTER_LINEAR;
    s.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    s.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    s.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    if (m_device.isSamplerAnisotropyEnabled()) {
        s.anisotropyEnable = VK_TRUE;
        s.maxAnisotropy = std::min(16.0f, m_device.getProperties().limits.maxSamplerAnisotropy);
    } else {
        s.anisotropyEnable = VK_FALSE;
        s.maxAnisotropy = 1.0f;
    }

    s.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    s.minLod = 0.0f;
    s.maxLod = static_cast<float>(m_mipLevels);

    VK_CHECK(vkCreateSampler(m_device.getLogicalDevice(), &s, nullptr, &m_sampler),
             "Failed to create texture array VkSampler");
}

void VulkanTextureArray::cleanup() noexcept {
    const VkDevice dev = m_device.getLogicalDevice();
    if (dev == VK_NULL_HANDLE) return;

    if (m_ownsSampler && m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(dev, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    if (m_imageView != VK_NULL_HANDLE) { vkDestroyImageView(dev, m_imageView, nullptr); m_imageView = VK_NULL_HANDLE; }
    if (m_image != VK_NULL_HANDLE)     { vkDestroyImage(dev, m_image, nullptr);         m_image = VK_NULL_HANDLE; }
    if (m_memory != VK_NULL_HANDLE)    { vkFreeMemory(dev, m_memory, nullptr);          m_memory = VK_NULL_HANDLE; }
}

} // namespace sidera
