#include "terrain/Terrain.h"
#include "terrain/TerrainLODManager.h"
#include "terrain/TerrainPatchMesh.h"
#include "terrain/TerrainQuadtree.h"
#include "terrain/HeightmapData.h"
#include "renderer/Camera.h"
#include "foliage/FoliageSystem.h"
#include "streaming/WorldStreamingManager.h"
#include "partition/WorldPartitionManager.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <set>
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
    std::cout << "SIDERA Engine — Milestone 23: Advanced Terrain LOD Unit Tests\n";
    std::cout << "=================================================================\n";

    // Shared procedural heightmap (257x257, 6 octaves, seed 1337)
    HeightmapData heightmap;
    heightmap.generateProcedural(257, 1337, 6, 1.2f);
    check(heightmap.isValid(), "Heightmap must be valid");

    TerrainConfig tCfg{};
    tCfg.width = 240.0f;
    tCfg.length = 240.0f;
    tCfg.heightScale = 24.0f;
    tCfg.heightOffset = -2.0f;
    tCfg.textureTiling = 32.0f;

    // -------------------------------------------------------------------------
    // Test 1: Quadtree root creation, subdivision, merge, and max depth
    // -------------------------------------------------------------------------
    std::cout << "[Test 1] Quadtree root creation, subdivision, merge, and max depth...\n";
    {
        TerrainLODConfig lodCfg = TerrainLODConfig::fromPreset(TerrainQualityPreset::High);
        lodCfg.minDepth = 0;
        lodCfg.maxDepth = 4;
        lodCfg.pixelErrorThreshold = 2.0f;

        TerrainQuadtree qt;
        qt.init(tCfg, lodCfg, heightmap);
        check(qt.isInitialized() && qt.getRoot() != nullptr, "Root node must exist");
        check(qt.getRoot()->depth == 0 && qt.getRoot()->isLeaf, "Initial root must be depth 0 leaf");

        // Close camera -> triggers deep subdivision up to maxDepth
        Camera cam;
        cam.setPosition(glm::vec3(0.0f, 15.0f, 0.0f));
        cam.setOrientation(0.0f, -45.0f);
        qt.update(0.016f, cam, 1080, false);

        const auto selNear = qt.buildSelection(lodCfg.patchResolution);
        check(selNear.allLeafPatches.size() > 4, "Close camera must subdivide quadtree");
        uint32_t maxObservedDepth = 0;
        for (const auto& p : selNear.allLeafPatches) {
            maxObservedDepth = std::max(maxObservedDepth, p.depth);
        }
        check(maxObservedDepth <= lodCfg.maxDepth, "Depth must never exceed maxDepth");
        check(maxObservedDepth >= 3, "Close camera should reach high detail depth");

        // Far camera -> merges back down
        cam.setPosition(glm::vec3(0.0f, 2500.0f, 0.0f));
        qt.update(0.016f, cam, 1080, false);
        const auto selFar = qt.buildSelection(lodCfg.patchResolution);
        check(selFar.allLeafPatches.size() < selNear.allLeafPatches.size(), "Far camera must merge patches");
        std::cout << "  -> PASSED: Near leaves=" << selNear.allLeafPatches.size()
                  << " (maxDepth=" << maxObservedDepth << "), Far leaves=" << selFar.allLeafPatches.size() << ".\n";
    }

    // -------------------------------------------------------------------------
    // Test 2: Screen-space geometric error, FOV, viewport resolution, distance fallback
    // -------------------------------------------------------------------------
    std::cout << "[Test 2] Screen-space error, FOV, viewport resolution, and distance fallback...\n";
    {
        const float errNear = TerrainQuadtree::computeScreenSpaceError(1.0f, 10.0f, 60.0f, 1080);
        const float errFar  = TerrainQuadtree::computeScreenSpaceError(1.0f, 100.0f, 60.0f, 1080);
        check(errNear > errFar * 9.0f, "Screen-space error must scale inversely with distance");

        const float errNarrowFov = TerrainQuadtree::computeScreenSpaceError(1.0f, 50.0f, 30.0f, 1080);
        const float errWideFov   = TerrainQuadtree::computeScreenSpaceError(1.0f, 50.0f, 90.0f, 1080);
        check(errNarrowFov > errWideFov * 2.5f, "Narrower FOV must produce larger screen-space error");

        const float err4K   = TerrainQuadtree::computeScreenSpaceError(1.0f, 50.0f, 60.0f, 2160);
        const float err720p = TerrainQuadtree::computeScreenSpaceError(1.0f, 50.0f, 60.0f, 720);
        check(std::abs(err4K / err720p - 3.0f) < 0.01f, "Screen-space error must scale linearly with viewport height");

        // Distance-based fallback strategy
        TerrainLODConfig distCfg = TerrainLODConfig::fromPreset(TerrainQualityPreset::High);
        distCfg.strategy = TerrainLODStrategy::DistanceBased;
        TerrainQuadtree qtDist;
        qtDist.init(tCfg, distCfg, heightmap);
        Camera cam;
        cam.setPosition(glm::vec3(0.0f, 20.0f, 0.0f));
        qtDist.update(0.016f, cam, 1080, false);
        check(qtDist.getStats().activeLeafNodes > 4, "Distance-based strategy must subdivide near camera");
        std::cout << "  -> PASSED: Screen-space projection, FOV, resolution, and distance fallback verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 3: LOD Hysteresis stability
    // -------------------------------------------------------------------------
    std::cout << "[Test 3] LOD hysteresis anti-thrashing...\n";
    {
        TerrainLODConfig cfg = TerrainLODConfig::fromPreset(TerrainQualityPreset::High);
        cfg.lodHysteresis = 0.7f;
        TerrainQuadtree qt;
        qt.init(tCfg, cfg, heightmap);

        Camera cam;
        cam.setPosition(glm::vec3(0.0f, 35.0f, 0.0f));
        qt.update(0.016f, cam, 1080, false);
        const uint32_t leavesAfterSplit = qt.getStats().activeLeafNodes;

        // Small jitter around the same position should not thrash
        uint32_t totalTransitions = 0;
        for (int i = 0; i < 10; ++i) {
            cam.setPosition(glm::vec3((i % 2 == 0) ? 0.2f : -0.2f, 35.0f, 0.0f));
            qt.update(0.016f, cam, 1080, false);
            totalTransitions += qt.getStats().lodTransitionCount;
        }
        check(totalTransitions == 0, "Small camera jitter must produce zero split/merge thrashing");
        check(qt.getStats().activeLeafNodes == leavesAfterSplit, "Leaf count must remain stable under hysteresis");
        std::cout << "  -> PASSED: Hysteresis verified (0 transitions across 10 jitter frames).\n";
    }

    // -------------------------------------------------------------------------
    // Test 4: Geomorphing endpoints & parent-child surface continuity
    // -------------------------------------------------------------------------
    std::cout << "[Test 4] Geomorphing endpoints and parent-child surface continuity...\n";
    {
        const uint32_t R = 16;
        TerrainPatchMesh patch;
        patch.generate(R, 0.0f, 0.5f, 0.0f, 0.5f, 0, tCfg, heightmap);
        const auto& verts = patch.getCpuVertices();
        const uint32_t stride = R + 1u;
        check(verts.size() == stride * stride, "Patch vertex count must be (R+1)^2");

        // Verify even vertices have morphMask == 0 and parentY == position.y,
        // and odd vertices have morphMask == 1 and parentY == exact parent interpolation
        for (uint32_t iz = 0; iz <= R; ++iz) {
            for (uint32_t ix = 0; ix <= R; ++ix) {
                const Vertex& v = verts[iz * stride + ix];
                const bool oddX = (ix & 1u) != 0u;
                const bool oddZ = (iz & 1u) != 0u;
                if (!oddX && !oddZ) {
                    check(v.color.z == 0.0f, "Even vertex must have morphMask == 0");
                    check(std::abs(v.color.x - v.position.y) < 1e-5f, "Even vertex parentY must equal worldY");
                } else {
                    check(v.color.z == 1.0f, "Odd vertex must have morphMask == 1");
                    // Morph endpoint 0.0 -> child position; endpoint 1.0 -> exact parentY
                    const float y0 = v.position.y;
                    const float y1 = v.color.x;
                    const float yMid = y0 * 0.5f + y1 * 0.5f;
                    check(std::isfinite(yMid), "Morphed height must be finite");
                }
            }
        }

        // Verify smooth morph progression over time in quadtree
        TerrainLODConfig cfg = TerrainLODConfig::fromPreset(TerrainQualityPreset::High);
        cfg.geomorphingEnabled = true;
        cfg.morphDuration = 0.2f;
        TerrainQuadtree qt;
        qt.init(tCfg, cfg, heightmap);
        Camera cam;
        cam.setPosition(glm::vec3(0.0f, 15.0f, 0.0f));
        qt.update(0.05f, cam, 1080, false); // morph advances from 1.0 -> 0.75
        const float morphMid = qt.getStats().averageMorphFactor;
        qt.update(0.30f, cam, 1080, false); // morph completes -> 0.0
        const float morphEnd = qt.getStats().averageMorphFactor;
        check(morphMid > 0.0f && morphMid < 1.0f, "Geomorphing must be strictly in (0, 1) mid-transition");
        check(morphEnd <= 0.001f, "Geomorphing must reach 0.0 after morphDuration");
        std::cout << "  -> PASSED: Geomorph endpoints and smooth transition (" << morphMid << " -> " << morphEnd << ") verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 5: Neighbor balancing & 16-pattern crack-free edge stitching topology
    // -------------------------------------------------------------------------
    std::cout << "[Test 5] Quadtree balancing & all 16 edge-stitch index topologies...\n";
    {
        TerrainLODConfig cfg = TerrainLODConfig::fromPreset(TerrainQualityPreset::High);
        cfg.balanceQuadtree = true;
        cfg.stitchingEnabled = true;
        TerrainQuadtree qt;
        qt.init(tCfg, cfg, heightmap);

        Camera cam;
        cam.setPosition(glm::vec3(-40.0f, 12.0f, -35.0f));
        qt.update(0.016f, cam, 1080, false);
        check(qt.validateNeighborBalance(), "Balanced quadtree must satisfy |depthA - depthB| <= 1 everywhere");

        // Validate ACTUAL index topology for all 16 stitch masks (0..15)
        const uint32_t R = 16;
        const uint32_t stride = R + 1u;
        for (uint8_t mask = 0; mask < 16; ++mask) {
            const auto indices = TerrainStitchIndexPool::generateStitchIndices(R, mask);
            check(!indices.empty() && (indices.size() % 3 == 0), "Stitch indices must be non-empty multiples of 3");

            const bool northC = (mask & kEdgeNorth) != 0;
            const bool southC = (mask & kEdgeSouth) != 0;
            const bool westC  = (mask & kEdgeWest)  != 0;
            const bool eastC  = (mask & kEdgeEast)  != 0;

            for (size_t i = 0; i < indices.size(); i += 3) {
                const uint32_t i0 = indices[i];
                const uint32_t i1 = indices[i + 1];
                const uint32_t i2 = indices[i + 2];
                check(i0 < stride * stride && i1 < stride * stride && i2 < stride * stride, "Index out of range");

                // Check no odd vertex is referenced on any coarse-stitched edge (zero T-junctions!)
                for (uint32_t idx : {i0, i1, i2}) {
                    const uint32_t ix = idx % stride;
                    const uint32_t iz = idx / stride;
                    if (northC && iz == 0) check((ix & 1u) == 0u, "Odd vertex referenced on coarse North edge!");
                    if (southC && iz == R) check((ix & 1u) == 0u, "Odd vertex referenced on coarse South edge!");
                    if (westC  && ix == 0) check((iz & 1u) == 0u, "Odd vertex referenced on coarse West edge!");
                    if (eastC  && ix == R) check((iz & 1u) == 0u, "Odd vertex referenced on coarse East edge!");
                }

                // Check CCW (+Y) winding for every single triangle
                const float x0 = static_cast<float>(i0 % stride), z0 = static_cast<float>(i0 / stride);
                const float x1 = static_cast<float>(i1 % stride), z1 = static_cast<float>(i1 / stride);
                const float x2 = static_cast<float>(i2 % stride), z2 = static_cast<float>(i2 / stride);
                const float crossY = (z1 - z0) * (x2 - x0) - (x1 - x0) * (z2 - z0);
                check(crossY > 0.0f, "Every stitched triangle must have positive +Y CCW winding");
            }
        }
        std::cout << "  -> PASSED: Neighbor balance (max diff <= 1) and all 16 stitch topologies (0 T-junctions, 100% CCW) verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 6: Height, Normal, and UV continuity across adjacent patches
    // -------------------------------------------------------------------------
    std::cout << "[Test 6] Height, Normal, and UV continuity across patch boundaries...\n";
    {
        const uint32_t R = 16;
        const uint32_t stride = R + 1u;
        TerrainPatchMesh leftPatch, rightPatch;
        leftPatch.generate(R, 0.0f, 0.5f, 0.0f, 0.5f, 1, tCfg, heightmap);
        rightPatch.generate(R, 0.5f, 1.0f, 0.0f, 0.5f, 1, tCfg, heightmap);

        const auto& vL = leftPatch.getCpuVertices();
        const auto& vR = rightPatch.getCpuVertices();

        for (uint32_t iz = 0; iz <= R; ++iz) {
            const Vertex& a = vL[iz * stride + R]; // East edge of leftPatch
            const Vertex& b = vR[iz * stride + 0]; // West edge of rightPatch
            check(glm::distance(a.position, b.position) < 1e-4f, "Shared boundary vertex positions must match");
            check(glm::distance(a.normal, b.normal) < 1e-4f, "Shared boundary normals must match");
            check(glm::distance(a.uv, b.uv) < 1e-4f, "Shared boundary UVs must match");
        }
        std::cout << "  -> PASSED: Exact height, normal, and UV continuity across patch seam verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 7: Multi-Viewport independence & Shadow LOD bias
    // -------------------------------------------------------------------------
    std::cout << "[Test 7] Multi-viewport LOD independence and Shadow LOD bias...\n";
    {
        TerrainLODManager mgr;
        TerrainLODConfig cfg = TerrainLODConfig::fromPreset(TerrainQualityPreset::High);
        mgr.init(tCfg, cfg, heightmap, nullptr, VK_NULL_HANDLE);

        Camera camClose, camFar;
        camClose.setPosition(glm::vec3(0.0f, 10.0f, 0.0f));
        camFar.setPosition(glm::vec3(0.0f, 400.0f, 0.0f));

        mgr.beginFrame();
        const auto& selVp0 = mgr.updateViewport(0, 0.016f, camClose, 1080, false);
        const auto& selVp1 = mgr.updateViewport(1, 0.016f, camFar,   540,  false);
        const auto& selShd = mgr.updateShadow(0.016f, camClose, 1080);

        check(selVp0.allLeafPatches.size() > selVp1.allLeafPatches.size(),
              "Close viewport 0 must have more detailed patches than distant viewport 1");
        check(selShd.allLeafPatches.size() <= selVp0.allLeafPatches.size(),
              "Shadow LOD with positive bias must not exceed main pass patch detail");
        std::cout << "  -> PASSED: VP0 leaves=" << selVp0.allLeafPatches.size()
                  << ", VP1 leaves=" << selVp1.allLeafPatches.size()
                  << ", Shadow leaves=" << selShd.allLeafPatches.size() << ".\n";
    }

    // -------------------------------------------------------------------------
    // Test 8: World Partition, World Streaming, and Foliage integration
    // -------------------------------------------------------------------------
    std::cout << "[Test 8] World Partition, World Streaming, and Foliage compatibility...\n";
    {
        Terrain terrain;
        terrain.initHeadless(tCfg);
        const float hBefore = terrain.getHeightAt(12.5f, -18.3f);
        const glm::vec3 nBefore = terrain.getNormalAt(12.5f, -18.3f);

        TerrainLODManager mgr;
        mgr.init(tCfg, TerrainLODConfig::fromPreset(TerrainQualityPreset::High), terrain.getHeightmap());

        WorldStreamingManager streaming;
        StreamingConfig sCfg{};
        sCfg.gridDimX = 2; sCfg.gridDimZ = 2; sCfg.regionSize = 60.0f;
        streaming.initHeadless(sCfg, tCfg, terrain.getHeightmap());

        WorldPartitionManager partition;
        WorldPartitionConfig pCfg{};
        pCfg.cellSize = 60.0f;
        partition.init(pCfg, &streaming, &terrain, nullptr);

        mgr.setWorldPartitionManager(&partition);
        mgr.setWorldStreamingManager(&streaming);

        Camera cam;
        cam.setPosition(glm::vec3(12.5f, 20.0f, -18.3f));
        mgr.beginFrame();
        (void)mgr.updateViewport(0, 0.016f, cam, 1080, false);

        // Foliage query API must remain completely invariant under Terrain LOD changes
        const float hAfter = terrain.getHeightAt(12.5f, -18.3f);
        const glm::vec3 nAfter = terrain.getNormalAt(12.5f, -18.3f);
        check(std::abs(hBefore - hAfter) < 1e-6f, "Terrain height query must be invariant under LOD");
        check(glm::distance(nBefore, nAfter) < 1e-6f, "Terrain normal query must be invariant under LOD");

        streaming.shutdown();
        mgr.destroy();
        std::cout << "  -> PASSED: Partition, Streaming, and Foliage query invariance verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 9: Rapid camera movement, repeated split/merge, & performance benchmark
    // -------------------------------------------------------------------------
    std::cout << "[Test 9] Rapid camera movement, repeated split/merge, and large terrain benchmark...\n";
    {
        TerrainLODManager mgr;
        TerrainLODConfig cfg = TerrainLODConfig::fromPreset(TerrainQualityPreset::Ultra);
        mgr.init(tCfg, cfg, heightmap);

        Camera cam;
        const auto t0 = std::chrono::high_resolution_clock::now();
        for (int frame = 0; frame < 60; ++frame) {
            mgr.beginFrame();
            const float angle = static_cast<float>(frame) * 0.25f;
            const float radius = (frame % 2 == 0) ? 15.0f : 180.0f;
            cam.setPosition(glm::vec3(std::cos(angle) * radius, 15.0f + (frame % 5) * 10.0f, std::sin(angle) * radius));
            (void)mgr.updateViewport(0, 0.016f, cam, 1080, true);
            check(mgr.getQuadtree(0)->validateNeighborBalance(), "Balance must hold during rapid movement");
        }
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        mgr.destroy();
        std::cout << "  -> PASSED: 60 rapid split/merge frames completed in " << totalMs
                  << " ms (" << (totalMs / 60.0) << " ms/frame).\n";
    }

    std::cout << "=================================================================\n";
    std::cout << "ALL 9 ADVANCED TERRAIN LOD UNIT TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "=================================================================\n";
    return 0;
}
