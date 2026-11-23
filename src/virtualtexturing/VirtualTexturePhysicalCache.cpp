#include "virtualtexturing/VirtualTexturePhysicalCache.h"

#include <algorithm>
#include <limits>

namespace sidera {

void VirtualTexturePhysicalCache::init(uint32_t capacity, uint32_t physicalTileSize) {
    clear();
    m_tileSize = std::max(physicalTileSize, 16u);
    m_slots.resize(std::max(capacity, 8u));
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_slots.size()); ++i) {
        m_slots[i].physicalPageId = i;
        m_slots[i].state = PageResidencyState::Unloaded;
    }
    m_residentCount = 0;
}

void VirtualTexturePhysicalCache::clear() noexcept {
    m_slots.clear();
    m_residentCount = 0;
}

void VirtualTexturePhysicalCache::flushRetiredSlots(uint64_t currentFrame) noexcept {
    for (auto& slot : m_slots) {
        if (slot.state == PageResidencyState::Evicting && currentFrame >= slot.retireFrame) {
            slot.state = PageResidencyState::Unloaded;
            slot.pinned = false;
            slot.refCount = 0;
        }
    }
}

bool VirtualTexturePhysicalCache::allocateSlot(const VirtualTexturePageID& pageId,
                                               uint64_t currentFrame,
                                               bool pinFallback,
                                               uint32_t& outPhysicalIndex,
                                               uint16_t& outVersion,
                                               VirtualTexturePageID* outEvictedPage,
                                               bool* outDidEvict) {
    if (outDidEvict) *outDidEvict = false;
    flushRetiredSlots(currentFrame);

    // 1. Try to find an unloaded free slot
    for (auto& slot : m_slots) {
        if (slot.state == PageResidencyState::Unloaded) {
            slot.virtualPage = pageId;
            slot.lastUsedFrame = currentFrame;
            slot.lastRequestedFrame = currentFrame;
            slot.pinned = pinFallback;
            slot.state = PageResidencyState::Resident;
            slot.refCount = 1;
            ++slot.version;
            ++m_residentCount;
            outPhysicalIndex = slot.physicalPageId;
            outVersion = slot.version;
            return true;
        }
    }

    // 2. Cache full: find Least Recently Used (LRU) unpinned slot safe for reuse
    uint32_t victimIdx = 0xFFFFFFFFu;
    uint64_t oldestFrame = std::numeric_limits<uint64_t>::max();
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_slots.size()); ++i) {
        const auto& s = m_slots[i];
        if (!s.pinned && s.state == PageResidencyState::Resident && s.lastUsedFrame < oldestFrame) {
            oldestFrame = s.lastUsedFrame;
            victimIdx = i;
        }
    }

    if (victimIdx == 0xFFFFFFFFu) {
        return false; // All slots pinned or in-flight evicting
    }

    auto& victim = m_slots[victimIdx];
    if (outEvictedPage) *outEvictedPage = victim.virtualPage;
    if (outDidEvict) *outDidEvict = true;

    victim.virtualPage = pageId;
    victim.lastUsedFrame = currentFrame;
    victim.lastRequestedFrame = currentFrame;
    victim.pinned = pinFallback;
    victim.state = PageResidencyState::Resident;
    victim.refCount = 1;
    ++victim.version;

    outPhysicalIndex = victim.physicalPageId;
    outVersion = victim.version;
    return true;
}

void VirtualTexturePhysicalCache::touchSlot(uint32_t physicalIndex, uint64_t currentFrame) noexcept {
    if (physicalIndex < m_slots.size() && m_slots[physicalIndex].state == PageResidencyState::Resident) {
        m_slots[physicalIndex].lastUsedFrame = currentFrame;
    }
}

void VirtualTexturePhysicalCache::releaseSlot(uint32_t physicalIndex, uint64_t currentFrame, uint64_t deferFrames) noexcept {
    if (physicalIndex < m_slots.size() && m_slots[physicalIndex].state == PageResidencyState::Resident && !m_slots[physicalIndex].pinned) {
        m_slots[physicalIndex].state = PageResidencyState::Evicting;
        m_slots[physicalIndex].retireFrame = currentFrame + deferFrames;
        if (m_residentCount > 0u) --m_residentCount;
    }
}

} // namespace sidera
