#pragma once

#include "memory/MemoryTypes.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace sidera {

class VulkanDevice;

// Centralized Memory Tracking, Vulkan Heap Budgeting, Resource Registry,
// Safe Deferred GPU Destruction, and Priority Streaming Scheduler (Milestone 34).
class MemoryBudgetManager {
public:
    MemoryBudgetManager() = default;
    ~MemoryBudgetManager();

    MemoryBudgetManager(const MemoryBudgetManager&) = delete;
    MemoryBudgetManager& operator=(const MemoryBudgetManager&) = delete;

    bool init(const VulkanDevice* device = nullptr, const MemoryBudgetConfig& config = {});
    void destroy() noexcept;

    // Vulkan device heap budget query (uses VK_EXT_memory_budget when available)
    void refreshVulkanHeapBudgets();

    // Resource Registry & Lifetime Management
    uint64_t registerResource(const ResourceRegistryEntry& entry);
    bool unregisterResource(uint64_t id);
    void touchResource(uint64_t id, uint64_t currentFrame) noexcept;
    void setPinned(uint64_t id, bool pinned) noexcept;
    void setActiveRefCount(uint64_t id, uint32_t refCount) noexcept;
    void setResidency(uint64_t id, ResourceResidencyState state) noexcept;
    [[nodiscard]] const ResourceRegistryEntry* findResource(uint64_t id) const noexcept;

    // Memory pressure evaluation (with hysteresis) & LRU+Priority eviction selection
    MemoryPressureLevel evaluatePressure() noexcept;
    [[nodiscard]] std::vector<uint64_t> selectEvictionCandidates(uint64_t currentFrame, uint64_t targetFreeBytes) const;
    bool evictResource(uint64_t id, uint64_t retireFrame, std::function<void()> deferredCallback = nullptr);

    // Frame-safe deferred GPU resource destruction (zero vkDeviceWaitIdle stalls)
    void deferDestruction(uint64_t resourceId, uint64_t retireFrame, std::function<void()> callback);
    uint32_t processDeferredDestructions(uint64_t completedFrame);

    // Deduplicated, priority-ordered, budget-bounded streaming upload scheduler
    bool enqueueStreamingRequest(const StreamingUploadRequest& req);
    bool cancelStreamingRequest(uint64_t resourceId);
    [[nodiscard]] std::vector<StreamingUploadRequest> scheduleFrameUploads(uint64_t currentFrame);

    // Accessors
    void setConfig(const MemoryBudgetConfig& cfg) noexcept { m_config = cfg; m_config.validate(); }
    [[nodiscard]] const MemoryBudgetConfig& getConfig() const noexcept { return m_config; }
    [[nodiscard]] const MemoryOverviewStats& getStats() const noexcept { return m_stats; }
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

private:
    struct DeferredItem {
        uint64_t resourceId = 0;
        uint64_t retireFrame = 0;
        std::function<void()> callback;
    };

    void recomputeStats() noexcept;

    const VulkanDevice* m_device = nullptr;
    MemoryBudgetConfig m_config{};
    MemoryOverviewStats m_stats{};
    bool m_initialized = false;
    bool m_extMemoryBudgetSupported = false;
    uint64_t m_nextId = 1;

    std::unordered_map<uint64_t, ResourceRegistryEntry> m_registry;
    std::vector<DeferredItem> m_deferredQueue;
    std::unordered_map<uint64_t, StreamingUploadRequest> m_streamingQueue;
};

} // namespace sidera
