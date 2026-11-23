#pragma once

#include "renderer/Camera.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "vegetation/GPUVegetationData.h"
#include "vegetation/VegetationTypes.h"

#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sidera {

class VulkanDevice;

// GPU-driven vegetation culling, LOD selection, atomic instance compaction, and indirect draw command generator (Milestone 26).
class GPUVegetationSystem {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    GPUVegetationSystem() = default;
    ~GPUVegetationSystem();

    GPUVegetationSystem(const GPUVegetationSystem&) = delete;
    GPUVegetationSystem& operator=(const GPUVegetationSystem&) = delete;

    // Initialize with Vulkan device (live GPU compute) or nullptr (headless CPU verification)
    bool init(const GPUVegetationConfig& config,
              const VulkanDevice* device = nullptr,
              VkCommandPool transferCommandPool = VK_NULL_HANDLE);
    void destroy() noexcept;

    // Register species LOD rules (maps speciesIdx -> 4 LOD indirect batch slots)
    void setSpeciesLODs(const std::vector<GPUVegetationSpeciesLOD>& speciesLods);

    // Upload or append persistent vegetation instances (stays GPU-resident; never re-uploaded per frame)
    bool uploadInstances(const std::vector<GPUVegetationInstance>& instances);
    bool appendInstances(const std::vector<GPUVegetationInstance>& additional);
    bool uploadFromPlacements(const std::vector<VegetationPlacement>& placements);
    void clearInstances();

    // Advance frame counter and safely retire deferred old buffers (frames-in-flight safe)
    void beginFrame() noexcept;

    // Record GPU compute culling, LOD selection, atomic compaction, and synchronization barrier
    void recordCullAndCompact(VkCommandBuffer cmd,
                              uint32_t frameIndex,
                              const Camera& camera,
                              bool isShadowPass = false);

    // Deterministic CPU reference implementation matching veg_cull_compact.comp (for headless unit tests)
    GPUVegetationStatistics simulateCullAndCompactCPU(const Camera& camera,
                                                      bool isShadowPass,
                                                      std::vector<uint32_t>& outVisibleIndices,
                                                      std::vector<GPUVegetationIndirectCmd>& outIndirectCmds) const;

    // Accessors
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] bool isGpuActive() const noexcept { return m_device != nullptr && m_computePipeline != VK_NULL_HANDLE; }
    [[nodiscard]] uint32_t getInstanceCount() const noexcept { return static_cast<uint32_t>(m_cpuInstances.size()); }
    [[nodiscard]] uint32_t getCapacity() const noexcept { return m_capacity; }
    [[nodiscard]] const GPUVegetationConfig& getConfig() const noexcept { return m_config; }
    [[nodiscard]] const GPUVegetationStatistics& getStats() const noexcept { return m_stats; }
    [[nodiscard]] VkBuffer getIndirectBuffer(uint32_t frameIndex) const noexcept;
    [[nodiscard]] VkBuffer getVisibleIndexBuffer(uint32_t frameIndex) const noexcept;
    [[nodiscard]] VkBuffer getInstanceBuffer() const noexcept { return m_instanceBuffer ? m_instanceBuffer->getHandle() : VK_NULL_HANDLE; }

private:
    struct DeferredBuffer {
        std::unique_ptr<VulkanBuffer> buffer;
        uint64_t retireFrame = 0;
    };

    bool createComputePipeline();
    bool allocateBuffers(uint32_t requiredCapacity);
    void updateDescriptorSets();
    void flushDeferredBuffers() noexcept;

    const VulkanDevice* m_device = nullptr;
    VkCommandPool m_transferCommandPool = VK_NULL_HANDLE;

    GPUVegetationConfig m_config{};
    GPUVegetationStatistics m_stats{};

    std::vector<GPUVegetationInstance> m_cpuInstances;
    std::vector<GPUVegetationSpeciesLOD> m_speciesLods;
    std::vector<GPUVegetationIndirectCmd> m_templateIndirectCmds;

    uint32_t m_capacity = 0;
    uint64_t m_frameIndex = 0;
    bool m_initialized = false;

    // Vulkan compute pipeline & descriptors
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_descriptorSets{};
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_computePipeline = VK_NULL_HANDLE;

    // GPU buffers
    std::unique_ptr<VulkanBuffer> m_instanceBuffer;
    std::unique_ptr<VulkanBuffer> m_speciesBuffer;
    std::array<std::unique_ptr<VulkanBuffer>, kMaxFramesInFlight> m_visibleIndexBuffers;
    std::array<std::unique_ptr<VulkanBuffer>, kMaxFramesInFlight> m_indirectCmdBuffers;
    std::array<std::unique_ptr<VulkanBuffer>, kMaxFramesInFlight> m_counterBuffers;
    std::vector<DeferredBuffer> m_deferredBuffers;
};

} // namespace sidera
