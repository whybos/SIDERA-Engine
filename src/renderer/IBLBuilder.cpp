#include "renderer/IBLBuilder.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanDevice.h"
#include "renderer/vulkan/VulkanShader.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cmath>
#include <algorithm>
#include <vector>

namespace sidera {

namespace {

void runCompute(VkDevice device,
                VkQueue queue,
                VkCommandPool pool,
                VkPipeline pipeline,
                VkPipelineLayout layout,
                VkDescriptorSet descriptorSet,
                uint32_t groupCountX,
                uint32_t groupCountY,
                uint32_t groupCountZ,
                const void* pushConstants = nullptr,
                uint32_t pushConstantsSize = 0) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &cmd), "Failed to allocate compute command buffer");

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &fence), "Failed to create compute fence");

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "Failed to begin compute command buffer");

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &descriptorSet, 0, nullptr);

    if (pushConstants && pushConstantsSize > 0) {
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstantsSize, pushConstants);
    }

    vkCmdDispatch(cmd, groupCountX, groupCountY, groupCountZ);

    VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end compute command buffer");

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, fence), "Failed to submit compute command buffer");
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "Failed waiting for compute fence");

    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
}

void transitionCubemapLayout(VkDevice device,
                             VkQueue queue,
                             VkCommandPool pool,
                             VkImage image,
                             VkImageLayout oldLayout,
                             VkImageLayout newLayout,
                             uint32_t baseMip,
                             uint32_t mipCount,
                             VkPipelineStageFlags2 srcStage,
                             VkAccessFlags2 srcAccess,
                             VkPipelineStageFlags2 dstStage,
                             VkAccessFlags2 dstAccess) {
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &cmd), "Failed to allocate transition cmd buffer");

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "Failed to begin transition cmd buffer");

    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
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
    barrier.subresourceRange.levelCount = mipCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 6;

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);

    VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end transition cmd buffer");

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit transition");
    VK_CHECK(vkQueueWaitIdle(queue), "Failed waiting for transition");

    vkFreeCommandBuffers(device, pool, 1, &cmd);
}

void transitionTextureLayout(VkDevice device,
                            VkQueue queue,
                            VkCommandPool pool,
                            VkImage image,
                            VkImageLayout oldLayout,
                            VkImageLayout newLayout,
                            VkPipelineStageFlags2 srcStage,
                            VkAccessFlags2 srcAccess,
                            VkPipelineStageFlags2 dstStage,
                            VkAccessFlags2 dstAccess) {
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &cmd), "Failed to allocate transition cmd buffer");

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "Failed to begin transition cmd buffer");

    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
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
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);

    VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end transition cmd buffer");

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit transition");
    VK_CHECK(vkQueueWaitIdle(queue), "Failed waiting for transition");

    vkFreeCommandBuffers(device, pool, 1, &cmd);
}

} // namespace

std::unique_ptr<VulkanTexture> IBLBuilder::loadHdrTexture(const VulkanDevice& device,
                                                         VkCommandPool commandPool,
                                                         const std::filesystem::path& hdrPath) {
    if (!hdrPath.empty() && std::filesystem::exists(hdrPath)) {
        int w = 0, h = 0, comp = 0;
        stbi_set_flip_vertically_on_load(0);
        float* data = stbi_loadf(hdrPath.string().c_str(), &w, &h, &comp, 4);
        if (data) {
            SIDERA_LOG_INFO("IBL", "Loaded HDR environment map from '{}' ({}x{}).", hdrPath.string(), w, h);
            auto tex = std::make_unique<VulkanTexture>(
                device,
                commandPool,
                data,
                static_cast<uint32_t>(w),
                static_cast<uint32_t>(h),
                VK_FORMAT_R32G32B32A32_SFLOAT,
                0,
                hdrPath.stem().string()
            );
            stbi_image_free(data);
            return tex;
        } else {
            SIDERA_LOG_WARN("IBL", "Failed to load HDR image '{}': {}. Falling back to procedural HDR sky.",
                            hdrPath.string(), stbi_failure_reason());
        }
    }
    return createProceduralHdrTexture(device, commandPool);
}

std::unique_ptr<VulkanTexture> IBLBuilder::createProceduralHdrTexture(const VulkanDevice& device,
                                                                    VkCommandPool commandPool,
                                                                    uint32_t width,
                                                                    uint32_t height) {
    std::vector<float> pixels(static_cast<size_t>(width) * height * 4, 0.0f);
    constexpr float PI = 3.14159265358979323846f;

    // Direction to sun in sky
    const float sunAzimuth = 0.8f * PI;
    const float sunAltitude = 0.35f * PI;
    const float sunDx = std::cos(sunAltitude) * std::cos(sunAzimuth);
    const float sunDy = std::sin(sunAltitude);
    const float sunDz = std::cos(sunAltitude) * std::sin(sunAzimuth);

    for (uint32_t y = 0; y < height; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
        const float theta = (v - 0.5f) * PI; // -PI/2 to PI/2

        for (uint32_t x = 0; x < width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            const float phi = (u - 0.5f) * 2.0f * PI; // -PI to PI

            const float dx = std::cos(theta) * std::cos(phi);
            const float dy = std::sin(theta);
            const float dz = std::cos(theta) * std::sin(phi);

            float r = 0.0f, g = 0.0f, b = 0.0f;

            if (dy >= 0.0f) {
                // Sky gradient: zenith blue to horizon warm haze
                const float horizonFactor = std::pow(1.0f - dy, 3.0f);
                r = 0.25f + horizonFactor * 0.45f;
                g = 0.45f + horizonFactor * 0.35f;
                b = 0.85f + horizonFactor * 0.20f;

                // Bright directional sun disc
                const float dotSun = dx * sunDx + dy * sunDy + dz * sunDz;
                if (dotSun > 0.0f) {
                    const float sunGlow = std::pow(dotSun, 32.0f) * 1.5f;
                    const float sunCore = std::pow(dotSun, 512.0f) * 25.0f;
                    r += (sunGlow + sunCore) * 1.0f;
                    g += (sunGlow + sunCore) * 0.95f;
                    b += (sunGlow + sunCore) * 0.80f;
                }
            } else {
                // Ground: darker neutral dirt/pavement gradient
                const float groundFactor = std::clamp(1.0f + dy * 2.0f, 0.0f, 1.0f);
                r = 0.12f * groundFactor + 0.05f;
                g = 0.10f * groundFactor + 0.04f;
                b = 0.09f * groundFactor + 0.04f;
            }

            const size_t idx = (static_cast<size_t>(y) * width + x) * 4;
            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
            pixels[idx + 3] = 1.0f;
        }
    }

    SIDERA_LOG_INFO("IBL", "Generated procedural physical HDR sky ({}x{} px).", width, height);

    return std::make_unique<VulkanTexture>(
        device,
        commandPool,
        pixels.data(),
        width,
        height,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        0,
        "ProceduralHDRSky"
    );
}

IBLMaps IBLBuilder::build(const VulkanDevice& device,
                          VkCommandPool commandPool,
                          const std::filesystem::path& hdrPath) {
    const VkDevice dev = device.getLogicalDevice();
    const VkQueue queue = device.getGraphicsQueue();

    // 1. Load or generate HDR 2D equirectangular texture
    auto hdrTexture = loadHdrTexture(device, commandPool, hdrPath);

    // 2. Allocate output cubemaps and LUT
    constexpr uint32_t kEnvSize = 512;
    constexpr uint32_t kIrrSize = 32;
    constexpr uint32_t kPrefilterSize = 128;
    constexpr uint32_t kPrefilterMips = 5; // 128, 64, 32, 16, 8
    constexpr uint32_t kLutSize = 512;

    auto envCubemap = std::make_unique<VulkanCubemap>(device, kEnvSize, kEnvSize, 1, VK_FORMAT_R16G16B16A16_SFLOAT, "EnvCubemap");
    auto irradianceMap = std::make_unique<VulkanCubemap>(device, kIrrSize, kIrrSize, 1, VK_FORMAT_R16G16B16A16_SFLOAT, "IrradianceCubemap");
    auto prefilteredMap = std::make_unique<VulkanCubemap>(device, kPrefilterSize, kPrefilterSize, kPrefilterMips, VK_FORMAT_R16G16B16A16_SFLOAT, "PrefilteredCubemap");
    auto brdfLut = std::make_unique<VulkanTexture>(device, kLutSize, kLutSize, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, "BRDF_LUT");

    // Descriptor pool for compute operations
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16}
    };
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 16;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(dev, &poolInfo, nullptr, &descPool), "Failed to create IBL compute desc pool");

    // Helper RAII to clean up compute pipelines/layouts
    struct ComputeShaderResource {
        VkDevice dev;
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
        VkPipeline pipe = VK_NULL_HANDLE;

        ~ComputeShaderResource() {
            if (pipe != VK_NULL_HANDLE) vkDestroyPipeline(dev, pipe, nullptr);
            if (pipeLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, pipeLayout, nullptr);
            if (setLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, setLayout, nullptr);
        }
    };

    // -------------------------------------------------------------
    // PASS 1: Equirectangular to Cubemap
    // -------------------------------------------------------------
    {
        SIDERA_LOG_INFO("IBL", "Starting Pass 1: Equirectangular HDR -> 512x512 Cubemap...");
        ComputeShaderResource res{dev};
        VulkanShader shader(dev, "shaders/equirect_to_cubemap.comp.spv");

        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &res.setLayout), "Failed set layout");

        VkPipelineLayoutCreateInfo pipeLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeLayoutInfo.setLayoutCount = 1;
        pipeLayoutInfo.pSetLayouts = &res.setLayout;
        VK_CHECK(vkCreatePipelineLayout(dev, &pipeLayoutInfo, nullptr, &res.pipeLayout), "Failed pipe layout");

        VkComputePipelineCreateInfo compPipeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        compPipeInfo.layout = res.pipeLayout;
        compPipeInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, shader.getHandle(), "main", nullptr};
        VK_CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &compPipeInfo, nullptr, &res.pipe), "Failed compute pipeline");

        // Prepare cubemap image for general storage write
        transitionCubemapLayout(dev, queue, commandPool, envCubemap->image(),
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                0, 1,
                                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

        VkImageView mipView = envCubemap->createMipView(0);

        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = descPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &res.setLayout;
        VK_CHECK(vkAllocateDescriptorSets(dev, &allocInfo, &set), "Failed to allocate compute desc set");

        VkDescriptorImageInfo imgInfoIn{};
        imgInfoIn.sampler = hdrTexture->sampler();
        imgInfoIn.imageView = hdrTexture->imageView();
        imgInfoIn.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo imgInfoOut{};
        imgInfoOut.imageView = mipView;
        imgInfoOut.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet writes[2]{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgInfoIn, nullptr, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &imgInfoOut, nullptr, nullptr};
        vkUpdateDescriptorSets(dev, 2, writes, 0, nullptr);

        runCompute(dev, queue, commandPool, res.pipe, res.pipeLayout, set, kEnvSize / 16, kEnvSize / 16, 6);

        vkDestroyImageView(dev, mipView, nullptr);

        // Transition cubemap to shader read only
        transitionCubemapLayout(dev, queue, commandPool, envCubemap->image(),
                                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                0, 1,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        SIDERA_LOG_INFO("IBL", "Pass 1 complete: Equirectangular HDR converted to 512x512 cubemap.");
    }

    // -------------------------------------------------------------
    // PASS 2: Diffuse Irradiance Convolution
    // -------------------------------------------------------------
    {
        SIDERA_LOG_INFO("IBL", "Starting Pass 2: Diffuse Irradiance convolution...");
        ComputeShaderResource res{dev};
        VulkanShader shader(dev, "shaders/irradiance_convolution.comp.spv");

        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &res.setLayout), "Failed set layout");

        VkPipelineLayoutCreateInfo pipeLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeLayoutInfo.setLayoutCount = 1;
        pipeLayoutInfo.pSetLayouts = &res.setLayout;
        VK_CHECK(vkCreatePipelineLayout(dev, &pipeLayoutInfo, nullptr, &res.pipeLayout), "Failed pipe layout");

        VkComputePipelineCreateInfo compPipeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        compPipeInfo.layout = res.pipeLayout;
        compPipeInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, shader.getHandle(), "main", nullptr};
        VK_CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &compPipeInfo, nullptr, &res.pipe), "Failed compute pipeline");

        transitionCubemapLayout(dev, queue, commandPool, irradianceMap->image(),
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                0, 1,
                                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

        VkImageView mipView = irradianceMap->createMipView(0);

        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = descPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &res.setLayout;
        VK_CHECK(vkAllocateDescriptorSets(dev, &allocInfo, &set), "Failed to allocate compute desc set");

        VkDescriptorImageInfo imgInfoIn{};
        imgInfoIn.sampler = envCubemap->sampler();
        imgInfoIn.imageView = envCubemap->imageView();
        imgInfoIn.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo imgInfoOut{};
        imgInfoOut.imageView = mipView;
        imgInfoOut.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet writes[2]{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgInfoIn, nullptr, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &imgInfoOut, nullptr, nullptr};
        vkUpdateDescriptorSets(dev, 2, writes, 0, nullptr);

        runCompute(dev, queue, commandPool, res.pipe, res.pipeLayout, set, kIrrSize / 16, kIrrSize / 16, 6);

        vkDestroyImageView(dev, mipView, nullptr);

        transitionCubemapLayout(dev, queue, commandPool, irradianceMap->image(),
                                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                0, 1,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        SIDERA_LOG_INFO("IBL", "Pass 2 complete: Diffuse irradiance cubemap convolved (32x32).");
    }

    // -------------------------------------------------------------
    // PASS 3: Specular Prefiltered Cubemap (Roughness Mip Chain)
    // -------------------------------------------------------------
    {
        SIDERA_LOG_INFO("IBL", "Starting Pass 3: Specular Prefiltered cubemap (5 mips)...");
        ComputeShaderResource res{dev};
        VulkanShader shader(dev, "shaders/prefilter_cubemap.comp.spv");

        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &res.setLayout), "Failed set layout");

        VkPushConstantRange pcRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float) * 2};

        VkPipelineLayoutCreateInfo pipeLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeLayoutInfo.setLayoutCount = 1;
        pipeLayoutInfo.pSetLayouts = &res.setLayout;
        pipeLayoutInfo.pushConstantRangeCount = 1;
        pipeLayoutInfo.pPushConstantRanges = &pcRange;
        VK_CHECK(vkCreatePipelineLayout(dev, &pipeLayoutInfo, nullptr, &res.pipeLayout), "Failed pipe layout");

        VkComputePipelineCreateInfo compPipeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        compPipeInfo.layout = res.pipeLayout;
        compPipeInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, shader.getHandle(), "main", nullptr};
        VK_CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &compPipeInfo, nullptr, &res.pipe), "Failed compute pipeline");

        transitionCubemapLayout(dev, queue, commandPool, prefilteredMap->image(),
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                0, kPrefilterMips,
                                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

        for (uint32_t mip = 0; mip < kPrefilterMips; ++mip) {
            const uint32_t mipWidth = kPrefilterSize >> mip;
            const uint32_t mipHeight = kPrefilterSize >> mip;
            const float roughness = static_cast<float>(mip) / static_cast<float>(kPrefilterMips - 1);

            VkImageView mipView = prefilteredMap->createMipView(mip);

            VkDescriptorSet set = VK_NULL_HANDLE;
            VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocInfo.descriptorPool = descPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &res.setLayout;
            VK_CHECK(vkAllocateDescriptorSets(dev, &allocInfo, &set), "Failed to allocate compute desc set");

            VkDescriptorImageInfo imgInfoIn{};
            imgInfoIn.sampler = envCubemap->sampler();
            imgInfoIn.imageView = envCubemap->imageView();
            imgInfoIn.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo imgInfoOut{};
            imgInfoOut.imageView = mipView;
            imgInfoOut.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet writes[2]{};
            writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgInfoIn, nullptr, nullptr};
            writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &imgInfoOut, nullptr, nullptr};
            vkUpdateDescriptorSets(dev, 2, writes, 0, nullptr);

            struct { float r; float res; } pcData{roughness, static_cast<float>(kEnvSize)};
            const uint32_t groupX = std::max(1u, (mipWidth + 15) / 16);
            const uint32_t groupY = std::max(1u, (mipHeight + 15) / 16);

            runCompute(dev, queue, commandPool, res.pipe, res.pipeLayout, set, groupX, groupY, 6, &pcData, sizeof(pcData));

            vkDestroyImageView(dev, mipView, nullptr);
        }

        transitionCubemapLayout(dev, queue, commandPool, prefilteredMap->image(),
                                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                0, kPrefilterMips,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        SIDERA_LOG_INFO("IBL", "Pass 3 complete: Specular prefiltered cubemap generated (128x128, 5 mip levels).");
    }

    // -------------------------------------------------------------
    // PASS 4: BRDF 2D Integration LUT
    // -------------------------------------------------------------
    {
        SIDERA_LOG_INFO("IBL", "Starting Pass 4: BRDF 2D Integration LUT (512x512)...");
        ComputeShaderResource res{dev};
        VulkanShader shader(dev, "shaders/brdf_lut.comp.spv");

        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &res.setLayout), "Failed set layout");

        VkPipelineLayoutCreateInfo pipeLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeLayoutInfo.setLayoutCount = 1;
        pipeLayoutInfo.pSetLayouts = &res.setLayout;
        VK_CHECK(vkCreatePipelineLayout(dev, &pipeLayoutInfo, nullptr, &res.pipeLayout), "Failed pipe layout");

        VkComputePipelineCreateInfo compPipeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        compPipeInfo.layout = res.pipeLayout;
        compPipeInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, shader.getHandle(), "main", nullptr};
        VK_CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &compPipeInfo, nullptr, &res.pipe), "Failed compute pipeline");

        transitionTextureLayout(dev, queue, commandPool, brdfLut->image(),
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = descPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &res.setLayout;
        VK_CHECK(vkAllocateDescriptorSets(dev, &allocInfo, &set), "Failed to allocate compute desc set");

        VkDescriptorImageInfo imgInfoOut{};
        imgInfoOut.imageView = brdfLut->imageView();
        imgInfoOut.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &imgInfoOut, nullptr, nullptr};
        vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

        runCompute(dev, queue, commandPool, res.pipe, res.pipeLayout, set, kLutSize / 16, kLutSize / 16, 1);

        transitionTextureLayout(dev, queue, commandPool, brdfLut->image(),
                                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        SIDERA_LOG_INFO("IBL", "Pass 4 complete: BRDF integration LUT generated (512x512 R16G16F).");
    }

    vkDestroyDescriptorPool(dev, descPool, nullptr);

    SIDERA_LOG_INFO("IBL", "IBL maps generation complete. Ready for PBR rendering.");

    return IBLMaps{
        std::move(envCubemap),
        std::move(irradianceMap),
        std::move(prefilteredMap),
        std::move(brdfLut)
    };
}

} // namespace sidera
