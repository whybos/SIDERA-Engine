#include "renderer/postprocess/PostProcessPipeline.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanDevice.h"
#include "renderer/vulkan/VulkanShader.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <vector>

namespace sidera {

namespace {
struct alignas(16) BloomPC {
    glm::vec4 params0{1.0f, 0.5f, 0.35f, 0.0f};
    glm::uvec4 dims{1u, 1u, 0u, 0u};
};
struct alignas(16) SSAOPC {
    glm::vec4 params0{0.65f, 0.025f, 1.25f, 0.0f};
    glm::vec4 projInfo{0.1f, 200.0f, 16.0f, 0.0f};
    glm::uvec4 dims{1u, 1u, 0u, 0u};
};
struct alignas(16) ToneMapPC {
    glm::vec4 params0{1.0f, 0.35f, 1.0f, 0.0f};
    glm::uvec4 dims{1u, 1u, 0u, 0u};
};
} // namespace

PostProcessPipeline::~PostProcessPipeline() {
    destroy();
}

VkFormat PostProcessPipeline::selectBestHDRFormat(VkPhysicalDevice physicalDevice) noexcept {
    if (physicalDevice == VK_NULL_HANDLE) {
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    const std::array<VkFormat, 3> candidates = {
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_B10G11R11_UFLOAT_PACK32,
        VK_FORMAT_R8G8B8A8_UNORM
    };
    const VkFormatFeatureFlags req = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    for (VkFormat fmt : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, fmt, &props);
        if ((props.optimalTilingFeatures & req) == req) {
            return fmt;
        }
    }
    return VK_FORMAT_R8G8B8A8_UNORM;
}

bool PostProcessPipeline::init(uint32_t width, uint32_t height, const VulkanDevice* device) {
    destroy();
    m_device = device;
    if (m_device) {
        VkSamplerCreateInfo sInfo{};
        sInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sInfo.magFilter = VK_FILTER_LINEAR;
        sInfo.minFilter = VK_FILTER_LINEAR;
        sInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_CHECK(vkCreateSampler(m_device->getLogicalDevice(), &sInfo, nullptr, &m_linearSampler),
                 "Failed to create post-process linear sampler");

        if (!createPipelines()) {
            SIDERA_LOG_WARN("PostProcess", "Post-process compute shaders unavailable; running headless fallback.");
        }
    }
    return resize(width, height);
}

bool PostProcessPipeline::resize(uint32_t width, uint32_t height) {
    const uint32_t w = std::max(width, 16u);
    const uint32_t h = std::max(height, 16u);
    const uint32_t halfW = std::max(w / 2u, 8u);
    const uint32_t halfH = std::max(h / 2u, 8u);

    m_stats.width = w;
    m_stats.height = h;
    m_stats.bloomWidth = halfW;
    m_stats.bloomHeight = halfH;
    m_stats.ssaoWidth = halfW;
    m_stats.ssaoHeight = halfH;

    if (m_device) {
        destroyTarget(m_hdrSceneTarget);
        destroyTarget(m_bloomA);
        destroyTarget(m_bloomB);
        destroyTarget(m_ssaoRaw);
        destroyTarget(m_ssaoBlur);
        destroyTarget(m_tonemapOutputTarget);

        const VkFormat hdrFmt = selectBestHDRFormat(m_device->getPhysicalDevice());
        m_stats.hdrFormatActive = (hdrFmt == VK_FORMAT_R16G16B16A16_SFLOAT || hdrFmt == VK_FORMAT_B10G11R11_UFLOAT_PACK32);
        const VkImageUsageFlags imgUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        createTarget(m_hdrSceneTarget, hdrFmt, w, h, imgUsage);
        createTarget(m_bloomA, hdrFmt, halfW, halfH, imgUsage);
        createTarget(m_bloomB, hdrFmt, halfW, halfH, imgUsage);
        createTarget(m_ssaoRaw, VK_FORMAT_R16_SFLOAT, halfW, halfH, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        createTarget(m_ssaoBlur, VK_FORMAT_R16_SFLOAT, halfW, halfH, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        createTarget(m_tonemapOutputTarget, hdrFmt, w, h, imgUsage);

        const uint64_t hdrBpp = (hdrFmt == VK_FORMAT_R16G16B16A16_SFLOAT) ? 8ULL : 4ULL;
        m_stats.totalGpuMemoryBytes = (static_cast<uint64_t>(w) * h * hdrBpp * 2ULL) +
                                      (static_cast<uint64_t>(halfW) * halfH * hdrBpp * 2ULL) +
                                      (static_cast<uint64_t>(halfW) * halfH * 2ULL * 2ULL);
    }

    m_initialized = true;
    SIDERA_LOG_INFO("PostProcess", "Initialized Advanced Post Processing ({}x{}, HDR={}, {:.2f} MB).",
                    w, h, m_stats.hdrFormatActive ? "R16G16B16A16_SFLOAT" : "Fallback",
                    static_cast<double>(m_stats.totalGpuMemoryBytes) / (1024.0 * 1024.0));
    return true;
}

void PostProcessPipeline::destroy() noexcept {
    if (m_device && m_device->getLogicalDevice() != VK_NULL_HANDLE) {
        const VkDevice dev = m_device->getLogicalDevice();
        if (m_bloomPipeline != VK_NULL_HANDLE)   { vkDestroyPipeline(dev, m_bloomPipeline, nullptr); m_bloomPipeline = VK_NULL_HANDLE; }
        if (m_bloomLayout != VK_NULL_HANDLE)     { vkDestroyPipelineLayout(dev, m_bloomLayout, nullptr); m_bloomLayout = VK_NULL_HANDLE; }
        if (m_bloomDSL != VK_NULL_HANDLE)        { vkDestroyDescriptorSetLayout(dev, m_bloomDSL, nullptr); m_bloomDSL = VK_NULL_HANDLE; }
        if (m_ssaoPipeline != VK_NULL_HANDLE)    { vkDestroyPipeline(dev, m_ssaoPipeline, nullptr); m_ssaoPipeline = VK_NULL_HANDLE; }
        if (m_ssaoLayout != VK_NULL_HANDLE)      { vkDestroyPipelineLayout(dev, m_ssaoLayout, nullptr); m_ssaoLayout = VK_NULL_HANDLE; }
        if (m_ssaoDSL != VK_NULL_HANDLE)         { vkDestroyDescriptorSetLayout(dev, m_ssaoDSL, nullptr); m_ssaoDSL = VK_NULL_HANDLE; }
        if (m_tonemapPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(dev, m_tonemapPipeline, nullptr); m_tonemapPipeline = VK_NULL_HANDLE; }
        if (m_tonemapLayout != VK_NULL_HANDLE)   { vkDestroyPipelineLayout(dev, m_tonemapLayout, nullptr); m_tonemapLayout = VK_NULL_HANDLE; }
        if (m_tonemapDSL != VK_NULL_HANDLE)      { vkDestroyDescriptorSetLayout(dev, m_tonemapDSL, nullptr); m_tonemapDSL = VK_NULL_HANDLE; }
        if (m_descriptorPool != VK_NULL_HANDLE)  { vkDestroyDescriptorPool(dev, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; }
        if (m_linearSampler != VK_NULL_HANDLE)   { vkDestroySampler(dev, m_linearSampler, nullptr); m_linearSampler = VK_NULL_HANDLE; }

        destroyTarget(m_hdrSceneTarget);
        destroyTarget(m_bloomA);
        destroyTarget(m_bloomB);
        destroyTarget(m_ssaoRaw);
        destroyTarget(m_ssaoBlur);
        destroyTarget(m_tonemapOutputTarget);
    }
    m_initialized = false;
}

bool PostProcessPipeline::createTarget(ImageTarget& out, VkFormat format, uint32_t w, uint32_t h, VkImageUsageFlags usage) {
    const VkDevice dev = m_device->getLogicalDevice();
    out.format = format;
    out.width = w;
    out.height = h;

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {w, h, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(dev, &info, nullptr, &out.image), "Failed to create post-process image");

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(dev, out.image, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size,
        VulkanBuffer::findMemoryType(m_device->getPhysicalDevice(), req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VK_CHECK(vkAllocateMemory(dev, &alloc, nullptr, &out.memory), "Failed to allocate post-process memory");
    VK_CHECK(vkBindImageMemory(dev, out.image, out.memory, 0), "Failed to bind post-process memory");

    VkImageViewCreateInfo vInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, out.image, VK_IMAGE_VIEW_TYPE_2D, format, {}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VK_CHECK(vkCreateImageView(dev, &vInfo, nullptr, &out.view), "Failed to create post-process view");
    return true;
}

void PostProcessPipeline::destroyTarget(ImageTarget& t) noexcept {
    if (!m_device) return;
    const VkDevice dev = m_device->getLogicalDevice();
    if (t.view != VK_NULL_HANDLE)   { vkDestroyImageView(dev, t.view, nullptr); t.view = VK_NULL_HANDLE; }
    if (t.image != VK_NULL_HANDLE)  { vkDestroyImage(dev, t.image, nullptr); t.image = VK_NULL_HANDLE; }
    if (t.memory != VK_NULL_HANDLE) { vkFreeMemory(dev, t.memory, nullptr); t.memory = VK_NULL_HANDLE; }
}

bool PostProcessPipeline::createPipelines() {
    const VkDevice dev = m_device->getLogicalDevice();
    const std::vector<std::string> dirs = {
#ifdef SIDERA_SHADER_BINARY_DIR
        SIDERA_SHADER_BINARY_DIR,
#endif
        "shaders", "../shaders"
    };
    auto findSpv = [&](const char* name) -> std::string {
        for (const auto& d : dirs) {
            const auto p = std::filesystem::path(d) / name;
            if (std::filesystem::exists(p)) return p.string();
        }
        return {};
    };

    const std::string bloomSpv   = findSpv("post_bloom.comp.spv");
    const std::string ssaoSpv    = findSpv("post_ssao.comp.spv");
    const std::string tonemapSpv = findSpv("post_tonemap.comp.spv");
    if (bloomSpv.empty() || ssaoSpv.empty() || tonemapSpv.empty()) return false;

    const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16}
    }};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 8, 2, poolSizes.data()};
    VK_CHECK(vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_descriptorPool), "Failed to create PP descriptor pool");

    auto make2BindingPipe = [&](const std::string& spv, uint32_t pcSize, VkDescriptorSetLayout& dsl, VkPipelineLayout& pl, VkPipeline& pipe) {
        std::array<VkDescriptorSetLayoutBinding, 2> b = {{
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
        }};
        VkDescriptorSetLayoutCreateInfo dInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 2, b.data()};
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &dInfo, nullptr, &dsl), "Failed to create PP 2-binding DSL");
        VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, pcSize};
        VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &dsl, 1, &pc};
        VK_CHECK(vkCreatePipelineLayout(dev, &plInfo, nullptr, &pl), "Failed to create PP pipeline layout");
        VulkanShader sh(dev, spv);
        VkComputePipelineCreateInfo cpInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, sh.getHandle(), "main", nullptr}, pl, VK_NULL_HANDLE, 0};
        VK_CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &pipe), "Failed to create PP compute pipeline");
    };

    make2BindingPipe(bloomSpv, sizeof(BloomPC), m_bloomDSL, m_bloomLayout, m_bloomPipeline);
    make2BindingPipe(ssaoSpv, sizeof(SSAOPC), m_ssaoDSL, m_ssaoLayout, m_ssaoPipeline);

    // Tonemap: 3 samplers + 1 storage image
    std::array<VkDescriptorSetLayoutBinding, 4> tmB = {{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
    }};
    VkDescriptorSetLayoutCreateInfo tmDslInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 4, tmB.data()};
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &tmDslInfo, nullptr, &m_tonemapDSL), "Failed to create tonemap DSL");
    VkPushConstantRange tmPc{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ToneMapPC)};
    VkPipelineLayoutCreateInfo tmPlInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_tonemapDSL, 1, &tmPc};
    VK_CHECK(vkCreatePipelineLayout(dev, &tmPlInfo, nullptr, &m_tonemapLayout), "Failed to create tonemap PL");
    VulkanShader tmSh(dev, tonemapSpv);
    VkComputePipelineCreateInfo tmCp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, tmSh.getHandle(), "main", nullptr}, m_tonemapLayout, VK_NULL_HANDLE, 0};
    VK_CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &tmCp, nullptr, &m_tonemapPipeline), "Failed to create tonemap pipeline");

    auto allocSet = [&](VkDescriptorSetLayout dsl, VkDescriptorSet& outSet) {
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_descriptorPool, 1, &dsl};
        VK_CHECK(vkAllocateDescriptorSets(dev, &ai, &outSet), "Failed to allocate PP descriptor set");
    };
    allocSet(m_bloomDSL, m_bloomSetBright);
    allocSet(m_bloomDSL, m_bloomSetBlurH);
    allocSet(m_bloomDSL, m_bloomSetBlurV);
    allocSet(m_ssaoDSL, m_ssaoSetGen);
    allocSet(m_ssaoDSL, m_ssaoSetBlur);
    allocSet(m_tonemapDSL, m_tonemapSet);
    return true;
}

void PostProcessPipeline::transitionImage(VkCommandBuffer cmd, VkImage img, VkImageLayout oldLayout, VkImageLayout newLayout,
                                          VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                          VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) noexcept {
    VkImageMemoryBarrier2 b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 1, &b};
    vkCmdPipelineBarrier2(cmd, &dep);
}

void PostProcessPipeline::updateDescriptors(VkImageView depthView) {
    const VkDevice dev = m_device->getLogicalDevice();
    auto writePair = [&](VkDescriptorSet set, VkImageView inView, VkImageView outView) {
        VkDescriptorImageInfo inInfo{m_linearSampler, inView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo outInfo{VK_NULL_HANDLE, outView, VK_IMAGE_LAYOUT_GENERAL};
        std::array<VkWriteDescriptorSet, 2> w = {{
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &inInfo, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &outInfo, nullptr, nullptr}
        }};
        vkUpdateDescriptorSets(dev, 2, w.data(), 0, nullptr);
    };

    writePair(m_bloomSetBright, m_hdrSceneTarget.view, m_bloomA.view);
    writePair(m_bloomSetBlurH,  m_bloomA.view, m_bloomB.view);
    writePair(m_bloomSetBlurV,  m_bloomB.view, m_bloomA.view);
    writePair(m_ssaoSetGen,     depthView, m_ssaoRaw.view);
    writePair(m_ssaoSetBlur,    m_ssaoRaw.view, m_ssaoBlur.view);

    VkDescriptorImageInfo hdrI{m_linearSampler, m_hdrSceneTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo blmI{m_linearSampler, m_bloomA.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo aoI{m_linearSampler, m_ssaoBlur.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo outI{VK_NULL_HANDLE, m_tonemapOutputTarget.view, VK_IMAGE_LAYOUT_GENERAL};
    std::array<VkWriteDescriptorSet, 4> tmW = {{
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_tonemapSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &hdrI, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_tonemapSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &blmI, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_tonemapSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &aoI, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_tonemapSet, 3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &outI, nullptr, nullptr}
    }};
    vkUpdateDescriptorSets(dev, 4, tmW.data(), 0, nullptr);
}

void PostProcessPipeline::recordPasses(VkCommandBuffer cmd, VkImageView depthImageView, const Camera& camera) {
    if (!isGpuActive() || !m_settings.enabled || depthImageView == VK_NULL_HANDLE) return;
    updateDescriptors(depthImageView);

    const uint32_t w = m_stats.width, h = m_stats.height;
    const uint32_t hw = m_stats.bloomWidth, hh = m_stats.bloomHeight;

    transitionImage(cmd, m_hdrSceneTarget.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);

    // 1. SSAO Generation + Blur
    if (m_settings.ssaoEnabled) {
        transitionImage(cmd, m_ssaoRaw.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ssaoPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ssaoLayout, 0, 1, &m_ssaoSetGen, 0, nullptr);
        SSAOPC spc{{m_settings.ssaoRadius, m_settings.ssaoBias, m_settings.ssaoIntensity, 0.0f},
                   {camera.getNearPlane(), camera.getFarPlane(), static_cast<float>(m_settings.ssaoSampleCount), 0.0f},
                   {hw, hh, 0u, 0u}};
        vkCmdPushConstants(cmd, m_ssaoLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SSAOPC), &spc);
        vkCmdDispatch(cmd, (hw + 7u) / 8u, (hh + 7u) / 8u, 1u);

        transitionImage(cmd, m_ssaoRaw.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        transitionImage(cmd, m_ssaoBlur.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ssaoLayout, 0, 1, &m_ssaoSetBlur, 0, nullptr);
        spc.params0.w = 1.0f;
        vkCmdPushConstants(cmd, m_ssaoLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SSAOPC), &spc);
        vkCmdDispatch(cmd, (hw + 7u) / 8u, (hh + 7u) / 8u, 1u);
        transitionImage(cmd, m_ssaoBlur.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    }

    // 2. Bloom Bright-Pass + Separable Gaussian Blur (H + V)
    if (m_settings.bloomEnabled) {
        transitionImage(cmd, m_bloomA.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_bloomPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_bloomLayout, 0, 1, &m_bloomSetBright, 0, nullptr);
        BloomPC bpc{{m_settings.bloomThreshold, m_settings.bloomSoftKnee, m_settings.bloomIntensity, 0.0f}, {hw, hh, 0u, 0u}};
        vkCmdPushConstants(cmd, m_bloomLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BloomPC), &bpc);
        vkCmdDispatch(cmd, (hw + 7u) / 8u, (hh + 7u) / 8u, 1u);

        transitionImage(cmd, m_bloomA.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        transitionImage(cmd, m_bloomB.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_bloomLayout, 0, 1, &m_bloomSetBlurH, 0, nullptr);
        bpc.params0.w = 1.0f;
        vkCmdPushConstants(cmd, m_bloomLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BloomPC), &bpc);
        vkCmdDispatch(cmd, (hw + 7u) / 8u, (hh + 7u) / 8u, 1u);

        transitionImage(cmd, m_bloomB.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        transitionImage(cmd, m_bloomA.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_bloomLayout, 0, 1, &m_bloomSetBlurV, 0, nullptr);
        bpc.params0.w = 2.0f;
        vkCmdPushConstants(cmd, m_bloomLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BloomPC), &bpc);
        vkCmdDispatch(cmd, (hw + 7u) / 8u, (hh + 7u) / 8u, 1u);
        transitionImage(cmd, m_bloomA.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    }

    // 3. Final HDR Composition & Tone Mapping (ACES / Reinhard / Linear)
    transitionImage(cmd, m_tonemapOutputTarget.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_tonemapPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_tonemapLayout, 0, 1, &m_tonemapSet, 0, nullptr);
    ToneMapPC tpc{{m_settings.getEffectiveExposure(),
                   m_settings.bloomEnabled ? m_settings.bloomIntensity : 0.0f,
                   m_settings.ssaoEnabled ? 1.0f : 0.0f,
                   static_cast<float>(m_settings.toneMapOperator)},
                  {w, h, static_cast<uint32_t>(m_settings.debugMode), 0u}};
    vkCmdPushConstants(cmd, m_tonemapLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ToneMapPC), &tpc);
    vkCmdDispatch(cmd, (w + 7u) / 8u, (h + 7u) / 8u, 1u);
}

} // namespace sidera
