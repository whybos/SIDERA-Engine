#include "core/FrameStats.h"

#include <algorithm>

namespace sidera {

FrameStats::FrameStats(float windowSeconds)
    : m_windowSeconds(windowSeconds) {
    m_samples.reserve(8192);
}

void FrameStats::reset() noexcept {
    m_elapsed = 0.0f;
    m_samples.clear();
}

bool FrameStats::record(float frameSeconds, FrameStatsReport& out) {
    if (m_samples.size() < m_samples.capacity()) {
        m_samples.push_back(frameSeconds);
    }
    m_elapsed += frameSeconds;

    if (m_elapsed < m_windowSeconds || m_samples.empty()) {
        return false;
    }

    float sum = 0.0f;
    float mn = m_samples.front();
    float mx = m_samples.front();
    for (float s : m_samples) {
        sum += s;
        mn = std::min(mn, s);
        mx = std::max(mx, s);
    }

    const size_t n = m_samples.size();
    const size_t p99Index = std::min(n - 1, static_cast<size_t>(static_cast<double>(n) * 0.99));
    std::nth_element(m_samples.begin(), m_samples.begin() + static_cast<std::ptrdiff_t>(p99Index), m_samples.end());
    const float p99 = m_samples[p99Index];

    out.frames = static_cast<unsigned>(n);
    out.fps = m_elapsed > 0.0f ? static_cast<float>(n) / m_elapsed : 0.0f;
    out.avgMs = sum / static_cast<float>(n) * 1000.0f;
    out.minMs = mn * 1000.0f;
    out.maxMs = mx * 1000.0f;
    out.onePercentLowFps = p99 > 0.0f ? 1.0f / p99 : 0.0f;

    reset();
    return true;
}

} // namespace sidera
