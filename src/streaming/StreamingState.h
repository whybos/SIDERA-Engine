#pragma once

#include <cstdint>
#include <string_view>

namespace sidera {

// Lifecycle states of a streaming world region
enum class StreamingState : uint8_t {
    Unloaded = 0,          // Metadata-only; no CPU payload, 0 GPU allocations
    QueuedForLoading,      // In background CPU worker queue
    Loading,               // Asynchronously generating terrain & foliage on worker thread
    CPUReady,              // CPU payload fully built and validated
    UploadQueued,          // Queued for main render thread GPU staging & allocation
    Resident,              // Uploaded to GPU memory (vertex/index buffers allocated)
    Active,                // Registered in active rendering passes (terrain drawn, foliage batched)
    QueuedForUnloading,    // Flagged for eviction / deactivation
    Unloading,             // Unregistering from render scene; GPU resources deferred for safe deletion
    Failed                 // Error occurred during generation or upload
};

[[nodiscard]] constexpr std::string_view getStreamingStateName(StreamingState state) noexcept {
    switch (state) {
        case StreamingState::Unloaded:           return "Unloaded";
        case StreamingState::QueuedForLoading:   return "QueuedForLoading";
        case StreamingState::Loading:            return "Loading";
        case StreamingState::CPUReady:           return "CPUReady";
        case StreamingState::UploadQueued:       return "UploadQueued";
        case StreamingState::Resident:           return "Resident";
        case StreamingState::Active:             return "Active";
        case StreamingState::QueuedForUnloading: return "QueuedForUnloading";
        case StreamingState::Unloading:          return "Unloading";
        case StreamingState::Failed:             return "Failed";
    }
    return "Unknown";
}

} // namespace sidera
