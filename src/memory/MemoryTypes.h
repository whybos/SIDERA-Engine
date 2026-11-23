#pragma once

#include "profiling/GPUProfileTypes.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace sidera {

enum class MemoryPressureLevel : uint32_t {
    Normal = 0,
    Moderate = 1,
    High = 2,
    Critical = 3
};

inline const char* memoryPressureName(MemoryPressureLevel p) noexcept {
    switch (p) {
        case MemoryPressureLevel::Normal:   return "Normal";
        case MemoryPressureLevel::Moderate: return "Moderate";
        case MemoryPressureLevel::High:     return "High";
        case MemoryPressureLevel::Critical: return "Critical";
    }
    return "Normal";
}

enum class ResourceLifetimeClass : uint32_t {
    Persistent = 0,
    Streaming = 1,
    Transient = 2
};

enum class ResourceResidencyState : uint32_t {
    Unloaded = 0,
    Queued = 1,
    Loading = 2,
    UploadPending = 3,
    Resident = 4,
    EvictionPending = 5,
    Retiring = 6
};

struct MemoryBudgetConfig {
    uint64_t globalGpuBudgetBytes = 2ull * 1024ull * 1024ull * 1024ull; // 2 GiB default engine budget
    double moderateThreshold = 0.70;
    double highThreshold = 0.85;
    double criticalThreshold = 0.95;
    double evictionHysteresis = 0.08;

    uint64_t uploadByteBudgetPerFrame = 16ull * 1024ull * 1024ull; // 16 MiB/frame
    uint32_t uploadOpBudgetPerFrame = 8u;
    uint32_t maxConcurrentLoads = 4u;

    bool enableCpuTracking = true;
    bool enableGpuTracking = true;

    void validate() noexcept {
        globalGpuBudgetBytes = std::max(globalGpuBudgetBytes, 64ull * 1024ull * 1024ull);
        moderateThreshold = std::clamp(moderateThreshold, 0.30, 0.90);
        highThreshold = std::clamp(highThreshold, moderateThreshold + 0.05, 0.95);
        criticalThreshold = std::clamp(criticalThreshold, highThreshold + 0.02, 0.99);
        evictionHysteresis = std::clamp(evictionHysteresis, 0.01, 0.25);
        uploadByteBudgetPerFrame = std::clamp(uploadByteBudgetPerFrame, 256ull * 1024ull, 256ull * 1024ull * 1024ull);
        uploadOpBudgetPerFrame = std::clamp(uploadOpBudgetPerFrame, 1u, 64u);
        maxConcurrentLoads = std::clamp(maxConcurrentLoads, 1u, 32u);
    }
};

struct VulkanHeapBudget {
    uint32_t heapIndex = 0;
    bool deviceLocal = false;
    uint64_t heapSizeBytes = 0;
    uint64_t heapUsageBytes = 0;
    uint64_t heapBudgetBytes = 0;
    bool extMemoryBudgetSupported = false;
};

struct ResourceRegistryEntry {
    uint64_t id = 0;
    std::string name;
    GPUMemorySubsystem subsystem = GPUMemorySubsystem::CoreRenderer;
    ResourceLifetimeClass lifetime = ResourceLifetimeClass::Persistent;
    ResourceResidencyState residency = ResourceResidencyState::Resident;
    uint64_t estimatedGpuBytes = 0;
    uint64_t actualGpuBytes = 0;
    uint64_t cpuBytes = 0;
    uint64_t lastUsedFrame = 0;
    float priority = 1.0f;
    uint32_t activeRefCount = 0;
    bool pinned = false;
};

struct StreamingUploadRequest {
    uint64_t resourceId = 0;
    std::string name;
    uint64_t byteSize = 0;
    float priority = 1.0f;
    float distance = 0.0f;
    uint32_t ageFrames = 0;
    bool pinned = false;
    bool cancelled = false;
};

struct MemoryOverviewStats {
    uint64_t currentGpuEstimatedBytes = 0;
    uint64_t currentGpuActualBytes = 0;
    uint64_t peakGpuBytes = 0;
    uint64_t currentCpuBytes = 0;
    uint64_t peakCpuBytes = 0;
    uint64_t persistentBytes = 0;
    uint64_t streamingBytes = 0;
    uint64_t transientBytes = 0;
    uint64_t allocationCount = 0;
    uint64_t deallocationCount = 0;
    uint32_t activeResourceCount = 0;
    uint32_t evictionCount = 0;
    uint32_t reloadCount = 0;
    uint32_t pendingDeferredDestructions = 0;
    uint32_t pendingStreamingRequests = 0;
    uint64_t lastFrameUploadedBytes = 0;
    uint32_t lastFrameUploadedOps = 0;
    MemoryPressureLevel pressureLevel = MemoryPressureLevel::Normal;
    std::vector<VulkanHeapBudget> heaps;
};

} // namespace sidera
