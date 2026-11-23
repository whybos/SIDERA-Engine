#pragma once

#include "virtualtexturing/VirtualTextureTypes.h"

#include <cstdint>
#include <vector>

namespace sidera {

struct PhysicalPageSlot {
    uint32_t physicalPageId = 0;
    VirtualTexturePageID virtualPage{};
    uint64_t lastUsedFrame = 0;
    uint64_t lastRequestedFrame = 0;
    uint64_t retireFrame = 0; // frames-in-flight safe reuse barrier
    uint16_t version = 0;
    PageResidencyState state = PageResidencyState::Unloaded;
    bool pinned = false;
    uint32_t refCount = 0;
};

// Software-managed physical page pool with LRU eviction and frames-in-flight deferred reuse.
class VirtualTexturePhysicalCache {
public:
    void init(uint32_t capacity, uint32_t physicalTileSize);
    void clear() noexcept;

    // Allocate a free slot or evict least-recently-used unpinned slot (respecting frames-in-flight)
    bool allocateSlot(const VirtualTexturePageID& pageId,
                      uint64_t currentFrame,
                      bool pinFallback,
                      uint32_t& outPhysicalIndex,
                      uint16_t& outVersion,
                      VirtualTexturePageID* outEvictedPage = nullptr,
                      bool* outDidEvict = nullptr);

    void touchSlot(uint32_t physicalIndex, uint64_t currentFrame) noexcept;
    void releaseSlot(uint32_t physicalIndex, uint64_t currentFrame, uint64_t deferFrames = 3ULL) noexcept;
    void flushRetiredSlots(uint64_t currentFrame) noexcept;

    [[nodiscard]] uint32_t getCapacity() const noexcept { return static_cast<uint32_t>(m_slots.size()); }
    [[nodiscard]] uint32_t getResidentCount() const noexcept { return m_residentCount; }
    [[nodiscard]] uint32_t getFreeCount() const noexcept { return getCapacity() - m_residentCount; }
    [[nodiscard]] uint64_t getPhysicalMemoryBytes() const noexcept {
        return static_cast<uint64_t>(m_slots.size()) * m_tileSize * m_tileSize * 4ULL;
    }
    [[nodiscard]] const std::vector<PhysicalPageSlot>& getSlots() const noexcept { return m_slots; }

private:
    std::vector<PhysicalPageSlot> m_slots;
    uint32_t m_tileSize = 136;
    uint32_t m_residentCount = 0;
};

} // namespace sidera
