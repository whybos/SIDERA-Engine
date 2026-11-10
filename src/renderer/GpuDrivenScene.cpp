#include "renderer/GpuDrivenScene.h"
#include "core/Log.h"
#include "renderer/Mesh.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanDevice.h"
#include "renderer/vulkan/VulkanShader.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace sidera {

namespace {
constexpr const char* kGenCommandsShaderPath = "shaders/gen_draw_commands.comp.spv";
constexpr uint32_t kGenCommandsLocalSize = 64; // must match local_size_x in gen_draw_commands.comp

struct GenCommandsPushConstants {
    uint32_t objectCount;
    uint32_t meshCount;
};
} // namespace

bool GpuDrivenScene::isSupported(const VulkanDevice& device) noexcept {
    // Per-object data is addressed through firstInstance (gl_InstanceIndex). Without
    // drawIndirectFirstInstance, non-zero firstInstance values are not allowed in indirect commands.
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

            m_commandBuffers.push_back(std::make_unique<VulkanBuffer>(
                m_device,
                sizeof(VkDrawIndexedIndirectCommand) * kMaxObjects,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
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
        "GPU-driven scene initialized (capacity: {} objects / {} mesh batches, multiDrawIndirect: {}, "
        "maxDrawIndirectCount: {}, {} frames in flight).",
        kMaxObjects,
        kMaxMeshes,
        m_multiDrawIndirect ? "ENABLED" : "UNAVAILABLE (separate indirect calls)",
        m_maxDrawIndirectCount,
        m_framesInFlight
    );
}

GpuDrivenScene::~GpuDrivenScene() {
    // The owner (Renderer) waits for the device to be idle before destroying this object.
    destroyComputeResources();
    m_meshInfoBuffer.reset();
    m_commandBuffers.clear();
    m_objectBuffers.clear();
    SIDERA_LOG_INFO("GpuDriven", "GPU-driven scene destroyed.");
}

void GpuDrivenScene::createComputeResources() {
    const VkDevice device = m_device.getLogicalDevice();

    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    for (uint32_t i = 0; i < bindings.size(); ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    VK_CHECK(
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_computeSetLayout),
        "Failed to create GPU-driven compute descriptor set layout"
    );

    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_framesInFlight * 3};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
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
        const std::array<VkDescriptorBufferInfo, 3> infos = {{
            {m_objectBuffers[f]->getHandle(), 0, VK_WHOLE_SIZE},
            {m_meshInfoBuffer->getHandle(), 0, VK_WHOLE_SIZE},
            {m_commandBuffers[f]->getHandle(), 0, VK_WHOLE_SIZE}
        }};

        std::array<VkWriteDescriptorSet, 3> writes{};
        for (uint32_t b = 0; b < writes.size(); ++b) {
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet = m_computeDescriptorSets[f];
            writes[b].dstBinding = b;
            writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[b].descriptorCount = 1;
            writes[b].pBufferInfo = &infos[b];
        }
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
        vkDestroyDescriptorPool(device, m_computeDescriptorPool, nullptr); // frees the sets
        m_computeDescriptorPool = VK_NULL_HANDLE;
        m_computeDescriptorSets.clear();
    }
    if (m_computeSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_computeSetLayout, nullptr);
        m_computeSetLayout = VK_NULL_HANDLE;
    }
}

void GpuDrivenScene::setScene(std::span<const Mesh* const> objectMeshes) {
    // Group objects by mesh (stable) so that every batch maps to a single vertex/index binding.
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

void GpuDrivenScene::recordGenerateCommands(VkCommandBuffer cmd, uint32_t frameIndex) const {
    if (m_objectCount == 0) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipelineLayout, 0, 1,
        &m_computeDescriptorSets[frameIndex], 0, nullptr
    );

    const GenCommandsPushConstants pc{m_objectCount, static_cast<uint32_t>(m_batches.size())};
    vkCmdPushConstants(cmd, m_computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    const uint32_t groups = (m_objectCount + kGenCommandsLocalSize - 1) / kGenCommandsLocalSize;
    vkCmdDispatch(cmd, groups, 1, 1);

    // Compute shader writes -> indirect command reads (shadow cascades + main pass).
    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = m_commandBuffers[frameIndex]->getHandle();
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.bufferMemoryBarrierCount = 1;
    dependencyInfo.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

uint32_t GpuDrivenScene::recordIndirectDraws(VkCommandBuffer cmd, uint32_t frameIndex) const {
    const VkBuffer commandBuffer = m_commandBuffers[frameIndex]->getHandle();
    constexpr uint32_t kStride = sizeof(VkDrawIndexedIndirectCommand);

    uint32_t apiCalls = 0;
    for (const GpuDrawBatch& batch : m_batches) {
        batch.mesh->bind(cmd);

        if (m_multiDrawIndirect) {
            // One call per batch (chunked only if the batch exceeds maxDrawIndirectCount).
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
            // Fallback: drawCount must be <= 1 without multiDrawIndirect.
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
