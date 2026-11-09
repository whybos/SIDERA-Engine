#pragma once

#include <vector>

namespace sidera {

struct FrameStatsReport {
    float fps = 0.0f;        // frames / elapsed over the sampling window
    float avgMs = 0.0f;
    float minMs = 0.0f;
    float maxMs = 0.0f;
    float onePercentLowFps = 0.0f; // fps equivalent of the 99th percentile frame time
    unsigned frames = 0;
};

// Accumulates frame times and emits a report once per sampling window.
// No allocations after construction (sample buffer is reserved up front).
class FrameStats {
public:
    explicit FrameStats(float windowSeconds = 0.5f);

    // Returns true when a new report is ready in `out`.
    bool record(float frameSeconds, FrameStatsReport& out);
    void reset() noexcept;

private:
    float m_windowSeconds;
    float m_elapsed = 0.0f;
    std::vector<float> m_samples;
};

} // namespace sidera
