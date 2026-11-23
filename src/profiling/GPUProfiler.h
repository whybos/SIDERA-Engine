#pragma once

#include "profiling/GPUProfileTypes.h"

#include <vulkan/vulkan.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sidera {

class VulkanDevice;

// Separate high-resolution CPU frame & stage profiler (never mixed with GPU timestamps)
class CPUProfiler {
public:
    void beginFrame(uint64_t frameIndex) noexcept;
    void endFrame() noexcept;

    void beginStage(const std::string& name) noexcept;
    void endStage(const std::string& name) noexcept;

    [[nodiscard]] const CPUFrameStatistics& getLastFrame() const noexcept { return m_lastFrame; }
    [[nodiscard]] TimingSummary getFrameTimeSummary() const;
    void clearHistory() noexcept { m_frameHistoryMs.clear(); }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_frameStart{};
    std::unordered_map<std::string, Clock::time_point> m_activeStages;
    CPUFrameStatistics m_currentFrame{};
    CPUFrameStatistics m_lastFrame{};
    std::vector<double> m_frameHistoryMs;
};

// Vulkan 1.3 asynchronous GPU timestamp profiler (zero CPU stalls, frame-in-flight query pools)
class GPUProfiler {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;
    static constexpr uint32_t kMaxPassesPerFrame = 32;
    static constexpr uint32_t kQueriesPerFrame = kMaxPassesPerFrame * 2;

    GPUProfiler() = default;
    ~GPUProfiler();

    GPUProfiler(const GPUProfiler&) = delete;
    GPUProfiler& operator=(const GPUProfiler&) = delete;

    bool init(const VulkanDevice* device, uint32_t queueFamilyIndex = 0);
    void destroy() noexcept;

    // Called at start of command buffer recording (after frame fence has already signaled)
    void beginFrame(VkCommandBuffer cmd, uint32_t frameSlot, uint64_t frameIndex);
    void endFrame(uint32_t frameSlot);

    // Record GPU timestamp brackets for a named render pass
    uint32_t beginPass(VkCommandBuffer cmd, uint32_t frameSlot, const std::string& name);
    void endPass(VkCommandBuffer cmd, uint32_t frameSlot, uint32_t passIndex);

    // Non-blocking timestamp conversion helper (respects timestampValidBits & timestampPeriodNs)
    [[nodiscard]] static double ticksToMilliseconds(uint64_t startTick, uint64_t endTick,
                                                    float timestampPeriodNs, uint32_t validBits) noexcept;

    void setEnabled(bool enabled) noexcept { m_enabled = enabled; }
    [[nodiscard]] bool isEnabled() const noexcept { return m_enabled; }
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] const GPUDeviceProfile& getDeviceProfile() const noexcept { return m_deviceProfile; }
    [[nodiscard]] const GPUFrameStatistics& getLastFrame() const noexcept { return m_lastFrame; }
    [[nodiscard]] const std::vector<GPUFrameStatistics>& getFrameHistory() const noexcept { return m_frameHistory; }
    [[nodiscard]] TimingSummary getFrameTimeSummary() const;
    [[nodiscard]] GPUMemoryStatistics& getMemoryStats() noexcept { return m_memoryStats; }
    [[nodiscard]] const GPUMemoryStatistics& getMemoryStats() const noexcept { return m_memoryStats; }
    void clearHistory() noexcept { m_frameHistory.clear(); }

private:
    struct FrameSlot {
        VkQueryPool queryPool = VK_NULL_HANDLE;
        uint64_t frameIndex = 0;
        uint32_t passCount = 0;
        std::array<std::string, kMaxPassesPerFrame> passNames;
        bool hasRecorded = false;
    };

    void resolveFrameSlot(uint32_t frameSlot);

    const VulkanDevice* m_device = nullptr;
    bool m_initialized = false;
    bool m_enabled = true;
    GPUDeviceProfile m_deviceProfile{};
    std::array<FrameSlot, kMaxFramesInFlight> m_slots{};
    GPUFrameStatistics m_lastFrame{};
    std::vector<GPUFrameStatistics> m_frameHistory;
    GPUMemoryStatistics m_memoryStats{};
};

} // namespace sidera
