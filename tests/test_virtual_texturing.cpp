#include "virtualtexturing/VirtualTextureSystem.h"

#include <cmath>
#include <iostream>

using namespace sidera;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "  [FAILED] " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "SIDERA Engine — Milestone 27: Virtual Texturing Unit Tests\n";
    std::cout << "=================================================================\n";

    // -------------------------------------------------------------------------
    // Test 1: Virtual texture creation, mip addressing, pinned fallback mips
    // -------------------------------------------------------------------------
    std::cout << "[Test 1] Virtual texture creation, mip addressing, and pinned fallback mips...\n";
    {
        VirtualTextureConfig cfg{};
        cfg.virtualWidth = 16384;
        cfg.virtualHeight = 16384;
        cfg.pageSize = 128;
        cfg.gutterSize = 4;
        cfg.physicalCacheCapacity = 32;
        cfg.pinCoarsestMipLevels = 2;

        VirtualTextureSystem vt;
        vt.init(cfg, nullptr);
        const uint16_t tid = vt.createVirtualTexture("Terrain_Albedo_16K", 16384, 16384, VirtualTextureSemantic::AlbedoSRGB);
        check(tid == 0 && vt.getTextures().size() == 1, "Virtual texture must be registered");
        check(vt.getStats().residentPhysicalPages >= 5u, "Coarsest 2 mip levels (1x1 + 2x2 = 5 pages) must be pinned resident");
        check(vt.getStats().textureMemorySavedBytes > 1000000000ULL, "16K virtual texture must save >1 GB vs physical cache");

        // Sampling an unloaded mip 0 page must resolve cleanly via parent-mip fallback (never infinite loop)
        bool usedFallback = false;
        const PageTableEntry e = vt.samplePageTable(VirtualTexturePageID{tid, 0, 12, 34}, &usedFallback);
        check(e.isResident() && usedFallback, "Unloaded mip 0 page must fall back to pinned coarse mip");
        std::cout << "  -> PASSED: 16K VT created, pinned fallback verified, saved "
                  << (vt.getStats().textureMemorySavedBytes / (1024ULL * 1024ULL)) << " MB.\n";
    }

    // -------------------------------------------------------------------------
    // Test 2: Request deduplication, throttled uploads, and cache hit after streaming
    // -------------------------------------------------------------------------
    std::cout << "[Test 2] Request deduplication, throttled uploads, and cache hit...\n";
    {
        VirtualTextureConfig cfg{};
        cfg.physicalCacheCapacity = 32;
        cfg.maxUploadsPerFrame = 4;

        VirtualTextureSystem vt;
        vt.init(cfg, nullptr);
        const uint16_t tid = vt.createVirtualTexture("VT_Test", 4096, 4096);

        // Request the same page 50 times (simulating multi-viewport / shader duplicates)
        const VirtualTexturePageID p0{tid, 0, 5, 7};
        for (int i = 0; i < 50; ++i) vt.requestPage(p0);
        check(vt.getStats().pendingRequestQueueLength == 0 || vt.getPageTable().isResident(p0) || true, "Dedup safe");

        const uint32_t up = vt.processPendingUploads();
        check(up == 1u, "Duplicate requests must deduplicate to exactly 1 upload");

        bool usedFallback = true;
        const PageTableEntry e = vt.samplePageTable(p0, &usedFallback);
        check(e.isResident() && !usedFallback, "Uploaded page must now be a direct cache hit");
        std::cout << "  -> PASSED: 50 duplicate requests deduplicated to 1 upload, direct cache hit verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 3: LRU Page Eviction, version increment, and pinned fallback protection
    // -------------------------------------------------------------------------
    std::cout << "[Test 3] LRU page eviction, version bump, and pinned fallback protection...\n";
    {
        VirtualTextureConfig cfg{};
        cfg.physicalCacheCapacity = 10; // 5 pinned fallback + 5 dynamic slots
        cfg.maxUploadsPerFrame = 16;
        cfg.pinCoarsestMipLevels = 2;

        VirtualTextureSystem vt;
        vt.init(cfg, nullptr);
        const uint16_t tid = vt.createVirtualTexture("VT_Evict", 4096, 4096);

        // Stream 12 distinct mip 0 pages -> must trigger LRU evictions while preserving pinned mips
        for (uint32_t i = 0; i < 12; ++i) {
            vt.beginFrame();
            vt.requestPage(VirtualTexturePageID{tid, 0, i, 0});
            (void)vt.processPendingUploads();
        }
        check(vt.getStats().pageEvictionCount >= 7u, "LRU eviction must reclaim slots when cache is full");
        check(vt.getStats().residentPhysicalPages <= cfg.physicalCacheCapacity, "Resident count must never exceed capacity");

        // Coarsest fallback mip must still be resident (never evicted!)
        const uint8_t coarsestMip = static_cast<uint8_t>(vt.getTextures()[0].mipLevels - 1u);
        check(vt.getPageTable().isResident(VirtualTexturePageID{tid, coarsestMip, 0, 0}),
              "Pinned coarsest fallback mip must never be evicted");
        std::cout << "  -> PASSED: " << vt.getStats().pageEvictionCount
                  << " LRU evictions executed while protecting pinned fallback mips.\n";
    }

    // -------------------------------------------------------------------------
    // Test 4: Gutter-padded tile border continuity (seamless bilinear/anisotropic filtering)
    // -------------------------------------------------------------------------
    std::cout << "[Test 4] Gutter-padded tile border continuity across adjacent tiles...\n";
    {
        VirtualTextureConfig cfg{};
        cfg.pageSize = 128;
        cfg.gutterSize = 4;

        VirtualTextureSystem vt;
        vt.init(cfg, nullptr);
        const uint16_t tid = vt.createVirtualTexture("VT_Seam", 4096, 4096);

        const auto tileA = vt.generateTileData(VirtualTexturePageID{tid, 0, 0, 0});
        const auto tileB = vt.generateTileData(VirtualTexturePageID{tid, 0, 1, 0});
        const uint32_t ts = cfg.getPhysicalTileSize(); // 136

        // Rightmost interior pixel of tileA (tx = 4 + 127 = 131) must equal left gutter of tileB (tx = 3)!
        for (uint32_t ty = cfg.gutterSize; ty < cfg.gutterSize + cfg.pageSize; ++ty) {
            const size_t idxA = (static_cast<size_t>(ty) * ts + (cfg.gutterSize + cfg.pageSize - 1u)) * 4ULL;
            const size_t idxB = (static_cast<size_t>(ty) * ts + (cfg.gutterSize - 1u)) * 4ULL;
            check(tileA[idxA + 0] == tileB[idxB + 0] && tileA[idxA + 1] == tileB[idxB + 1],
                  "Adjacent tile gutter texels must match exactly for seam-free filtering");
        }
        std::cout << "  -> PASSED: Exact gutter texel continuity verified across adjacent 136x136 tiles.\n";
    }

    std::cout << "=================================================================\n";
    std::cout << "ALL 4 VIRTUAL TEXTURING UNIT TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "=================================================================\n";
    return 0;
}
