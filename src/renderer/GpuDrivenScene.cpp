#include "renderer/GpuDrivenScene.h"
#include "core/Log.h"
#include "renderer/Mesh.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanDevice.h"
#include "renderer/vulkan/VulkanShader.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace sidera {

namespace {
constexpr const char* kGenCommandsShaderPath = "shaders/gen_draw_commands.comp.spv";
constexpr uint32_t kGenCommandsLocalSize = 64; // must match local_size_x in gen_draw_commands.comp

struct GenCommandsPushConstants {
    uint32_t objectCount;
    uint32_t meshCount;
    uint32_t frustumCullingEnabled;
    uint32_t occlusionCullingEnabled;
    uint32_t hizValid;
    uint32_t hizMipLevels;
    glm::vec2 hizSize;
};
} // namespace

bool GpuDrivenScene::isSupported(const VulkanDevice& device) noexcept {
    return device.isDrawIndirectFirstInstanceEnabled();
}

GpuDrivenScene::GpuDrivenScene(const VulkanDevice& device, uint32_t framesInFlight)
    : m_device(device)
    , m_framesInFlight(framesInFlight)
    , m_multiDrawIndirect(device.isMultiDrawIndirectEnabled())
    , m_maxDrawIndirectCount(std::max(1u, device.getProperties().limits.maxDrawIndirectCount)) {
    if (!isSupported(device)) {
        throw std::runtime_error(
            "GPU-driven rendering requires the drawIndirectFirstInstance device feature, which is unavailable.");
    }

    try {
        for (uint32_t i = 0; i < m_framesInFlight; ++i) {
            m_objectBuffers.push_back(std::make_unique<VulkanBuffer>(
                m_device,
                sizeof(GpuObjectData) * kMaxObjects,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            ));

            m_mainCommandBuffers.push_back(std::make_unique<VulkanBuffer>(
                m_device,
                sizeof(VkDrawIndexedIndirectCommand) * kMaxObjects,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            ));

            m_shadowCommandBuffers.push_back(std::make_unique<VulkanBuffer>(
                m_device,
                sizeof(VkDrawIndexedIndirectCommand) * kMaxObjects,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            ));

            m_cullingUniformBuffers.push_back(std::make_unique<VulkanBuffer>(
                m_device,
                sizeof(CullingUniformData),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            ));

            m_cullingStatsBuffers.push_back(std::make_unique<VulkanBuffer>(
                m_device,
                sizeof(CullingStats),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            ));
        }

        m_meshInfoBuffer = std::make_unique<VulkanBuffer>(
            m_device,
            sizeof(GpuMeshDrawInfo) * kMaxMeshes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        createComputeResources();
    } catch (...) {
        destroyComputeResources();
        throw;
    }

    SIDERA_LOG_INFO(
        "GpuDriven",
        "GPU-driven visibility scene initialized (capacity: {} objects / {} mesh batches, multiDrawIndirect: {}, "
        "maxDrawIndirectCount: {}, {} frames in flight).",
        kMaxObjects,
        kMaxMeshes,
        m_multiDrawIndirect ? "ENABLED" : "UNAVAILABLE (separate indirect calls)",
        m_maxDrawIndirectCount,
        m_framesInFlight
    );
}

GpuDrivenScene::~GpuDrivenScene() {
    destroyComputeResources();
    m_meshInfoBuffer.reset();
    m_cullingStatsBuffers.clear();
    m_cullingUniformBuffers.clear();
    m_shadowCommandBuffers.clear();
    m_mainCommandBuffers.clear();
    m_objectBuffers.clear();
    SIDERA_LOG_INFO("GpuDriven", "GPU-driven scene destroyed.");
}

void GpuDrivenScene::createComputeResources() {
    const VkDevice device = m_device.getLogicalDevice();

    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};

    // Binding 0: ObjectBuffer (SSBO)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: MeshInfoBuffer (SSBO)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: MainCommandBuffer (SSBO, writeonly)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 3: ShadowCommandBuffer (SSBO, writeonly)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 4: CullingUBO (Uniform buffer)
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 5: CullingStatsBuffer (SSBO, readwrite)
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 6: uHiZMap (Combined image sampler)
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    VK_CHECK(
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_computeSetLayout),
        "Failed to create GPU-driven compute descriptor set layout"
    );

    std::array<VkDescriptorPoolSize, 3> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_framesInFlight * 5},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_framesInFlight * 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_framesInFlight * 1}
    }};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = m_framesInFlight;
    VK_CHECK(
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_computeDescriptorPool),
        "Failed to create GPU-driven compute descriptor pool"
    );

    const std::vector<VkDescriptorSetLayout> layouts(m_framesInFlight, m_computeSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_computeDescriptorPool;
    allocInfo.descriptorSetCount = m_framesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    m_computeDescriptorSets.resize(m_framesInFlight);
    VK_CHECK(
        vkAllocateDescriptorSets(device, &allocInfo, m_computeDescriptorSets.data()),
        "Failed to allocate GPU-driven compute descriptor sets"
    );

    for (uint32_t f = 0; f < m_framesInFlight; ++f) {
        const VkDescriptorBufferInfo objInfo{m_objectBuffers[f]->getHandle(), 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo meshInfo{m_meshInfoBuffer->getHandle(), 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo mainCmdInfo{m_mainCommandBuffers[f]->getHandle(), 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo shdCmdInfo{m_shadowCommandBuffers[f]->getHandle(), 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo cullUboInfo{m_cullingUniformBuffers[f]->getHandle(), 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo statsInfo{m_cullingStatsBuffers[f]->getHandle(), 0, VK_WHOLE_SIZE};

        std::array<VkWriteDescriptorSet, 6> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_computeDescriptorSets[f];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &objInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_computeDescriptorSets[f];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &meshInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = m_computeDescriptorSets[f];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &mainCmdInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = m_computeDescriptorSets[f];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &shdCmdInfo;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = m_computeDescriptorSets[f];
        writes[4].dstBinding = 4;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo = &cullUboInfo;

        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = m_computeDescriptorSets[f];
        writes[5].dstBinding = 5;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].descriptorCount = 1;
        writes[5].pBufferInfo = &statsInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(GenCommandsPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_computeSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    VK_CHECK(
        vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_computePipelineLayout),
        "Failed to create GPU-driven compute pipeline layout"
    );

    VulkanShader shader(device, kGenCommandsShaderPath);

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shader.getHandle();
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = m_computePipelineLayout;
    VK_CHECK(
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_computePipeline),
        "Failed to create GPU-driven command-generation compute pipeline"
    );
}

void GpuDrivenScene::destroyComputeResources() noexcept {
    const VkDevice device = m_device.getLogicalDevice();
    if (device == VK_NULL_HANDLE) {
        return;
    }
    if (m_computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_computePipeline, nullptr);
        m_computePipeline = VK_NULL_HANDLE;
    }
    if (m_computePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_computePipelineLayout, nullptr);
        m_computePipelineLayout = VK_NULL_HANDLE;
    }
    if (m_computeDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_computeDescriptorPool, nullptr);
        m_computeDescriptorPool = VK_NULL_HANDLE;
        m_computeDescriptorSets.clear();
    }
    if (m_computeSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_computeSetLayout, nullptr);
        m_computeSetLayout = VK_NULL_HANDLE;
    }
}

void GpuDrivenScene::setScene(std::span<const Mesh* const> objectMeshes) {
    std::vector<const Mesh*> uniqueMeshes;
    std::unordered_map<const Mesh*, uint32_t> batchOfMesh;
    std::vector<std::vector<uint32_t>> objectsOfBatch;

    for (uint32_t obj = 0; obj < objectMeshes.size(); ++obj) {
        const Mesh* mesh = objectMeshes[obj];
        if (!mesh) {
            continue;
        }
        auto [it, inserted] = batchOfMesh.try_emplace(mesh, static_cast<uint32_t>(uniqueMeshes.size()));
        if (inserted) {
            uniqueMeshes.push_back(mesh);
            objectsOfBatch.emplace_back();
        }
        objectsOfBatch[it->second].push_back(obj);
    }

    uint32_t totalObjects = 0;
    for (const auto& list : objectsOfBatch) {
        totalObjects += static_cast<uint32_t>(list.size());
    }
    if (totalObjects > kMaxObjects || uniqueMeshes.size() > kMaxMeshes) {
        throw std::runtime_error("GPU-driven scene exceeds capacity (objects or mesh batches).");
    }

    m_batches.clear();
    m_slotToObject.clear();
    m_slotToBatch.clear();

    std::vector<GpuMeshDrawInfo> meshInfos(uniqueMeshes.size());
    for (uint32_t b = 0; b < uniqueMeshes.size(); ++b) {
        meshInfos[b] = GpuMeshDrawInfo{uniqueMeshes[b]->getIndexCount(), 0, 0, 0};

        GpuDrawBatch batch{};
        batch.mesh = uniqueMeshes[b];
        batch.firstSlot = static_cast<uint32_t>(m_slotToObject.size());
        batch.count = static_cast<uint32_t>(objectsOfBatch[b].size());
        for (const uint32_t obj : objectsOfBatch[b]) {
            m_slotToObject.push_back(obj);
            m_slotToBatch.push_back(b);
        }
        m_batches.push_back(batch);
    }
    m_objectCount = totalObjects;

    if (!meshInfos.empty()) {
        m_meshInfoBuffer->uploadData(meshInfos.data(), sizeof(GpuMeshDrawInfo) * meshInfos.size(), 0);
    }

    SIDERA_LOG_INFO(
        "GpuDriven",
        "GPU scene layout: {} objects processed into {} indirect command slots across {} mesh batches.",
        objectMeshes.size(),
        m_objectCount,
        m_batches.size()
    );
}

void GpuDrivenScene::uploadObjects(uint32_t frameIndex, std::span<const GpuObjectData> slotData) {
    if (slotData.size() > kMaxObjects) {
        throw std::runtime_error("GPU-driven object upload exceeds buffer capacity.");
    }
    if (!slotData.empty()) {
        m_objectBuffers[frameIndex]->uploadData(slotData.data(), sizeof(GpuObjectData) * slotData.size(), 0);
    }
}

void GpuDrivenScene::uploadCullingUniforms(uint32_t frameIndex, const CullingUniformData& data) {
    m_cullingUniformBuffers[frameIndex]->uploadData(&data, sizeof(CullingUniformData), 0);
}

void GpuDrivenScene::updateHiZDescriptor(uint32_t frameIndex, VkImageView hizImageView, VkSampler hizSampler) {
    if (hizImageView == VK_NULL_HANDLE || hizSampler == VK_NULL_HANDLE) {
        return;
    }
    const VkDevice device = m_device.getLogicalDevice();

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = hizSampler;
    imageInfo.imageView = hizImageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_computeDescriptorSets[frameIndex];
    write.dstBinding = 6;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void GpuDrivenScene::resetCullingStats(uint32_t frameIndex) {
    CullingStats zeroStats{};
    zeroStats.totalObjects = m_objectCount;
    m_cullingStatsBuffers[frameIndex]->uploadData(&zeroStats, sizeof(CullingStats), 0);
}

CullingStats GpuDrivenScene::readCullingStats(uint32_t frameIndex) const noexcept {
    CullingStats s{};
    if (frameIndex < m_cullingStatsBuffers.size() && m_cullingStatsBuffers[frameIndex]) {
        try {
            m_cullingStatsBuffers[frameIndex]->downloadData(&s, sizeof(CullingStats), 0);
        } catch (...) {}
    }
    return s;
}

void GpuDrivenScene::recordGenerateCommands(VkCommandBuffer cmd,
                                            uint32_t frameIndex,
                                            bool frustumCull,
                                            bool occlusionCull,
                                            bool hizValid,
                                            uint32_t hizMipLevels,
                                            glm::vec2 hizSize) const {
    if (m_objectCount == 0) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipelineLayout, 0, 1,
        &m_computeDescriptorSets[frameIndex], 0, nullptr
    );

    const GenCommandsPushConstants pc{
        .objectCount = m_objectCount,
        .meshCount = static_cast<uint32_t>(m_batches.size()),
        .frustumCullingEnabled = frustumCull ? 1u : 0u,
        .occlusionCullingEnabled = occlusionCull ? 1u : 0u,
        .hizValid = hizValid ? 1u : 0u,
        .hizMipLevels = hizMipLevels,
        .hizSize = hizSize
    };
    vkCmdPushConstants(cmd, m_computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    const uint32_t groups = (m_objectCount + kGenCommandsLocalSize - 1) / kGenCommandsLocalSize;
    vkCmdDispatch(cmd, groups, 1, 1);

    // Compute shader writes -> indirect command reads (main pass & shadow cascades)
    std::array<VkBufferMemoryBarrier2, 2> barriers{};
    barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].buffer = m_mainCommandBuffers[frameIndex]->getHandle();
    barriers[0].offset = 0;
    barriers[0].size = VK_WHOLE_SIZE;

    barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].buffer = m_shadowCommandBuffers[frameIndex]->getHandle();
    barriers[1].offset = 0;
    barriers[1].size = VK_WHOLE_SIZE;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependencyInfo.pBufferMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

uint32_t GpuDrivenScene::recordIndirectDraws(VkCommandBuffer cmd, uint32_t frameIndex, bool isShadow) const {
    const VkBuffer commandBuffer = isShadow
        ? m_shadowCommandBuffers[frameIndex]->getHandle()
        : m_mainCommandBuffers[frameIndex]->getHandle();
    constexpr uint32_t kStride = sizeof(VkDrawIndexedIndirectCommand);

    uint32_t apiCalls = 0;
    for (const GpuDrawBatch& batch : m_batches) {
        batch.mesh->bind(cmd);

        if (m_multiDrawIndirect) {
            uint32_t remaining = batch.count;
            uint32_t slot = batch.firstSlot;
            while (remaining > 0) {
                const uint32_t chunk = std::min(remaining, m_maxDrawIndirectCount);
                vkCmdDrawIndexedIndirect(cmd, commandBuffer, static_cast<VkDeviceSize>(slot) * kStride, chunk, kStride);
                ++apiCalls;
                slot += chunk;
                remaining -= chunk;
            }
        } else {
            for (uint32_t i = 0; i < batch.count; ++i) {
                vkCmdDrawIndexedIndirect(
                    cmd, commandBuffer, static_cast<VkDeviceSize>(batch.firstSlot + i) * kStride, 1, kStride);
                ++apiCalls;
            }
        }
    }
    return apiCalls;
}

} // namespace sidera
