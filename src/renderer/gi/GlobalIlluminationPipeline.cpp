#include "renderer/gi/GlobalIlluminationPipeline.h"
#include "renderer/vulkan/VulkanDevice.h"
#include "core/Log.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace sidera {

namespace {

std::vector<char> loadSpvFileGI(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) return {};
    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), static_cast<std::streamsize>(size));
    return buf;
}

VkPipeline createGIComputePipeline(VkDevice dev, const std::string& spvPath, VkPipelineLayout layout) {
    auto code = loadSpvFileGI(spvPath);
    if (code.empty()) {
        SIDERA_LOG_WARN("GI", "Shader not found: {}", spvPath);
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo smCI{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smCI.codeSize = code.size();
    smCI.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule sm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev, &smCI, nullptr, &sm) != VK_SUCCESS) return VK_NULL_HANDLE;

    VkComputePipelineCreateInfo pipeCI{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeCI.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeCI.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeCI.stage.module = sm;
    pipeCI.stage.pName = "main";
    pipeCI.layout = layout;

    VkPipeline pipe = VK_NULL_HANDLE;
    vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pipeCI, nullptr, &pipe);
    vkDestroyShaderModule(dev, sm, nullptr);
    return pipe;
}

} // namespace

GlobalIlluminationPipeline::~GlobalIlluminationPipeline() {
    destroy();
}

GICapabilities GlobalIlluminationPipeline::queryDeviceCapabilities(VkPhysicalDevice physicalDevice) noexcept {
    GICapabilities caps{};
    caps.ssgiSupported = true;
    if (physicalDevice == VK_NULL_HANDLE) return caps;

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, exts.data());

    for (const auto& e : exts) {
        if (std::strcmp(e.extensionName, "VK_KHR_acceleration_structure") == 0) caps.hasAccelerationStructure = true;
        else if (std::strcmp(e.extensionName, "VK_KHR_ray_tracing_pipeline") == 0) caps.hasRayTracingPipeline = true;
        else if (std::strcmp(e.extensionName, "VK_KHR_ray_query") == 0) caps.hasRayQuery = true;
        else if (std::strcmp(e.extensionName, "VK_KHR_buffer_device_address") == 0) caps.hasBufferDeviceAddress = true;
    }
    return caps;
}

bool GlobalIlluminationPipeline::createTarget2D(ImageTarget2D& out, VkFormat format, uint32_t w, uint32_t h, VkImageUsageFlags usage) {
    out.width = std::max(w, 1u);
    out.height = std::max(h, 1u);
    out.format = format;
    if (!m_device) return true;

    VkDevice dev = m_device->getLogicalDevice();
    VkImageCreateInfo imgCI{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imgCI.imageType = VK_IMAGE_TYPE_2D;
    imgCI.format = format;
    imgCI.extent = {out.width, out.height, 1};
    imgCI.mipLevels = 1;
    imgCI.arrayLayers = 1;
    imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage = usage;
    imgCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(dev, &imgCI, nullptr, &out.image) != VK_SUCCESS) return false;

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(dev, out.image, &memReq);

    VkMemoryAllocateInfo allocCI{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocCI.allocationSize = memReq.size;
    allocCI.memoryTypeIndex = VulkanBuffer::findMemoryType(
        m_device->getPhysicalDevice(), memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(dev, &allocCI, nullptr, &out.memory) != VK_SUCCESS) return false;
    vkBindImageMemory(dev, out.image, out.memory, 0);

    VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewCI.image = out.image;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format = format;
    viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return vkCreateImageView(dev, &viewCI, nullptr, &out.view) == VK_SUCCESS;
}

void GlobalIlluminationPipeline::destroyTarget2D(ImageTarget2D& t) noexcept {
    if (m_device) {
        VkDevice dev = m_device->getLogicalDevice();
        if (t.view)   { vkDestroyImageView(dev, t.view, nullptr); t.view = VK_NULL_HANDLE; }
        if (t.image)  { vkDestroyImage(dev, t.image, nullptr);    t.image = VK_NULL_HANDLE; }
        if (t.memory) { vkFreeMemory(dev, t.memory, nullptr);     t.memory = VK_NULL_HANDLE; }
    }
    t = {};
}

bool GlobalIlluminationPipeline::createPipelines() {
    if (!m_device) return true;
    VkDevice dev = m_device->getLogicalDevice();

    VkSamplerCreateInfo sampCI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampCI.magFilter = VK_FILTER_LINEAR;
    sampCI.minFilter = VK_FILTER_LINEAR;
    sampCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(dev, &sampCI, nullptr, &m_linearSampler);

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 24},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10}
    };
    VkDescriptorPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolCI.maxSets = 10;
    poolCI.poolSizeCount = 3;
    poolCI.pPoolSizes = poolSizes;
    vkCreateDescriptorPool(dev, &poolCI, nullptr, &m_descriptorPool);

    // 1. Trace: 0=preGiHdr, 1=depth, 2=hiZ, 3=rawGI(img2D), 4=UBO
    {
        VkDescriptorSetLayoutBinding b[5]{};
        b[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[2] = {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[4] = {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo dslCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dslCI.bindingCount = 5; dslCI.pBindings = b;
        vkCreateDescriptorSetLayout(dev, &dslCI, nullptr, &m_traceDSL);
        VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plCI.setLayoutCount = 1; plCI.pSetLayouts = &m_traceDSL;
        vkCreatePipelineLayout(dev, &plCI, nullptr, &m_traceLayout);
        m_tracePipeline = createGIComputePipeline(dev, "shaders/ssgi_trace.comp.spv", m_traceLayout);
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = m_descriptorPool; ai.descriptorSetCount = 1; ai.pSetLayouts = &m_traceDSL;
        vkAllocateDescriptorSets(dev, &ai, &m_traceSet);
    }

    // 2. Temporal: 0=rawGI, 1=prevHist, 2=depth, 3=currHist(img2D), 4=UBO
    {
        VkDescriptorSetLayoutBinding b[5]{};
        b[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[2] = {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[4] = {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo dslCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dslCI.bindingCount = 5; dslCI.pBindings = b;
        vkCreateDescriptorSetLayout(dev, &dslCI, nullptr, &m_temporalDSL);
        VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plCI.setLayoutCount = 1; plCI.pSetLayouts = &m_temporalDSL;
        vkCreatePipelineLayout(dev, &plCI, nullptr, &m_temporalLayout);
        m_temporalPipeline = createGIComputePipeline(dev, "shaders/ssgi_temporal.comp.spv", m_temporalLayout);
        VkDescriptorSetLayout layouts[2] = {m_temporalDSL, m_temporalDSL};
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = m_descriptorPool; ai.descriptorSetCount = 2; ai.pSetLayouts = layouts;
        vkAllocateDescriptorSets(dev, &ai, m_temporalSets.data());
    }

    // 3. Denoise: 0=giHist, 1=depth, 2=denoisedGI(img2D), 3=UBO
    {
        VkDescriptorSetLayoutBinding b[4]{};
        b[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[3] = {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo dslCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dslCI.bindingCount = 4; dslCI.pBindings = b;
        vkCreateDescriptorSetLayout(dev, &dslCI, nullptr, &m_denoiseDSL);
        VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plCI.setLayoutCount = 1; plCI.pSetLayouts = &m_denoiseDSL;
        vkCreatePipelineLayout(dev, &plCI, nullptr, &m_denoiseLayout);
        m_denoisePipeline = createGIComputePipeline(dev, "shaders/ssgi_denoise.comp.spv", m_denoiseLayout);
        VkDescriptorSetLayout layouts[2] = {m_denoiseDSL, m_denoiseDSL};
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = m_descriptorPool; ai.descriptorSetCount = 2; ai.pSetLayouts = layouts;
        vkAllocateDescriptorSets(dev, &ai, m_denoiseSets.data());
    }

    // 4. Composite: 0=hdrScene(img2D), 1=denoisedGI, 2=rawGI, 3=giHist, 4=depth, 5=UBO
    {
        VkDescriptorSetLayoutBinding b[6]{};
        b[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[2] = {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[3] = {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[4] = {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[5] = {5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo dslCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dslCI.bindingCount = 6; dslCI.pBindings = b;
        vkCreateDescriptorSetLayout(dev, &dslCI, nullptr, &m_compositeDSL);
        VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plCI.setLayoutCount = 1; plCI.pSetLayouts = &m_compositeDSL;
        vkCreatePipelineLayout(dev, &plCI, nullptr, &m_compositeLayout);
        m_compositePipeline = createGIComputePipeline(dev, "shaders/ssgi_composite.comp.spv", m_compositeLayout);
        VkDescriptorSetLayout layouts[2] = {m_compositeDSL, m_compositeDSL};
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = m_descriptorPool; ai.descriptorSetCount = 2; ai.pSetLayouts = layouts;
        vkAllocateDescriptorSets(dev, &ai, m_compositeSets.data());
    }

    return true;
}

bool GlobalIlluminationPipeline::init(uint32_t screenWidth, uint32_t screenHeight, const VulkanDevice* device) {
    destroy();
    m_device = device;
    m_settings.validate();
    m_capabilities = queryDeviceCapabilities(m_device ? m_device->getPhysicalDevice() : VK_NULL_HANDLE);

    if (m_device) {
        m_uboBuffer = std::make_unique<VulkanBuffer>(
            *m_device, sizeof(GIUniformData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    createPipelines();
    if (!resize(screenWidth, screenHeight)) return false;

    m_initialized = true;
    return true;
}

bool GlobalIlluminationPipeline::resize(uint32_t screenWidth, uint32_t screenHeight) {
    m_screenWidth = std::max(screenWidth, 1u);
    m_screenHeight = std::max(screenHeight, 1u);
    m_settings.validate();

    const uint32_t giW = std::max(static_cast<uint32_t>(m_screenWidth * m_settings.resolutionScale), 1u);
    const uint32_t giH = std::max(static_cast<uint32_t>(m_screenHeight * m_settings.resolutionScale), 1u);

    destroyTarget2D(m_preGiHdrCopy);
    destroyTarget2D(m_rawGI);
    destroyTarget2D(m_history[0]);
    destroyTarget2D(m_history[1]);
    destroyTarget2D(m_denoisedGI);

    const VkFormat fmt = PostProcessPipeline::selectBestHDRFormat(m_device ? m_device->getPhysicalDevice() : VK_NULL_HANDLE);
    createTarget2D(m_preGiHdrCopy, fmt, m_screenWidth, m_screenHeight,
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    createTarget2D(m_rawGI, fmt, giW, giH, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    createTarget2D(m_history[0], fmt, giW, giH, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    createTarget2D(m_history[1], fmt, giW, giH, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    createTarget2D(m_denoisedGI, fmt, giW, giH, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    invalidateHistory();

    m_stats.giWidth = giW;
    m_stats.giHeight = giH;
    m_stats.fullWidth = m_screenWidth;
    m_stats.fullHeight = m_screenHeight;
    m_stats.hiZMipLevels = computeHiZMipLevels(m_screenWidth, m_screenHeight);
    m_stats.rayCount = m_settings.rayCount;
    const uint64_t bpp = (fmt == VK_FORMAT_R16G16B16A16_SFLOAT) ? 8ull : 4ull;
    m_stats.gpuMemoryBytes =
        static_cast<uint64_t>(m_screenWidth) * m_screenHeight * bpp +
        static_cast<uint64_t>(giW) * giH * bpp * 4ull;
    return true;
}

void GlobalIlluminationPipeline::destroy() noexcept {
    destroyTarget2D(m_preGiHdrCopy);
    destroyTarget2D(m_rawGI);
    destroyTarget2D(m_history[0]);
    destroyTarget2D(m_history[1]);
    destroyTarget2D(m_denoisedGI);
    m_uboBuffer.reset();

    if (m_device) {
        VkDevice dev = m_device->getLogicalDevice();
        if (m_tracePipeline)     { vkDestroyPipeline(dev, m_tracePipeline, nullptr);         m_tracePipeline = VK_NULL_HANDLE; }
        if (m_traceLayout)       { vkDestroyPipelineLayout(dev, m_traceLayout, nullptr);     m_traceLayout = VK_NULL_HANDLE; }
        if (m_traceDSL)          { vkDestroyDescriptorSetLayout(dev, m_traceDSL, nullptr);   m_traceDSL = VK_NULL_HANDLE; }
        if (m_temporalPipeline)  { vkDestroyPipeline(dev, m_temporalPipeline, nullptr);       m_temporalPipeline = VK_NULL_HANDLE; }
        if (m_temporalLayout)    { vkDestroyPipelineLayout(dev, m_temporalLayout, nullptr);   m_temporalLayout = VK_NULL_HANDLE; }
        if (m_temporalDSL)       { vkDestroyDescriptorSetLayout(dev, m_temporalDSL, nullptr); m_temporalDSL = VK_NULL_HANDLE; }
        if (m_denoisePipeline)   { vkDestroyPipeline(dev, m_denoisePipeline, nullptr);        m_denoisePipeline = VK_NULL_HANDLE; }
        if (m_denoiseLayout)     { vkDestroyPipelineLayout(dev, m_denoiseLayout, nullptr);    m_denoiseLayout = VK_NULL_HANDLE; }
        if (m_denoiseDSL)        { vkDestroyDescriptorSetLayout(dev, m_denoiseDSL, nullptr);  m_denoiseDSL = VK_NULL_HANDLE; }
        if (m_compositePipeline) { vkDestroyPipeline(dev, m_compositePipeline, nullptr);      m_compositePipeline = VK_NULL_HANDLE; }
        if (m_compositeLayout)   { vkDestroyPipelineLayout(dev, m_compositeLayout, nullptr);  m_compositeLayout = VK_NULL_HANDLE; }
        if (m_compositeDSL)      { vkDestroyDescriptorSetLayout(dev, m_compositeDSL, nullptr);m_compositeDSL = VK_NULL_HANDLE; }
        if (m_descriptorPool)    { vkDestroyDescriptorPool(dev, m_descriptorPool, nullptr);   m_descriptorPool = VK_NULL_HANDLE; }
        if (m_linearSampler)     { vkDestroySampler(dev, m_linearSampler, nullptr);           m_linearSampler = VK_NULL_HANDLE; }
    }
    m_viewportHistories.clear();
    m_device = nullptr;
    m_initialized = false;
}

void GlobalIlluminationPipeline::invalidateHistory(uint32_t viewportId) noexcept {
    if (viewportId == 0xFFFFFFFFu) {
        for (auto& [id, h] : m_viewportHistories) h.valid = false;
    } else {
        auto it = m_viewportHistories.find(viewportId);
        if (it != m_viewportHistories.end()) it->second.valid = false;
    }
    m_stats.historyValid = false;
    ++m_stats.historyResetCount;
}

bool GlobalIlluminationPipeline::checkAndUpdateViewportHistory(uint32_t viewportId, const Camera& camera) noexcept {
    auto& h = m_viewportHistories[viewportId];
    bool usable = h.valid && m_settings.temporalEnabled;
    if (usable) {
        if (glm::length(camera.getPosition() - h.prevCameraPos) > m_settings.cameraCutThreshold) {
            usable = false;
            ++m_stats.historyResetCount;
        }
    }
    m_stats.historyValid = usable;
    return usable;
}

void GlobalIlluminationPipeline::transitionImage(VkCommandBuffer cmd, VkImage img,
                                                 VkImageLayout oldLayout, VkImageLayout newLayout,
                                                 VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                                 VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) noexcept {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.image = img;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void GlobalIlluminationPipeline::recordPasses(VkCommandBuffer cmd,
                                              const PostProcessPipeline::ImageTarget& hdrTarget,
                                              VkImageView depthImageView,
                                              VkImageView hiZImageView,
                                              VkSampler hiZSampler,
                                              uint32_t hiZMipLevels,
                                              const Camera& camera,
                                              uint32_t viewportId) {
    m_stats.active = m_settings.enabled && m_settings.ssgiEnabled && isGpuActive();
    if (!m_stats.active || cmd == VK_NULL_HANDLE || hdrTarget.image == VK_NULL_HANDLE || depthImageView == VK_NULL_HANDLE) {
        return;
    }

    m_settings.validate();
    const bool historyUsable = checkAndUpdateViewportHistory(viewportId, camera);
    auto& hist = m_viewportHistories[viewportId];
    const uint32_t currIdx = hist.pingPongIndex & 1u;
    const uint32_t prevIdx = 1u - currIdx;

    const glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    GIUniformData ubo{};
    ubo.viewProj = viewProj;
    ubo.invViewProj = glm::inverse(viewProj);
    ubo.prevViewProj = historyUsable ? hist.prevViewProj : viewProj;
    ubo.cameraPosNear = glm::vec4(camera.getPosition(), camera.getNearPlane());
    ubo.rayParams = glm::vec4(m_settings.maxRayDistance, m_settings.rayThickness, m_settings.giIntensity, camera.getFarPlane());
    ubo.temporalParams = glm::vec4(m_settings.temporalEnabled ? m_settings.temporalBlend : 0.0f,
                                   m_settings.depthRejectionThreshold, m_settings.normalRejectionThreshold,
                                   m_settings.indirectDiffuseIntensity);
    ubo.dims = glm::uvec4(m_rawGI.width, m_rawGI.height, hdrTarget.width, hdrTarget.height);
    const uint32_t flags = (historyUsable ? 1u : 0u) | (m_settings.denoiseEnabled ? 2u : 0u);
    ubo.counts = glm::uvec4(m_settings.rayCount, m_settings.maxSteps, m_frameCounter++, flags);
    ubo.debugParams = glm::uvec4(static_cast<uint32_t>(m_settings.debugMode), m_settings.denoiseRadius, hiZMipLevels, 1u);
    if (m_uboBuffer) {
        m_uboBuffer->uploadData(&ubo, sizeof(ubo));
    }

    // Step 0: Snapshot pre-GI HDR target to prevent same-frame recursive GI feedback
    transitionImage(cmd, hdrTarget.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    transitionImage(cmd, m_preGiHdrCopy.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageCopy copyRegion{};
    copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.extent = {std::min(hdrTarget.width, m_preGiHdrCopy.width), std::min(hdrTarget.height, m_preGiHdrCopy.height), 1};
    vkCmdCopyImage(cmd, hdrTarget.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   m_preGiHdrCopy.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    transitionImage(cmd, m_preGiHdrCopy.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    VkDevice dev = m_device->getLogicalDevice();
    VkDescriptorBufferInfo uboBI{m_uboBuffer->getHandle(), 0, sizeof(GIUniformData)};
    VkDescriptorImageInfo preGiSamp{m_linearSampler, m_preGiHdrCopy.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo depthSamp{m_linearSampler, depthImageView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo hiZSamp{hiZSampler ? hiZSampler : m_linearSampler,
                                  hiZImageView ? hiZImageView : depthImageView,
                                  hiZImageView ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};

    // Update Trace set
    VkDescriptorImageInfo rawOut{VK_NULL_HANDLE, m_rawGI.view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet tw[5]{};
    tw[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_traceSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &preGiSamp, nullptr, nullptr};
    tw[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_traceSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depthSamp, nullptr, nullptr};
    tw[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_traceSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &hiZSamp, nullptr, nullptr};
    tw[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_traceSet, 3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &rawOut, nullptr, nullptr};
    tw[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_traceSet, 4, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboBI, nullptr};
    vkUpdateDescriptorSets(dev, 5, tw, 0, nullptr);

    // Update Temporal set
    VkDescriptorImageInfo rawSamp{m_linearSampler, m_rawGI.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo prevHistSamp{m_linearSampler, m_history[prevIdx].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo currHistOut{VK_NULL_HANDLE, m_history[currIdx].view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet hw[5]{};
    hw[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_temporalSets[currIdx], 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &rawSamp, nullptr, nullptr};
    hw[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_temporalSets[currIdx], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &prevHistSamp, nullptr, nullptr};
    hw[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_temporalSets[currIdx], 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depthSamp, nullptr, nullptr};
    hw[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_temporalSets[currIdx], 3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &currHistOut, nullptr, nullptr};
    hw[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_temporalSets[currIdx], 4, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboBI, nullptr};
    vkUpdateDescriptorSets(dev, 5, hw, 0, nullptr);

    // Update Denoise set
    VkDescriptorImageInfo currHistSamp{m_linearSampler, m_history[currIdx].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo denoiseOut{VK_NULL_HANDLE, m_denoisedGI.view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet dw[4]{};
    dw[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_denoiseSets[currIdx], 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &currHistSamp, nullptr, nullptr};
    dw[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_denoiseSets[currIdx], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depthSamp, nullptr, nullptr};
    dw[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_denoiseSets[currIdx], 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &denoiseOut, nullptr, nullptr};
    dw[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_denoiseSets[currIdx], 3, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboBI, nullptr};
    vkUpdateDescriptorSets(dev, 4, dw, 0, nullptr);

    // Update Composite set
    VkDescriptorImageInfo hdrImg{VK_NULL_HANDLE, hdrTarget.view, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo denoiseSamp{m_linearSampler, m_denoisedGI.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet cw[6]{};
    cw[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_compositeSets[currIdx], 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &hdrImg, nullptr, nullptr};
    cw[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_compositeSets[currIdx], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &denoiseSamp, nullptr, nullptr};
    cw[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_compositeSets[currIdx], 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &rawSamp, nullptr, nullptr};
    cw[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_compositeSets[currIdx], 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &currHistSamp, nullptr, nullptr};
    cw[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_compositeSets[currIdx], 4, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depthSamp, nullptr, nullptr};
    cw[5] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_compositeSets[currIdx], 5, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboBI, nullptr};
    vkUpdateDescriptorSets(dev, 6, cw, 0, nullptr);

    const uint32_t gx = (m_rawGI.width + 7u) / 8u;
    const uint32_t gy = (m_rawGI.height + 7u) / 8u;

    // Pass 1: SSGI Trace
    transitionImage(cmd, m_rawGI.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_tracePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_traceLayout, 0, 1, &m_traceSet, 0, nullptr);
    vkCmdDispatch(cmd, gx, gy, 1u);

    // Pass 2: Temporal Accumulation
    transitionImage(cmd, m_rawGI.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    transitionImage(cmd, m_history[prevIdx].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    transitionImage(cmd, m_history[currIdx].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_temporalPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_temporalLayout, 0, 1, &m_temporalSets[currIdx], 0, nullptr);
    vkCmdDispatch(cmd, gx, gy, 1u);

    // Pass 3: Edge-Aware Bilateral Denoise
    transitionImage(cmd, m_history[currIdx].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    transitionImage(cmd, m_denoisedGI.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_denoisePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_denoiseLayout, 0, 1, &m_denoiseSets[currIdx], 0, nullptr);
    vkCmdDispatch(cmd, gx, gy, 1u);

    // Pass 4: Linear HDR Composition (before Volumetric, Bloom, and Tone Mapping)
    transitionImage(cmd, m_denoisedGI.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    transitionImage(cmd, hdrTarget.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compositePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compositeLayout, 0, 1, &m_compositeSets[currIdx], 0, nullptr);
    vkCmdDispatch(cmd, (hdrTarget.width + 15u) / 16u, (hdrTarget.height + 15u) / 16u, 1u);

    hist.prevViewProj = viewProj;
    hist.prevCameraPos = camera.getPosition();
    hist.pingPongIndex = prevIdx;
    hist.valid = true;
}

} // namespace sidera
