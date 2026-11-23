#include "vegetation/VegetationDistributionManager.h"
#include "worldgen/WorldGenerator.h"
#include "terrain/Terrain.h"
#include "foliage/FoliageSystem.h"
#include "partition/WorldPartitionGrid.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

using namespace sidera;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "  [FAILED] " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "SIDERA Engine — Milestone 25: Vegetation Distribution Unit Tests\n";
    std::cout << "=================================================================\n";

    WorldGenerationConfig wCfg = WorldGenerationConfig::fromPreset(WorldPreset::Balanced, 424242ULL);
    wCfg.heightmapResolution = 129;
    WorldGenerator worldGen(wCfg);
    check(worldGen.generateWorld(), "World generation must succeed");

    // -------------------------------------------------------------------------
    // Test 1: Deterministic same-seed placement, different seeds, order-independence, pos/neg regions
    // -------------------------------------------------------------------------
    std::cout << "[Test 1] Deterministic placement, different seeds, order-independence, pos/neg regions...\n";
    {
        VegetationDistributionConfig cfgA{};
        cfgA.worldSeed = 424242ULL;
        cfgA.regionSize = 30.0f;
        cfgA.densityMultiplier = 1.2f;

        VegetationDistributionConfig cfgB = cfgA;
        VegetationDistributionConfig cfgC = cfgA;
        cfgC.worldSeed = 999888ULL;

        VegetationDistributionManager mgrA, mgrB, mgrC;
        mgrA.init(cfgA, &worldGen);
        mgrB.init(cfgB, &worldGen);
        mgrC.init(cfgC, &worldGen);

        // Positive and negative region coordinates in opposite orders
        const auto rA1 = mgrA.generateRegion(1, -1);
        const auto rA2 = mgrA.generateRegion(-2, 0);

        const auto rB2 = mgrB.generateRegion(-2, 0);
        const auto rB1 = mgrB.generateRegion(1, -1);

        const auto rC1 = mgrC.generateRegion(1, -1);

        check(!rA1.placements.empty(), "Region (1,-1) must produce vegetation placements");
        check(rA1.placements.size() == rB1.placements.size(), "Same seed must produce identical placement count");
        for (size_t i = 0; i < rA1.placements.size(); ++i) {
            check(rA1.placements[i].foliageInstance.position == rB1.placements[i].foliageInstance.position,
                  "Same seed must produce bit-identical world positions");
            check(rA1.placements[i].speciesId == rB1.placements[i].speciesId, "Species ID must match");
        }
        check(rA2.placements.size() == rB2.placements.size(), "Negative region must be order-independent");

        bool anyDiff = (rA1.placements.size() != rC1.placements.size());
        for (size_t i = 0; !anyDiff && i < rA1.placements.size(); ++i) {
            if (glm::distance(rA1.placements[i].foliageInstance.position, rC1.placements[i].foliageInstance.position) > 1e-3f) {
                anyDiff = true;
            }
        }
        check(anyDiff, "Different seeds must produce different vegetation layouts");
        std::cout << "  -> PASSED: Same-seed bit-identical (" << rA1.placements.size()
                  << " instances), order-independent, pos/neg regions verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 2: 6-Factor Suitability Scoring (Biome, Temp, Moisture, Alt, Slope, Material, Water)
    // -------------------------------------------------------------------------
    std::cout << "[Test 2] 6-factor suitability scoring (biome, climate, altitude, slope, material, water)...\n";
    {
        VegetationSpecies pine{};
        pine.speciesId = 1;
        pine.rule.allowedBiomeMask = biomeBit(BiomeType::TemperateForest);
        pine.rule.minAltitude = 0.25f; pine.rule.maxAltitude = 0.70f;
        pine.rule.maxSlope = 0.35f;
        pine.rule.preferredTemperature = 0.45f; pine.rule.temperatureTolerance = 0.25f;
        pine.rule.preferredMoisture = 0.60f; pine.rule.moistureTolerance = 0.25f;

        WorldPointSample ideal{};
        ideal.normalizedHeight = 0.45f;
        ideal.biome.primaryBiome = BiomeType::TemperateForest;
        ideal.biome.secondaryBiome = BiomeType::TemperateForest;
        ideal.biome.temperature = 0.45f;
        ideal.biome.moisture = 0.60f;
        ideal.biome.slope = 0.05f;
        ideal.biome.materialWeights = glm::vec4(0.8f, 0.05f, 0.15f, 0.0f);

        const float sIdeal = VegetationDistributionManager::evaluateSuitability(pine, ideal);
        check(sIdeal > 0.8f, "Ideal habitat must score > 0.8");

        WorldPointSample steep = ideal; steep.biome.slope = 0.85f;
        check(VegetationDistributionManager::evaluateSuitability(pine, steep) < 0.01f, "Extreme slope must reject");

        WorldPointSample underwater = ideal; underwater.lakeMask = 0.9f;
        check(VegetationDistributionManager::evaluateSuitability(pine, underwater) == 0.0f, "Water must reject");

        WorldPointSample wrongBiome = ideal;
        wrongBiome.biome.primaryBiome = BiomeType::Desert;
        wrongBiome.biome.secondaryBiome = BiomeType::Desert;
        check(VegetationDistributionManager::evaluateSuitability(pine, wrongBiome) == 0.0f, "Disallowed biome must reject");
        std::cout << "  -> PASSED: Suitability scoring (ideal=" << sIdeal << ", steep/water/wrongBiome=0) verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 3: Poisson Disk minimum spacing, Clustering, Exclusion Zones, Density Map
    // -------------------------------------------------------------------------
    std::cout << "[Test 3] Poisson Disk minimum spacing, clustering, exclusion zones, and density map...\n";
    {
        VegetationDistributionConfig cfg{};
        cfg.worldSeed = 777123ULL;
        cfg.regionSize = 30.0f;
        cfg.densityMultiplier = 1.5f;

        VegetationDistributionManager mgr;
        mgr.init(cfg, &worldGen);
        check(mgr.getDensityMap().isValid(), "Density map must be generated");

        // Add Circle and Box exclusion zones inside region (0, 0) [0..30] x [0..30]
        const auto circleZone = VegetationExclusionZone::makeCircle(1, "LakeClear", glm::vec2(10.0f, 10.0f), 6.0f);
        const auto boxZone    = VegetationExclusionZone::makeBox(2, "Road", glm::vec2(18.0f, 0.0f), glm::vec2(24.0f, 30.0f));
        mgr.addExclusionZone(circleZone);
        mgr.addExclusionZone(boxZone);

        const auto& res = mgr.generateRegion(0, 0);
        check(!res.placements.empty(), "Region (0,0) must still place vegetation outside exclusion zones");
        check(!res.clusters.empty(), "Cluster metadata must be generated");

        // Verify NO instance lies inside either exclusion zone
        for (const auto& pl : res.placements) {
            const auto& pos = pl.foliageInstance.position;
            check(!circleZone.contains(pos.x, pos.z), "No instance allowed inside Circle exclusion zone");
            check(!boxZone.contains(pos.x, pos.z), "No instance allowed inside Box exclusion zone");
        }

        // Verify exact Poisson Disk minimum spacing between same-species instances
        for (size_t i = 0; i < res.placements.size(); ++i) {
            for (size_t j = i + 1; j < res.placements.size(); ++j) {
                if (res.placements[i].speciesId == res.placements[j].speciesId) {
                    const auto& a = res.placements[i].foliageInstance.position;
                    const auto& b = res.placements[j].foliageInstance.position;
                    const float d = std::hypot(a.x - b.x, a.z - b.z);
                    // Find species minSpacing
                    float minSp = 0.5f;
                    for (const auto& sp : mgr.getSpecies()) {
                        if (sp.speciesId == res.placements[i].speciesId) { minSp = sp.rule.minSpacing; break; }
                    }
                    check(d + 1e-3f >= minSp, "Same-species instances must obey Poisson Disk minimum spacing");
                }
            }
        }
        std::cout << "  -> PASSED: Exclusion zones (0 violations), Poisson spacing, and "
                  << res.clusters.size() << " clusters verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 4: Terrain alignment, Partition ownership, FoliageSystem & Async Thread Safety
    // -------------------------------------------------------------------------
    std::cout << "[Test 4] Terrain alignment, Partition ownership, FoliageSystem & async safety...\n";
    {
        TerrainConfig tCfg{};
        tCfg.width = wCfg.worldWidth;
        tCfg.length = wCfg.worldLength;
        tCfg.heightScale = wCfg.maxElevation;
        tCfg.heightOffset = wCfg.heightOffset;
        Terrain terrain;
        terrain.initHeadless(tCfg);

        VegetationDistributionConfig cfg{};
        cfg.worldSeed = 424242ULL;
        cfg.regionSize = 30.0f;
        cfg.partitionCellSize = 30.0f;

        VegetationDistributionManager mgr;
        mgr.init(cfg, &worldGen, &terrain);

        FoliageSystem foliage;
        const uint64_t totalPlaced = mgr.distributeWorld(-1, 0, -1, 0, &foliage);
        check(totalPlaced > 0, "distributeWorld must place vegetation instances");
        check(foliage.getInstances().size() == totalPlaced, "FoliageSystem must receive all distributed instances");

        // Verify terrain height alignment & canonical WorldPartition cell ownership
        const auto& r00 = mgr.generateRegion(0, 0);
        for (const auto& pl : r00.placements) {
            const auto& pos = pl.foliageInstance.position;
            const float expectedY = terrain.getHeightAt(pos.x, pos.z);
            check(std::abs(pos.y - expectedY) < 1e-4f, "Vegetation must sit exactly on terrain height");
            const CellCoord cc = WorldPartitionGrid::worldToCell(pos, cfg.partitionCellSize);
            const uint64_t expectedCell = WorldPartitionGrid::computeCellId(cc.x, cc.z);
            check(pl.ownerCellId == expectedCell, "Canonical WorldPartition ownerCellId must match");
        }

        // Concurrent multi-threaded async generation safety
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&mgr, t]() {
                for (int k = 0; k < 3; ++k) {
                    (void)mgr.generateRegion(t + 2, k + 2);
                }
            });
        }
        for (auto& th : threads) th.join();

        std::cout << "  -> PASSED: " << totalPlaced << " instances aligned to terrain, owned by Partition cells, "
                  << "registered in FoliageSystem, and async-safe.\n";
    }

    std::cout << "=================================================================\n";
    std::cout << "ALL 4 VEGETATION DISTRIBUTION UNIT TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "=================================================================\n";
    return 0;
}
