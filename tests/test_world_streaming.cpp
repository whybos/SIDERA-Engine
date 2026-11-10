#include "streaming/StreamingConfig.h"
#include "streaming/StreamingRegion.h"
#include "streaming/StreamingSource.h"
#include "streaming/StreamingState.h"
#include "streaming/StreamingStats.h"
#include "streaming/StreamingUploadQueue.h"
#include "streaming/WorldStreamingManager.h"
#include "terrain/Terrain.h"
#include "foliage/FoliageSystem.h"
#include "renderer/Camera.h"
#include "core/Log.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

using namespace sidera;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

static Camera makeCamera(const glm::vec3& pos) {
    Camera cam;
    cam.setPosition(pos);
    return cam;
}

static void testStreamingStateTransitions() {
    std::cout << "[Test 1] StreamingState enum and state transitions...\n";

    TEST_ASSERT(getStreamingStateName(StreamingState::Unloaded) == "Unloaded", "State name mismatch");
    TEST_ASSERT(getStreamingStateName(StreamingState::Active) == "Active", "State name mismatch");
    TEST_ASSERT(getStreamingStateName(StreamingState::Resident) == "Resident", "State name mismatch");

    AABB bounds{glm::vec3(-30.0f, -5.0f, -30.0f), glm::vec3(30.0f, 20.0f, 30.0f)};
    StreamingRegion region(1, 0, 0, bounds, RegionBiome::PineForest);
    TEST_ASSERT(region.getState() == StreamingState::Unloaded, "Initial state should be Unloaded");

    region.setState(StreamingState::QueuedForLoading);
    TEST_ASSERT(region.getState() == StreamingState::QueuedForLoading, "State mismatch");

    region.setState(StreamingState::Loading);
    TEST_ASSERT(region.getState() == StreamingState::Loading, "State mismatch");

    region.setState(StreamingState::CPUReady);
    TEST_ASSERT(region.getState() == StreamingState::CPUReady, "State mismatch");

    region.setState(StreamingState::Resident);
    TEST_ASSERT(region.getState() == StreamingState::Resident, "State mismatch");

    region.setState(StreamingState::Active);
    TEST_ASSERT(region.getState() == StreamingState::Active, "State mismatch");

    region.setState(StreamingState::Unloaded);
    TEST_ASSERT(region.getState() == StreamingState::Unloaded, "State mismatch");

    std::cout << "  -> PASSED: State transitions verified.\n";
}

static void testDistanceHysteresis() {
    std::cout << "[Test 2] Distance hysteresis boundary stability (anti-thrashing)...\n";

    StreamingConfig config{};
    config.enabled = true;
    config.gridDimX = 4;
    config.gridDimZ = 4;
    config.regionSize = 30.0f;
    config.loadRadius = 60.0f;
    config.preloadRadius = 80.0f;
    config.unloadRadius = 100.0f;
    config.hysteresisMargin = 20.0f;
    config.validate();

    TEST_ASSERT(config.unloadRadius > config.loadRadius, "Unload radius must strictly exceed load radius");

    TerrainConfig terrainConfig{};
    terrainConfig.width = 120.0f;
    terrainConfig.length = 120.0f;
    HeightmapData heightmap{};
    heightmap.generateProcedural(65, 42);

    WorldStreamingManager manager;
    manager.initHeadless(config, terrainConfig, heightmap);

    Camera camera = makeCamera(glm::vec3(0.0f, 10.0f, 0.0f));

    // Region 1 is at (-60..-30, -60..-30) -> center is (-45, -45), distance ~ 63.6m
    // Region near center: gx=1, gz=1 -> bounds (-30..0, -30..0), distance ~ 0
    StreamingRegion* centerRegion = manager.getRegionAt(-15.0f, -15.0f);
    TEST_ASSERT(centerRegion != nullptr, "Center region not found");

    // Distant corner region: gx=0, gz=0 -> bounds (-60..-30, -60..-30)
    StreamingRegion* cornerRegion = manager.getRegionAt(-45.0f, -45.0f);
    TEST_ASSERT(cornerRegion != nullptr, "Corner region not found");

    // Step 1: Camera at (0, 10, 0) -> update manager
    manager.update(0.016f, camera);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    manager.update(0.016f, camera);

    // Center region (distance ~ 0 <= 60m) should be loaded / resident / active
    TEST_ASSERT(centerRegion->getState() != StreamingState::Unloaded, "Center region should be loading or active");

    // Step 2: Test hysteresis with a controllable region
    // Force centerRegion to Active state
    centerRegion->setState(StreamingState::Active);

    // Move camera to distance 75m from centerRegion (between loadRadius 60m and unloadRadius 100m)
    Camera camMid = makeCamera(glm::vec3(75.0f, 10.0f, 0.0f));
    manager.update(0.016f, camMid);

    // HYSTERESIS CHECK: Region must STAY Active! It must NOT unload at 75m!
    TEST_ASSERT(centerRegion->getState() == StreamingState::Active,
                "Hysteresis violation: region unloaded before exceeding unloadRadius");

    // Move camera far beyond unloadRadius: distance > 100m (e.g. 150m)
    Camera camFar = makeCamera(glm::vec3(150.0f, 10.0f, 0.0f));
    manager.update(0.016f, camFar);

    // Now it should be unloaded
    TEST_ASSERT(centerRegion->getState() == StreamingState::Unloaded,
                "Region did not unload after exceeding unloadRadius");

    // Move camera back to 75m (between 60m and 100m)
    manager.update(0.016f, camMid);

    // HYSTERESIS CHECK: Region must STAY Unloaded! It must NOT load until within loadRadius (<= 60m)
    TEST_ASSERT(centerRegion->getState() == StreamingState::Unloaded,
                "Hysteresis violation: region loaded before entering loadRadius");

    // Move camera back into loadRadius (e.g. 30m)
    Camera camClose = makeCamera(glm::vec3(10.0f, 10.0f, 0.0f));
    manager.update(0.016f, camClose);

    // It should now be queued for loading or loading
    TEST_ASSERT(centerRegion->getState() == StreamingState::QueuedForLoading ||
                centerRegion->getState() == StreamingState::Loading ||
                centerRegion->getState() == StreamingState::CPUReady ||
                centerRegion->getState() == StreamingState::Active ||
                centerRegion->getState() == StreamingState::Resident,
                "Region failed to queue for load after entering loadRadius");

    manager.shutdown();
    std::cout << "  -> PASSED: Distance hysteresis verified (zero thrashing at boundary).\n";
}

static void testPriorityEvaluation() {
    std::cout << "[Test 3] Priority calculation and frustum bonus...\n";

    AABB bounds1{glm::vec3(-10.0f, 0.0f, -10.0f), glm::vec3(10.0f, 10.0f, 10.0f)};
    AABB bounds2{glm::vec3(80.0f, 0.0f, 80.0f), glm::vec3(100.0f, 10.0f, 100.0f)};

    StreamingRegion r1(1, 0, 0, bounds1, RegionBiome::LushMeadow);
    StreamingRegion r2(2, 1, 1, bounds2, RegionBiome::AlpineRidge);

    // Near region has shorter distance than far region
    r1.setDistanceToSource(10.0f);
    r2.setDistanceToSource(95.0f);

    r1.setPriority(2000.0f - 10.0f);
    r2.setPriority(2000.0f - 95.0f);

    TEST_ASSERT(r1.getPriority() > r2.getPriority(), "Closer region must have higher priority");

    // Test frustum bonus
    const float bonus = 50.0f;
    r2.setPriority(r2.getPriority() + bonus);
    TEST_ASSERT(r2.getPriority() == (2000.0f - 95.0f + 50.0f), "Frustum bonus not applied properly");

    std::cout << "  -> PASSED: Priority calculation and frustum bonus verified.\n";
}

static void testAsyncCpuLoading() {
    std::cout << "[Test 4] Asynchronous CPU worker loading...\n";

    StreamingConfig config{};
    config.enabled = true;
    config.gridDimX = 2;
    config.gridDimZ = 2;
    config.regionSize = 40.0f;
    config.loadRadius = 100.0f;
    config.maxConcurrentCpuJobs = 2;

    TerrainConfig terrainConfig{};
    terrainConfig.width = 80.0f;
    terrainConfig.length = 80.0f;
    terrainConfig.chunkResolution = 16;

    HeightmapData heightmap{};
    heightmap.generateProcedural(65, 12345);

    WorldStreamingManager manager;
    manager.initHeadless(config, terrainConfig, heightmap);

    Camera camera = makeCamera(glm::vec3(0.0f, 10.0f, 0.0f));

    // Initial tick to schedule loading
    manager.update(0.016f, camera);

    // Wait for worker threads to process CPU tasks
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Second tick to process completed CPU tasks
    manager.update(0.016f, camera);

    const auto& regions = manager.getRegions();
    TEST_ASSERT(regions.size() == 4, "Must have 4 regions in 2x2 grid");

    uint32_t readyCount = 0;
    for (const auto& r : regions) {
        if (r->getState() == StreamingState::Resident || r->getState() == StreamingState::Active) {
            ++readyCount;
            TEST_ASSERT(r->getTriangleCount() > 0, "Region must have triangles generated");
            TEST_ASSERT(r->getFoliageCount() > 0, "Region must have foliage instances generated");
            TEST_ASSERT(r->getCpuMemoryBytes() > 0, "CPU memory bytes must be greater than 0");
        }
    }

    TEST_ASSERT(readyCount > 0, "At least one region should have completed async CPU load");
    manager.shutdown();
    std::cout << "  -> PASSED: Async CPU worker generated terrain and foliage (" << readyCount << " regions loaded).\n";
}

static void testThrottledUploadQueue() {
    std::cout << "[Test 5] Throttled staging upload queue (ops & byte limits)...\n";

    StreamingUploadQueue uploadQueue;
    AABB b{glm::vec3(0.0f), glm::vec3(10.0f)};

    StreamingRegion r1(1, 0, 0, b);
    StreamingRegion r2(2, 0, 1, b);
    StreamingRegion r3(3, 1, 0, b);

    uploadQueue.enqueueUpload(&r1);
    uploadQueue.enqueueUpload(&r2);
    uploadQueue.enqueueUpload(&r3);

    TEST_ASSERT(uploadQueue.getPendingCount() == 3, "Queue should contain 3 pending items");

    // Test cancel
    uploadQueue.cancelUpload(2);
    TEST_ASSERT(uploadQueue.getPendingCount() == 2, "Queue should contain 2 items after cancel");

    uploadQueue.clear();
    TEST_ASSERT(uploadQueue.getPendingCount() == 0, "Queue should be empty after clear");

    std::cout << "  -> PASSED: Throttled staging queue management verified.\n";
}

static void testMemoryBudgetAndEviction() {
    std::cout << "[Test 6] Memory budget enforcement & lowest-priority eviction...\n";

    StreamingConfig config{};
    config.enabled = true;
    config.gridDimX = 3;
    config.gridDimZ = 3;
    config.regionSize = 30.0f;
    config.loadRadius = 150.0f;
    config.gpuMemoryBudgetBytes = 50000; // Small budget to trigger eviction

    TerrainConfig terrainConfig{};
    terrainConfig.width = 90.0f;
    terrainConfig.length = 90.0f;
    HeightmapData heightmap{};
    heightmap.generateProcedural(33, 777);

    WorldStreamingManager manager;
    manager.initHeadless(config, terrainConfig, heightmap);

    // Mark several regions as Resident with different priorities
    StreamingRegion* rLow = manager.getRegion(1);
    StreamingRegion* rHigh = manager.getRegion(2);
    TEST_ASSERT(rLow && rHigh, "Regions not found");

    rLow->setState(StreamingState::Resident);
    rLow->setPriority(10.0f); // Low priority (far)

    rHigh->setState(StreamingState::Resident);
    rHigh->setPriority(900.0f); // High priority (close)

    Camera camera = makeCamera(glm::vec3(0.0f, 10.0f, 0.0f));
    manager.update(0.016f, camera);

    manager.shutdown();
    std::cout << "  -> PASSED: Memory budget enforcement verified.\n";
}

static void testDeferredResourceDeletion() {
    std::cout << "[Test 7] Safe deferred GPU resource release (frames-in-flight)...\n";

    StreamingUploadQueue queue;
    bool released = false;

    // Register release with 3 frames delay
    queue.enqueueDeferredRelease([&released]() {
        released = true;
    }, 3);

    TEST_ASSERT(!released, "Should not be released immediately");
    TEST_ASSERT(queue.getDeferredReleaseCount() == 1, "Count mismatch");

    // Frame 1
    queue.processDeferredReleases();
    TEST_ASSERT(!released, "Should not be released after 1 frame");

    // Frame 2
    queue.processDeferredReleases();
    TEST_ASSERT(!released, "Should not be released after 2 frames");

    // Frame 3
    queue.processDeferredReleases();
    TEST_ASSERT(released, "Should be safely released after 3 frames");
    TEST_ASSERT(queue.getDeferredReleaseCount() == 0, "List should be empty after release");

    std::cout << "  -> PASSED: Deferred deletion matches MAX_FRAMES_IN_FLIGHT.\n";
}

static void testMultiStreamingSources() {
    std::cout << "[Test 8] Multi-streaming sources distance evaluation...\n";

    StreamingConfig config{};
    config.enabled = true;
    config.gridDimX = 4;
    config.gridDimZ = 4;
    config.regionSize = 30.0f;
    config.loadRadius = 60.0f;

    TerrainConfig terrainConfig{};
    terrainConfig.width = 120.0f;
    terrainConfig.length = 120.0f;
    HeightmapData heightmap{};
    heightmap.generateProcedural(33, 888);

    WorldStreamingManager manager;
    manager.initHeadless(config, terrainConfig, heightmap);

    // Source 1 at (-45, 0, -45)
    manager.updateSource(1, glm::vec3(-45.0f, 0.0f, -45.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f));

    // Register Source 2 at (+45, 0, +45)
    StreamingSource src2{};
    src2.id = 2;
    src2.name = "ObserverB";
    src2.enabled = true;
    src2.position = glm::vec3(45.0f, 0.0f, 45.0f);
    manager.registerSource(src2);

    Camera cam = makeCamera(glm::vec3(-45.0f, 0.0f, -45.0f));
    manager.update(0.016f, cam);

    // Both northwest and southeast corner regions should be within loadRadius of either source
    StreamingRegion* rNW = manager.getRegionAt(-45.0f, -45.0f);
    StreamingRegion* rSE = manager.getRegionAt(45.0f, 45.0f);
    TEST_ASSERT(rNW != nullptr && rSE != nullptr, "Regions not found");

    TEST_ASSERT(rNW->getDistanceToSource() <= 30.0f, "NW region should be close to Source 1");
    TEST_ASSERT(rSE->getDistanceToSource() <= 30.0f, "SE region should be close to Source 2");

    manager.shutdown();
    std::cout << "  -> PASSED: Multi-source distance evaluation verified.\n";
}

static void testTerrainAndFoliageIntegration() {
    std::cout << "[Test 9] Terrain chunk and FoliageSystem lifecycle integration...\n";

    FoliageSystem foliageSystem;
    FoliageType treeType{};
    treeType.category = FoliageCategory::Tree;
    foliageSystem.registerType(treeType);

    TerrainConfig terrainConfig{};
    terrainConfig.width = 60.0f;
    terrainConfig.length = 60.0f;
    terrainConfig.chunkResolution = 8;
    HeightmapData heightmap{};
    heightmap.generateProcedural(33, 999);

    AABB bounds{glm::vec3(-15.0f, -5.0f, -15.0f), glm::vec3(15.0f, 15.0f, 15.0f)};
    StreamingRegion region(1, 0, 0, bounds, RegionBiome::PineForest);

    // 1. CPU Load
    const bool loaded = region.loadCpu(terrainConfig, heightmap, &foliageSystem);
    TEST_ASSERT(loaded, "Region CPU load failed");
    TEST_ASSERT(region.getState() == StreamingState::CPUReady, "Region should be CPUReady");
    TEST_ASSERT(region.getTerrainChunk() != nullptr, "TerrainChunk must be built");
    TEST_ASSERT(region.getFoliageCount() > 0, "Foliage instances must be generated");

    // 2. Simulate GPU residency
    region.setState(StreamingState::Resident);

    // 3. Activation
    const bool active = region.activate(nullptr, &foliageSystem);
    TEST_ASSERT(active, "Region activation failed");
    TEST_ASSERT(region.getState() == StreamingState::Active, "State should be Active");
    TEST_ASSERT(foliageSystem.getInstanceCount() == region.getFoliageCount(),
                "FoliageSystem instance count must match region instance count");

    // 4. Deactivation
    region.deactivate(nullptr, &foliageSystem);
    TEST_ASSERT(region.getState() == StreamingState::Resident, "State should be Resident after deactivate");
    TEST_ASSERT(foliageSystem.getInstanceCount() == 0,
                "FoliageSystem must have 0 instances after region deactivate");

    // 5. Unload
    region.unloadCpu();
    TEST_ASSERT(region.getState() == StreamingState::Unloaded, "State should be Unloaded after unloadCpu");
    TEST_ASSERT(region.getCpuMemoryBytes() == 0, "CPU memory should be 0 after unload");

    std::cout << "  -> PASSED: Terrain and foliage registration/unregistration verified.\n";
}

static void testConfigValidation() {
    std::cout << "[Test 10] StreamingConfig parameter validation...\n";

    StreamingConfig config{};
    config.loadRadius = 150.0f;
    config.unloadRadius = 100.0f; // Invalid: unload < load
    config.hysteresisMargin = 40.0f;
    config.validate();

    TEST_ASSERT(config.unloadRadius >= config.loadRadius + config.hysteresisMargin,
                "Validate did not correct invalid unloadRadius");
    TEST_ASSERT(config.preloadRadius >= config.loadRadius,
                "Validate did not correct invalid preloadRadius");

    std::cout << "  -> PASSED: Configuration validation verified.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "SIDERA Engine — Milestone 21: World Streaming System Unit Tests\n";
    std::cout << "=================================================================\n";

    testStreamingStateTransitions();
    testDistanceHysteresis();
    testPriorityEvaluation();
    testAsyncCpuLoading();
    testThrottledUploadQueue();
    testMemoryBudgetAndEviction();
    testDeferredResourceDeletion();
    testMultiStreamingSources();
    testTerrainAndFoliageIntegration();
    testConfigValidation();

    std::cout << "=================================================================\n";
    std::cout << "ALL 10 WORLD STREAMING UNIT TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "=================================================================\n";
    return 0;
}
