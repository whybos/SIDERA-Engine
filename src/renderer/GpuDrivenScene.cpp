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
    uint32_t instanceCount;
    uint32_t batchCount;
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
    , m_maxDrawIndirectCount(std::max(1u, device.getProperties().limits.maxDrawIndirectCount))
    , m_instanceCapacity(kInitialMaxInstances) {
    if (!isSupported(device)) {
        throw std::runtime_error(
            "GPU-driven rendering requires the drawIndirectFirstInstance device feature, which is unavailable.");
    }

    try {
        for (uint32_t i = 0; i < m_framesInFlight; ++i) {
            m_objectBuffers.push_back(std::make_unique<VulkanBuffer>(
                m_device,
                sizeof(GpuInstanceData) * m_instanceCapacity,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            ));

            m_visibleIndicesBuffers.push_back(std::make_unique<VulkanBuffer>(
                m_device,
                sizeof(uint32_t) * m_instanceCapacity,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            ));

            m_mainCommandBuffers.push_back(std::make_unique<VulkanBuffer>(
                m_device,
                sizeof(VkDrawIndexedIndirectCommand) * kMaxBatches,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            ));

            m_shadowCommandBuffers.push_back(std::make_unique<VulkanBuffer>(
                m_device,
                sizeof(VkDrawIndexedIndirectCommand) * kMaxBatches,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            ));

            m_batchInfoBuffers.push_back(std::make_unique<VulkanBuffer>(
                m_device,
                sizeof(GpuBatchInfo) * kMaxBatches,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
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
        "GPU-driven instancing scene initialized (capacity: {} instances / {} batches, multiDraw: {}, {} frames).",
        m_instanceCapacity,
        kMaxBatches,
        m_multiDrawIndirect ? "ENABLED" : "UNAVAILABLE",
        m_framesInFlight
    );
}

GpuDrivenScene::~GpuDrivenScene() {
    destroyComputeResources();
    m_meshInfoBuffer.reset();
    m_cullingStatsBuffers.clear();
    m_cullingUniformBuffers.clear();
    m_batchInfoBuffers.clear();
    m_shadowCommandBuffers.clear();
    m_mainCommandBuffers.clear();
    m_visibleIndicesBuffers.clear();
    m_objectBuffers.clear();
    SIDERA_LOG_INFO("GpuDriven", "GPU-driven scene destroyed.");
}

void GpuDrivenScene::createComputeResources() {
    const VkDevice device = m_device.getLogicalDevice();

    std::array<VkDescriptorSetLayoutBinding, 9> bindings{};

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

    // Binding 2: MainCommandBuffer (SSBO, readwrite)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 3: ShadowCommandBuffer (SSBO, readwrite)
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

    // Binding 7: VisibleIndicesBuffer (SSBO, writeonly)
    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 8: BatchInfoBuffer (SSBO, readonly)
    bindings[8].binding = 8;
    bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[8].descriptorCount = 1;
    bindings[8].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    VK_CHECK(
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_computeSetLayout),
        "Failed to create GPU-driven compute descriptor set layout"
    );

    std::array<VkDescriptorPoolSize, 3> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_framesInFlight * 7},
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

    updateComputeDescriptors();

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

void GpuDrivenScene::updateComputeDescriptors() {
    const VkDevice device = m_device.getLogicalDevice();

    for (uint32_t f = 0; f < m_framesInFlight; ++f) {
        const VkDescriptorBufferInfo objInfo{m_objectBuffers[f]->getHandle(), 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo meshInfo{m_meshInfoBuffer->getHandle(), 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo mainCmdInfo{m_mainCommandBuffers[f]->getHandle(), 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo shdCmdInfo{m_shadowCommandBuffers[f]->getHandle(), 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo cullUboInfo{m_cullingUniformBuffers[f]->getHandle(), 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo statsInfo{m_cullingStatsBuffers[f]->getHandle(), 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo visibleInfo{m_visibleIndicesBuffers[f]->getHandle(), 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo batchInfo{m_batchInfoBuffers[f]->getHandle(), 0, VK_WHOLE_SIZE};

        std::array<VkWriteDescriptorSet, 8> writes{};

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

        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = m_computeDescriptorSets[f];
        writes[6].dstBinding = 7;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[6].descriptorCount = 1;
        writes[6].pBufferInfo = &visibleInfo;

        writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet = m_computeDescriptorSets[f];
        writes[7].dstBinding = 8;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[7].descriptorCount = 1;
        writes[7].pBufferInfo = &batchInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
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

void GpuDrivenScene::ensureInstanceCapacity(uint32_t requiredCapacity) {
    if (requiredCapacity <= m_instanceCapacity) {
        return;
    }

    uint32_t newCap = m_instanceCapacity;
    while (newCap < requiredCapacity) {
        newCap *= 2;
    }

    SIDERA_LOG_INFO("GpuDriven", "Expanding instance capacity from {} to {} instances.", m_instanceCapacity, newCap);
    m_device.waitIdle();
    m_instanceCapacity = newCap;

    for (uint32_t f = 0; f < m_framesInFlight; ++f) {
        m_objectBuffers[f] = std::make_unique<VulkanBuffer>(
            m_device,
            sizeof(GpuInstanceData) * m_instanceCapacity,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        m_visibleIndicesBuffers[f] = std::make_unique<VulkanBuffer>(
            m_device,
            sizeof(uint32_t) * m_instanceCapacity,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
    }

    updateComputeDescriptors();

    if (m_onBuffersReallocated) {
        m_onBuffersReallocated();
    }
}

void GpuDrivenScene::setSceneBatches(const InstanceBatchManager& batchManager) {
    const auto& batches = batchManager.getBatches();
    if (batches.size() > kMaxBatches) {
        throw std::runtime_error("Instance batch count exceeds kMaxBatches limit.");
    }

    ensureInstanceCapacity(batchManager.getTotalInstanceCount());

    m_batches.clear();
    m_templateMainCommands.clear();

    std::vector<VkDrawIndexedIndirectCommand> shadowCommands;
    std::vector<GpuBatchInfo> batchInfos;
    std::vector<GpuMeshDrawInfo> meshInfos;

    shadowCommands.reserve(batches.size());
    batchInfos.reserve(batches.size());
    meshInfos.reserve(batches.size());
    m_templateMainCommands.reserve(batches.size());

    for (uint32_t b = 0; b < batches.size(); ++b) {
        const auto& batch = batches[b];
        const Mesh* mesh = batch->getMesh();
        const uint32_t indexCount = mesh ? mesh->getIndexCount() : 0;
        const uint32_t firstInst = batch->getFirstGlobalInstance();
        const uint32_t instCount = batch->getInstanceCount();

        GpuDrawBatch drawBatch{};
        drawBatch.mesh = mesh;
        drawBatch.batchId = b;
        drawBatch.firstInstance = firstInst;
        drawBatch.instanceCount = instCount;
        m_batches.push_back(drawBatch);

        // Template main command: instanceCount set to 0, incremented atomically by compute shader
        VkDrawIndexedIndirectCommand mainCmd{};
        mainCmd.indexCount = indexCount;
        mainCmd.instanceCount = 0;
        mainCmd.firstIndex = 0;
        mainCmd.vertexOffset = 0;
        mainCmd.firstInstance = firstInst;
        m_templateMainCommands.push_back(mainCmd);

        // Shadow command: instanceCount set to total instances (all instances cast shadows!)
        VkDrawIndexedIndirectCommand shdCmd{};
        shdCmd.indexCount = indexCount;
        shdCmd.instanceCount = instCount;
        shdCmd.firstIndex = 0;
        shdCmd.vertexOffset = 0;
        shdCmd.firstInstance = firstInst;
        shadowCommands.push_back(shdCmd);

        GpuBatchInfo bInfo{};
        bInfo.meshIndex = b;
        bInfo.firstInstance = firstInst;
        bInfo.instanceCount = instCount;
        bInfo.pad = 0;
        batchInfos.push_back(bInfo);

        meshInfos.push_back(GpuMeshDrawInfo{indexCount, 0, 0, 0});
    }

    m_totalInstanceCount = batchManager.getTotalInstanceCount();

    // Upload static batch metadata
    if (!meshInfos.empty()) {
        m_meshInfoBuffer->uploadData(meshInfos.data(), sizeof(GpuMeshDrawInfo) * meshInfos.size(), 0);
    }
    for (uint32_t f = 0; f < m_framesInFlight; ++f) {
        if (!shadowCommands.empty()) {
            m_shadowCommandBuffers[f]->uploadData(
                shadowCommands.data(), sizeof(VkDrawIndexedIndirectCommand) * shadowCommands.size(), 0);
        }
        if (!batchInfos.empty()) {
            m_batchInfoBuffers[f]->uploadData(batchInfos.data(), sizeof(GpuBatchInfo) * batchInfos.size(), 0);
        }
        resetMainCommands(f);
    }

    SIDERA_LOG_INFO(
        "GpuDriven",
        "Configured GPU instanced scene: {} total instances across {} batches.",
        m_totalInstanceCount,
        m_batches.size()
    );
}

void GpuDrivenScene::resetMainCommands(uint32_t frameIndex) {
    if (!m_templateMainCommands.empty()) {
        m_mainCommandBuffers[frameIndex]->uploadData(
            m_templateMainCommands.data(),
            sizeof(VkDrawIndexedIndirectCommand) * m_templateMainCommands.size(),
            0
        );
    }
}

void GpuDrivenScene::uploadInstances(uint32_t frameIndex, std::span<const GpuInstanceData> instances) {
    ensureInstanceCapacity(static_cast<uint32_t>(instances.size()));
    if (!instances.empty()) {
        m_objectBuffers[frameIndex]->uploadData(instances.data(), sizeof(GpuInstanceData) * instances.size(), 0);
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
    zeroStats.totalObjects = m_totalInstanceCount;
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
    if (m_totalInstanceCount == 0 || m_batches.empty()) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipelineLayout, 0, 1,
        &m_computeDescriptorSets[frameIndex], 0, nullptr
    );

    const GenCommandsPushConstants pc{
        .instanceCount = m_totalInstanceCount,
        .batchCount = static_cast<uint32_t>(m_batches.size()),
        .frustumCullingEnabled = frustumCull ? 1u : 0u,
        .occlusionCullingEnabled = occlusionCull ? 1u : 0u,
        .hizValid = hizValid ? 1u : 0u,
        .hizMipLevels = hizMipLevels,
        .hizSize = hizSize
    };
    vkCmdPushConstants(cmd, m_computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    const uint32_t groups = (m_totalInstanceCount + kGenCommandsLocalSize - 1) / kGenCommandsLocalSize;
    vkCmdDispatch(cmd, groups, 1, 1);

    // Compute shader writes -> indirect command reads & vertex shader buffer reads
    std::array<VkBufferMemoryBarrier2, 3> barriers{};
    // 1. MainCommandBuffer barrier: Compute Write -> Indirect Command Read
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

    // 2. ShadowCommandBuffer barrier
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

    // 3. VisibleIndicesBuffer barrier: Compute Write -> Vertex Shader Read
    barriers[2].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barriers[2].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barriers[2].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    barriers[2].dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    barriers[2].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    barriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[2].buffer = m_visibleIndicesBuffers[frameIndex]->getHandle();
    barriers[2].offset = 0;
    barriers[2].size = VK_WHOLE_SIZE;

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
    for (uint32_t b = 0; b < m_batches.size(); ++b) {
        const GpuDrawBatch& batch = m_batches[b];
        if (!batch.mesh || batch.instanceCount == 0) {
            continue;
        }
        batch.mesh->bind(cmd);
        vkCmdDrawIndexedIndirect(
            cmd,
            commandBuffer,
            static_cast<VkDeviceSize>(b) * kStride,
            1,
            kStride
        );
        ++apiCalls;
    }
    return apiCalls;
}

} // namespace sidera
