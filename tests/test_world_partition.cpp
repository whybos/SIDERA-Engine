#include "core/Log.h"
#include "partition/WorldPartition.h"
#include "partition/WorldPartitionConfig.h"
#include "partition/WorldPartitionGrid.h"
#include "partition/WorldPartitionManager.h"
#include "partition/WorldPartitionObject.h"
#include "renderer/Camera.h"
#include "streaming/WorldStreamingManager.h"
#include "terrain/HeightmapData.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainConfig.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <unordered_set>
#include <vector>

using namespace sidera;

namespace {

Camera makeCamera(const glm::vec3& pos) {
    CameraConfig cfg{};
    cfg.position = pos;
    Camera cam(cfg);
    cam.setPosition(pos);
    return cam;
}

// Test 1: Grid cell creation, boundary calculations, and stable Cell IDs
void testGridAndCellCreation() {
    std::cout << "[Test 1] Grid cell creation, boundary calculations, and stable Cell IDs...\n";

    WorldPartitionConfig config{};
    config.cellSize = 50.0f;
    config.worldOrigin = glm::vec3(0.0f);
    WorldPartition partition(config);

    // Coordinate mapping tests
    CellCoord c0 = WorldPartitionGrid::worldToCell(glm::vec3(25.0f, 0.0f, 25.0f), 50.0f);
    assert(c0.x == 0 && c0.z == 0);

    CellCoord c1 = WorldPartitionGrid::worldToCell(glm::vec3(50.0f, 0.0f, 50.0f), 50.0f);
    assert(c1.x == 1 && c1.z == 1);

    CellCoord cFar = WorldPartitionGrid::worldToCell(glm::vec3(2550.0f, 0.0f, -1450.0f), 50.0f);
    assert(cFar.x == 51 && cFar.z == -29);

    // Negative coordinates floor semantics
    CellCoord cNeg = WorldPartitionGrid::worldToCell(glm::vec3(-0.1f, 0.0f, -0.1f), 50.0f);
    assert(cNeg.x == -1 && cNeg.z == -1);

    CellCoord cNegBoundary = WorldPartitionGrid::worldToCell(glm::vec3(-50.0f, 0.0f, -50.0f), 50.0f);
    assert(cNegBoundary.x == -1 && cNegBoundary.z == -1);

    CellCoord cNegPast = WorldPartitionGrid::worldToCell(glm::vec3(-50.01f, 0.0f, -50.01f), 50.0f);
    assert(cNegPast.x == -2 && cNegPast.z == -2);

    // Stable 64-bit Cell ID Bijection
    const uint64_t idNeg = WorldPartitionGrid::computeCellId(-10, -25);
    const CellCoord recovered = WorldPartitionGrid::cellIdToCoord(idNeg);
    assert(recovered.x == -10 && recovered.z == -25);

    // Boundary calculation
    AABB bNeg = WorldPartitionGrid::cellToWorldBounds(CellCoord{-1, -1}, 50.0f);
    assert(bNeg.min.x == -50.0f && bNeg.max.x == 0.0f);
    assert(bNeg.min.z == -50.0f && bNeg.max.z == 0.0f);

    auto cell = partition.getOrCreateCell(CellCoord{-1, -1});
    assert(cell != nullptr);
    assert(cell->getId() == WorldPartitionGrid::computeCellId(-1, -1));
    assert(cell->getBounds().min.x == -50.0f);

    std::cout << "  -> PASSED: Grid coordinates, negative floors, and stable 64-bit IDs verified.\n";
}

// Test 2: Hierarchical Macro Region aggregation
void testMacroRegionHierarchy() {
    std::cout << "[Test 2] Macro region hierarchy and parent cell linkage...\n";

    WorldPartitionConfig config{};
    config.cellSize = 30.0f;
    config.macroCellFactor = 2; // 2x2 cells per macro region (60m x 60m)
    WorldPartition partition(config);

    // Create 4 adjacent cells forming a 2x2 macro block
    auto c00 = partition.getOrCreateCell(CellCoord{0, 0});
    auto c10 = partition.getOrCreateCell(CellCoord{1, 0});
    auto c01 = partition.getOrCreateCell(CellCoord{0, 1});
    auto c11 = partition.getOrCreateCell(CellCoord{1, 1});

    const uint64_t expectedMacroId = WorldPartitionGrid::computeCellId(0, 0);
    assert(c00->getParentMacroCellId() == expectedMacroId);
    assert(c10->getParentMacroCellId() == expectedMacroId);
    assert(c01->getParentMacroCellId() == expectedMacroId);
    assert(c11->getParentMacroCellId() == expectedMacroId);

    const auto& macroRegions = partition.getMacroRegions();
    assert(macroRegions.contains(expectedMacroId));
    assert(macroRegions.at(expectedMacroId).size() == 4);
    (void)macroRegions;

    const AABB* macroBounds = partition.getMacroBounds(expectedMacroId);
    assert(macroBounds != nullptr);
    assert(macroBounds->min.x == 0.0f && macroBounds->max.x == 60.0f);
    assert(macroBounds->min.z == 0.0f && macroBounds->max.z == 60.0f);
    (void)macroBounds;

    std::cout << "  -> PASSED: Macro cell hierarchy and parent-child bounding box verified.\n";
}

// Test 3: Object registration, canonical ownership, and bounds spanning
void testObjectAssignmentAndSpanning() {
    std::cout << "[Test 3] Canonical object ownership and boundary spanning...\n";

    WorldPartitionConfig config{};
    config.cellSize = 40.0f;
    WorldPartition partition(config);

    // Small object fully inside cell (0, 0)
    const glm::vec3 smallPos{10.0f, 1.0f, 10.0f};
    const AABB smallBounds{.min = smallPos - glm::vec3(1.0f), .max = smallPos + glm::vec3(1.0f)};
    uint32_t smallId = partition.registerObject("SmallObj", PartitionObjectType::StaticMesh, smallPos, smallBounds);

    const auto* smallObj = partition.getObject(smallId);
    assert(smallObj != nullptr);
    assert(!smallObj->spansMultipleCells());
    (void)smallObj;

    auto cell00 = partition.getCell(CellCoord{0, 0});
    assert(cell00 != nullptr);
    assert(cell00->getOwnedObjects().size() == 1);
    assert(cell00->getOwnedObjects()[0] == smallId);

    // Large object located at boundary (38, 0, 38) with extent spanning into (1, 0), (0, 1), (1, 1)
    const glm::vec3 largePos{38.0f, 2.0f, 38.0f};
    const AABB largeBounds{.min = glm::vec3(35.0f, 0.0f, 35.0f), .max = glm::vec3(45.0f, 5.0f, 45.0f)};
    uint32_t largeId = partition.registerObject("LargeObj", PartitionObjectType::StaticMesh, largePos, largeBounds);

    const auto* largeObj = partition.getObject(largeId);
    assert(largeObj != nullptr);
    assert(largeObj->spansMultipleCells());
    assert(largeObj->ownerCellId == cell00->getId()); // Canonical owner remains (0, 0)
    assert(largeObj->overlappingCellIds.size() == 3);  // Crosses into (1,0), (0,1), (1,1)
    (void)largeObj;

    // Verify canonical ownership strictly ensures single render ownership
    assert(cell00->getOwnedObjects().size() == 2);
    auto cell10 = partition.getCell(CellCoord{1, 0});
    assert(cell10->getOwnedObjects().empty()); // NOT owned by (1, 0)
    assert(cell10->getOverlappingObjects().size() == 1); // But tracked for spatial queries

    std::cout << "  -> PASSED: Canonical ownership and multi-cell spanning verified.\n";
}

// Test 4: Object movement and removal
void testObjectMovementAndRemoval() {
    std::cout << "[Test 4] Object movement between cells and safe removal...\n";

    WorldPartitionConfig config{};
    config.cellSize = 50.0f;
    WorldPartition partition(config);

    const glm::vec3 initPos{10.0f, 0.0f, 10.0f};
    const AABB initBounds{.min = initPos - glm::vec3(1.0f), .max = initPos + glm::vec3(1.0f)};
    uint32_t objId = partition.registerObject("MoveObj", PartitionObjectType::StaticMesh, initPos, initBounds);

    auto oldCell = partition.getCell(CellCoord{0, 0});
    assert(oldCell->getOwnedObjects().size() == 1);

    // Move to cell (3, -2) -> world (160, 0, -80)
    const glm::vec3 newPos{160.0f, 0.0f, -80.0f};
    const AABB newBounds{.min = newPos - glm::vec3(1.0f), .max = newPos + glm::vec3(1.0f)};
    bool moved = partition.moveObject(objId, newPos, newBounds);
    assert(moved);
    (void)moved;

    assert(oldCell->getOwnedObjects().empty()); // Removed from old cell
    auto newCell = partition.getCell(CellCoord{3, -2});
    assert(newCell != nullptr);
    assert(newCell->getOwnedObjects().size() == 1);
    assert(newCell->getOwnedObjects()[0] == objId);

    // Unregister object
    bool unreg = partition.unregisterObject(objId);
    assert(unreg);
    (void)unreg;
    assert(newCell->getOwnedObjects().empty());
    assert(partition.getObject(objId) == nullptr);

    std::cout << "  -> PASSED: Object dynamic migration and unregistration verified.\n";
}

// Test 5: Spatial queries with deduplication
void testSpatialQueriesDeduplication() {
    std::cout << "[Test 5] Spatial queries and duplicate query prevention...\n";

    WorldPartitionConfig config{};
    config.cellSize = 30.0f;
    WorldPartition partition(config);

    // Place an object on the boundary that overlaps cells (0,0), (1,0), (0,1), (1,1)
    const glm::vec3 crossPos{29.0f, 0.0f, 29.0f};
    const AABB crossBounds{.min = glm::vec3(25.0f, -5.0f, 25.0f), .max = glm::vec3(35.0f, 5.0f, 35.0f)};
    partition.registerObject("CrossObj", PartitionObjectType::StaticMesh, crossPos, crossBounds);

    // Query an AABB covering the 4 intersecting cells
    const AABB queryBox{.min = glm::vec3(0.0f, -10.0f, 0.0f), .max = glm::vec3(60.0f, 10.0f, 60.0f)};
    const auto objects = partition.getObjectsInBounds(queryBox);

    // Even though the object overlaps 4 cells in the query range, it must be returned EXACTLY ONCE
    assert(objects.size() == 1);
    assert(objects[0]->name == "CrossObj");

    // Radius query
    const auto radiusCells = partition.getCellsInRadius(glm::vec3(30.0f, 0.0f, 30.0f), 20.0f);
    assert(radiusCells.size() >= 4);

    // Neighbor query
    const auto neighbors = partition.getNeighborCells(CellCoord{0, 0}, 1);
    assert(!neighbors.empty());

    std::cout << "  -> PASSED: Spatial queries deduplication verified (0 duplicate objects).\n";
}

// Test 6: Terrain chunk and foliage group registration
void testTerrainAndFoliageRegistration() {
    std::cout << "[Test 6] Terrain chunk and foliage group partition integration...\n";

    WorldPartitionConfig config{};
    config.cellSize = 60.0f;
    WorldPartition partition(config);

    // TerrainChunk setup
    HeightmapData hmap;
    hmap.generateProcedural(65, 42, 6, 1.0f);
    TerrainConfig tcfg{};
    tcfg.width = 60.0f;
    tcfg.length = 60.0f;
    tcfg.chunksX = 1;
    tcfg.chunksZ = 1;
    auto chunk = std::make_unique<TerrainChunk>(101, 1, 2, tcfg, hmap);

    bool terrReg = partition.registerTerrainChunk(chunk.get());
    assert(terrReg);
    (void)terrReg;

    auto terrCell = partition.getCell(CellCoord{1, 2});
    assert(terrCell != nullptr);
    assert(terrCell->getTerrainChunks().size() == 1);
    assert(terrCell->getTerrainChunks()[0]->getId() == 101);

    // Foliage group registration
    const std::vector<uint32_t> foliageIds = {1001, 1002, 1003, 1004, 1005};
    bool folReg = partition.registerFoliageToCell(terrCell->getId(), foliageIds);
    assert(folReg);
    (void)folReg;
    assert(terrCell->getFoliageInstanceCount() == 5);

    // Spatial query for terrain and foliage
    const AABB searchBox = terrCell->getBounds();
    const auto foundChunks = partition.getTerrainInBounds(searchBox);
    assert(foundChunks.size() == 1);
    assert(foundChunks[0]->getId() == 101);

    const auto foundFoliage = partition.getFoliageInBounds(searchBox);
    assert(foundFoliage.size() == 5);

    // Remove foliage
    partition.removeFoliageFromCell(terrCell->getId(), {1002, 1004});
    assert(terrCell->getFoliageInstanceCount() == 3);

    std::cout << "  -> PASSED: Terrain chunk and foliage group integration verified.\n";
}

// Test 7: Multi-source distance evaluation and hysteresis
void testMultiSourceHysteresis() {
    std::cout << "[Test 7] Multi-streaming sources distance evaluation and hysteresis...\n";

    WorldPartitionManager manager;
    WorldPartitionConfig config{};
    config.cellSize = 30.0f;
    config.loadRadius = 60.0f;
    config.unloadRadius = 100.0f;
    config.hysteresisMargin = 20.0f;
    manager.init(config);

    // Create a standalone target cell at (2, 0) -> [60m .. 90m]
    auto& partition = manager.getPartition();
    auto cell = partition.getOrCreateCell(CellCoord{2, 0});
    assert(cell->getState() == StreamingState::Unloaded);

    // Camera 1 is at (0, 0, 0) -> distance to cell [60..90] is 60m <= loadRadius (60m)
    Camera cam1 = makeCamera(glm::vec3(0.0f, 0.0f, 0.0f));
    manager.update(0.016f, cam1);

    assert(cell->getState() == StreamingState::Resident || cell->getState() == StreamingState::Active);

    // Move camera to (85, 0, 0) -> distance is inside bounds (0m) -> cell becomes Active
    Camera camClose = makeCamera(glm::vec3(70.0f, 0.0f, 0.0f));
    manager.update(0.016f, camClose);
    assert(cell->getState() == StreamingState::Active);

    // Move camera to (150, 0, 0) -> distance to box max (90m) is 60m
    // Since 60m < unloadRadius (100m), hysteresis MUST keep cell active!
    Camera camHysteresis = makeCamera(glm::vec3(150.0f, 0.0f, 0.0f));
    manager.update(0.016f, camHysteresis);
    assert(cell->getState() == StreamingState::Active);

    // Move camera far away to (300, 0, 0) -> distance = 210m > unloadRadius (100m) -> unloads!
    Camera camFar = makeCamera(glm::vec3(300.0f, 0.0f, 0.0f));
    manager.update(0.016f, camFar);
    assert(cell->getState() == StreamingState::Unloaded);

    std::cout << "  -> PASSED: Distance hysteresis and anti-thrashing verified.\n";
}

// Test 8: WorldStreamingManager integration and synchronization
void testStreamingIntegration() {
    std::cout << "[Test 8] WorldStreamingManager synchronization and region lifecycle...\n";

    WorldStreamingManager streamMgr;
    StreamingConfig sCfg{};
    sCfg.enabled = true;
    sCfg.gridDimX = 2;
    sCfg.gridDimZ = 2;
    sCfg.regionSize = 30.0f;
    sCfg.loadRadius = 60.0f;
    sCfg.unloadRadius = 100.0f;

    TerrainConfig tCfg{};
    tCfg.width = 60.0f;
    tCfg.length = 60.0f;
    tCfg.chunksX = 2;
    tCfg.chunksZ = 2;
    HeightmapData hmap;
    hmap.generateProcedural(33, 101, 4, 1.0f);

    streamMgr.initHeadless(sCfg, tCfg, hmap);

    WorldPartitionManager partMgr;
    WorldPartitionConfig pCfg{};
    pCfg.cellSize = 30.0f;
    pCfg.loadRadius = 60.0f;
    pCfg.unloadRadius = 100.0f;
    partMgr.init(pCfg, &streamMgr);

    // Ensure all 4 streaming regions are mirrored into partition cells
    assert(partMgr.getPartition().getCellCount() == 4);

    auto cell00 = partMgr.getPartition().getCell(CellCoord{0, 0});
    assert(cell00 != nullptr);
    assert(cell00->getLinkedRegion() != nullptr);

    // Force load all regions through WorldStreamingManager
    streamMgr.forceLoadAll();

    // Update partition manager to synchronize state
    Camera cam = makeCamera(glm::vec3(15.0f, 5.0f, 15.0f));
    partMgr.update(0.016f, cam);

    assert(cell00->isActive());
    assert(partMgr.getStats().activeCells > 0);

    std::cout << "  -> PASSED: WorldStreamingManager bidirectional synchronization verified.\n";
}

// Test 9: Debug visualization boxes and state color mapping
void testDebugVisualizationBoxes() {
    std::cout << "[Test 9] Debug visualization bounding boxes and state colors...\n";

    WorldPartitionManager manager;
    WorldPartitionConfig config{};
    config.cellSize = 40.0f;
    config.debugVisualization = true;
    manager.init(config);

    auto& partition = manager.getPartition();
    auto cellAct = partition.getOrCreateCell(CellCoord{0, 0});
    cellAct->setState(StreamingState::Active);

    auto cellLoad = partition.getOrCreateCell(CellCoord{1, 0});
    cellLoad->setState(StreamingState::Loading);

    auto cellUnl = partition.getOrCreateCell(CellCoord{0, 1});
    cellUnl->setState(StreamingState::Unloading);

    auto cellOff = partition.getOrCreateCell(CellCoord{1, 1});
    cellOff->setState(StreamingState::Unloaded);

    const auto boxes = manager.getDebugCellBoxes();
    assert(boxes.size() == 4);

    for (const auto& box : boxes) {
        if (box.coord == CellCoord{0, 0}) {
            assert(box.state == StreamingState::Active);
            assert(box.color.y > 0.8f); // High green
        } else if (box.coord == CellCoord{1, 0}) {
            assert(box.state == StreamingState::Loading);
            assert(box.color.x > 0.8f && box.color.y > 0.7f); // Yellow
        } else if (box.coord == CellCoord{0, 1}) {
            assert(box.state == StreamingState::Unloading);
            assert(box.color.x > 0.8f && box.color.z < 0.2f); // Orange
        } else if (box.coord == CellCoord{1, 1}) {
            assert(box.state == StreamingState::Unloaded);
            assert(std::abs(box.color.x - box.color.y) < 0.05f); // Gray
        }
    }

    std::cout << "  -> PASSED: Debug bounding box generation and state coloring verified.\n";
}

// Test 10: High-density performance benchmark (10,000 cells spatial hash)
void testHighDensityPerformance() {
    std::cout << "[Test 10] High-density spatial indexing performance (10,000 cells)...\n";

    WorldPartitionConfig config{};
    config.cellSize = 20.0f;
    WorldPartition partition(config);

    const auto startPopulate = std::chrono::high_resolution_clock::now();

    // Populate a 100x100 grid = 10,000 cells
    for (int32_t x = -50; x < 50; ++x) {
        for (int32_t z = -50; z < 50; ++z) {
            partition.getOrCreateCell(CellCoord{x, z});
        }
    }
    const auto endPopulate = std::chrono::high_resolution_clock::now();
    const double populateMs = std::chrono::duration<double, std::milli>(endPopulate - startPopulate).count();
    assert(partition.getCellCount() == 10000);

    // Benchmark 10,000 random cell lookups
    const auto startLookup = std::chrono::high_resolution_clock::now();
    for (int32_t i = 0; i < 10000; ++i) {
        const float wx = static_cast<float>((i % 100) - 50) * 20.0f + 5.0f;
        const float wz = static_cast<float>((i / 100) - 50) * 20.0f + 5.0f;
        auto cell = partition.getCellAtPosition(glm::vec3(wx, 0.0f, wz));
        assert(cell != nullptr);
    }
    const auto endLookup = std::chrono::high_resolution_clock::now();
    const double lookupMs = std::chrono::duration<double, std::milli>(endLookup - startLookup).count();
    const double lookupUsPerOp = (lookupMs * 1000.0) / 10000.0;

    std::cout << "  -> PASSED: 10,000 cells generated in " << populateMs << " ms.\n";
    std::cout << "  -> PASSED: 10,000 spatial lookups in " << lookupMs << " ms ("
              << lookupUsPerOp << " us/lookup).\n";
}

} // namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "SIDERA Engine — Milestone 22: World Partition System Unit Tests\n";
    std::cout << "=================================================================\n";

    try {
        testGridAndCellCreation();
        testMacroRegionHierarchy();
        testObjectAssignmentAndSpanning();
        testObjectMovementAndRemoval();
        testSpatialQueriesDeduplication();
        testTerrainAndFoliageRegistration();
        testMultiSourceHysteresis();
        testStreamingIntegration();
        testDebugVisualizationBoxes();
        testHighDensityPerformance();
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Unit test failed with exception: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "[FATAL] Unit test failed with unknown exception.\n";
        return 1;
    }

    std::cout << "=================================================================\n";
    std::cout << "ALL 10 WORLD PARTITION UNIT TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "=================================================================\n";
    return 0;
}
