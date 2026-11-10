#pragma once

#include "renderer/vulkan/VulkanBuffer.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
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

// GPU-resident per-object record (std430, 160 bytes). Must match GpuObjectData in
// gen_draw_commands.comp, triangle_indirect.vert and shadow_depth_indirect.vert.
struct alignas(16) GpuObjectData {
    glm::mat4 model{1.0f};
    glm::vec4 albedoMetallic{1.0f, 1.0f, 1.0f, 0.0f};   // rgb = albedo tint, a = metallic
    glm::vec4 roughnessAoTex{0.5f, 1.0f, 0.0f, 0.0f};   // x = roughness, y = ao, z = texture strength
    glm::uvec4 meshInfo{0u, 1u, 0u, 0u};                // x = mesh batch index, y = active flag
    glm::vec4 boundingSphere{0.0f, 0.0f, 0.0f, 1.0f};  // xyz = world center, w = world radius
    glm::vec4 aabbMin{-1.0f, -1.0f, -1.0f, 0.0f};      // xyz = world AABB min, w = unused
    glm::vec4 aabbMax{1.0f, 1.0f, 1.0f, 0.0f};         // xyz = world AABB max, w = unused
};
static_assert(sizeof(GpuObjectData) == 160, "GpuObjectData must match the std430 layout used by the shaders.");

// Per-mesh draw metadata read by the command-generation compute shader (std430).
struct GpuMeshDrawInfo {
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t pad = 0;
};
static_assert(sizeof(GpuMeshDrawInfo) == 16, "GpuMeshDrawInfo must match the std430 layout used by the shaders.");

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

// A contiguous run of indirect command slots that all draw the same mesh.
struct GpuDrawBatch {
    const Mesh* mesh = nullptr;
    uint32_t firstSlot = 0;
    uint32_t count = 0;
};

// GPU-driven draw submission & visibility culling (Milestone 14 + 15).
class GpuDrivenScene {
public:
    static constexpr uint32_t kMaxObjects = 512;
    static constexpr uint32_t kMaxMeshes = 64;

    GpuDrivenScene(const VulkanDevice& device, uint32_t framesInFlight);
    ~GpuDrivenScene();

    GpuDrivenScene(const GpuDrivenScene&) = delete;
    GpuDrivenScene& operator=(const GpuDrivenScene&) = delete;
    GpuDrivenScene(GpuDrivenScene&&) = delete;
    GpuDrivenScene& operator=(GpuDrivenScene&&) = delete;

    [[nodiscard]] static bool isSupported(const VulkanDevice& device) noexcept;

    void setScene(std::span<const Mesh* const> objectMeshes);

    [[nodiscard]] const std::vector<uint32_t>& getSlotToObject() const noexcept { return m_slotToObject; }
    [[nodiscard]] const std::vector<uint32_t>& getSlotToBatch() const noexcept { return m_slotToBatch; }

    void uploadObjects(uint32_t frameIndex, std::span<const GpuObjectData> slotData);
    void uploadCullingUniforms(uint32_t frameIndex, const CullingUniformData& data);

    // Updates descriptor binding 6 with the Hi-Z texture view of the previous frame.
    void updateHiZDescriptor(uint32_t frameIndex, VkImageView hizImageView, VkSampler hizSampler);

    // Resets GPU culling statistics counters for this frame slot.
    void resetCullingStats(uint32_t frameIndex);
    void resetCullingStats(VkCommandBuffer cmd, uint32_t frameIndex) { (void)cmd; resetCullingStats(frameIndex); }

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

    // Records the batched indirect draws. isShadow selects the shadow command buffer (un-culled).
    uint32_t recordIndirectDraws(VkCommandBuffer cmd, uint32_t frameIndex, bool isShadow = false) const;

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

    std::vector<std::unique_ptr<VulkanBuffer>> m_objectBuffers;         // per frame, host-visible
    std::vector<std::unique_ptr<VulkanBuffer>> m_mainCommandBuffers;    // per frame, device-local (camera culled)
    std::vector<std::unique_ptr<VulkanBuffer>> m_shadowCommandBuffers;  // per frame, device-local (all shadow casters)
    std::vector<std::unique_ptr<VulkanBuffer>> m_cullingUniformBuffers; // per frame, host-visible
    std::vector<std::unique_ptr<VulkanBuffer>> m_cullingStatsBuffers;   // per frame, host-visible/coherent
    std::unique_ptr<VulkanBuffer> m_meshInfoBuffer;                     // static, host-visible

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
