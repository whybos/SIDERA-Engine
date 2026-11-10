#include "streaming/StreamingUploadQueue.h"
#include "core/Log.h"

#include <algorithm>

namespace sidera {

void StreamingUploadQueue::enqueueUpload(StreamingRegion* region) {
    if (!region) return;

    // Avoid duplicate insertions
    for (const auto* existing : m_pendingUploads) {
        if (existing && existing->getId() == region->getId()) {
            return;
        }
    }

    region->setState(StreamingState::UploadQueued);
    m_pendingUploads.push_back(region);
}

void StreamingUploadQueue::cancelUpload(uint32_t regionId) {
    auto it = std::remove_if(m_pendingUploads.begin(), m_pendingUploads.end(), [regionId](const StreamingRegion* r) {
        return r && r->getId() == regionId;
    });
    m_pendingUploads.erase(it, m_pendingUploads.end());
}

StreamingUploadQueue::UploadResult StreamingUploadQueue::processUploads(
    const VulkanDevice& device,
    VkCommandPool transferCommandPool,
    uint64_t maxBytes,
    uint32_t maxOps
) {
    UploadResult result{};

    while (!m_pendingUploads.empty() && result.opsCompleted < maxOps) {
        StreamingRegion* region = m_pendingUploads.front();
        if (!region) {
            m_pendingUploads.pop_front();
            continue;
        }

        // Check if region was cancelled or is in invalid state
        if (region->getState() != StreamingState::UploadQueued && region->getState() != StreamingState::CPUReady) {
            m_pendingUploads.pop_front();
            continue;
        }

        // Estimate size from CPU mesh
        const uint64_t estimatedBytes = region->getTerrainChunk()
            ? (static_cast<uint64_t>(region->getTerrainChunk()->getVertexCount()) * sizeof(Vertex) +
               static_cast<uint64_t>(region->getTerrainChunk()->getTriangleCount() * 3) * sizeof(uint32_t))
            : 0;

        // If not first upload and exceeds byte budget, stop for this frame
        if (result.opsCompleted > 0 && (result.bytesUploaded + estimatedBytes > maxBytes)) {
            break;
        }

        m_pendingUploads.pop_front();

        const bool ok = region->uploadGpu(device, transferCommandPool);
        if (ok) {
            result.bytesUploaded += region->getGpuMemoryBytes();
            result.opsCompleted += 1;
        } else {
            SIDERA_LOG_WARN("Streaming", "Failed to upload region #{} to GPU.", region->getId());
        }
    }

    return result;
}

void StreamingUploadQueue::enqueueDeferredRelease(std::function<void()> releaseAction, uint32_t framesToWait) {
    if (!releaseAction) return;
    m_deferredReleases.push_back(DeferredReleaseItem{
        .framesRemaining = std::max(1u, framesToWait),
        .releaseAction = std::move(releaseAction)
    });
}

void StreamingUploadQueue::processDeferredReleases() {
    auto it = m_deferredReleases.begin();
    while (it != m_deferredReleases.end()) {
        if (it->framesRemaining <= 1) {
            if (it->releaseAction) {
                it->releaseAction();
            }
            it = m_deferredReleases.erase(it);
        } else {
            --(it->framesRemaining);
            ++it;
        }
    }
}

void StreamingUploadQueue::flushAllDeferredReleases() {
    for (auto& item : m_deferredReleases) {
        if (item.releaseAction) {
            item.releaseAction();
        }
    }
    m_deferredReleases.clear();
}

void StreamingUploadQueue::clear() noexcept {
    m_pendingUploads.clear();
    flushAllDeferredReleases();
}

} // namespace sidera
