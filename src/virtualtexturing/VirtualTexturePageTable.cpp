#include "virtualtexturing/VirtualTexturePageTable.h"

#include <algorithm>

namespace sidera {

void VirtualTexturePageTable::init(uint32_t flatTableSize) {
    clear();
    m_flatEntries.assign(std::max(flatTableSize, 64u), PageTableEntry{});
}

void VirtualTexturePageTable::clear() noexcept {
    m_entries.clear();
    m_flatEntries.clear();
}

void VirtualTexturePageTable::mapPage(const VirtualTexturePageID& id, uint32_t physicalPageIndex, uint16_t version) {
    PageTableEntry e{};
    e.physicalPageIndex = physicalPageIndex;
    e.residentAndMip = 1u | (static_cast<uint32_t>(id.mipLevel) << 8u) | (static_cast<uint32_t>(version) << 16u);
    e.fallbackPhysicalIndex = physicalPageIndex;
    e.fallbackMip = id.mipLevel;

    m_entries[id.pack()] = e;
    if (!m_flatEntries.empty()) {
        const size_t flatIdx = static_cast<size_t>(id.pack() % m_flatEntries.size());
        m_flatEntries[flatIdx] = e;
    }
}

void VirtualTexturePageTable::unmapPage(const VirtualTexturePageID& id) {
    m_entries.erase(id.pack());
    if (!m_flatEntries.empty()) {
        const size_t flatIdx = static_cast<size_t>(id.pack() % m_flatEntries.size());
        m_flatEntries[flatIdx] = PageTableEntry{};
    }
}

bool VirtualTexturePageTable::isResident(const VirtualTexturePageID& id) const noexcept {
    auto it = m_entries.find(id.pack());
    return it != m_entries.end() && it->second.isResident();
}

PageTableEntry VirtualTexturePageTable::resolveWithFallback(const VirtualTexturePageID& id,
                                                            uint32_t maxMipLevels,
                                                            bool* outUsedFallback) const noexcept {
    if (outUsedFallback) *outUsedFallback = false;

    VirtualTexturePageID cur = id;
    const uint32_t maxSteps = std::min(maxMipLevels, 16u);
    for (uint32_t step = 0; step < maxSteps; ++step) {
        auto it = m_entries.find(cur.pack());
        if (it != m_entries.end() && it->second.isResident()) {
            if (outUsedFallback) *outUsedFallback = (step > 0u);
            return it->second;
        }
        if (cur.mipLevel + 1u >= maxMipLevels) {
            break;
        }
        ++cur.mipLevel;
        cur.pageX >>= 1u;
        cur.pageY >>= 1u;
    }
    if (outUsedFallback) *outUsedFallback = true;
    return PageTableEntry{};
}

} // namespace sidera
