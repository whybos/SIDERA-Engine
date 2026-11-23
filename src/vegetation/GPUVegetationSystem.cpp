#include "vegetation/GPUVegetationSystem.h"
#include "core/Log.h"
#include "renderer/Frustum.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanDevice.h"
#include "renderer/vulkan/VulkanShader.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace sidera {

namespace {
constexpr uint64_t kDeferredRetireFrames = 3ULL;
} // namespace

GPUVegetationSystem::~GPUVegetationSystem() {
    destroy();
}

bool GPUVegetationSystem::init(const GPUVegetationConfig& config,
                               const VulkanDevice* device,
                               VkCommandPool transferCommandPool) {
    destroy();
    m_config = config;
    m_device = device;
    m_transferCommandPool = transferCommandPool;

    // Default species LOD table (4 categories x 4 LODs = 16 batches)
    m_speciesLods.clear();
    for (uint32_t s = 0; s < 8u; ++s) {
        GPUVegetationSpeciesLOD sp{};
        const uint32_t baseBatch = (s % 4u) * 4u;
        sp.lodBatchIndex = glm::uvec4(baseBatch, baseBatch + 1u, baseBatch + 2u, baseBatch + 3u);
        sp.lodDistances = (s < 4u)
            ? glm::vec4(25.0f, 55.0f, 100.0f, 200.0f)  // Trees
            : glm::vec4(14.0f, 30.0f, 55.0f, 110.0f);  // Bushes / Grass / SmallVeg
        sp.windParams = glm::vec4(0.45f, 1.25f, 0.85f, (s < 6u) ? 1.0f : 0.0f);
        m_speciesLods.push_back(sp);
    }

    m_capacity = std::max(m_config.initialCapacity, 256u);
    m_stats.currentCapacity = m_capacity;
    m_stats.totalBatches = 16u;

    if (m_device) {
        if (!createComputePipeline()) {
            SIDERA_LOG_WARN("GPUVegetation", "Compute shader veg_cull_compact.comp.spv unavailable; falling back to headless mode.");
        } else {
            allocateBuffers(m_capacity);
            updateDescriptorSets();
        }
    }

    m_initialized = true;
    SIDERA_LOG_INFO("GPUVegetation", "GPUVegetationSystem initialized (capacity={}, gpuActive={}).",
                    m_capacity, isGpuActive() ? "YES" : "NO");
    return true;
}

void GPUVegetationSystem::destroy() noexcept {
    if (m_device && m_device->getLogicalDevice() != VK_NULL_HANDLE) {
        const VkDevice dev = m_device->getLogicalDevice();
        if (m_computePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(dev, m_computePipeline, nullptr);
            m_computePipeline = VK_NULL_HANDLE;
        }
        if (m_pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(dev, m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
        }
        if (m_descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(dev, m_descriptorPool, nullptr);
            m_descriptorPool = VK_NULL_HANDLE;
        }
        if (m_descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(dev, m_descriptorSetLayout, nullptr);
            m_descriptorSetLayout = VK_NULL_HANDLE;
        }
    }

    m_deferredBuffers.clear();
    m_instanceBuffer.reset();
    m_speciesBuffer.reset();
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        m_visibleIndexBuffers[i].reset();
        m_indirectCmdBuffers[i].reset();
        m_counterBuffers[i].reset();
    }
    m_cpuInstances.clear();
    m_initialized = false;
}

bool GPUVegetationSystem::createComputePipeline() {
    const VkDevice dev = m_device->getLogicalDevice();

    // Resolve compiled SPIR-V compute shader
    const std::vector<std::string> searchPaths = {
#ifdef SIDERA_SHADER_BINARY_DIR
        SIDERA_SHADER_BINARY_DIR,
#endif
        "shaders",
        "../shaders"
    };
    std::string spvPath;
    for (const auto& dir : searchPaths) {
        const auto p = std::filesystem::path(dir) / "veg_cull_compact.comp.spv";
        if (std::filesystem::exists(p)) {
            spvPath = p.string();
            break;
        }
    }
    if (spvPath.empty()) {
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    for (uint32_t i = 0; i < 5u; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    dslInfo.pBindings = bindings.data();
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &dslInfo, nullptr, &m_descriptorSetLayout),
             "Failed to create GPU vegetation descriptor set layout");

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 5u * kMaxFramesInFlight;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = kMaxFramesInFlight;
    VK_CHECK(vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_descriptorPool),
             "Failed to create GPU vegetation descriptor pool");

    std::array<VkDescriptorSetLayout, kMaxFramesInFlight> setLayouts{};
    setLayouts.fill(m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = kMaxFramesInFlight;
    allocInfo.pSetLayouts = setLayouts.data();
    VK_CHECK(vkAllocateDescriptorSets(dev, &allocInfo, m_descriptorSets.data()),
             "Failed to allocate GPU vegetation descriptor sets");

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(GPUVegetationCullPC);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_descriptorSetLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;
    VK_CHECK(vkCreatePipelineLayout(dev, &plInfo, nullptr, &m_pipelineLayout),
             "Failed to create GPU vegetation pipeline layout");

    VulkanShader compShader(dev, spvPath);
    VkComputePipelineCreateInfo cpInfo{};
    cpInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpInfo.stage.module = compShader.getHandle();
    cpInfo.stage.pName = "main";
    cpInfo.layout = m_pipelineLayout;

    VK_CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &m_computePipeline),
             "Failed to create GPU vegetation compute pipeline");
    return true;
}

bool GPUVegetationSystem::allocateBuffers(uint32_t requiredCapacity) {
    if (!m_device) return true;

    const uint32_t safeCap = std::min(std::max(requiredCapacity, 256u), m_config.maxCapacity);
    const uint32_t batchCount = std::max(m_config.maxBatches, 16u);
    const uint32_t slotCap = std::max(1u, safeCap);

    // Defer destruction of old GPU buffers for frames-in-flight safety
    auto retire = [&](std::unique_ptr<VulkanBuffer>& buf) {
        if (buf) {
            m_deferredBuffers.push_back(DeferredBuffer{std::move(buf), m_frameIndex + kDeferredRetireFrames});
        }
    };
    retire(m_instanceBuffer);
    retire(m_speciesBuffer);
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        retire(m_visibleIndexBuffers[i]);
        retire(m_indirectCmdBuffers[i]);
        retire(m_counterBuffers[i]);
    }

    const VkDeviceSize instBytes = static_cast<VkDeviceSize>(safeCap) * sizeof(GPUVegetationInstance);
    const VkDeviceSize spBytes   = static_cast<VkDeviceSize>(std::max<size_t>(m_speciesLods.size(), 1)) * sizeof(GPUVegetationSpeciesLOD);
    const VkDeviceSize visBytes  = static_cast<VkDeviceSize>(batchCount) * slotCap * sizeof(uint32_t);
    const VkDeviceSize indBytes  = static_cast<VkDeviceSize>(batchCount) * sizeof(GPUVegetationIndirectCmd);
    const VkDeviceSize cntBytes  = sizeof(GPUVegetationCounters);

    m_instanceBuffer = std::make_unique<VulkanBuffer>(
        *m_device, instBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_speciesBuffer = std::make_unique<VulkanBuffer>(
        *m_device, spBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!m_speciesLods.empty()) {
        m_speciesBuffer->uploadData(m_speciesLods.data(), spBytes, 0);
    }

    uint64_t totalMem = static_cast<uint64_t>(instBytes + spBytes);
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        m_visibleIndexBuffers[i] = std::make_unique<VulkanBuffer>(
            *m_device, visBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        m_indirectCmdBuffers[i] = std::make_unique<VulkanBuffer>(
            *m_device, indBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_counterBuffers[i] = std::make_unique<VulkanBuffer>(
            *m_device, cntBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        totalMem += static_cast<uint64_t>(visBytes + indBytes + cntBytes);
    }

    m_capacity = safeCap;
    m_stats.currentCapacity = m_capacity;
    m_stats.instanceBufferMemoryBytes = static_cast<uint64_t>(instBytes);
    m_stats.totalGpuMemoryBytes = totalMem;
    return true;
}

void GPUVegetationSystem::updateDescriptorSets() {
    if (!m_device || m_descriptorPool == VK_NULL_HANDLE || !m_instanceBuffer) return;
    const VkDevice dev = m_device->getLogicalDevice();

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        std::array<VkDescriptorBufferInfo, 5> infos{};
        infos[0] = {m_instanceBuffer->getHandle(), 0, m_instanceBuffer->getSize()};
        infos[1] = {m_speciesBuffer->getHandle(), 0, m_speciesBuffer->getSize()};
        infos[2] = {m_visibleIndexBuffers[i]->getHandle(), 0, m_visibleIndexBuffers[i]->getSize()};
        infos[3] = {m_indirectCmdBuffers[i]->getHandle(), 0, m_indirectCmdBuffers[i]->getSize()};
        infos[4] = {m_counterBuffers[i]->getHandle(), 0, m_counterBuffers[i]->getSize()};

        std::array<VkWriteDescriptorSet, 5> writes{};
        for (uint32_t b = 0; b < 5u; ++b) {
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet = m_descriptorSets[i];
            writes[b].dstBinding = b;
            writes[b].descriptorCount = 1;
            writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[b].pBufferInfo = &infos[b];
        }
        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void GPUVegetationSystem::setSpeciesLODs(const std::vector<GPUVegetationSpeciesLOD>& speciesLods) {
    m_speciesLods = speciesLods;
    if (m_speciesBuffer && !m_speciesLods.empty()) {
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(m_speciesLods.size() * sizeof(GPUVegetationSpeciesLOD));
        if (bytes <= m_speciesBuffer->getSize()) {
            m_speciesBuffer->uploadData(m_speciesLods.data(), bytes, 0);
        }
    }
}

bool GPUVegetationSystem::uploadInstances(const std::vector<GPUVegetationInstance>& instances) {
    m_cpuInstances = instances;
    if (m_cpuInstances.size() > m_capacity) {
        uint32_t newCap = m_capacity;
        while (newCap < m_cpuInstances.size() && newCap < m_config.maxCapacity) {
            newCap = std::min(newCap * 2u, m_config.maxCapacity);
        }
        if (m_device) {
            allocateBuffers(newCap);
            updateDescriptorSets();
        } else {
            m_capacity = newCap;
            m_stats.currentCapacity = m_capacity;
        }
    }

    if (m_instanceBuffer && !m_cpuInstances.empty()) {
        const uint32_t uploadCount = std::min<uint32_t>(static_cast<uint32_t>(m_cpuInstances.size()), m_capacity);
        m_instanceBuffer->uploadData(m_cpuInstances.data(), static_cast<VkDeviceSize>(uploadCount) * sizeof(GPUVegetationInstance), 0);
    }
    m_stats.totalInstances = m_cpuInstances.size();
    return true;
}

bool GPUVegetationSystem::appendInstances(const std::vector<GPUVegetationInstance>& additional) {
    m_cpuInstances.insert(m_cpuInstances.end(), additional.begin(), additional.end());
    return uploadInstances(m_cpuInstances);
}

bool GPUVegetationSystem::uploadFromPlacements(const std::vector<VegetationPlacement>& placements) {
    std::vector<GPUVegetationInstance> gpuInsts;
    gpuInsts.reserve(placements.size());
    for (const auto& pl : placements) {
        GPUVegetationInstance gi{};
        gi.transform = pl.foliageInstance.transform;
        gi.boundsSphere = glm::vec4(pl.foliageInstance.worldSphere.center, pl.foliageInstance.worldSphere.radius);
        gi.meta0 = glm::vec4(
            static_cast<float>(pl.speciesId > 0u ? (pl.speciesId - 1u) : 0u),
            static_cast<float>(pl.foliageInstance.typeId),
            static_cast<float>(pl.foliageInstance.randomSeed % 1000u) * 0.00628318f,
            (pl.category == VegetationCategory::Grass) ? 1.0f : 0.4f
        );
        gi.meta1 = glm::vec4(
            static_cast<float>(pl.foliageInstance.randomSeed & 0xFFFFu),
            1.0f,
            static_cast<float>(pl.ownerCellId & 0xFFFFu),
            1.0f
        );
        gpuInsts.push_back(gi);
    }
    return uploadInstances(gpuInsts);
}

void GPUVegetationSystem::clearInstances() {
    m_cpuInstances.clear();
    m_stats.totalInstances = 0;
}

void GPUVegetationSystem::beginFrame() noexcept {
    ++m_frameIndex;
    flushDeferredBuffers();
}

void GPUVegetationSystem::flushDeferredBuffers() noexcept {
    auto it = m_deferredBuffers.begin();
    while (it != m_deferredBuffers.end()) {
        if (m_frameIndex >= it->retireFrame) {
            it = m_deferredBuffers.erase(it);
        } else {
            ++it;
        }
    }
}

void GPUVegetationSystem::recordCullAndCompact(VkCommandBuffer cmd,
                                               uint32_t frameIndex,
                                               const Camera& camera,
                                               bool isShadowPass) {
    if (!isGpuActive() || m_cpuInstances.empty()) {
        return;
    }

    const uint32_t slot = frameIndex % kMaxFramesInFlight;
    const uint32_t instCount = std::min<uint32_t>(static_cast<uint32_t>(m_cpuInstances.size()), m_capacity);
    const uint32_t batchCount = std::max(m_config.maxBatches, 16u);

    // Reset indirect commands (preserving firstInstance offsets) and atomic counters
    std::vector<GPUVegetationIndirectCmd> resetCmds(batchCount);
    for (uint32_t b = 0; b < batchCount; ++b) {
        resetCmds[b].indexCount = 36u;
        resetCmds[b].instanceCount = 0u;
        resetCmds[b].firstIndex = 0u;
        resetCmds[b].vertexOffset = 0;
        resetCmds[b].firstInstance = b * m_capacity;
    }
    m_indirectCmdBuffers[slot]->uploadData(resetCmds.data(), static_cast<VkDeviceSize>(batchCount) * sizeof(GPUVegetationIndirectCmd), 0);

    GPUVegetationCounters zeroCounters{};
    m_counterBuffers[slot]->uploadData(&zeroCounters, sizeof(GPUVegetationCounters), 0);

    // Compute frustum planes
    const glm::mat4 vp = camera.getProjectionMatrix() * camera.getViewMatrix();
    const Frustum frustum = Frustum::fromViewProj(vp);

    GPUVegetationCullPC pc{};
    for (int i = 0; i < 6; ++i) {
        pc.frustumPlanes[i] = glm::vec4(frustum.planes[i].normal, frustum.planes[i].distance);
    }
    pc.cameraPosMaxDist = glm::vec4(camera.getPosition(), m_config.globalMaxDrawDistance);
    uint32_t flags = 0u;
    if (m_config.frustumCullingEnabled && !isShadowPass) flags |= 1u;
    if (m_config.distanceCullingEnabled)                 flags |= 2u;
    if (isShadowPass)                                    flags |= 4u;
    pc.countsFlags = glm::uvec4(instCount, static_cast<uint32_t>(m_speciesLods.size()), flags, m_capacity);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSets[slot], 0, nullptr);
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GPUVegetationCullPC), &pc);

    const uint32_t groupCount = (instCount + 63u) / 64u;
    vkCmdDispatch(cmd, groupCount, 1, 1);
    ++m_stats.computeDispatchCount;

    // Vulkan 1.3 Synchronization2 barrier: Compute Write -> Indirect Draw Read + Vertex Shader Read
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.memoryBarrierCount = 1;
    depInfo.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);
}

GPUVegetationStatistics GPUVegetationSystem::simulateCullAndCompactCPU(
    const Camera& camera,
    bool isShadowPass,
    std::vector<uint32_t>& outVisibleIndices,
    std::vector<GPUVegetationIndirectCmd>& outIndirectCmds) const {

    const auto t0 = std::chrono::high_resolution_clock::now();
    GPUVegetationStatistics stats = m_stats;
    stats.visibleInstances = 0;
    stats.culledInstances = 0;
    stats.lod0Count = stats.lod1Count = stats.lod2Count = stats.lod3Count = stats.overflowCount = 0;

    const uint32_t batchCount = std::max(m_config.maxBatches, 16u);
    const uint32_t slotCap = std::max(m_capacity, 1u);
    outIndirectCmds.assign(batchCount, GPUVegetationIndirectCmd{});
    for (uint32_t b = 0; b < batchCount; ++b) {
        outIndirectCmds[b].indexCount = 36u;
        outIndirectCmds[b].firstInstance = b * slotCap;
    }
    outVisibleIndices.assign(static_cast<size_t>(batchCount) * slotCap, 0u);

    const glm::mat4 vp = camera.getProjectionMatrix() * camera.getViewMatrix();
    const Frustum frustum = Frustum::fromViewProj(vp);
    const glm::vec3 camPos = camera.getPosition();

    for (uint32_t gid = 0; gid < static_cast<uint32_t>(m_cpuInstances.size()); ++gid) {
        const auto& inst = m_cpuInstances[gid];
        const glm::vec3 center(inst.boundsSphere);
        const float radius = std::max(inst.boundsSphere.w, 0.25f);
        uint32_t spIdx = static_cast<uint32_t>(inst.meta0.x);
        if (spIdx >= m_speciesLods.size()) spIdx = 0u;
        const auto& sp = m_speciesLods[spIdx];

        if (isShadowPass && sp.windParams.w < 0.5f) {
            ++stats.culledInstances;
            continue;
        }
        const float dist = glm::distance(camPos, center);
        const float maxDist = std::min(sp.lodDistances.w, m_config.globalMaxDrawDistance);
        if (m_config.distanceCullingEnabled && dist > maxDist) {
            ++stats.culledInstances;
            continue;
        }
        if (m_config.frustumCullingEnabled && !isShadowPass) {
            bool vis = true;
            for (int i = 0; i < 6; ++i) {
                if (glm::dot(frustum.planes[i].normal, center) + frustum.planes[i].distance < -radius) {
                    vis = false;
                    break;
                }
            }
            if (!vis) {
                ++stats.culledInstances;
                continue;
            }
        }

        const float effDist = dist * std::max(inst.meta1.w, 0.25f);
        uint32_t lod = 3u;
        if (effDist <= sp.lodDistances.x)      lod = 0u;
        else if (effDist <= sp.lodDistances.y) lod = 1u;
        else if (effDist <= sp.lodDistances.z) lod = 2u;

        const uint32_t batchIdx = sp.lodBatchIndex[lod];
        const uint32_t localSlot = outIndirectCmds[batchIdx].instanceCount++;
        if (localSlot < slotCap) {
            outVisibleIndices[static_cast<size_t>(outIndirectCmds[batchIdx].firstInstance) + localSlot] = gid;
            ++stats.visibleInstances;
            if (lod == 0u) ++stats.lod0Count;
            else if (lod == 1u) ++stats.lod1Count;
            else if (lod == 2u) ++stats.lod2Count;
            else ++stats.lod3Count;
        } else {
            ++stats.overflowCount;
        }
    }

    uint32_t activeCmds = 0;
    for (const auto& cmd : outIndirectCmds) {
        if (cmd.instanceCount > 0u) ++activeCmds;
    }
    stats.indirectDrawCommands = activeCmds;
    const auto t1 = std::chrono::high_resolution_clock::now();
    stats.cullDispatchTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    return stats;
}

VkBuffer GPUVegetationSystem::getIndirectBuffer(uint32_t frameIndex) const noexcept {
    const auto& b = m_indirectCmdBuffers[frameIndex % kMaxFramesInFlight];
    return b ? b->getHandle() : VK_NULL_HANDLE;
}

VkBuffer GPUVegetationSystem::getVisibleIndexBuffer(uint32_t frameIndex) const noexcept {
    const auto& b = m_visibleIndexBuffers[frameIndex % kMaxFramesInFlight];
    return b ? b->getHandle() : VK_NULL_HANDLE;
}

} // namespace sidera
