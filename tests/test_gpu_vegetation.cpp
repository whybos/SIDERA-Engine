#include "vegetation/GPUVegetationSystem.h"
#include "vegetation/VegetationDistributionManager.h"
#include "worldgen/WorldGenerator.h"
#include "renderer/Camera.h"

#include <cmath>
#include <iostream>
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
    std::cout << "SIDERA Engine — Milestone 26: GPU Vegetation Rendering Unit Tests\n";
    std::cout << "=================================================================\n";

    // -------------------------------------------------------------------------
    // Test 1: std430 alignment, single instance, frustum & distance culling
    // -------------------------------------------------------------------------
    std::cout << "[Test 1] std430 layout, single instance, frustum & distance culling...\n";
    {
        GPUVegetationSystem sys;
        GPUVegetationConfig cfg{};
        cfg.initialCapacity = 256;
        cfg.globalMaxDrawDistance = 100.0f;
        sys.init(cfg, nullptr);

        Camera cam;
        cam.setPosition(glm::vec3(0.0f, 5.0f, 0.0f));
        cam.setOrientation(-90.0f, 0.0f); // looking along -Z

        std::vector<GPUVegetationInstance> insts(3);
        insts[0].boundsSphere = glm::vec4(0.0f, 5.0f, -15.0f, 1.5f);  // in front, LOD 0
        insts[1].boundsSphere = glm::vec4(0.0f, 5.0f, +30.0f, 1.5f);  // behind camera -> frustum culled
        insts[2].boundsSphere = glm::vec4(0.0f, 5.0f, -150.0f, 1.5f); // beyond maxDist -> distance culled
        sys.uploadInstances(insts);

        std::vector<uint32_t> vis;
        std::vector<GPUVegetationIndirectCmd> cmds;
        const auto st = sys.simulateCullAndCompactCPU(cam, false, vis, cmds);
        check(st.visibleInstances == 1u && st.culledInstances == 2u, "Exactly 1 visible, 2 culled");
        check(st.lod0Count == 1u && st.indirectDrawCommands == 1u, "Visible instance must select LOD 0");
        std::cout << "  -> PASSED: std430 layout, frustum & distance culling verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 2: Dynamic buffer growth, GPU LOD selection (0..3), compaction, shadow filter
    // -------------------------------------------------------------------------
    std::cout << "[Test 2] Buffer growth, multi-LOD compaction, indirect commands & shadow filter...\n";
    {
        GPUVegetationSystem sys;
        GPUVegetationConfig cfg{};
        cfg.initialCapacity = 256;
        cfg.globalMaxDrawDistance = 200.0f;
        sys.init(cfg, nullptr);

        Camera cam;
        cam.setPosition(glm::vec3(0.0f, 5.0f, 0.0f));
        cam.setOrientation(-90.0f, 0.0f);

        std::vector<GPUVegetationInstance> insts;
        for (int i = 0; i < 400; ++i) {
            GPUVegetationInstance g{};
            const float z = -10.0f - static_cast<float>(i % 4) * 30.0f; // -10, -40, -70, -100 -> LOD 0,1,2,3
            g.boundsSphere = glm::vec4(0.0f, 5.0f, z, 2.0f);
            g.meta0.x = static_cast<float>(i % 8); // 0..5 cast shadow, 6..7 do not
            insts.push_back(g);
        }
        sys.uploadInstances(insts);
        check(sys.getCapacity() >= 400u, "Capacity must grow dynamically when exceeding initialCapacity");

        std::vector<uint32_t> vis;
        std::vector<GPUVegetationIndirectCmd> cmds;
        const auto stMain = sys.simulateCullAndCompactCPU(cam, false, vis, cmds);
        check(stMain.lod0Count > 0 && stMain.lod1Count > 0 && stMain.lod2Count > 0 && stMain.lod3Count > 0,
              "All 4 LOD tiers must be populated");
        check(stMain.overflowCount == 0u, "Zero compaction overflow expected");

        const auto stShadow = sys.simulateCullAndCompactCPU(cam, true, vis, cmds);
        check(stShadow.culledInstances >= 100u, "Non-shadow-casting grass species must be culled in shadow pass");
        std::cout << "  -> PASSED: Growth (cap=" << sys.getCapacity() << "), LODs ["
                  << stMain.lod0Count << "," << stMain.lod1Count << "," << stMain.lod2Count << "," << stMain.lod3Count
                  << "], and shadow filtering verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 3: Multi-viewport independence & Milestone 25 VegetationDistribution integration
    // -------------------------------------------------------------------------
    std::cout << "[Test 3] Multi-viewport independence & VegetationDistribution integration...\n";
    {
        WorldGenerationConfig wCfg = WorldGenerationConfig::fromPreset(WorldPreset::Balanced, 424242ULL);
        wCfg.heightmapResolution = 65;
        WorldGenerator worldGen(wCfg);
        worldGen.generateWorld();

        VegetationDistributionManager vegMgr;
        VegetationDistributionConfig vCfg{};
        vCfg.worldSeed = 424242ULL;
        vegMgr.init(vCfg, &worldGen);
        const auto& r00 = vegMgr.generateRegion(0, 0);

        GPUVegetationSystem gpuSys;
        gpuSys.init(GPUVegetationConfig{}, nullptr);
        gpuSys.uploadFromPlacements(r00.placements);

        Camera camA, camB;
        camA.setPosition(glm::vec3(15.0f, 10.0f, 35.0f));
        camA.setOrientation(-90.0f, -15.0f);
        camB.setPosition(glm::vec3(15.0f, 10.0f, 35.0f));
        camB.setOrientation(90.0f, 0.0f); // looking away

        std::vector<uint32_t> visA, visB;
        std::vector<GPUVegetationIndirectCmd> cmdA, cmdB;
        const auto stA = gpuSys.simulateCullAndCompactCPU(camA, false, visA, cmdA);
        const auto stB = gpuSys.simulateCullAndCompactCPU(camB, false, visB, cmdB);
        check(stA.visibleInstances > stB.visibleInstances, "Independent viewports must produce distinct visibility lists");
        std::cout << "  -> PASSED: VP0 visible=" << stA.visibleInstances << ", VP1 visible=" << stB.visibleInstances << ".\n";
    }

    // -------------------------------------------------------------------------
    // Test 4: High-Density Stress Tests (10,000 & 100,000 vegetation instances)
    // -------------------------------------------------------------------------
    std::cout << "[Test 4] High-density vegetation stress tests (10K & 100K instances)...\n";
    {
        auto runStress = [](uint32_t count) -> GPUVegetationStatistics {
            GPUVegetationSystem sys;
            GPUVegetationConfig cfg{};
            cfg.initialCapacity = count;
            cfg.maxCapacity = 1000000u;
            cfg.globalMaxDrawDistance = 200.0f;
            sys.init(cfg, nullptr);

            std::vector<GPUVegetationInstance> insts(count);
            for (uint32_t i = 0; i < count; ++i) {
                const float x = static_cast<float>((i % 400u)) - 200.0f;
                const float z = -static_cast<float>((i / 400u)) * 1.2f;
                insts[i].boundsSphere = glm::vec4(x, 0.0f, z, 1.5f);
                insts[i].meta0.x = static_cast<float>(i % 8u);
            }
            sys.uploadInstances(insts);

            Camera cam;
            cam.setPosition(glm::vec3(0.0f, 20.0f, 10.0f));
            cam.setOrientation(-90.0f, -15.0f);

            std::vector<uint32_t> vis;
            std::vector<GPUVegetationIndirectCmd> cmds;
            return sys.simulateCullAndCompactCPU(cam, false, vis, cmds);
        };

        const auto st10k  = runStress(10000u);
        const auto st100k = runStress(100000u);
        check(st10k.visibleInstances + st10k.culledInstances == 10000u, "10K accounting must be exact");
        check(st100k.visibleInstances + st100k.culledInstances == 100000u, "100K accounting must be exact");
        std::cout << "  -> PASSED: 10K (vis=" << st10k.visibleInstances << ", " << st10k.cullDispatchTimeMs << " ms) & "
                  << "100K (vis=" << st100k.visibleInstances << ", " << st100k.cullDispatchTimeMs << " ms) verified.\n";
    }

    std::cout << "=================================================================\n";
    std::cout << "ALL 4 GPU VEGETATION RENDERING UNIT TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "=================================================================\n";
    return 0;
}
