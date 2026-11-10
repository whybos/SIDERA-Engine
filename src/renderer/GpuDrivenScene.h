#pragma once

#include "renderer/vulkan/VulkanBuffer.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sidera {

class Mesh;
class VulkanDevice;

// GPU-resident per-object record (std430). Must match GpuObjectData in
// gen_draw_commands.comp, triangle_indirect.vert and shadow_depth_indirect.vert.
struct alignas(16) GpuObjectData {
    glm::mat4 model{1.0f};
    glm::vec4 albedoMetallic{1.0f, 1.0f, 1.0f, 0.0f};   // rgb = albedo tint, a = metallic
    glm::vec4 roughnessAoTex{0.5f, 1.0f, 0.0f, 0.0f};   // x = roughness, y = ao, z = texture strength
    glm::uvec4 meshInfo{0u, 1u, 0u, 0u};                // x = mesh batch index, y = active flag
};
static_assert(sizeof(GpuObjectData) == 112, "GpuObjectData must match the std430 layout used by the shaders.");

// Per-mesh draw metadata read by the command-generation compute shader (std430).
struct GpuMeshDrawInfo {
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t pad = 0;
};
static_assert(sizeof(GpuMeshDrawInfo) == 16, "GpuMeshDrawInfo must match the std430 layout used by the shaders.");

static_assert(sizeof(VkDrawIndexedIndirectCommand) == 20, "Unexpected VkDrawIndexedIndirectCommand size.");

// A contiguous run of indirect command slots that all draw the same mesh. Every mesh owns its own
// vertex/index buffer, so one batch == one buffer binding == one (multi-)indirect call.
struct GpuDrawBatch {
    const Mesh* mesh = nullptr;
    uint32_t firstSlot = 0;
    uint32_t count = 0;
};

// GPU-driven draw submission (Milestone 14).
//
// Owns the per-frame object and indirect-command buffers, the static mesh-metadata buffer and the
// compute pipeline that turns object data into VkDrawIndexedIndirectCommand entries.
// No culling is performed: the command count is fixed (== object count) and unused slots would be
// zero-index-count commands.
class GpuDrivenScene {
public:
    static constexpr uint32_t kMaxObjects = 512;
    static constexpr uint32_t kMaxMeshes = 64;

    // Throws std::runtime_error if the required device capabilities are missing or resources fail.
    GpuDrivenScene(const VulkanDevice& device, uint32_t framesInFlight);
    ~GpuDrivenScene();

    GpuDrivenScene(const GpuDrivenScene&) = delete;
    GpuDrivenScene& operator=(const GpuDrivenScene&) = delete;
    GpuDrivenScene(GpuDrivenScene&&) = delete;
    GpuDrivenScene& operator=(GpuDrivenScene&&) = delete;

    // True when the device can run the GPU-driven path (needs drawIndirectFirstInstance).
    [[nodiscard]] static bool isSupported(const VulkanDevice& device) noexcept;

    // Groups objects by mesh and uploads mesh metadata. The caller must guarantee the GPU is not
    // using the mesh-info buffer (e.g. wait for idle). objectMeshes[i] may be null (skipped).
    void setScene(std::span<const Mesh* const> objectMeshes);

    // For each GPU slot: index of the source object / mesh batch index. Sizes == getObjectCount().
    [[nodiscard]] const std::vector<uint32_t>& getSlotToObject() const noexcept { return m_slotToObject; }
    [[nodiscard]] const std::vector<uint32_t>& getSlotToBatch() const noexcept { return m_slotToBatch; }

    // Uploads per-slot object data for the given frame slot (host-coherent write; the caller has
    // already waited for that frame's fence, so the GPU is no longer reading this buffer).
    void uploadObjects(uint32_t frameIndex, std::span<const GpuObjectData> slotData);

    // Records the command-generation dispatch plus the compute->indirect-read barrier.
    void recordGenerateCommands(VkCommandBuffer cmd, uint32_t frameIndex) const;

    // Records the batched indirect draws. The caller must have bound the graphics pipeline and
    // descriptor set. Returns the number of vkCmdDrawIndexedIndirect API calls issued.
    uint32_t recordIndirectDraws(VkCommandBuffer cmd, uint32_t frameIndex) const;

    [[nodiscard]] VkBuffer getObjectBuffer(uint32_t frameIndex) const noexcept { return m_objectBuffers[frameIndex]->getHandle(); }
    [[nodiscard]] VkDeviceSize getObjectBufferSize() const noexcept { return sizeof(GpuObjectData) * kMaxObjects; }

    [[nodiscard]] uint32_t getObjectCount() const noexcept { return m_objectCount; }
    [[nodiscard]] uint32_t getBatchCount() const noexcept { return static_cast<uint32_t>(m_batches.size()); }
    [[nodiscard]] uint32_t getCommandsGenerated() const noexcept { return m_objectCount; }
    [[nodiscard]] bool isMultiDrawIndirect() const noexcept { return m_multiDrawIndirect; }

private:
    void createComputeResources();
    void destroyComputeResources() noexcept;

    const VulkanDevice& m_device;
    uint32_t m_framesInFlight = 0;
    bool m_multiDrawIndirect = false;
    uint32_t m_maxDrawIndirectCount = 1;

    std::vector<std::unique_ptr<VulkanBuffer>> m_objectBuffers;   // per frame, host-visible
    std::vector<std::unique_ptr<VulkanBuffer>> m_commandBuffers;  // per frame, device-local
    std::unique_ptr<VulkanBuffer> m_meshInfoBuffer;               // static, host-visible

    VkDescriptorSetLayout m_computeSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_computeDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_computeDescriptorSets;
    VkPipelineLayout m_computePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_computePipeline = VK_NULL_HANDLE;

    std::vector<GpuDrawBatch> m_batches;
    std::vector<uint32_t> m_slotToObject;
    std::vector<uint32_t> m_slotToBatch;
    uint32_t m_objectCount = 0;
};

} // namespace sidera
