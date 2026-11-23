#include "memory/MemoryTypes.h"
#include "memory/MemoryBudgetManager.h"

#include <iostream>

using namespace sidera;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { ++g_passed; } \
        else { ++g_failed; std::cerr << "[FAIL] " << msg << "\n"; } \
    } while (0)

static void testMemoryTrackingAndPressure() {
    MemoryBudgetConfig cfg{};
    cfg.globalGpuBudgetBytes = 100ull * 1024ull * 1024ull; // 100 MiB
    cfg.moderateThreshold = 0.70;
    cfg.highThreshold = 0.85;
    cfg.criticalThreshold = 0.95;
    cfg.evictionHysteresis = 0.08;

    MemoryBudgetManager mgr;
    CHECK(mgr.init(nullptr, cfg), "MemoryBudgetManager::init succeeds");
    CHECK(mgr.getStats().pressureLevel == MemoryPressureLevel::Normal, "Initial pressure == Normal");

    ResourceRegistryEntry r1{};
    r1.name = "TerrainChunk_0";
    r1.subsystem = GPUMemorySubsystem::Terrain;
    r1.lifetime = ResourceLifetimeClass::Streaming;
    r1.estimatedGpuBytes = 75ull * 1024ull * 1024ull;
    r1.cpuBytes = 8ull * 1024ull * 1024ull;
    r1.priority = 1.0f;
    r1.lastUsedFrame = 1;
    const uint64_t id1 = mgr.registerResource(r1);

    CHECK(mgr.getStats().pressureLevel == MemoryPressureLevel::Moderate, "75% usage triggers Moderate pressure");
    CHECK(mgr.getStats().currentCpuBytes == 8ull * 1024ull * 1024ull, "CPU memory tracked");
    CHECK(mgr.getStats().peakGpuBytes == 75ull * 1024ull * 1024ull, "Peak GPU memory tracked");

    ResourceRegistryEntry r2{};
    r2.name = "VTPagePool";
    r2.subsystem = GPUMemorySubsystem::VirtualTexturing;
    r2.lifetime = ResourceLifetimeClass::Streaming;
    r2.estimatedGpuBytes = 22ull * 1024ull * 1024ull;
    r2.priority = 5.0f;
    r2.lastUsedFrame = 2;
    r2.pinned = true;
    const uint64_t id2 = mgr.registerResource(r2);

    CHECK(mgr.getStats().pressureLevel == MemoryPressureLevel::Critical, "97% usage triggers Critical pressure");

    // Eviction must skip pinned r2 and select unpinned r1
    const auto cands = mgr.selectEvictionCandidates(10, 20ull * 1024ull * 1024ull);
    CHECK(cands.size() == 1u && cands[0] == id1, "Eviction selects unpinned lower-priority r1 and protects pinned r2");
    CHECK(!mgr.evictResource(id2, 12), "Direct eviction of pinned resource rejected");

    bool destroyedFlag = false;
    CHECK(mgr.evictResource(id1, 12, [&]() { destroyedFlag = true; }), "Eviction of r1 queued for deferred destruction");
    CHECK(!destroyedFlag, "Deferred destruction does NOT run before retire frame");
    CHECK(mgr.processDeferredDestructions(11) == 0u && !destroyedFlag, "Frame 11 < retireFrame 12: still deferred");
    CHECK(mgr.processDeferredDestructions(12) == 1u && destroyedFlag, "Frame 12 >= retireFrame 12: safely destroyed");

    // Reload transition increments reloadCount
    mgr.setResidency(id1, ResourceResidencyState::Resident);
    CHECK(mgr.getStats().reloadCount == 1u, "Reload count incremented when Unloaded -> Resident");
}

static void testStreamingSchedulerAndDeduplication() {
    MemoryBudgetConfig cfg{};
    cfg.uploadByteBudgetPerFrame = 10ull * 1024ull * 1024ull; // 10 MiB/frame
    cfg.uploadOpBudgetPerFrame = 2u;

    MemoryBudgetManager mgr;
    mgr.init(nullptr, cfg);

    // Enqueue 3 requests + 1 duplicate with upgraded priority
    CHECK(mgr.enqueueStreamingRequest({101, "ChunkA", 4ull * 1024ull * 1024ull, 1.0f, 50.0f, 0, false, false}), "Enqueue ChunkA");
    CHECK(!mgr.enqueueStreamingRequest({101, "ChunkA", 4ull * 1024ull * 1024ull, 9.0f, 50.0f, 0, false, false}), "Duplicate ChunkA deduplicated");
    CHECK(mgr.enqueueStreamingRequest({102, "ChunkB", 4ull * 1024ull * 1024ull, 5.0f, 20.0f, 0, false, false}), "Enqueue ChunkB");
    CHECK(mgr.enqueueStreamingRequest({103, "ChunkC", 4ull * 1024ull * 1024ull, 2.0f, 30.0f, 0, false, false}), "Enqueue ChunkC");
    CHECK(mgr.cancelStreamingRequest(103), "Cancel ChunkC succeeds");

    const auto batch = mgr.scheduleFrameUploads(1);
    CHECK(batch.size() == 2u, "Op budget == 2 schedules ChunkA (upgraded prio 9) and ChunkB (prio 5)");
    CHECK(batch[0].resourceId == 101u && batch[1].resourceId == 102u, "Highest priority scheduled first");
    CHECK(mgr.getStats().lastFrameUploadedBytes == 8ull * 1024ull * 1024ull, "Uploaded bytes == 8 MiB");

    mgr.destroy();
    CHECK(!mgr.isInitialized(), "MemoryBudgetManager::destroy clears initialized flag");
}

int main() {
    testMemoryTrackingAndPressure();
    testStreamingSchedulerAndDeduplication();

    std::cout << "test_memory_streaming: " << g_passed << " passed, " << g_failed << " failed.\n";
    return (g_failed == 0) ? 0 : 1;
}
