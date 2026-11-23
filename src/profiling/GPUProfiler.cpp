#include "profiling/GPUProfiler.h"
#include "renderer/vulkan/VulkanDevice.h"

#include <algorithm>
#include <vector>

namespace sidera {

// --- CPUProfiler ---

void CPUProfiler::beginFrame(uint64_t frameIndex) noexcept {
    m_frameStart = Clock::now();
    m_currentFrame = {};
    m_currentFrame.frameIndex = frameIndex;
    m_activeStages.clear();
}

void CPUProfiler::endFrame() noexcept {
    const auto now = Clock::now();
    m_currentFrame.totalCpuMs = std::chrono::duration<double, std::milli>(now - m_frameStart).count();
    m_currentFrame.valid = true;
    m_lastFrame = m_currentFrame;
    if (m_frameHistoryMs.size() < 2048u) {
        m_frameHistoryMs.push_back(m_lastFrame.totalCpuMs);
    }
}

void CPUProfiler::beginStage(const std::string& name) noexcept {
    m_activeStages[name] = Clock::now();
}

void CPUProfiler::endStage(const std::string& name) noexcept {
    auto it = m_activeStages.find(name);
    if (it == m_activeStages.end()) return;
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - it->second).count();
    m_currentFrame.stages.push_back({name, ms, true});
    m_activeStages.erase(it);
}

TimingSummary CPUProfiler::getFrameTimeSummary() const {
    return computeTimingSummary(m_frameHistoryMs);
}

// --- GPUProfiler ---

GPUProfiler::~GPUProfiler() {
    destroy();
}

double GPUProfiler::ticksToMilliseconds(uint64_t startTick, uint64_t endTick,
                                        float timestampPeriodNs, uint32_t validBits) noexcept {
    if (validBits == 0u || timestampPeriodNs <= 0.0f) return 0.0;
    const uint64_t mask = (validBits >= 64u) ? ~0ull : ((1ull << validBits) - 1ull);
    const uint64_t delta = (endTick - startTick) & mask;
    return (static_cast<double>(delta) * static_cast<double>(timestampPeriodNs)) * 1e-6;
}

bool GPUProfiler::init(const VulkanDevice* device, uint32_t queueFamilyIndex) {
    destroy();
    m_device = device;
    m_deviceProfile = {};

    if (!m_device) {
        m_deviceProfile.deviceName = "CPU-Only (No Vulkan Device)";
        m_deviceProfile.timestampsSupported = false;
        m_initialized = true;
        return true;
    }

    const VkPhysicalDeviceProperties& props = m_device->getProperties();
    m_deviceProfile.deviceName = props.deviceName;
    m_deviceProfile.apiVersion = props.apiVersion;
    m_deviceProfile.driverVersion = props.driverVersion;
    m_deviceProfile.timestampPeriodNs = props.limits.timestampPeriod;

    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_device->getPhysicalDevice(), &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> qProps(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_device->getPhysicalDevice(), &qCount, qProps.data());

    if (queueFamilyIndex < qCount) {
        m_deviceProfile.timestampValidBits = qProps[queueFamilyIndex].timestampValidBits;
    } else {
        m_deviceProfile.timestampValidBits = 0u;
    }
    m_deviceProfile.timestampsSupported =
        (m_deviceProfile.timestampValidBits > 0u) && (m_deviceProfile.timestampPeriodNs > 0.0f);

    if (m_deviceProfile.timestampsSupported) {
        VkQueryPoolCreateInfo qpCI{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qpCI.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qpCI.queryCount = kQueriesPerFrame;
        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            if (vkCreateQueryPool(m_device->getLogicalDevice(), &qpCI, nullptr, &m_slots[i].queryPool) != VK_SUCCESS) {
                m_deviceProfile.timestampsSupported = false;
                break;
            }
        }
    }

    m_initialized = true;
    return true;
}

void GPUProfiler::destroy() noexcept {
    if (m_device) {
        VkDevice dev = m_device->getLogicalDevice();
        for (auto& slot : m_slots) {
            if (slot.queryPool != VK_NULL_HANDLE) {
                vkDestroyQueryPool(dev, slot.queryPool, nullptr);
                slot.queryPool = VK_NULL_HANDLE;
            }
            slot.hasRecorded = false;
            slot.passCount = 0;
        }
    }
    m_device = nullptr;
    m_initialized = false;
}

void GPUProfiler::resolveFrameSlot(uint32_t frameSlot) {
    if (!m_device || !m_deviceProfile.timestampsSupported) return;
    FrameSlot& slot = m_slots[frameSlot % kMaxFramesInFlight];
    if (!slot.hasRecorded || slot.passCount == 0 || slot.queryPool == VK_NULL_HANDLE) return;

    const uint32_t queryCount = slot.passCount * 2u;
    std::vector<uint64_t> results(queryCount * 2u, 0ull); // [timestamp, availability] pairs

    // Non-blocking: never uses VK_QUERY_RESULT_WAIT_BIT
    const VkResult res = vkGetQueryPoolResults(
        m_device->getLogicalDevice(),
        slot.queryPool,
        0u,
        queryCount,
        results.size() * sizeof(uint64_t),
        results.data(),
        sizeof(uint64_t) * 2u,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

    if (res != VK_SUCCESS && res != VK_NOT_READY) return;

    GPUFrameStatistics stats{};
    stats.frameIndex = slot.frameIndex;
    double totalMs = 0.0;
    bool anyValid = false;

    for (uint32_t p = 0; p < slot.passCount; ++p) {
        const uint64_t tStart = results[(p * 2u) * 2u];
        const uint64_t aStart = results[(p * 2u) * 2u + 1u];
        const uint64_t tEnd   = results[(p * 2u + 1u) * 2u];
        const uint64_t aEnd   = results[(p * 2u + 1u) * 2u + 1u];
        const bool ok = (aStart != 0ull) && (aEnd != 0ull);
        const double ms = ok
            ? ticksToMilliseconds(tStart, tEnd, m_deviceProfile.timestampPeriodNs, m_deviceProfile.timestampValidBits)
            : 0.0;
        stats.passes.push_back({slot.passNames[p], ms, ok});
        if (ok) {
            totalMs += ms;
            anyValid = true;
        }
    }
    stats.totalGpuMs = totalMs;
    stats.valid = anyValid;

    if (anyValid) {
        m_lastFrame = stats;
        if (m_frameHistory.size() < 2048u) {
            m_frameHistory.push_back(stats);
        }
    }
}

void GPUProfiler::beginFrame(VkCommandBuffer cmd, uint32_t frameSlot, uint64_t frameIndex) {
    if (!m_enabled || !m_device || !m_deviceProfile.timestampsSupported || cmd == VK_NULL_HANDLE) return;
    const uint32_t idx = frameSlot % kMaxFramesInFlight;
    resolveFrameSlot(idx);

    FrameSlot& slot = m_slots[idx];
    slot.frameIndex = frameIndex;
    slot.passCount = 0;
    slot.hasRecorded = false;
    vkCmdResetQueryPool(cmd, slot.queryPool, 0u, kQueriesPerFrame);
}

void GPUProfiler::endFrame(uint32_t frameSlot) {
    if (!m_enabled || !m_device || !m_deviceProfile.timestampsSupported) return;
    FrameSlot& slot = m_slots[frameSlot % kMaxFramesInFlight];
    slot.hasRecorded = (slot.passCount > 0);
}

uint32_t GPUProfiler::beginPass(VkCommandBuffer cmd, uint32_t frameSlot, const std::string& name) {
    if (!m_enabled || !m_device || !m_deviceProfile.timestampsSupported || cmd == VK_NULL_HANDLE) {
        return 0xFFFFFFFFu;
    }
    FrameSlot& slot = m_slots[frameSlot % kMaxFramesInFlight];
    if (slot.passCount >= kMaxPassesPerFrame) return 0xFFFFFFFFu;

    const uint32_t passIdx = slot.passCount++;
    slot.passNames[passIdx] = name;
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, slot.queryPool, passIdx * 2u);
    return passIdx;
}

void GPUProfiler::endPass(VkCommandBuffer cmd, uint32_t frameSlot, uint32_t passIndex) {
    if (!m_enabled || !m_device || !m_deviceProfile.timestampsSupported ||
        cmd == VK_NULL_HANDLE || passIndex >= kMaxPassesPerFrame) {
        return;
    }
    FrameSlot& slot = m_slots[frameSlot % kMaxFramesInFlight];
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, slot.queryPool, passIndex * 2u + 1u);
}

TimingSummary GPUProfiler::getFrameTimeSummary() const {
    std::vector<double> totals;
    totals.reserve(m_frameHistory.size());
    for (const auto& f : m_frameHistory) {
        if (f.valid) totals.push_back(f.totalGpuMs);
    }
    return computeTimingSummary(totals);
}

} // namespace sidera
