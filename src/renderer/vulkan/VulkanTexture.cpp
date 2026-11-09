#include "renderer/vulkan/VulkanTexture.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanDevice.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace sidera {

namespace {
void imageBarrier(VkCommandBuffer cmd,
                  VkImage image,
                  VkImageLayout oldLayout,
                  VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage,
                  VkAccessFlags2 srcAccess,
                  VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess) noexcept {
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
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}
} // namespace

VulkanTexture::VulkanTexture(const VulkanDevice& device,
                             VkCommandPool transferCommandPool,
                             const void* rgbaPixels,
                             uint32_t width,
                             uint32_t height,
                             std::string_view name)
    : m_device(device)
    , m_name(name)
    , m_width(width)
    , m_height(height) {
    if (!rgbaPixels || width == 0 || height == 0) {
        throw std::invalid_argument("VulkanTexture requires pixel data and non-zero dimensions.");
    }

    try {
        createImageAndMemory();
        uploadPixelsToImage(transferCommandPool, rgbaPixels);
        createImageView();
        createSampler();
    } catch (...) {
        cleanup();
        throw;
    }

    SIDERA_LOG_INFO("Texture", "Created 2D Texture '{}' ({}x{} px, VK_FORMAT_R8G8B8A8_SRGB, DEVICE_LOCAL).",
                    m_name, m_width, m_height);
}

VulkanTexture::~VulkanTexture() {
    SIDERA_LOG_INFO("Texture", "Destroying 2D Texture '{}'...", m_name);
    cleanup();
}

std::unique_ptr<VulkanTexture> VulkanTexture::createProceduralTestTexture(
    const VulkanDevice& device,
    VkCommandPool transferCommandPool,
    uint32_t width,
    uint32_t height) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u, 255u);

    const uint32_t cells = 8;
    const uint32_t cellW = std::max(1u, width / cells);
    const uint32_t cellH = std::max(1u, height / cells);

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t cx = x / cellW;
            const uint32_t cy = y / cellH;
            const uint32_t lx = x % cellW;
            const uint32_t ly = y % cellH;

            const bool outerBorder = (x < 6 || x >= width - 6 || y < 6 || y >= height - 6);
            const bool gridLine = (lx < 2 || ly < 2);
            const bool checker = ((cx + cy) % 2) == 0;

            const float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f;
            const float ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(height) * 2.0f - 1.0f;
            const float radius = std::sqrt(nx * nx + ny * ny);
            const bool ring = (std::abs(radius - 0.58f) < 0.032f) || (std::abs(radius - 0.28f) < 0.025f);

            uint8_t r, g, b;
            if (outerBorder)      { r = 38;  g = 44;  b = 56;  }
            else if (gridLine)    { r = 55;  g = 64;  b = 78;  }
            else if (ring)        { r = 255; g = 255; b = 255; }
            else if (checker)     { r = 238; g = 242; b = 248; }
            else                  { r = 150; g = 160; b = 178; }

            const size_t idx = (static_cast<size_t>(y) * width + x) * 4u;
            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
            pixels[idx + 3] = 255;
        }
    }

    return std::make_unique<VulkanTexture>(device, transferCommandPool, pixels.data(), width, height,
                                           "ProceduralUVGrid");
}

void VulkanTexture::createImageAndMemory() {
    const VkDevice dev = m_device.getLogicalDevice();

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = {m_width, m_height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.format = m_format;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateImage(dev, &info, nullptr, &m_image), "Failed to create texture VkImage");

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(dev, m_image, &req);

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = VulkanBuffer::findMemoryType(
        m_device.getPhysicalDevice(), req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vkAllocateMemory(dev, &alloc, nullptr, &m_memory), "Failed to allocate texture memory");
    VK_CHECK(vkBindImageMemory(dev, m_image, m_memory, 0), "Failed to bind texture memory");
}

void VulkanTexture::uploadPixelsToImage(VkCommandPool pool, const void* rgbaPixels) {
    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(m_width) * m_height * 4u;

    VulkanBuffer staging(m_device, byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.uploadData(rgbaPixels, byteSize, 0);

    const VkDevice dev = m_device.getLogicalDevice();

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandPool = pool;
    alloc.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(dev, &alloc, &cmd), "Failed to allocate texture upload command buffer");

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
    VK_CHECK(vkCreateFence(dev, &fenceInfo, nullptr, &fence), "Failed to create texture upload fence");

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin), "Failed to begin texture upload command buffer");

    imageBarrier(cmd, m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {m_width, m_height, 1};
    vkCmdCopyBufferToImage(cmd, staging.getHandle(), m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    imageBarrier(cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end texture upload command buffer");

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmd;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;

    VK_CHECK(vkQueueSubmit2(m_device.getGraphicsQueue(), 1, &submit, fence), "Failed to submit texture upload");
    VK_CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
             "Failed waiting for texture upload fence");
}

void VulkanTexture::createImageView() {
    VkImageViewCreateInfo v{};
    v.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    v.image = m_image;
    v.viewType = VK_IMAGE_VIEW_TYPE_2D;
    v.format = m_format;
    v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(m_device.getLogicalDevice(), &v, nullptr, &m_imageView),
             "Failed to create texture VkImageView");
}

void VulkanTexture::createSampler() {
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

    s.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    s.minLod = 0.0f;
    s.maxLod = 0.0f;

    VK_CHECK(vkCreateSampler(m_device.getLogicalDevice(), &s, nullptr, &m_sampler),
             "Failed to create texture VkSampler");
}

void VulkanTexture::cleanup() noexcept {
    const VkDevice dev = m_device.getLogicalDevice();
    if (dev == VK_NULL_HANDLE) return;

    if (m_sampler != VK_NULL_HANDLE)   { vkDestroySampler(dev, m_sampler, nullptr);     m_sampler = VK_NULL_HANDLE; }
    if (m_imageView != VK_NULL_HANDLE) { vkDestroyImageView(dev, m_imageView, nullptr); m_imageView = VK_NULL_HANDLE; }
    if (m_image != VK_NULL_HANDLE)     { vkDestroyImage(dev, m_image, nullptr);         m_image = VK_NULL_HANDLE; }
    if (m_memory != VK_NULL_HANDLE)    { vkFreeMemory(dev, m_memory, nullptr);          m_memory = VK_NULL_HANDLE; }
}

} // namespace sidera
