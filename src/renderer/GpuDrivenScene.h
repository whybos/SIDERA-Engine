#pragma once

#include "renderer/InstanceBatch.h"
#include "renderer/InstanceData.h"
#include "renderer/vulkan/VulkanBuffer.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace sidera {

class Mesh;
class VulkanDevice;

// GPU-resident per-object / per-instance record (std430, 160 bytes).
// Must match GpuObjectData in gen_draw_commands.comp, triangle_indirect.vert and shadow_depth_indirect.vert.
using GpuObjectData = GpuInstanceData;

// Per-mesh draw metadata read by the command-generation compute shader (std430, 16 bytes).
struct GpuMeshDrawInfo {
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t pad = 0;
};
static_assert(sizeof(GpuMeshDrawInfo) == 16, "GpuMeshDrawInfo must match the std430 layout used by shaders.");
static_assert(sizeof(VkDrawIndexedIndirectCommand) == 20, "Unexpected VkDrawIndexedIndirectCommand size.");

// Camera Frustum and Projection data passed to gen_draw_commands.comp
struct alignas(16) CullingUniformData {
    glm::mat4 viewProj{1.0f};
    glm::mat4 prevViewProj{1.0f};
    std::array<glm::vec4, 6> frustumPlanes{}; // Left, Right, Bottom, Top, Near, Far
    glm::vec4 cameraPos{0.0f};
};
static_assert(sizeof(CullingUniformData) == 240, "CullingUniformData must be 240 bytes aligned.");

// Atomic culling statistics gathered by gen_draw_commands.comp on the GPU
struct alignas(16) CullingStats {
    uint32_t totalObjects = 0;
    uint32_t frustumCulled = 0;
    uint32_t occlusionCulled = 0;
    uint32_t finalVisible = 0;
};
static_assert(sizeof(CullingStats) == 16, "CullingStats must be 16 bytes.");

// Tracks an active GPU instanced draw batch.
struct GpuDrawBatch {
    const Mesh* mesh = nullptr;
    uint32_t batchId = 0;
    uint32_t firstInstance = 0;
    uint32_t instanceCount = 0;
};

// GPU-driven draw submission, GPU instancing & visibility culling (Milestones 14, 15, 16).
class GpuDrivenScene {
public:
    static constexpr uint32_t kInitialMaxInstances = 16384;
    static constexpr uint32_t kMaxBatches = 256;
    static constexpr uint32_t kMaxMeshes = 64;

    GpuDrivenScene(const VulkanDevice& device, uint32_t framesInFlight);
    ~GpuDrivenScene();

    GpuDrivenScene(const GpuDrivenScene&) = delete;
    GpuDrivenScene& operator=(const GpuDrivenScene&) = delete;
    GpuDrivenScene(GpuDrivenScene&&) = delete;
    GpuDrivenScene& operator=(GpuDrivenScene&&) = delete;

    [[nodiscard]] static bool isSupported(const VulkanDevice& device) noexcept;

    // Registers scene batches from the InstanceBatchManager.
    void setSceneBatches(const InstanceBatchManager& batchManager);

    // Dynamic buffer expansion for large instance counts (e.g. 10,000+).
    void ensureInstanceCapacity(uint32_t requiredCapacity);

    // Uploads instance array for this frame slot.
    void uploadInstances(uint32_t frameIndex, std::span<const GpuInstanceData> instances);
    void uploadCullingUniforms(uint32_t frameIndex, const CullingUniformData& data);

    // Updates descriptor binding 6 with the Hi-Z texture view of the previous frame.
    void updateHiZDescriptor(uint32_t frameIndex, VkImageView hizImageView, VkSampler hizSampler);

    // Resets GPU culling statistics counters and main command counters for this frame slot.
    void resetCullingStats(uint32_t frameIndex);
    void resetCullingStats(VkCommandBuffer cmd, uint32_t frameIndex) { (void)cmd; resetCullingStats(frameIndex); }
    void resetMainCommands(uint32_t frameIndex);

    // Asynchronously reads culling statistics from the frame slot that just completed execution.
    [[nodiscard]] CullingStats readCullingStats(uint32_t frameIndex) const noexcept;

    // Records the command-generation & culling dispatch plus the compute->indirect-read barriers.
    void recordGenerateCommands(VkCommandBuffer cmd,
                                uint32_t frameIndex,
                                bool frustumCull = true,
                                bool occlusionCull = true,
                                bool hizValid = true,
                                uint32_t hizMipLevels = 11,
                                glm::vec2 hizSize = {1024.0f, 512.0f}) const;

    // Records instanced indirect draws (1 draw call per batch).
    // isShadow selects the shadow command buffer (un-culled, draws all shadow casters).
    uint32_t recordIndirectDraws(VkCommandBuffer cmd, uint32_t frameIndex, bool isShadow = false) const;

    [[nodiscard]] VkBuffer getObjectBuffer(uint32_t frameIndex) const noexcept { return m_objectBuffers[frameIndex]->getHandle(); }
    [[nodiscard]] VkDeviceSize getObjectBufferSize() const noexcept { return sizeof(GpuInstanceData) * m_instanceCapacity; }

    [[nodiscard]] VkBuffer getVisibleIndicesBuffer(uint32_t frameIndex) const noexcept { return m_visibleIndicesBuffers[frameIndex]->getHandle(); }
    [[nodiscard]] VkDeviceSize getVisibleIndicesBufferSize() const noexcept { return sizeof(uint32_t) * m_instanceCapacity; }

    [[nodiscard]] uint32_t getTotalInstanceCount() const noexcept { return m_totalInstanceCount; }
    [[nodiscard]] uint32_t getObjectCount() const noexcept { return m_totalInstanceCount; }
    [[nodiscard]] uint32_t getBatchCount() const noexcept { return static_cast<uint32_t>(m_batches.size()); }
    [[nodiscard]] uint32_t getCommandsGenerated() const noexcept { return m_totalInstanceCount; }
    [[nodiscard]] uint32_t getInstanceCapacity() const noexcept { return m_instanceCapacity; }
    [[nodiscard]] bool isMultiDrawIndirect() const noexcept { return m_multiDrawIndirect; }

    void setOnBuffersReallocated(std::function<void()> cb) { m_onBuffersReallocated = std::move(cb); }

private:
    void createComputeResources();
    void updateComputeDescriptors();
    void destroyComputeResources() noexcept;

    const VulkanDevice& m_device;
    uint32_t m_framesInFlight = 0;
    bool m_multiDrawIndirect = false;
    uint32_t m_maxDrawIndirectCount = 1;
    uint32_t m_instanceCapacity = kInitialMaxInstances;

    std::vector<std::unique_ptr<VulkanBuffer>> m_objectBuffers;         // per frame, host-visible
    std::vector<std::unique_ptr<VulkanBuffer>> m_visibleIndicesBuffers; // per frame, host-visible / storage
    std::vector<std::unique_ptr<VulkanBuffer>> m_mainCommandBuffers;    // per frame, host-visible / indirect
    std::vector<std::unique_ptr<VulkanBuffer>> m_shadowCommandBuffers;  // per frame, host-visible / indirect
    std::vector<std::unique_ptr<VulkanBuffer>> m_batchInfoBuffers;      // per frame, host-visible
    std::vector<std::unique_ptr<VulkanBuffer>> m_cullingUniformBuffers; // per frame, host-visible
    std::vector<std::unique_ptr<VulkanBuffer>> m_cullingStatsBuffers;   // per frame, host-visible/coherent
    std::unique_ptr<VulkanBuffer> m_meshInfoBuffer;                     // static, host-visible

    VkDescriptorSetLayout m_computeSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_computeDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_computeDescriptorSets;
    VkPipelineLayout m_computePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_computePipeline = VK_NULL_HANDLE;

    std::vector<GpuDrawBatch> m_batches;
    std::vector<VkDrawIndexedIndirectCommand> m_templateMainCommands;
    uint32_t m_totalInstanceCount = 0;

    std::function<void()> m_onBuffersReallocated;
};

} // namespace sidera
