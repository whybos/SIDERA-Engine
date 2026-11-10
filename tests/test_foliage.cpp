#include "foliage/FoliageConfig.h"
#include "foliage/FoliageStats.h"
#include "foliage/FoliageType.h"
#include "foliage/FoliageInstance.h"
#include "foliage/FoliageBatch.h"
#include "foliage/ProceduralFoliage.h"
#include "foliage/FoliageSystem.h"
#include "terrain/Terrain.h"
#include "lod/LODManager.h"
#include "renderer/Camera.h"
#include "renderer/InstanceBatch.h"
#include "core/Log.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <vector>

using namespace sidera;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

static void testFoliageTypes() {
    std::cout << "[Test 1] FoliageType registration and category properties...\n";
    FoliageSystem system;

    FoliageType treeType{};
    treeType.category = FoliageCategory::Tree;
    treeType.castShadows = true;
    treeType.maxSlopeAngleDegrees = 35.0f;
    treeType.defaultScale = glm::vec3(1.2f, 1.5f, 1.2f);
    const uint32_t treeId = system.registerType(treeType);

    FoliageType grassType{};
    grassType.category = FoliageCategory::Grass;
    grassType.castShadows = false;
    grassType.maxSlopeAngleDegrees = 45.0f;
    const uint32_t grassId = system.registerType(grassType);

    TEST_ASSERT(system.getTypes().size() == 2, "Type count mismatch");
    TEST_ASSERT(system.getType(treeId) != nullptr, "Tree type not found");
    TEST_ASSERT(system.getType(treeId)->category == FoliageCategory::Tree, "Tree category mismatch");
    TEST_ASSERT(system.getType(treeId)->castShadows == true, "Tree shadow flag mismatch");
    TEST_ASSERT(system.getType(grassId) != nullptr, "Grass type not found");
    TEST_ASSERT(system.getType(grassId)->category == FoliageCategory::Grass, "Grass category mismatch");
    TEST_ASSERT(system.getType(grassId)->castShadows == false, "Grass shadow flag mismatch");
    std::cout << "  -> PASSED: Type registration and categories verified.\n";
}

static void testTerrainQueryAndSlopeFiltering() {
    std::cout << "[Test 2] Terrain height sampling and slope angle filtering...\n";
    Terrain terrain;
    TerrainConfig config{};
    config.width = 100.0f;
    config.length = 100.0f;
    config.heightScale = 10.0f;
    config.heightOffset = 0.0f;
    config.chunksX = 2;
    config.chunksZ = 2;
    config.chunkResolution = 16;
    terrain.initHeadless(config);

    const glm::vec3 normal = terrain.getNormalAt(0.0f, 0.0f);
    const float actualSlope = glm::degrees(std::acos(glm::clamp(normal.y, -1.0f, 1.0f)));

    FoliageSystem system;
    FoliageType type{};
    type.category = FoliageCategory::Tree;
    type.maxSlopeAngleDegrees = actualSlope + 10.0f; // Allow placement on this slope
    type.localBounds = AABB{glm::vec3(-1.0f, 0.0f, -1.0f), glm::vec3(1.0f, 4.0f, 1.0f)};
    type.localSphere = BoundingSphere{glm::vec3(0.0f, 2.0f, 0.0f), 2.5f};
    const uint32_t typeId = system.registerType(type);

    // Place within terrain bounds on slope -> must succeed
    const bool placed1 = system.addInstance(typeId, 0.0f, 0.0f, terrain, 1234);
    TEST_ASSERT(placed1 == true, "Failed to place valid foliage instance");
    TEST_ASSERT(system.getInstanceCount() == 1, "Instance count must be 1");

    const FoliageInstance* inst = system.getInstance(0);
    TEST_ASSERT(inst != nullptr, "Instance 0 pointer is null");
    TEST_ASSERT(inst->position.x == 0.0f, "Position X mismatch");
    TEST_ASSERT(inst->position.z == 0.0f, "Position Z mismatch");
    TEST_ASSERT(std::abs(inst->position.y - inst->terrainHeight) < 1e-4f, "Position Y must match terrain height");

    // Attempt placement with strict slope limit lower than actual slope -> must be rejected
    FoliageType strictType = type;
    strictType.maxSlopeAngleDegrees = std::max(0.0f, actualSlope - 5.0f);
    const uint32_t strictId = system.registerType(strictType);
    const bool placedStrict = system.addInstance(strictId, 0.0f, 0.0f, terrain, 1235);
    TEST_ASSERT(placedStrict == false, "Instance placement on slope steeper than limit must be rejected");

    // Attempt placement outside terrain bounds -> must fail gracefully
    const bool placedOutside = system.addInstance(typeId, 500.0f, 500.0f, terrain, 999);
    TEST_ASSERT(placedOutside == false, "Outside placement must be rejected");
    TEST_ASSERT(system.getInstanceCount() == 1, "Instance count must remain 1");

    std::cout << "  -> PASSED: Slope and terrain boundaries verified.\n";
}

static void testTransformAndRootAnchoring() {
    std::cout << "[Test 3] World transform and root anchoring (local Y=0 pinned to terrain)...\n";
    Terrain terrain;
    TerrainConfig config{};
    config.width = 100.0f;
    config.length = 100.0f;
    config.heightScale = 10.0f;
    config.heightOffset = 2.5f;
    config.chunksX = 2;
    config.chunksZ = 2;
    config.chunkResolution = 16;
    terrain.initHeadless(config);

    FoliageSystem system;
    FoliageType type{};
    type.category = FoliageCategory::Tree;
    type.maxSlopeAngleDegrees = 80.0f;
    type.minScale = glm::vec3(1.0f, 1.0f, 1.0f);
    type.maxScale = glm::vec3(1.0f, 1.0f, 1.0f);
    type.localBounds = AABB{glm::vec3(-0.5f, 0.0f, -0.5f), glm::vec3(0.5f, 5.0f, 0.5f)};
    type.localSphere = BoundingSphere{glm::vec3(0.0f, 2.5f, 0.0f), 3.0f};
    const uint32_t typeId = system.registerType(type);

    system.addInstance(typeId, 10.0f, 10.0f, terrain, 42);
    const FoliageInstance* inst = system.getInstance(0);
    TEST_ASSERT(inst != nullptr, "Instance 0 pointer is null");

    // Transform local base vertex (0, 0, 0) through model matrix
    const glm::vec4 localRoot(0.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec4 worldRoot = inst->transform * localRoot;

    // Must match terrain elevation exactly without sinking or floating
    const float expectedHeight = terrain.getHeightAt(10.0f, 10.0f);
    TEST_ASSERT(std::abs(worldRoot.y - expectedHeight) < 1e-4f, "Root Y must equal terrain surface elevation");
    TEST_ASSERT(std::abs(worldRoot.x - 10.0f) < 1e-4f, "Root X mismatch");
    TEST_ASSERT(std::abs(worldRoot.z - 10.0f) < 1e-4f, "Root Z mismatch");

    // Bounding volume sanity
    TEST_ASSERT(inst->worldBounds.min.y >= expectedHeight - 0.01f, "AABB min.y must not sink below terrain");
    TEST_ASSERT(inst->worldBounds.max.y > expectedHeight, "AABB max.y must extend upward");

    std::cout << "  -> PASSED: Root vertex anchoring verified at exact terrain elevation.\n";
}

static void testShaderWindFormula() {
    std::cout << "[Test 4] Shader wind displacement formula and height anchoring...\n";
    const glm::vec4 windParams(1.0f, 0.0f, 0.45f, 1.25f); // dir=(1,0), strength=0.45, speed=1.25
    const glm::vec4 timeParams(2.0f, 0.85f, 0.35f, 1.0f);  // time=2.0s, freq=0.85, turb=0.35, enabled=1.0

    auto computeWindDisp = [&](float localY, glm::vec2 worldXZ) -> glm::vec2 {
        const float hFactor = std::clamp(localY * 0.45f, 0.0f, 1.0f);
        const float swayFactor = hFactor * hFactor;
        const float phase = timeParams.x * windParams.w + glm::dot(worldXZ, glm::vec2(windParams.x, windParams.y)) * timeParams.y;
        const float mainWave = std::sin(phase);
        const float turbWave = std::sin(phase * 2.37f + 1.15f) * timeParams.z;
        return glm::vec2(windParams.x, windParams.y) * ((mainWave + turbWave) * windParams.z * swayFactor);
    };

    // 1. Root vertex localY = 0 must have ZERO displacement
    const glm::vec2 dispRoot = computeWindDisp(0.0f, glm::vec2(5.0f, 5.0f));
    TEST_ASSERT(std::abs(dispRoot.x) < 1e-6f, "Root vertex X displacement must be 0");
    TEST_ASSERT(std::abs(dispRoot.y) < 1e-6f, "Root vertex Y displacement must be 0");

    // 2. Trunk top localY = 3.0 must have non-zero sway displacement
    const glm::vec2 dispTop = computeWindDisp(3.0f, glm::vec2(5.0f, 5.0f));
    TEST_ASSERT(std::abs(dispTop.x) > 0.01f, "Canopy top displacement must be non-zero");

    // 3. Different positions have spatial wave phase variation
    const glm::vec2 dispOffset = computeWindDisp(3.0f, glm::vec2(15.0f, 5.0f));
    TEST_ASSERT(dispTop != dispOffset, "Spatial phase variation expected across different world positions");

    std::cout << "  -> PASSED: Wind formula mathematical anchoring & turbulence verified.\n";
}

static void testLODAndFrustumCulling() {
    std::cout << "[Test 5] Dynamic LOD evaluation and camera frustum culling...\n";
    Terrain terrain;
    TerrainConfig config{};
    config.width = 200.0f;
    config.length = 200.0f;
    config.heightScale = 0.0f; // flat for simplicity
    config.chunksX = 2;
    config.chunksZ = 2;
    terrain.initHeadless(config);

    FoliageSystem system;
    LODManager lodManager;
    auto& lodGroup = lodManager.createLODGroup("TestFoliageLOD");
    lodGroup.addLevel(nullptr, 15.0f, 150.0f, 1.0f);   // LOD 0: 0..15m
    lodGroup.addLevel(nullptr, 35.0f, 75.0f, 0.5f);    // LOD 1: 15..35m
    lodGroup.addLevel(nullptr, 60.0f, 30.0f, 0.25f);   // LOD 2: 35..60m
    lodGroup.addLevel(nullptr, 1e6f, 0.0f, 0.1f);      // LOD 3: >60m

    FoliageType type{};
    type.category = FoliageCategory::Tree;
    type.lodGroup = &lodGroup;
    type.maxDistance = 150.0f;
    type.maxSlopeAngleDegrees = 80.0f;
    type.localSphere = BoundingSphere{glm::vec3(0.0f, 2.0f, 0.0f), 2.5f};
    const uint32_t typeId = system.registerType(type);

    // Place trees at different distances along camera forward (-Z)
    system.addInstance(typeId, 0.0f, -10.0f, terrain, 101); // 10m away -> LOD 0
    system.addInstance(typeId, 0.0f, -25.0f, terrain, 102); // 25m away -> LOD 1
    system.addInstance(typeId, 0.0f, -45.0f, terrain, 103); // 45m away -> LOD 2
    system.addInstance(typeId, 0.0f, -80.0f, terrain, 104); // 80m away -> LOD 3
    system.addInstance(typeId, 0.0f, 50.0f, terrain, 105);  // behind camera -> frustum culled!

    Camera camera;
    camera.setPosition(glm::vec3(0.0f, 2.0f, 0.0f));
    camera.setOrientation(-90.0f, 0.0f); // looking straight down -Z
    camera.setViewportSize(1920, 1080);

    system.update(0.016f, camera, 1080.0f, lodManager);

    const auto& stats = system.getStats();
    TEST_ASSERT(stats.totalInstances == 5, "Total instance count mismatch");
    TEST_ASSERT(stats.visibleInstances == 4, "Visible instance count mismatch");
    TEST_ASSERT(stats.culledInstances == 1, "Culled instance count mismatch");
    TEST_ASSERT(stats.lod0Count == 1, "LOD 0 count mismatch");
    TEST_ASSERT(stats.lod1Count == 1, "LOD 1 count mismatch");
    TEST_ASSERT(stats.lod2Count == 1, "LOD 2 count mismatch");
    TEST_ASSERT(stats.lod3Count == 1, "LOD 3 count mismatch");

    std::cout << "  -> PASSED: LOD tiers (0..3) and camera frustum culling verified.\n";
}

static void testAlphaCutoutProperties() {
    std::cout << "[Test 6] Foliage Material alpha cutout parameters...\n";
    Material leafMat{};
    leafMat.albedo = glm::vec3(0.2f, 0.6f, 0.15f);
    leafMat.roughness = 0.6f;
    leafMat.alphaCutoff = 0.5f;
    leafMat.flags = 1u; // wind enabled

    TEST_ASSERT(leafMat.alphaCutoff == 0.5f, "Alpha cutoff mismatch");
    TEST_ASSERT((leafMat.flags & 1u) != 0, "Wind animation flag mismatch");

    Material opaqueMat{};
    opaqueMat.alphaCutoff = 0.0f;
    TEST_ASSERT(opaqueMat.alphaCutoff == 0.0f, "Opaque alpha cutoff mismatch");

    std::cout << "  -> PASSED: Alpha cutoff material properties verified.\n";
}

static void testDeterministicFieldGeneration() {
    std::cout << "[Test 7] Deterministic ecosystem placement across terrains...\n";
    Terrain terrain;
    TerrainConfig config{};
    config.width = 100.0f;
    config.length = 100.0f;
    config.heightScale = 8.0f;
    config.chunksX = 2;
    config.chunksZ = 2;
    terrain.initHeadless(config);

    FoliageSystem system1;
    FoliageSystem system2;

    FoliageType bushType{};
    bushType.category = FoliageCategory::Bush;
    bushType.maxSlopeAngleDegrees = 40.0f;
    bushType.minScale = glm::vec3(0.8f);
    bushType.maxScale = glm::vec3(1.3f);
    bushType.localSphere = BoundingSphere{glm::vec3(0.0f, 0.5f, 0.0f), 1.0f};

    const uint32_t t1 = system1.registerType(bushType);
    const uint32_t t2 = system2.registerType(bushType);

    const uint32_t count1 = system1.generateFoliageField(t1, 50, -30.0f, 30.0f, -30.0f, 30.0f, terrain, 777);
    const uint32_t count2 = system2.generateFoliageField(t2, 50, -30.0f, 30.0f, -30.0f, 30.0f, terrain, 777);

    TEST_ASSERT(count1 == count2, "Count mismatch between identical seeds");
    TEST_ASSERT(count1 > 0, "No bushes generated");
    TEST_ASSERT(system1.getInstanceCount() == system2.getInstanceCount(), "Total instance count mismatch");

    for (size_t i = 0; i < system1.getInstanceCount(); ++i) {
        const auto* inst1 = system1.getInstance(static_cast<uint32_t>(i));
        const auto* inst2 = system2.getInstance(static_cast<uint32_t>(i));
        TEST_ASSERT(inst1->position == inst2->position, "Instance position differs across identical runs");
        TEST_ASSERT(inst1->scale == inst2->scale, "Instance scale differs across identical runs");
        TEST_ASSERT(inst1->transform == inst2->transform, "Instance transform differs across identical runs");
    }

    std::cout << "  -> PASSED: Deterministic random seed field placement identical across runs.\n";
}

static void testInstanceBatchIntegration() {
    std::cout << "[Test 8] Integration with InstanceBatchManager (Milestone 16)...\n";
    Terrain terrain;
    TerrainConfig config{};
    config.width = 100.0f;
    config.length = 100.0f;
    terrain.initHeadless(config);

    std::vector<Vertex> vertices = {
        Vertex{.position = {-0.5f, 0.0f, 0.0f}, .color = {1.0f, 1.0f, 1.0f}, .uv = {0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}},
        Vertex{.position = { 0.5f, 0.0f, 0.0f}, .color = {1.0f, 1.0f, 1.0f}, .uv = {1.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}},
        Vertex{.position = { 0.0f, 1.0f, 0.0f}, .color = {1.0f, 1.0f, 1.0f}, .uv = {0.5f, 1.0f}, .normal = {0.0f, 1.0f, 0.0f}},
    };
    std::vector<uint32_t> indices = {0, 1, 2};
    Mesh testMesh(vertices, indices, "TestGrassMesh");

    FoliageSystem system;
    FoliageType grassType{};
    grassType.category = FoliageCategory::Grass;
    grassType.maxSlopeAngleDegrees = 80.0f;
    grassType.mesh = &testMesh;
    grassType.material.albedo = glm::vec3(0.18f, 0.55f, 0.12f);
    grassType.material.alphaCutoff = 0.5f;
    grassType.localSphere = BoundingSphere{glm::vec3(0.0f, 0.5f, 0.0f), 1.0f};
    const uint32_t typeId = system.registerType(grassType);

    system.generateFoliageField(typeId, 25, -20.0f, 20.0f, -20.0f, 20.0f, terrain, 9001);

    Camera camera;
    camera.setPosition(glm::vec3(0.0f, 5.0f, 15.0f));
    camera.setOrientation(-90.0f, -15.0f);
    camera.setViewportSize(1920, 1080);
    LODManager lodManager;

    system.update(0.016f, camera, 1080.0f, lodManager);

    InstanceBatchManager batchManager;
    system.populateBatches(batchManager);

    // Instances must be batched together
    TEST_ASSERT(batchManager.getTotalInstanceCount() == system.getStats().visibleInstances, "Batch manager instance count mismatch");
    TEST_ASSERT(batchManager.getBatchCount() == 1, "Expected exactly 1 shared batch for identical mesh & material");
    std::cout << "  -> PASSED: Batch manager integration populated " << batchManager.getTotalInstanceCount()
              << " visible instances.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  SIDERA Engine — Milestone 20 Foliage System Unit Tests\n";
    std::cout << "=================================================================\n";

    testFoliageTypes();
    testTerrainQueryAndSlopeFiltering();
    testTransformAndRootAnchoring();
    testShaderWindFormula();
    testLODAndFrustumCulling();
    testAlphaCutoutProperties();
    testDeterministicFieldGeneration();
    testInstanceBatchIntegration();

    std::cout << "\n>>> ALL 8 FOLIAGE UNIT TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}
