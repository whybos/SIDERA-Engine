#pragma once

#include "renderer/Camera.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "virtualtexturing/VirtualTexturePageTable.h"
#include "virtualtexturing/VirtualTexturePhysicalCache.h"
#include "virtualtexturing/VirtualTextureTypes.h"

#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_set>
#include <vector>

namespace sidera {

class VulkanDevice;

// Central Virtual Texturing System (Milestone 27):
// Manages virtual textures, GPU page table, software physical page cache with LRU eviction,
// GPU feedback collection with delayed frames-in-flight readback, and throttled tile streaming.
class VirtualTextureSystem {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    VirtualTextureSystem() = default;
    ~VirtualTextureSystem();

    VirtualTextureSystem(const VirtualTextureSystem&) = delete;
    VirtualTextureSystem& operator=(const VirtualTextureSystem&) = delete;

    bool init(const VirtualTextureConfig& config,
              const VulkanDevice* device = nullptr,
              VkCommandPool transferCommandPool = VK_NULL_HANDLE);
    void destroy() noexcept;

    // Register a new high-resolution virtual texture and pin its coarsest fallback mip pages
    uint16_t createVirtualTexture(const std::string& name,
                                  uint32_t width,
                                  uint32_t height,
                                  VirtualTextureSemantic semantic = VirtualTextureSemantic::AlbedoSRGB);

    // Advance frame: perform delayed feedback readback, deduplicate requests, stream throttled pages, sync page table
    void beginFrame();

    // Submit CPU/multi-viewport page requests (deduplicated automatically)
    void requestPage(const VirtualTexturePageID& id);
    uint32_t processPendingUploads();

    // Generate seamless gutter-padded RGBA8 tile texels (for procedural/test sources & border filtering verification)
    [[nodiscard]] std::vector<uint8_t> generateTileData(const VirtualTexturePageID& id) const;

    // Resolve page with parent-mip fallback (for CPU verification & diagnostics)
    [[nodiscard]] PageTableEntry samplePageTable(const VirtualTexturePageID& id, bool* outUsedFallback = nullptr);

    // Record GPU feedback compute pass with Vulkan 1.3 synchronization
    void recordFeedbackPass(VkCommandBuffer cmd, uint32_t frameIndex, const Camera& camera, float worldSize = 120.0f);

    // Accessors
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] bool isGpuActive() const noexcept { return m_device != nullptr && m_feedbackPipeline != VK_NULL_HANDLE; }
    [[nodiscard]] const VirtualTextureConfig& getConfig() const noexcept { return m_config; }
    [[nodiscard]] const VirtualTextureStatistics& getStats() const noexcept { return m_stats; }
    [[nodiscard]] const VirtualTexturePageTable& getPageTable() const noexcept { return m_pageTable; }
    [[nodiscard]] const VirtualTexturePhysicalCache& getPhysicalCache() const noexcept { return m_physicalCache; }
    [[nodiscard]] const std::vector<VirtualTextureAsset>& getTextures() const noexcept { return m_textures; }

private:
    bool createFeedbackPipeline();
    void allocateGpuBuffers();
    void updateDescriptorSets();
    void readbackDelayedFeedback(uint32_t frameIndex);
    void syncPageTableToGpu(uint32_t frameIndex);
    void updateStatistics() noexcept;

    const VulkanDevice* m_device = nullptr;
    VkCommandPool m_transferCommandPool = VK_NULL_HANDLE;

    VirtualTextureConfig m_config{};
    VirtualTextureStatistics m_stats{};

    std::vector<VirtualTextureAsset> m_textures;
    VirtualTexturePageTable m_pageTable;
    VirtualTexturePhysicalCache m_physicalCache;

    std::deque<VirtualTexturePageID> m_requestQueue;
    std::unordered_set<uint64_t> m_pendingSet;

    uint64_t m_frameIndex = 0;
    bool m_initialized = false;

    // Vulkan compute feedback pipeline & double-buffered SSBOs
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_descriptorSets{};
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_feedbackPipeline = VK_NULL_HANDLE;

    std::array<std::unique_ptr<VulkanBuffer>, kMaxFramesInFlight> m_pageTableBuffers;
    std::array<std::unique_ptr<VulkanBuffer>, kMaxFramesInFlight> m_feedbackBuffers;
};

} // namespace sidera
