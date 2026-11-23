#include "memory/MemoryBudgetManager.h"
#include "renderer/vulkan/VulkanDevice.h"

#include <algorithm>
#include <cstring>

namespace sidera {

MemoryBudgetManager::~MemoryBudgetManager() {
    destroy();
}

bool MemoryBudgetManager::init(const VulkanDevice* device, const MemoryBudgetConfig& config) {
    destroy();
    m_device = device;
    m_config = config;
    m_config.validate();

    if (m_device) {
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(m_device->getPhysicalDevice(), nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(m_device->getPhysicalDevice(), nullptr, &extCount, exts.data());
        for (const auto& e : exts) {
            if (std::strcmp(e.extensionName, "VK_EXT_memory_budget") == 0) {
                m_extMemoryBudgetSupported = true;
                break;
            }
        }
        refreshVulkanHeapBudgets();
    }

    m_initialized = true;
    return true;
}

void MemoryBudgetManager::destroy() noexcept {
    // Flush any remaining deferred destruction callbacks cleanly on shutdown
    for (auto& item : m_deferredQueue) {
        if (item.callback) item.callback();
    }
    m_deferredQueue.clear();
    m_streamingQueue.clear();
    m_registry.clear();
    m_stats = {};
    m_device = nullptr;
    m_extMemoryBudgetSupported = false;
    m_initialized = false;
}

void MemoryBudgetManager::refreshVulkanHeapBudgets() {
    m_stats.heaps.clear();
    if (!m_device) return;

    VkPhysicalDevice phys = m_device->getPhysicalDevice();
    if (m_extMemoryBudgetSupported) {
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
        VkPhysicalDeviceMemoryProperties2 memProps2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
        memProps2.pNext = &budgetProps;
        vkGetPhysicalDeviceMemoryProperties2(phys, &memProps2);

        const uint32_t heapCount = memProps2.memoryProperties.memoryHeapCount;
        for (uint32_t i = 0; i < heapCount; ++i) {
            VulkanHeapBudget hb{};
            hb.heapIndex = i;
            hb.deviceLocal = (memProps2.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
            hb.heapSizeBytes = memProps2.memoryProperties.memoryHeaps[i].size;
            hb.heapUsageBytes = budgetProps.heapUsage[i];
            hb.heapBudgetBytes = budgetProps.heapBudget[i];
            hb.extMemoryBudgetSupported = true;
            m_stats.heaps.push_back(hb);
        }
    } else {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
        for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
            VulkanHeapBudget hb{};
            hb.heapIndex = i;
            hb.deviceLocal = (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
            hb.heapSizeBytes = memProps.memoryHeaps[i].size;
            hb.heapBudgetBytes = memProps.memoryHeaps[i].size;
            hb.extMemoryBudgetSupported = false;
            m_stats.heaps.push_back(hb);
        }
    }
}

uint64_t MemoryBudgetManager::registerResource(const ResourceRegistryEntry& entry) {
    ResourceRegistryEntry e = entry;
    if (e.id == 0) e.id = m_nextId++;
    else m_nextId = std::max(m_nextId, e.id + 1u);

    m_registry[e.id] = e;
    ++m_stats.allocationCount;
    recomputeStats();
    return e.id;
}

bool MemoryBudgetManager::unregisterResource(uint64_t id) {
    auto it = m_registry.find(id);
    if (it == m_registry.end()) return false;
    m_registry.erase(it);
    ++m_stats.deallocationCount;
    recomputeStats();
    return true;
}

void MemoryBudgetManager::touchResource(uint64_t id, uint64_t currentFrame) noexcept {
    auto it = m_registry.find(id);
    if (it != m_registry.end()) {
        it->second.lastUsedFrame = currentFrame;
    }
}

void MemoryBudgetManager::setPinned(uint64_t id, bool pinned) noexcept {
    auto it = m_registry.find(id);
    if (it != m_registry.end()) {
        it->second.pinned = pinned;
    }
}

void MemoryBudgetManager::setActiveRefCount(uint64_t id, uint32_t refCount) noexcept {
    auto it = m_registry.find(id);
    if (it != m_registry.end()) {
        it->second.activeRefCount = refCount;
    }
}

void MemoryBudgetManager::setResidency(uint64_t id, ResourceResidencyState state) noexcept {
    auto it = m_registry.find(id);
    if (it != m_registry.end()) {
        if (it->second.residency == ResourceResidencyState::Unloaded && state == ResourceResidencyState::Resident) {
            ++m_stats.reloadCount;
        }
        it->second.residency = state;
        recomputeStats();
    }
}

const ResourceRegistryEntry* MemoryBudgetManager::findResource(uint64_t id) const noexcept {
    auto it = m_registry.find(id);
    return (it != m_registry.end()) ? &it->second : nullptr;
}

void MemoryBudgetManager::recomputeStats() noexcept {
    uint64_t gpuEst = 0, gpuAct = 0, cpu = 0;
    uint64_t pers = 0, strm = 0, tran = 0;

    for (const auto& [id, r] : m_registry) {
        if (r.residency == ResourceResidencyState::Unloaded) continue;
        const uint64_t g = (r.actualGpuBytes > 0) ? r.actualGpuBytes : r.estimatedGpuBytes;
        gpuEst += r.estimatedGpuBytes;
        gpuAct += r.actualGpuBytes;
        cpu += r.cpuBytes;
        if (r.lifetime == ResourceLifetimeClass::Persistent) pers += g;
        else if (r.lifetime == ResourceLifetimeClass::Streaming) strm += g;
        else tran += g;
    }

    m_stats.currentGpuEstimatedBytes = gpuEst;
    m_stats.currentGpuActualBytes = gpuAct;
    m_stats.peakGpuBytes = std::max(m_stats.peakGpuBytes, std::max(gpuEst, gpuAct));
    m_stats.currentCpuBytes = cpu;
    m_stats.peakCpuBytes = std::max(m_stats.peakCpuBytes, cpu);
    m_stats.persistentBytes = pers;
    m_stats.streamingBytes = strm;
    m_stats.transientBytes = tran;
    m_stats.activeResourceCount = static_cast<uint32_t>(m_registry.size());
    m_stats.pendingDeferredDestructions = static_cast<uint32_t>(m_deferredQueue.size());
    m_stats.pendingStreamingRequests = static_cast<uint32_t>(m_streamingQueue.size());
    evaluatePressure();
}

MemoryPressureLevel MemoryBudgetManager::evaluatePressure() noexcept {
    const double used = static_cast<double>(std::max(m_stats.currentGpuEstimatedBytes, m_stats.currentGpuActualBytes));
    const double ratio = used / static_cast<double>(std::max(m_config.globalGpuBudgetBytes, 1ull));
    const double h = m_config.evictionHysteresis;

    MemoryPressureLevel curr = m_stats.pressureLevel;
    if (ratio >= m_config.criticalThreshold) {
        curr = MemoryPressureLevel::Critical;
    } else if (ratio >= m_config.highThreshold) {
        if (curr != MemoryPressureLevel::Critical || ratio < (m_config.criticalThreshold - h)) {
            curr = MemoryPressureLevel::High;
        }
    } else if (ratio >= m_config.moderateThreshold) {
        if (curr < MemoryPressureLevel::High || ratio < (m_config.highThreshold - h)) {
            curr = MemoryPressureLevel::Moderate;
        }
    } else if (ratio < (m_config.moderateThreshold - h) || curr == MemoryPressureLevel::Normal) {
        curr = MemoryPressureLevel::Normal;
    }

    m_stats.pressureLevel = curr;
    return curr;
}

std::vector<uint64_t> MemoryBudgetManager::selectEvictionCandidates(uint64_t currentFrame, uint64_t targetFreeBytes) const {
    std::vector<const ResourceRegistryEntry*> eligible;
    for (const auto& [id, r] : m_registry) {
        if (r.pinned || r.activeRefCount > 0 || r.lifetime == ResourceLifetimeClass::Persistent) continue;
        if (r.residency != ResourceResidencyState::Resident) continue;
        if (r.lastUsedFrame >= currentFrame) continue; // protect resources used this frame
        eligible.push_back(&r);
    }

    // Sort by lowest priority first, then oldest LRU frame
    std::sort(eligible.begin(), eligible.end(), [](const ResourceRegistryEntry* a, const ResourceRegistryEntry* b) {
        if (a->priority != b->priority) return a->priority < b->priority;
        return a->lastUsedFrame < b->lastUsedFrame;
    });

    std::vector<uint64_t> selected;
    uint64_t freed = 0;
    for (const auto* r : eligible) {
        selected.push_back(r->id);
        freed += (r->actualGpuBytes > 0) ? r->actualGpuBytes : r->estimatedGpuBytes;
        if (freed >= targetFreeBytes) break;
    }
    return selected;
}

bool MemoryBudgetManager::evictResource(uint64_t id, uint64_t retireFrame, std::function<void()> deferredCallback) {
    auto it = m_registry.find(id);
    if (it == m_registry.end()) return false;
    if (it->second.pinned || it->second.activeRefCount > 0) return false;

    it->second.residency = ResourceResidencyState::Retiring;
    ++m_stats.evictionCount;
    deferDestruction(id, retireFrame, [this, id, cb = std::move(deferredCallback)]() {
        if (cb) cb();
        auto rit = m_registry.find(id);
        if (rit != m_registry.end()) {
            rit->second.residency = ResourceResidencyState::Unloaded;
        }
    });
    recomputeStats();
    return true;
}

void MemoryBudgetManager::deferDestruction(uint64_t resourceId, uint64_t retireFrame, std::function<void()> callback) {
    m_deferredQueue.push_back({resourceId, retireFrame, std::move(callback)});
    m_stats.pendingDeferredDestructions = static_cast<uint32_t>(m_deferredQueue.size());
}

uint32_t MemoryBudgetManager::processDeferredDestructions(uint64_t completedFrame) {
    uint32_t destroyed = 0;
    auto it = m_deferredQueue.begin();
    while (it != m_deferredQueue.end()) {
        if (completedFrame >= it->retireFrame) {
            if (it->callback) it->callback();
            it = m_deferredQueue.erase(it);
            ++destroyed;
        } else {
            ++it;
        }
    }
    if (destroyed > 0) recomputeStats();
    return destroyed;
}

bool MemoryBudgetManager::enqueueStreamingRequest(const StreamingUploadRequest& req) {
    if (req.resourceId == 0 || req.cancelled) return false;
    auto it = m_streamingQueue.find(req.resourceId);
    if (it != m_streamingQueue.end()) {
        // Deduplicate: upgrade priority & pinning if higher
        it->second.priority = std::max(it->second.priority, req.priority);
        it->second.pinned = it->second.pinned || req.pinned;
        return false; // deduplicated
    }
    m_streamingQueue[req.resourceId] = req;
    m_stats.pendingStreamingRequests = static_cast<uint32_t>(m_streamingQueue.size());
    return true;
}

bool MemoryBudgetManager::cancelStreamingRequest(uint64_t resourceId) {
    const bool removed = m_streamingQueue.erase(resourceId) > 0;
    m_stats.pendingStreamingRequests = static_cast<uint32_t>(m_streamingQueue.size());
    return removed;
}

std::vector<StreamingUploadRequest> MemoryBudgetManager::scheduleFrameUploads(uint64_t /*currentFrame*/) {
    std::vector<StreamingUploadRequest> scheduled;
    if (m_streamingQueue.empty()) {
        m_stats.lastFrameUploadedBytes = 0;
        m_stats.lastFrameUploadedOps = 0;
        return scheduled;
    }

    // Throttle upload budget under High/Critical memory pressure
    uint64_t byteBudget = m_config.uploadByteBudgetPerFrame;
    uint32_t opBudget = m_config.uploadOpBudgetPerFrame;
    if (m_stats.pressureLevel == MemoryPressureLevel::High) {
        byteBudget /= 2u;
        opBudget = std::max(opBudget / 2u, 1u);
    } else if (m_stats.pressureLevel == MemoryPressureLevel::Critical) {
        byteBudget /= 4u;
        opBudget = 1u;
    }

    std::vector<StreamingUploadRequest*> candidates;
    candidates.reserve(m_streamingQueue.size());
    for (auto& [id, req] : m_streamingQueue) {
        ++req.ageFrames; // prevent starvation via aging
        candidates.push_back(&req);
    }

    std::sort(candidates.begin(), candidates.end(), [](const StreamingUploadRequest* a, const StreamingUploadRequest* b) {
        if (a->pinned != b->pinned) return a->pinned > b->pinned;
        const float scoreA = a->priority + static_cast<float>(a->ageFrames) * 0.05f;
        const float scoreB = b->priority + static_cast<float>(b->ageFrames) * 0.05f;
        return scoreA > scoreB;
    });

    uint64_t usedBytes = 0;
    for (auto* req : candidates) {
        if (scheduled.size() >= opBudget) break;
        if (!scheduled.empty() && (usedBytes + req->byteSize > byteBudget)) continue;
        scheduled.push_back(*req);
        usedBytes += req->byteSize;
    }

    for (const auto& s : scheduled) {
        m_streamingQueue.erase(s.resourceId);
    }

    m_stats.lastFrameUploadedBytes = usedBytes;
    m_stats.lastFrameUploadedOps = static_cast<uint32_t>(scheduled.size());
    m_stats.pendingStreamingRequests = static_cast<uint32_t>(m_streamingQueue.size());
    return scheduled;
}

} // namespace sidera
