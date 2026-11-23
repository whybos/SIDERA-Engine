#include "worldgen/WorldGenerator.h"
#include "worldgen/WorldNoise.h"
#include "worldgen/WorldSeed.h"
#include "terrain/Terrain.h"
#include "terrain/TerrainLODManager.h"
#include "streaming/WorldStreamingManager.h"
#include "partition/WorldPartitionManager.h"
#include "renderer/Camera.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <set>
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
    std::cout << "SIDERA Engine — Milestone 24: Procedural World Generation Tests\n";
    std::cout << "=================================================================\n";

    // -------------------------------------------------------------------------
    // Test 1: Deterministic seed — same seed == identical world, different seed !=
    // -------------------------------------------------------------------------
    std::cout << "[Test 1] Deterministic seed (same seed identical, different seeds distinct)...\n";
    {
        WorldGenerationConfig cfgA = WorldGenerationConfig::fromPreset(WorldPreset::Balanced, 987654ULL);
        cfgA.heightmapResolution = 65;
        WorldGenerationConfig cfgB = cfgA;
        WorldGenerationConfig cfgC = WorldGenerationConfig::fromPreset(WorldPreset::Balanced, 123456ULL);
        cfgC.heightmapResolution = 65;

        WorldGenerator genA(cfgA), genB(cfgB), genC(cfgC);
        check(genA.generateWorld() && genB.generateWorld() && genC.generateWorld(), "Worlds must generate");

        const auto& hA = genA.getGlobalHeightmap().getRawData();
        const auto& hB = genB.getGlobalHeightmap().getRawData();
        const auto& hC = genC.getGlobalHeightmap().getRawData();
        check(hA.size() == hB.size() && !hA.empty(), "Heightmap sizes must match");

        uint32_t diffBC = 0;
        for (size_t i = 0; i < hA.size(); ++i) {
            check(hA[i] == hB[i], "Same seed must produce bit-identical heightmap");
            if (std::abs(hA[i] - hC[i]) > 1e-4f) ++diffBC;
        }
        check(diffBC > hA.size() / 2, "Different seeds must produce distinct heightmaps");
        std::cout << "  -> PASSED: Bit-identical same-seed heightmap & distinct different-seed world verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 2: Order-independent region generation & adjacent boundary continuity
    // -------------------------------------------------------------------------
    std::cout << "[Test 2] Order-independent region generation & adjacent boundary continuity...\n";
    {
        WorldGenerationConfig cfg = WorldGenerationConfig::fromPreset(WorldPreset::Continental, 555111ULL);
        cfg.regionResolution = 33;
        cfg.regionSize = 30.0f;

        WorldGenerator g1(cfg), g2(cfg);
        const auto r1A = g1.generateRegion(2, -1);
        const auto r1B = g1.generateRegion(-1, 3);

        // Reverse generation order
        const auto r2B = g2.generateRegion(-1, 3);
        const auto r2A = g2.generateRegion(2, -1);

        check(r1A.heights == r2A.heights && r1A.biomes == r2A.biomes, "Region (2,-1) must be order-independent");
        check(r1B.heights == r2B.heights && r1B.biomes == r2B.biomes, "Region (-1,3) must be order-independent");

        // Adjacent regions (0,0) and (1,0) must match exactly along their shared East/West boundary
        const auto& left  = g1.generateRegion(0, 0);
        const auto& right = g1.generateRegion(1, 0);
        const uint32_t res = left.resolution;
        for (uint32_t iz = 0; iz < res; ++iz) {
            const size_t idxL = static_cast<size_t>(iz) * res + (res - 1u);
            const size_t idxR = static_cast<size_t>(iz) * res + 0u;
            check(std::abs(left.heights[idxL] - right.heights[idxR]) < 1e-5f, "Adjacent region seam height must match");
            check(left.biomes[idxL] == right.biomes[idxR], "Adjacent region seam biome must match");
        }
        std::cout << "  -> PASSED: Order-independence and exact seam height/biome continuity verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 3: Noise library determinism (Value, Perlin, Simplex, Cellular, FBM, Ridged, Warp)
    // -------------------------------------------------------------------------
    std::cout << "[Test 3] Procedural noise library determinism and bounds...\n";
    {
        const WorldSeed s(7777ULL);
        NoiseParams p{};
        p.seed = s;
        p.octaves = 4;
        p.frequency = 0.05f;

        check(WorldNoise::value2D(12.3f, -4.5f, s) == WorldNoise::value2D(12.3f, -4.5f, s), "Value noise deterministic");
        check(WorldNoise::perlin2D(12.3f, -4.5f, s) == WorldNoise::perlin2D(12.3f, -4.5f, s), "Perlin noise deterministic");
        check(WorldNoise::simplex2D(12.3f, -4.5f, s) == WorldNoise::simplex2D(12.3f, -4.5f, s), "Simplex noise deterministic");
        const float cell = WorldNoise::cellular2D(12.3f, -4.5f, s);
        check(cell >= 0.0f && cell <= 1.0f && cell == WorldNoise::cellular2D(12.3f, -4.5f, s), "Cellular noise deterministic");
        const float fbm = WorldNoise::fbm2D(12.3f, -4.5f, p);
        check(fbm >= -1.0f && fbm <= 1.0f && fbm == WorldNoise::fbm2D(12.3f, -4.5f, p), "FBM deterministic");
        const float ridged = WorldNoise::ridged2D(12.3f, -4.5f, p);
        check(ridged >= 0.0f && ridged <= 1.0f && ridged == WorldNoise::ridged2D(12.3f, -4.5f, p), "Ridged deterministic");
        const glm::vec2 w1 = WorldNoise::domainWarp2D(12.3f, -4.5f, 8.0f, p);
        const glm::vec2 w2 = WorldNoise::domainWarp2D(12.3f, -4.5f, 8.0f, p);
        check(w1 == w2, "Domain warp deterministic");
        std::cout << "  -> PASSED: All 7 noise primitives verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 4: Continentalness, Mountains, Valleys, Rivers, Lakes, Biomes, Material Weights
    // -------------------------------------------------------------------------
    std::cout << "[Test 4] Continentalness, mountains, rivers, lakes, biomes, and material weights...\n";
    {
        WorldGenerationConfig cfg = WorldGenerationConfig::fromPreset(WorldPreset::Balanced, 424242ULL);
        WorldGenerator gen(cfg);

        std::set<BiomeType> observedBiomes;
        bool sawRiver = false;
        bool sawMountain = false;
        float minH = 1e9f, maxH = -1e9f;

        for (int iz = -40; iz <= 40; ++iz) {
            for (int ix = -40; ix <= 40; ++ix) {
                const float wx = static_cast<float>(ix) * 4.0f;
                const float wz = static_cast<float>(iz) * 4.0f;
                const WorldPointSample pt = gen.samplePoint(wx, wz);

                observedBiomes.insert(pt.biome.primaryBiome);
                if (pt.riverMask > 0.1f) sawRiver = true;
                if (pt.mountainMask > 0.45f) sawMountain = true;
                minH = std::min(minH, pt.normalizedHeight);
                maxH = std::max(maxH, pt.normalizedHeight);

                // Verify normalized RGBA material weights sum to 1.0
                const float wSum = pt.biome.materialWeights.r + pt.biome.materialWeights.g +
                                   pt.biome.materialWeights.b + pt.biome.materialWeights.a;
                check(std::abs(wSum - 1.0f) < 1e-4f, "Material splat weights must sum to 1.0");
                check(pt.biome.temperature >= 0.0f && pt.biome.temperature <= 1.0f, "Temperature in [0,1]");
                check(pt.biome.moisture >= 0.0f && pt.biome.moisture <= 1.0f, "Moisture in [0,1]");
            }
        }
        check(observedBiomes.size() >= 3, "Balanced world should exhibit multiple distinct biomes");
        check(sawMountain, "Balanced world should contain mountain ridges");
        check(sawRiver, "Balanced world should contain river channels");
        check(maxH - minH > 0.25f, "World must exhibit meaningful elevation relief");
        std::cout << "  -> PASSED: " << observedBiomes.size() << " biomes, relief=[" << minH << ", " << maxH << "], rivers & mountains verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 5: Generation presets produce genuinely distinct worlds
    // -------------------------------------------------------------------------
    std::cout << "[Test 5] Generation presets produce distinct elevation/climate profiles...\n";
    {
        auto measureAvg = [](WorldPreset preset) -> std::pair<float, float> {
            WorldGenerator g(WorldGenerationConfig::fromPreset(preset, 424242ULL));
            float sumH = 0.0f, sumT = 0.0f;
            int n = 0;
            for (int iz = -10; iz <= 10; ++iz) {
                for (int ix = -10; ix <= 10; ++ix) {
                    const auto pt = g.samplePoint(ix * 6.0f, iz * 6.0f);
                    sumH += pt.normalizedHeight;
                    sumT += pt.biome.temperature;
                    ++n;
                }
            }
            return {sumH / n, sumT / n};
        };

        const auto [hMtn, tMtn] = measureAvg(WorldPreset::Mountainous);
        const auto [hArc, tArc] = measureAvg(WorldPreset::Archipelago);
        const auto [hDes, tDes] = measureAvg(WorldPreset::DesertWorld);
        const auto [hAlp, tAlp] = measureAvg(WorldPreset::AlpineWorld);

        check(hMtn > hArc, "Mountainous preset must have higher average elevation than Archipelago");
        check(tDes > tAlp, "Desert preset must be warmer than Alpine preset");
        std::cout << "  -> PASSED: Presets distinct (MtnH=" << hMtn << " > ArcH=" << hArc
                  << ", DesT=" << tDes << " > AlpT=" << tAlp << ").\n";
    }

    // -------------------------------------------------------------------------
    // Test 6: Integration with Terrain LOD, World Partition, World Streaming & Async Safety
    // -------------------------------------------------------------------------
    std::cout << "[Test 6] Terrain LOD, World Partition, World Streaming & async thread safety...\n";
    {
        WorldGenerationConfig wCfg = WorldGenerationConfig::fromPreset(WorldPreset::Balanced, 424242ULL);
        wCfg.heightmapResolution = 129;
        wCfg.regionResolution = 33;
        WorldGenerator gen(wCfg);
        check(gen.generateWorld(), "World generation must succeed");

        // Feed generated heightmap into Advanced Terrain LOD (Milestone 23 & 23.1)
        TerrainConfig tCfg{};
        tCfg.width = wCfg.worldWidth;
        tCfg.length = wCfg.worldLength;
        tCfg.heightScale = wCfg.maxElevation;
        tCfg.heightOffset = wCfg.heightOffset;

        TerrainLODManager lodMgr;
        lodMgr.init(tCfg, TerrainLODConfig::fromPreset(TerrainQualityPreset::High), gen.getGlobalHeightmap());
        Camera cam;
        cam.setPosition(glm::vec3(0.0f, 18.0f, 0.0f));
        lodMgr.beginFrame();
        const auto& sel = lodMgr.updateViewport(0, 0.016f, cam, 1080, false);
        check(!sel.visiblePatches.empty(), "Terrain LOD must produce patches from generated world");
        check(lodMgr.getQuadtree(0)->validateNeighborBalance(), "Generated terrain quadtree must stay balanced");

        // World Streaming & Partition links
        WorldStreamingManager streaming;
        StreamingConfig sCfg{};
        sCfg.gridDimX = 2; sCfg.gridDimZ = 2; sCfg.regionSize = 60.0f;
        streaming.initHeadless(sCfg, tCfg, gen.getGlobalHeightmap());

        WorldPartitionManager partition;
        WorldPartitionConfig pCfg{};
        pCfg.cellSize = 60.0f;
        partition.init(pCfg, &streaming, nullptr, nullptr);

        gen.setWorldPartitionManager(&partition);
        gen.setWorldStreamingManager(&streaming);

        // Concurrent multi-threaded region generation safety test
        const auto t0 = std::chrono::high_resolution_clock::now();
        std::vector<std::thread> workers;
        for (int t = 0; t < 4; ++t) {
            workers.emplace_back([&gen, t]() {
                for (int k = 0; k < 4; ++k) {
                    (void)gen.generateRegion(t, k);
                }
            });
        }
        for (auto& th : workers) th.join();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double asyncMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        check(gen.getStats().generatedRegionCount == 16u, "All 16 async regions must be cached cleanly");
        streaming.shutdown();
        lodMgr.destroy();
        std::cout << "  -> PASSED: Terrain LOD (" << sel.visiblePatches.size()
                  << " patches), Partition, Streaming, and 16 async regions in " << asyncMs << " ms.\n";
    }

    std::cout << "=================================================================\n";
    std::cout << "ALL 6 PROCEDURAL WORLD GENERATION UNIT TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "=================================================================\n";
    return 0;
}
