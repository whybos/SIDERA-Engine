#include "renderer/volumetric/VolumetricPipeline.h"
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

std::vector<char> loadSpvFileVolumetric(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) return {};
    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), static_cast<std::streamsize>(size));
    return buf;
}

VkPipeline createVolumetricComputePipeline(VkDevice dev, const std::string& spvPath, VkPipelineLayout layout) {
    auto code = loadSpvFileVolumetric(spvPath);
    if (code.empty()) {
        SIDERA_LOG_WARN("Volumetric", "Shader not found: {}", spvPath);
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

VolumetricPipeline::~VolumetricPipeline() {
    destroy();
}

VkFormat VolumetricPipeline::selectBestVolumeFormat(VkPhysicalDevice physicalDevice) noexcept {
    if (physicalDevice == VK_NULL_HANDLE) {
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    const VkFormat candidates[] = {
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R8G8B8A8_UNORM
    };
    const VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    for (VkFormat fmt : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, fmt, &props);
        if ((props.optimalTilingFeatures & required) == required) {
            return fmt;
        }
    }
    return VK_FORMAT_R8G8B8A8_UNORM;
}

bool VolumetricPipeline::createVolume3D(VolumeTarget3D& out, VkFormat format, uint32_t w, uint32_t h, uint32_t d) {
    out.width = std::max(w, 1u);
    out.height = std::max(h, 1u);
    out.depth = std::max(d, 1u);
    out.format = format;
    if (!m_device) return true;

    VkDevice dev = m_device->getLogicalDevice();
    VkImageCreateInfo imgCI{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imgCI.imageType = VK_IMAGE_TYPE_3D;
    imgCI.format = format;
    imgCI.extent = {out.width, out.height, out.depth};
    imgCI.mipLevels = 1;
    imgCI.arrayLayers = 1;
    imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(dev, &imgCI, nullptr, &out.image) != VK_SUCCESS) return false;

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(dev, out.image, &memReq);

    VkMemoryAllocateInfo allocCI{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocCI.allocationSize = memReq.size;
    allocCI.memoryTypeIndex = VulkanBuffer::findMemoryType(m_device->getPhysicalDevice(), memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(dev, &allocCI, nullptr, &out.memory) != VK_SUCCESS) return false;
    vkBindImageMemory(dev, out.image, out.memory, 0);

    VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewCI.image = out.image;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewCI.format = format;
    viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(dev, &viewCI, nullptr, &out.view) != VK_SUCCESS) return false;
    return true;
}

void VolumetricPipeline::destroyVolume3D(VolumeTarget3D& v) noexcept {
    if (m_device) {
        VkDevice dev = m_device->getLogicalDevice();
        if (v.view)   { vkDestroyImageView(dev, v.view, nullptr); v.view = VK_NULL_HANDLE; }
        if (v.image)  { vkDestroyImage(dev, v.image, nullptr);    v.image = VK_NULL_HANDLE; }
        if (v.memory) { vkFreeMemory(dev, v.memory, nullptr);     v.memory = VK_NULL_HANDLE; }
    }
    v = {};
}

void VolumetricPipeline::allocateBuffers() {
    if (!m_device) return;
    m_uboBuffer = std::make_unique<VulkanBuffer>(
        *m_device, sizeof(VolumetricUniformData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_lightBuffer = std::make_unique<VulkanBuffer>(
        *m_device, sizeof(GPULight) * LightManager::kMaxLights,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_localVolumeBuffer = std::make_unique<VulkanBuffer>(
        *m_device, sizeof(LocalFogVolume) * kMaxLocalVolumes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

bool VolumetricPipeline::createPipelines() {
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
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 12},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 12}
    };
    VkDescriptorPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolCI.maxSets = 12;
    poolCI.poolSizeCount = 4;
    poolCI.pPoolSizes = poolSizes;
    vkCreateDescriptorPool(dev, &poolCI, nullptr, &m_descriptorPool);

    // 1. Inject pipeline: 0=img3D, 1=lights SSBO, 2=localVolumes SSBO, 3=CSM shadow, 4=UBO
    {
        VkDescriptorSetLayoutBinding b[5]{};
        b[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[3] = {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[4] = {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo dslCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dslCI.bindingCount = 5; dslCI.pBindings = b;
        vkCreateDescriptorSetLayout(dev, &dslCI, nullptr, &m_injectDSL);

        VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plCI.setLayoutCount = 1; plCI.pSetLayouts = &m_injectDSL;
        vkCreatePipelineLayout(dev, &plCI, nullptr, &m_injectLayout);
        m_injectPipeline = createVolumetricComputePipeline(dev, "shaders/volumetric_inject.comp.spv", m_injectLayout);

        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = m_descriptorPool; ai.descriptorSetCount = 1; ai.pSetLayouts = &m_injectDSL;
        vkAllocateDescriptorSets(dev, &ai, &m_injectSet);
    }

    // 2. Temporal pipeline: 0=currInject(samp3D), 1=prevHist(samp3D), 2=currHist(img3D), 3=UBO
    {
        VkDescriptorSetLayoutBinding b[4]{};
        b[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[3] = {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo dslCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dslCI.bindingCount = 4; dslCI.pBindings = b;
        vkCreateDescriptorSetLayout(dev, &dslCI, nullptr, &m_temporalDSL);

        VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plCI.setLayoutCount = 1; plCI.pSetLayouts = &m_temporalDSL;
        vkCreatePipelineLayout(dev, &plCI, nullptr, &m_temporalLayout);
        m_temporalPipeline = createVolumetricComputePipeline(dev, "shaders/volumetric_temporal.comp.spv", m_temporalLayout);

        VkDescriptorSetLayout layouts[2] = {m_temporalDSL, m_temporalDSL};
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = m_descriptorPool; ai.descriptorSetCount = 2; ai.pSetLayouts = layouts;
        vkAllocateDescriptorSets(dev, &ai, m_temporalSets.data());
    }

    // 3. Integrate pipeline: 0=history(samp3D), 1=integrated(img3D), 2=UBO
    {
        VkDescriptorSetLayoutBinding b[3]{};
        b[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[2] = {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo dslCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dslCI.bindingCount = 3; dslCI.pBindings = b;
        vkCreateDescriptorSetLayout(dev, &dslCI, nullptr, &m_integrateDSL);

        VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plCI.setLayoutCount = 1; plCI.pSetLayouts = &m_integrateDSL;
        vkCreatePipelineLayout(dev, &plCI, nullptr, &m_integrateLayout);
        m_integratePipeline = createVolumetricComputePipeline(dev, "shaders/volumetric_integrate.comp.spv", m_integrateLayout);

        VkDescriptorSetLayout layouts[2] = {m_integrateDSL, m_integrateDSL};
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = m_descriptorPool; ai.descriptorSetCount = 2; ai.pSetLayouts = layouts;
        vkAllocateDescriptorSets(dev, &ai, m_integrateSets.data());
    }

    // 4. Composite pipeline: 0=hdrScene(img2D), 1=depth(samp2D), 2=integrated(samp3D), 3=history(samp3D), 4=UBO
    {
        VkDescriptorSetLayoutBinding b[5]{};
        b[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[2] = {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[3] = {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[4] = {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo dslCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dslCI.bindingCount = 5; dslCI.pBindings = b;
        vkCreateDescriptorSetLayout(dev, &dslCI, nullptr, &m_compositeDSL);

        VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plCI.setLayoutCount = 1; plCI.pSetLayouts = &m_compositeDSL;
        vkCreatePipelineLayout(dev, &plCI, nullptr, &m_compositeLayout);
        m_compositePipeline = createVolumetricComputePipeline(dev, "shaders/volumetric_composite.comp.spv", m_compositeLayout);

        VkDescriptorSetLayout layouts[2] = {m_compositeDSL, m_compositeDSL};
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = m_descriptorPool; ai.descriptorSetCount = 2; ai.pSetLayouts = layouts;
        vkAllocateDescriptorSets(dev, &ai, m_compositeSets.data());
    }

    return true;
}

bool VolumetricPipeline::init(uint32_t screenWidth, uint32_t screenHeight, const VulkanDevice* device) {
    destroy();
    m_device = device;
    m_settings.validate();

    if (m_device) {
        m_maxDim3D = std::max(m_device->getProperties().limits.maxImageDimension3D, 64u);
    } else {
        m_maxDim3D = 2048u;
    }

    allocateBuffers();
    createPipelines();

    if (!resize(screenWidth, screenHeight)) return false;

    // Seed a subtle default local fog volume in the valley for demonstration
    if (m_localVolumes.empty()) {
        LocalFogVolume valley{};
        valley.positionType = glm::vec4(0.0f, 4.0f, 0.0f, 0.0f); // Sphere
        valley.extentsDensity = glm::vec4(28.0f, 12.0f, 28.0f, 0.03f);
        valley.colorFalloff = glm::vec4(0.75f, 0.85f, 0.98f, 1.5f);
        valley.params = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        m_localVolumes.push_back(valley);
    }

    m_initialized = true;
    return true;
}

bool VolumetricPipeline::resize(uint32_t screenWidth, uint32_t screenHeight) {
    m_screenWidth = std::max(screenWidth, 1u);
    m_screenHeight = std::max(screenHeight, 1u);
    m_settings.validate();

    uint32_t fx = 0, fy = 0, fz = 0;
    computeFroxelDimensions(m_screenWidth, m_screenHeight,
                            m_settings.froxelXYScale, m_settings.froxelDepthSlices,
                            m_maxDim3D, fx, fy, fz);

    destroyVolume3D(m_volumeInject);
    destroyVolume3D(m_volumeHistory[0]);
    destroyVolume3D(m_volumeHistory[1]);
    destroyVolume3D(m_volumeIntegrated);

    const VkFormat volFmt = selectBestVolumeFormat(m_device ? m_device->getPhysicalDevice() : VK_NULL_HANDLE);
    createVolume3D(m_volumeInject, volFmt, fx, fy, fz);
    createVolume3D(m_volumeHistory[0], volFmt, fx, fy, fz);
    createVolume3D(m_volumeHistory[1], volFmt, fx, fy, fz);
    createVolume3D(m_volumeIntegrated, volFmt, fx, fy, fz);

    // Invalidate all viewport histories on resize
    invalidateHistory();

    m_stats.froxelX = fx;
    m_stats.froxelY = fy;
    m_stats.froxelZ = fz;
    m_stats.totalFroxels = fx * fy * fz;
    const uint64_t bytesPerVoxel = (volFmt == VK_FORMAT_R16G16B16A16_SFLOAT) ? 8ull : 4ull;
    m_stats.gpuMemoryBytes = static_cast<uint64_t>(m_stats.totalFroxels) * bytesPerVoxel * 4ull; // 4 x 3D volumes
    return true;
}

void VolumetricPipeline::destroy() noexcept {
    destroyVolume3D(m_volumeInject);
    destroyVolume3D(m_volumeHistory[0]);
    destroyVolume3D(m_volumeHistory[1]);
    destroyVolume3D(m_volumeIntegrated);

    m_uboBuffer.reset();
    m_lightBuffer.reset();
    m_localVolumeBuffer.reset();

    if (m_device) {
        VkDevice dev = m_device->getLogicalDevice();
        if (m_injectPipeline)    { vkDestroyPipeline(dev, m_injectPipeline, nullptr);       m_injectPipeline = VK_NULL_HANDLE; }
        if (m_injectLayout)      { vkDestroyPipelineLayout(dev, m_injectLayout, nullptr);   m_injectLayout = VK_NULL_HANDLE; }
        if (m_injectDSL)         { vkDestroyDescriptorSetLayout(dev, m_injectDSL, nullptr); m_injectDSL = VK_NULL_HANDLE; }
        if (m_temporalPipeline)  { vkDestroyPipeline(dev, m_temporalPipeline, nullptr);       m_temporalPipeline = VK_NULL_HANDLE; }
        if (m_temporalLayout)    { vkDestroyPipelineLayout(dev, m_temporalLayout, nullptr);   m_temporalLayout = VK_NULL_HANDLE; }
        if (m_temporalDSL)       { vkDestroyDescriptorSetLayout(dev, m_temporalDSL, nullptr); m_temporalDSL = VK_NULL_HANDLE; }
        if (m_integratePipeline) { vkDestroyPipeline(dev, m_integratePipeline, nullptr);       m_integratePipeline = VK_NULL_HANDLE; }
        if (m_integrateLayout)   { vkDestroyPipelineLayout(dev, m_integrateLayout, nullptr);   m_integrateLayout = VK_NULL_HANDLE; }
        if (m_integrateDSL)      { vkDestroyDescriptorSetLayout(dev, m_integrateDSL, nullptr); m_integrateDSL = VK_NULL_HANDLE; }
        if (m_compositePipeline) { vkDestroyPipeline(dev, m_compositePipeline, nullptr);       m_compositePipeline = VK_NULL_HANDLE; }
        if (m_compositeLayout)   { vkDestroyPipelineLayout(dev, m_compositeLayout, nullptr);   m_compositeLayout = VK_NULL_HANDLE; }
        if (m_compositeDSL)      { vkDestroyDescriptorSetLayout(dev, m_compositeDSL, nullptr); m_compositeDSL = VK_NULL_HANDLE; }
        if (m_descriptorPool)    { vkDestroyDescriptorPool(dev, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; }
        if (m_linearSampler)     { vkDestroySampler(dev, m_linearSampler, nullptr);         m_linearSampler = VK_NULL_HANDLE; }
    }
    m_viewportHistories.clear();
    m_device = nullptr;
    m_initialized = false;
}

uint32_t VolumetricPipeline::addLocalVolume(const LocalFogVolume& vol) {
    if (m_localVolumes.size() >= kMaxLocalVolumes) return 0xFFFFFFFFu;
    m_localVolumes.push_back(vol);
    return static_cast<uint32_t>(m_localVolumes.size() - 1u);
}

void VolumetricPipeline::invalidateHistory(uint32_t viewportId) noexcept {
    if (viewportId == 0xFFFFFFFFu) {
        for (auto& [id, h] : m_viewportHistories) {
            h.valid = false;
        }
    } else {
        auto it = m_viewportHistories.find(viewportId);
        if (it != m_viewportHistories.end()) {
            it->second.valid = false;
        }
    }
    m_stats.historyValid = false;
    ++m_stats.historyResetCount;
}

bool VolumetricPipeline::checkAndUpdateViewportHistory(uint32_t viewportId, const Camera& camera) noexcept {
    auto& h = m_viewportHistories[viewportId];
    const glm::vec3 currPos = camera.getPosition();
    bool usable = h.valid && m_settings.temporalEnabled;
    if (usable) {
        const float moved = glm::length(currPos - h.prevCameraPos);
        if (moved > m_settings.cameraCutPositionThreshold) {
            usable = false;
            ++m_stats.historyResetCount;
        }
    }
    m_stats.historyValid = usable;
    return usable;
}

void VolumetricPipeline::transitionImage(VkCommandBuffer cmd, VkImage img,
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

void VolumetricPipeline::recordPasses(VkCommandBuffer cmd,
                                      const PostProcessPipeline::ImageTarget& hdrTarget,
                                      VkImageView depthImageView,
                                      VkImageView csmShadowView,
                                      VkSampler csmShadowSampler,
                                      const std::array<glm::mat4, 4>& cascadeMatrices,
                                      const std::array<float, 4>& cascadeSplits,
                                      uint32_t cascadeCount,
                                      const Camera& camera,
                                      const LightManager& lightManager,
                                      uint32_t viewportId) {
    m_stats.active = m_settings.enabled && isGpuActive();
    if (!m_stats.active || cmd == VK_NULL_HANDLE || hdrTarget.image == VK_NULL_HANDLE || depthImageView == VK_NULL_HANDLE) {
        return;
    }

    m_settings.validate();
    const bool historyUsable = checkAndUpdateViewportHistory(viewportId, camera);
    auto& hist = m_viewportHistories[viewportId];
    const uint32_t currIdx = hist.pingPongIndex & 1u;
    const uint32_t prevIdx = 1u - currIdx;

    // Upload lights & local volumes
    const auto& lights = lightManager.getLights();
    const uint32_t lightCount = std::min(static_cast<uint32_t>(lights.size()), LightManager::kMaxLights);
    if (lightCount > 0 && m_lightBuffer) {
        m_lightBuffer->uploadData(lights.data(), lightCount * sizeof(GPULight));
    }
    const uint32_t localCount = std::min(static_cast<uint32_t>(m_localVolumes.size()), kMaxLocalVolumes);
    if (localCount > 0 && m_localVolumeBuffer) {
        m_localVolumeBuffer->uploadData(m_localVolumes.data(), localCount * sizeof(LocalFogVolume));
    }

    // Upload UBO
    const glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    VolumetricUniformData ubo{};
    ubo.invViewProj = glm::inverse(viewProj);
    ubo.prevViewProj = historyUsable ? hist.prevViewProj : viewProj;
    ubo.cascadeMatrices = cascadeMatrices;
    ubo.cascadeSplits = glm::vec4(cascadeSplits[0], cascadeSplits[1], cascadeSplits[2], cascadeSplits[3]);
    ubo.cameraPosNear = glm::vec4(camera.getPosition(), camera.getNearPlane());
    ubo.fogParams = glm::vec4(m_settings.fogDensity, m_settings.fogHeight, m_settings.heightFalloff, m_settings.maxDensity);
    ubo.scatterParams = glm::vec4(m_settings.scatteringCoefficient, m_settings.extinctionCoefficient,
                                  m_settings.anisotropy, m_settings.scatteringIntensity);
    ubo.fogColorMaxDist = glm::vec4(m_settings.fogColor, m_settings.maxDistance);
    ubo.scatterColorFlags = glm::vec4(m_settings.scatteringColor, static_cast<float>(m_settings.packFeatureFlags()));
    ubo.gridCounts = glm::uvec4(m_volumeInject.width, m_volumeInject.height, m_volumeInject.depth, lightCount);
    ubo.extraCounts = glm::uvec4(localCount, static_cast<uint32_t>(m_settings.debugMode), historyUsable ? 1u : 0u, m_frameCounter++);
    ubo.temporalParams = glm::vec4(m_settings.temporalEnabled ? m_settings.temporalBlend : 0.0f,
                                   camera.getFarPlane(), m_settings.shadowBias, static_cast<float>(cascadeCount));
    if (m_uboBuffer) {
        m_uboBuffer->uploadData(&ubo, sizeof(ubo));
    }

    m_stats.activeLights = lightCount;
    m_stats.activeLocalVolumes = localCount;

    VkDevice dev = m_device->getLogicalDevice();
    VkDescriptorBufferInfo uboBI{m_uboBuffer->getHandle(), 0, sizeof(VolumetricUniformData)};
    VkDescriptorBufferInfo lightBI{m_lightBuffer->getHandle(), 0, sizeof(GPULight) * LightManager::kMaxLights};
    VkDescriptorBufferInfo volBI{m_localVolumeBuffer->getHandle(), 0, sizeof(LocalFogVolume) * kMaxLocalVolumes};

    // Update Inject descriptor set
    VkDescriptorImageInfo injectOut{VK_NULL_HANDLE, m_volumeInject.view, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo csmInfo{csmShadowSampler ? csmShadowSampler : m_linearSampler, csmShadowView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet iw[5]{};
    iw[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_injectSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &injectOut, nullptr, nullptr};
    iw[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_injectSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &lightBI, nullptr};
    iw[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_injectSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &volBI, nullptr};
    iw[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_injectSet, 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &csmInfo, nullptr, nullptr};
    iw[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_injectSet, 4, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboBI, nullptr};
    vkUpdateDescriptorSets(dev, 5, iw, 0, nullptr);

    // Update Temporal descriptor set for currIdx
    VkDescriptorImageInfo injectSamp{m_linearSampler, m_volumeInject.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo prevHistSamp{m_linearSampler, m_volumeHistory[prevIdx].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo currHistOut{VK_NULL_HANDLE, m_volumeHistory[currIdx].view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet tw[4]{};
    tw[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_temporalSets[currIdx], 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &injectSamp, nullptr, nullptr};
    tw[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_temporalSets[currIdx], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &prevHistSamp, nullptr, nullptr};
    tw[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_temporalSets[currIdx], 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &currHistOut, nullptr, nullptr};
    tw[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_temporalSets[currIdx], 3, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboBI, nullptr};
    vkUpdateDescriptorSets(dev, 4, tw, 0, nullptr);

    // Update Integrate descriptor set for currIdx
    VkDescriptorImageInfo currHistSamp{m_linearSampler, m_volumeHistory[currIdx].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo integOut{VK_NULL_HANDLE, m_volumeIntegrated.view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet nw[3]{};
    nw[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_integrateSets[currIdx], 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &currHistSamp, nullptr, nullptr};
    nw[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_integrateSets[currIdx], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &integOut, nullptr, nullptr};
    nw[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_integrateSets[currIdx], 2, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboBI, nullptr};
    vkUpdateDescriptorSets(dev, 3, nw, 0, nullptr);

    // Update Composite descriptor set for currIdx
    VkDescriptorImageInfo hdrImg{VK_NULL_HANDLE, hdrTarget.view, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo depthSamp{m_linearSampler, depthImageView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo integSamp{m_linearSampler, m_volumeIntegrated.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet cw[5]{};
    cw[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_compositeSets[currIdx], 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &hdrImg, nullptr, nullptr};
    cw[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_compositeSets[currIdx], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depthSamp, nullptr, nullptr};
    cw[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_compositeSets[currIdx], 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &integSamp, nullptr, nullptr};
    cw[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_compositeSets[currIdx], 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &currHistSamp, nullptr, nullptr};
    cw[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_compositeSets[currIdx], 4, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboBI, nullptr};
    vkUpdateDescriptorSets(dev, 5, cw, 0, nullptr);

    const uint32_t gx = (m_volumeInject.width  + 7u) / 8u;
    const uint32_t gy = (m_volumeInject.height + 7u) / 8u;
    const uint32_t gz = (m_volumeInject.depth  + 3u) / 4u;

    // Pass 1: Density & Light Injection
    transitionImage(cmd, m_volumeInject.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_injectPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_injectLayout, 0, 1, &m_injectSet, 0, nullptr);
    vkCmdDispatch(cmd, gx, gy, gz);

    // Pass 2: Temporal Reprojection & Neighborhood Clamping
    transitionImage(cmd, m_volumeInject.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    transitionImage(cmd, m_volumeHistory[prevIdx].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    transitionImage(cmd, m_volumeHistory[currIdx].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_temporalPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_temporalLayout, 0, 1, &m_temporalSets[currIdx], 0, nullptr);
    vkCmdDispatch(cmd, gx, gy, gz);

    // Pass 3: Front-to-Back Volumetric Integration
    transitionImage(cmd, m_volumeHistory[currIdx].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    transitionImage(cmd, m_volumeIntegrated.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_integratePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_integrateLayout, 0, 1, &m_integrateSets[currIdx], 0, nullptr);
    vkCmdDispatch(cmd, gx, gy, 1u);

    // Pass 4: Linear HDR Composition (before Bloom & Tone Mapping)
    transitionImage(cmd, m_volumeIntegrated.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    transitionImage(cmd, hdrTarget.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compositePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compositeLayout, 0, 1, &m_compositeSets[currIdx], 0, nullptr);
    vkCmdDispatch(cmd, (hdrTarget.width + 15u) / 16u, (hdrTarget.height + 15u) / 16u, 1u);

    // Update per-viewport temporal history for next frame
    hist.prevViewProj = viewProj;
    hist.prevCameraPos = camera.getPosition();
    hist.pingPongIndex = prevIdx;
    hist.valid = true;
}

} // namespace sidera
