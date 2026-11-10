#include "renderer/vulkan/VulkanHiZBuffer.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanDevice.h"
#include "renderer/vulkan/VulkanShader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace sidera {

namespace {
constexpr const char* kHiZGenShaderPath = "shaders/hiz_generate.comp.spv";
constexpr const char* kHiZDownsampleShaderPath = "shaders/hiz_downsample.comp.spv";

struct HiZPushConstants {
    float outWidth;
    float outHeight;
};

void transitionImageLayout(VkCommandBuffer cmd,
                           VkImage image,
                           uint32_t baseMip,
                           uint32_t levelCount,
                           VkImageLayout oldLayout,
                           VkImageLayout newLayout,
                           VkPipelineStageFlags2 srcStage,
                           VkAccessFlags2 srcAccess,
                           VkPipelineStageFlags2 dstStage,
                           VkAccessFlags2 dstAccess) noexcept {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMip;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);
}
} // namespace

VulkanHiZBuffer::VulkanHiZBuffer(const VulkanDevice& device, uint32_t width, uint32_t height)
    : m_device(device) {
    createResources(width, height);
    createComputePipelines();
    SIDERA_LOG_INFO(
        "HiZ",
        "Created VulkanHiZBuffer ({}x{} px, {} mip levels, VK_FORMAT_R32_SFLOAT).",
        m_extent.width,
        m_extent.height,
        m_mipLevels
    );
}

VulkanHiZBuffer::~VulkanHiZBuffer() {
    destroyComputePipelines();
    cleanup();
    SIDERA_LOG_INFO("HiZ", "Destroyed VulkanHiZBuffer.");
}

void VulkanHiZBuffer::recreate(uint32_t width, uint32_t height) {
    cleanup();
    createResources(width, height);
}

void VulkanHiZBuffer::createResources(uint32_t width, uint32_t height) {
    m_extent = {width, height};
    m_mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    const VkDevice device = m_device.getLogicalDevice();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = m_mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R32_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateImage(device, &imageInfo, nullptr, &m_image), "Failed to create Hi-Z image");

    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(device, m_image, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = VulkanBuffer::findMemoryType(
        m_device.getPhysicalDevice(), memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &m_memory), "Failed to allocate memory for Hi-Z buffer");
    VK_CHECK(vkBindImageMemory(device, m_image, m_memory, 0), "Failed to bind memory for Hi-Z buffer");

    // Full image view (samples all mips via textureLod)
    VkImageViewCreateInfo fullViewInfo{};
    fullViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    fullViewInfo.image = m_image;
    fullViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    fullViewInfo.format = VK_FORMAT_R32_SFLOAT;
    fullViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    fullViewInfo.subresourceRange.baseMipLevel = 0;
    fullViewInfo.subresourceRange.levelCount = m_mipLevels;
    fullViewInfo.subresourceRange.baseArrayLayer = 0;
    fullViewInfo.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(device, &fullViewInfo, nullptr, &m_fullImageView), "Failed to create full Hi-Z view");

    // Individual per-mip image views (bound as writeonly storage images during compute downsampling)
    m_mipImageViews.resize(m_mipLevels);
    for (uint32_t i = 0; i < m_mipLevels; ++i) {
        VkImageViewCreateInfo mipViewInfo{};
        mipViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        mipViewInfo.image = m_image;
        mipViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        mipViewInfo.format = VK_FORMAT_R32_SFLOAT;
        mipViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        mipViewInfo.subresourceRange.baseMipLevel = i;
        mipViewInfo.subresourceRange.levelCount = 1;
        mipViewInfo.subresourceRange.baseArrayLayer = 0;
        mipViewInfo.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(device, &mipViewInfo, nullptr, &m_mipImageViews[i]), "Failed to create Hi-Z mip view");
    }

    // Hi-Z sampler: nearest filtering with clamp to edge (preserves exact maximum depth values)
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(m_mipLevels);
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler), "Failed to create Hi-Z sampler");
}

void VulkanHiZBuffer::cleanup() noexcept {
    const VkDevice device = m_device.getLogicalDevice();
    if (device == VK_NULL_HANDLE) {
        return;
    }

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }

    for (VkImageView view : m_mipImageViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
        }
    }
    m_mipImageViews.clear();

    if (m_fullImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_fullImageView, nullptr);
        m_fullImageView = VK_NULL_HANDLE;
    }

    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }

    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
}

void VulkanHiZBuffer::createComputePipelines() {
    const VkDevice device = m_device.getLogicalDevice();

    // 1. Descriptor Pool for downsample passes
    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_mipLevels + 4},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_mipLevels + 4}
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = m_mipLevels + 4;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool), "Failed to create Hi-Z descriptor pool");

    // 2. Pass 0 Layout & Pipeline (Scene Depth -> HiZ Mip 0)
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_genDescSetLayout), "Failed to create Hi-Z gen set layout");

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(HiZPushConstants);

        VkPipelineLayoutCreateInfo pipeLayout{};
        pipeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeLayout.setLayoutCount = 1;
        pipeLayout.pSetLayouts = &m_genDescSetLayout;
        pipeLayout.pushConstantRangeCount = 1;
        pipeLayout.pPushConstantRanges = &pcRange;
        VK_CHECK(vkCreatePipelineLayout(device, &pipeLayout, nullptr, &m_genPipelineLayout), "Failed to create Hi-Z gen pipeline layout");

        VulkanShader shader(device, kHiZGenShaderPath);
        VkComputePipelineCreateInfo computePipe{};
        computePipe.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipe.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        computePipe.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        computePipe.stage.module = shader.getHandle();
        computePipe.stage.pName = "main";
        computePipe.layout = m_genPipelineLayout;
        VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipe, nullptr, &m_genPipeline), "Failed to create Hi-Z gen pipeline");
    }

    // 3. Pass 1..N-1 Layout & Pipeline (HiZ Mip k-1 -> HiZ Mip k)
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_downsampleDescSetLayout), "Failed to create Hi-Z downsample set layout");

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(HiZPushConstants);

        VkPipelineLayoutCreateInfo pipeLayout{};
        pipeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeLayout.setLayoutCount = 1;
        pipeLayout.pSetLayouts = &m_downsampleDescSetLayout;
        pipeLayout.pushConstantRangeCount = 1;
        pipeLayout.pPushConstantRanges = &pcRange;
        VK_CHECK(vkCreatePipelineLayout(device, &pipeLayout, nullptr, &m_downsamplePipelineLayout), "Failed to create Hi-Z downsample pipeline layout");

        VulkanShader shader(device, kHiZDownsampleShaderPath);
        VkComputePipelineCreateInfo computePipe{};
        computePipe.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipe.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        computePipe.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        computePipe.stage.module = shader.getHandle();
        computePipe.stage.pName = "main";
        computePipe.layout = m_downsamplePipelineLayout;
        VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipe, nullptr, &m_downsamplePipeline), "Failed to create Hi-Z downsample pipeline");
    }
}

void VulkanHiZBuffer::destroyComputePipelines() noexcept {
    const VkDevice device = m_device.getLogicalDevice();
    if (device == VK_NULL_HANDLE) {
        return;
    }

    if (m_genPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_genPipeline, nullptr);
        m_genPipeline = VK_NULL_HANDLE;
    }
    if (m_genPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_genPipelineLayout, nullptr);
        m_genPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_genDescSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_genDescSetLayout, nullptr);
        m_genDescSetLayout = VK_NULL_HANDLE;
    }

    if (m_downsamplePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_downsamplePipeline, nullptr);
        m_downsamplePipeline = VK_NULL_HANDLE;
    }
    if (m_downsamplePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_downsamplePipelineLayout, nullptr);
        m_downsamplePipelineLayout = VK_NULL_HANDLE;
    }
    if (m_downsampleDescSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_downsampleDescSetLayout, nullptr);
        m_downsampleDescSetLayout = VK_NULL_HANDLE;
    }

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
}

void VulkanHiZBuffer::build(VkCommandBuffer cmd, VkImageView sceneDepthView, VkExtent2D /*sceneExtent*/) {
    const VkDevice device = m_device.getLogicalDevice();

    // Reset descriptor pool for this frame's downsample dispatches
    vkResetDescriptorPool(device, m_descriptorPool, 0);

    // Transition all Hi-Z mips to GENERAL layout for compute storage writes and reads
    transitionImageLayout(
        cmd, m_image, 0, m_mipLevels,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT
    );

    // -------------------------------------------------------------------------
    // Pass 0: Scene Depth -> Hi-Z Mip 0
    // -------------------------------------------------------------------------
    {
        VkDescriptorSet genSet = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_genDescSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &genSet), "Failed to allocate gen descriptor set");

        VkDescriptorImageInfo depthInfo{};
        depthInfo.sampler = m_sampler;
        depthInfo.imageView = sceneDepthView;
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo outMip0Info{};
        outMip0Info.imageView = m_mipImageViews[0];
        outMip0Info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = genSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &depthInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = genSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &outMip0Info;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_genPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_genPipelineLayout, 0, 1, &genSet, 0, nullptr);

        const HiZPushConstants pc{
            .outWidth = static_cast<float>(m_extent.width),
            .outHeight = static_cast<float>(m_extent.height)
        };
        vkCmdPushConstants(cmd, m_genPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (m_extent.width + 15) / 16;
        const uint32_t groupsY = (m_extent.height + 15) / 16;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    }

    // -------------------------------------------------------------------------
    // Passes 1..M-1: Downsample Mip k-1 into Mip k
    // -------------------------------------------------------------------------
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_downsamplePipeline);

    for (uint32_t k = 1; k < m_mipLevels; ++k) {
        // Barrier on Mip k-1: compute write -> compute read
        transitionImageLayout(
            cmd, m_image, k - 1, 1,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT
        );

        const uint32_t mipW = std::max(1u, m_extent.width >> k);
        const uint32_t mipH = std::max(1u, m_extent.height >> k);

        VkDescriptorSet downsampleSet = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_downsampleDescSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &downsampleSet), "Failed to allocate downsample descriptor set");

        VkDescriptorImageInfo inMipInfo{};
        inMipInfo.sampler = m_sampler;
        inMipInfo.imageView = m_mipImageViews[k - 1];
        inMipInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo outMipInfo{};
        outMipInfo.imageView = m_mipImageViews[k];
        outMipInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = downsampleSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &inMipInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = downsampleSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &outMipInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_downsamplePipelineLayout, 0, 1, &downsampleSet, 0, nullptr);

        const HiZPushConstants pc{
            .outWidth = static_cast<float>(mipW),
            .outHeight = static_cast<float>(mipH)
        };
        vkCmdPushConstants(cmd, m_downsamplePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (mipW + 15) / 16;
        const uint32_t groupsY = (mipH + 15) / 16;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    }

    // Final barrier on all mips: transition to SHADER_READ_ONLY_OPTIMAL for culling in next frame
    transitionImageLayout(
        cmd, m_image, 0, m_mipLevels,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT
    );
}

} // namespace sidera
