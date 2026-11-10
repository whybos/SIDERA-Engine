#pragma once

#include "streaming/StreamingRegion.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace sidera {

class VulkanDevice;

// Throttled main-thread staging upload queue and deferred GPU resource deletion authority
class StreamingUploadQueue {
public:
    struct DeferredReleaseItem {
        uint32_t framesRemaining = 3;
        std::function<void()> releaseAction;
    };

    StreamingUploadQueue() = default;
    ~StreamingUploadQueue() = default;

    StreamingUploadQueue(const StreamingUploadQueue&) = delete;
    StreamingUploadQueue& operator=(const StreamingUploadQueue&) = delete;

    // Enqueues a region whose CPU generation is ready for GPU upload
    void enqueueUpload(StreamingRegion* region);

    // Cancel pending upload if region is unloaded before reaching the front of the queue
    void cancelUpload(uint32_t regionId);

    // Processes throttled staging transfers on the render thread
    // Returns {bytesUploaded, opsCompleted}
    struct UploadResult {
        uint64_t bytesUploaded = 0;
        uint32_t opsCompleted = 0;
    };
    UploadResult processUploads(const VulkanDevice& device,
                                VkCommandPool transferCommandPool,
                                uint64_t maxBytes,
                                uint32_t maxOps);

    // Enqueue an action to be executed after MAX_FRAMES_IN_FLIGHT (safe Vulkan buffer destruction)
    void enqueueDeferredRelease(std::function<void()> releaseAction, uint32_t framesToWait = 3);

    // Advances the frame counter and executes retired releases
    void processDeferredReleases();

    // Flushes all deferred releases immediately (used during engine shutdown)
    void flushAllDeferredReleases();

    void clear() noexcept;

    [[nodiscard]] size_t getPendingCount() const noexcept { return m_pendingUploads.size(); }
    [[nodiscard]] size_t getDeferredReleaseCount() const noexcept { return m_deferredReleases.size(); }

private:
    std::deque<StreamingRegion*> m_pendingUploads;
    std::vector<DeferredReleaseItem> m_deferredReleases;
};

} // namespace sidera
