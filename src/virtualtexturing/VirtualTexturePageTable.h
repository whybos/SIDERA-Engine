#pragma once

#include "virtualtexturing/VirtualTextureTypes.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sidera {

// Logical virtual texture descriptor + mip pyramid addressing
struct VirtualTextureAsset {
    uint16_t textureId = 0;
    std::string name = "VirtualTexture";
    VirtualTextureSemantic semantic = VirtualTextureSemantic::AlbedoSRGB;
    uint32_t width = 16384;
    uint32_t height = 16384;
    uint32_t pageSize = 128;
    uint32_t gutterSize = 4;
    uint32_t mipLevels = 8;

    [[nodiscard]] uint32_t getPagesX(uint32_t mip) const noexcept {
        const uint32_t mipW = std::max(1u, width >> mip);
        return std::max(1u, (mipW + pageSize - 1u) / pageSize);
    }

    [[nodiscard]] uint32_t getPagesY(uint32_t mip) const noexcept {
        const uint32_t mipH = std::max(1u, height >> mip);
        return std::max(1u, (mipH + pageSize - 1u) / pageSize);
    }

    [[nodiscard]] uint64_t getTotalVirtualMemoryBytes() const noexcept {
        // Full RGBA8 mip chain memory if loaded conventionally (~1.33x base mip)
        const uint64_t base = static_cast<uint64_t>(width) * height * 4ULL;
        return base + (base / 3ULL);
    }

    // Gutter-corrected physical tile UV for seamless bilinear/trilinear/anisotropic filtering
    [[nodiscard]] glm::vec2 computePhysicalTileUV(float localU, float localV) const noexcept {
        const float tileSize = static_cast<float>(pageSize + 2u * gutterSize);
        const float g = static_cast<float>(gutterSize);
        const float p = static_cast<float>(pageSize);
        return glm::vec2((g + localU * p) / tileSize, (g + localV * p) / tileSize);
    }
};

// Manages virtual-to-physical page mapping with bounded parent-mip fallback
class VirtualTexturePageTable {
public:
    void init(uint32_t flatTableSize);
    void clear() noexcept;

    void mapPage(const VirtualTexturePageID& id, uint32_t physicalPageIndex, uint16_t version);
    void unmapPage(const VirtualTexturePageID& id);
    [[nodiscard]] bool isResident(const VirtualTexturePageID& id) const noexcept;

    // Resolve requested page; if not resident, walks up parent mips (bounded by maxMipLevels)
    [[nodiscard]] PageTableEntry resolveWithFallback(const VirtualTexturePageID& id,
                                                     uint32_t maxMipLevels,
                                                     bool* outUsedFallback = nullptr) const noexcept;

    [[nodiscard]] const std::vector<PageTableEntry>& getFlatEntries() const noexcept { return m_flatEntries; }
    [[nodiscard]] uint64_t getMemoryBytes() const noexcept { return m_flatEntries.size() * sizeof(PageTableEntry); }

private:
    std::unordered_map<uint64_t, PageTableEntry> m_entries;
    std::vector<PageTableEntry> m_flatEntries;
};

} // namespace sidera
