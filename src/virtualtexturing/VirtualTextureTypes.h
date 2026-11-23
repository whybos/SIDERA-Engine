#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <cstdint>
#include <string>

namespace sidera {

enum class PageResidencyState : uint8_t {
    Unloaded = 0,
    Requested,
    Loading,
    Resident,
    Evicting
};

enum class VirtualTextureSemantic : uint8_t {
    AlbedoSRGB = 0,
    NormalLinear,
    RoughnessMetallicAOLinear
};

// Deterministic 64-bit Virtual Texture Page ID
struct VirtualTexturePageID {
    uint16_t textureId = 0;
    uint8_t  mipLevel  = 0;
    uint32_t pageX     = 0;
    uint32_t pageY     = 0;

    [[nodiscard]] constexpr uint64_t pack() const noexcept {
        return (static_cast<uint64_t>(textureId) << 48) |
               (static_cast<uint64_t>(mipLevel) << 40) |
               (static_cast<uint64_t>(pageX & 0xFFFFFu) << 20) |
               static_cast<uint64_t>(pageY & 0xFFFFFu);
    }

    [[nodiscard]] constexpr bool operator==(const VirtualTexturePageID& o) const noexcept {
        return textureId == o.textureId && mipLevel == o.mipLevel && pageX == o.pageX && pageY == o.pageY;
    }
};

// std430-aligned GPU Page Table Entry (16 bytes)
struct alignas(16) PageTableEntry {
    uint32_t physicalPageIndex = 0xFFFFFFFFu;
    uint32_t residentAndMip = 0u;             // bit 0 = resident, bits 8..15 = mip, bits 16..31 = version
    uint32_t fallbackPhysicalIndex = 0u;
    uint32_t fallbackMip = 0u;

    [[nodiscard]] bool isResident() const noexcept { return (residentAndMip & 1u) != 0u; }
    [[nodiscard]] uint32_t getMip() const noexcept { return (residentAndMip >> 8u) & 0xFFu; }
    [[nodiscard]] uint32_t getVersion() const noexcept { return (residentAndMip >> 16u) & 0xFFFFu; }
};
static_assert(sizeof(PageTableEntry) == 16, "PageTableEntry must be 16 bytes (std430).");

// std430-aligned GPU Feedback Request (16 bytes)
struct alignas(16) VTFeedbackRequest {
    uint32_t textureId = 0;
    uint32_t pageX = 0;
    uint32_t pageY = 0;
    uint32_t mipLevel = 0;
};
static_assert(sizeof(VTFeedbackRequest) == 16, "VTFeedbackRequest must be 16 bytes (std430).");

// 48-byte push constants for vt_feedback_resolve.comp
struct alignas(16) VTFeedbackPC {
    glm::vec4 cameraPosFootprint{0.0f, 0.0f, 0.0f, 120.0f};
    glm::uvec4 gridParams{256u, 0u, 128u, 4096u}; // sampleCount, textureId, pagesPerAxisMip0, maxFeedbackCap
    glm::uvec4 mipParams{8u, 128u, 4u, 0u};       // maxMipLevels, pageSize, gutterSize, frameIndex
};
static_assert(sizeof(VTFeedbackPC) == 48, "VTFeedbackPC must be 48 bytes.");

struct VirtualTextureConfig {
    uint32_t virtualWidth = 16384;
    uint32_t virtualHeight = 16384;
    uint32_t pageSize = 128;              // 128 or 256
    uint32_t gutterSize = 4;              // border texels on each side for seamless filtering
    uint32_t physicalCacheCapacity = 256; // max resident physical pages
    uint32_t maxUploadsPerFrame = 8;
    uint64_t maxUploadBytesPerFrame = 4ULL * 1024ULL * 1024ULL;
    uint32_t feedbackCapacity = 4096;
    uint32_t pinCoarsestMipLevels = 2;    // always keep lowest-res fallback mips resident

    [[nodiscard]] uint32_t getPhysicalTileSize() const noexcept {
        return pageSize + 2u * gutterSize;
    }
};

struct VirtualTextureStatistics {
    uint32_t totalVirtualTextures = 0;
    uint64_t totalVirtualPages = 0;
    uint32_t residentPhysicalPages = 0;
    uint32_t freePhysicalPages = 0;
    uint32_t cacheCapacity = 0;

    uint64_t virtualTextureMemoryBytes = 0;
    uint64_t physicalCacheMemoryBytes = 0;
    uint64_t pageTableMemoryBytes = 0;
    uint64_t feedbackBufferMemoryBytes = 0;
    uint64_t textureMemorySavedBytes = 0;

    uint64_t pageUploadBytes = 0;
    uint64_t pageUploadsTotal = 0;
    uint64_t pageEvictionCount = 0;
    uint64_t cacheHits = 0;
    uint64_t cacheMisses = 0;
    uint64_t fallbackSampleCount = 0;
    uint64_t feedbackOverflowCount = 0;
    uint32_t pendingRequestQueueLength = 0;

    float cacheHitRate = 0.0f;
    float cacheUtilization = 0.0f;
    float feedbackProcessTimeMs = 0.0f;
    float pageUploadTimeMs = 0.0f;
};

} // namespace sidera
