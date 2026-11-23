#include "renderer/lighting/RenderPath.h"
#include "renderer/lighting/LightTypes.h"
#include "renderer/lighting/TiledLightCuller.h"
#include "renderer/Camera.h"

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
    std::cout << "SIDERA Engine — Milestone 28: Deferred / Forward+ Unit Tests\n";
    std::cout << "=================================================================\n";

    // -------------------------------------------------------------------------
    // Test 1: LightManager CRUD (Directional, Point, Spot) & dirty tracking
    // -------------------------------------------------------------------------
    std::cout << "[Test 1] LightManager CRUD (Directional, Point, Spot) and dirty tracking...\n";
    {
        LightManager lm;
        const uint32_t dId = lm.addDirectionalLight(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f), 2.5f);
        const uint32_t pId = lm.addPointLight(glm::vec3(0.0f, 3.0f, -10.0f), glm::vec3(1.0f, 0.5f, 0.2f), 5.0f, 12.0f);
        const uint32_t sId = lm.addSpotLight(glm::vec3(2.0f, 4.0f, -8.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.2f, 0.8f, 1.0f), 8.0f, 15.0f, 0.3f, 0.5f);
        check(dId == 0 && pId == 1 && sId == 2 && lm.getLightCount() == 3u, "3 lights must be registered");
        check(lm.isDirty(), "LightManager must be dirty after adding lights");
        lm.clearDirty();
        check(!lm.isDirty(), "Dirty flag must clear");

        lm.setLightEnabled(pId, false);
        check(lm.isDirty() && lm.getLights()[pId].spotParams.z == 0.0f, "Disabling light must mark dirty");
        lm.removeLight(sId);
        check(lm.getLightCount() == 2u, "Removing light must decrement count");
        std::cout << "  -> PASSED: Directional, Point, Spot CRUD & dirty tracking verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 2: 16x16 Tiled Light Culling across 1, 16, 64, 128, and 256 light scenes
    // -------------------------------------------------------------------------
    std::cout << "[Test 2] 16x16 tiled light culling (1, 16, 64, 128, 256 lights) & overflow...\n";
    {
        TiledLightCuller culler;
        culler.init(1920, 1080, nullptr);
        check(culler.getTilesX() == 120u && culler.getTilesY() == 68u, "1920x1080 must partition into 120x68 tiles");

        Camera cam;
        cam.setPosition(glm::vec3(0.0f, 8.0f, 25.0f));
        cam.setOrientation(-90.0f, -15.0f);

        std::vector<uint32_t> counts;
        for (uint32_t n : {0u, 16u, 64u, 128u, 256u}) {
            LightManager lm;
            lm.populateDemoLights(n);
            const auto st = culler.simulateCullCPU(cam, lm, counts);
            check(counts.size() == 120u * 68u, "Tile count buffer must cover all tiles");
            check(st.maxLightsInTile <= TiledLightCuller::kMaxLightsPerTile, "Tile light count must never overflow 64");
            check(st.averageLightsPerTile >= 1.0f, "Global directional light must cover all tiles");
            std::cout << "    [" << (n + 1u) << " lights] avg/tile=" << st.averageLightsPerTile
                      << ", max/tile=" << st.maxLightsInTile << ", overflow=" << st.overflowCount
                      << ", time=" << st.cullTimeMs << " ms\n";
        }
        std::cout << "  -> PASSED: 16x16 tiled culling & overflow protection verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 3: Viewport resize & RenderPath switching
    // -------------------------------------------------------------------------
    std::cout << "[Test 3] Viewport resize and RenderPath switching...\n";
    {
        TiledLightCuller culler;
        culler.init(1280, 720, nullptr);
        check(culler.getTilesX() == 80u && culler.getTilesY() == 45u, "720p -> 80x45 tiles");
        culler.resize(2560, 1440);
        check(culler.getTilesX() == 160u && culler.getTilesY() == 90u, "1440p -> 160x90 tiles");
        check(getRenderPathName(RenderPath::Forward) == "Forward", "Forward name");
        check(getRenderPathName(RenderPath::Deferred) == "Deferred", "Deferred name");
        check(getRenderPathName(RenderPath::ForwardPlus) == "Forward+", "Forward+ name");
        std::cout << "  -> PASSED: Viewport resize & RenderPath switching verified.\n";
    }

    std::cout << "=================================================================\n";
    std::cout << "ALL 3 DEFERRED / FORWARD+ RENDERING UNIT TESTS PASSED!\n";
    std::cout << "=================================================================\n";
    return 0;
}
