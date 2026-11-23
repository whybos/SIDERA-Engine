#include "renderer/postprocess/PostProcessPipeline.h"
#include "renderer/postprocess/PostProcessSettings.h"

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
    std::cout << "SIDERA Engine — Milestone 29: Advanced Post Processing Unit Tests\n";
    std::cout << "=================================================================\n";

    // -------------------------------------------------------------------------
    // Test 1: Settings defaults, validation clamping, and EV exposure math
    // -------------------------------------------------------------------------
    std::cout << "[Test 1] PostProcessSettings defaults, clamping, and EV exposure...\n";
    {
        PostProcessSettings s{};
        check(s.enabled && s.hdrEnabled && s.bloomEnabled && s.ssaoEnabled, "Defaults must be enabled");
        check(s.toneMapOperator == ToneMapOperator::ACES, "Default operator must be ACES");
        check(std::abs(s.getEffectiveExposure() - 1.0f) < 1e-5f, "Default exposure must be 1.0");

        s.exposure = 1.0f;
        s.exposureCompensation = 2.0f; // +2 EV = 4x
        check(std::abs(s.getEffectiveExposure() - 4.0f) < 1e-4f, "+2 EV must quadruple exposure");

        s.bloomThreshold = -5.0f;
        s.ssaoSampleCount = 128u;
        s.exposure = 999.0f;
        s.validate();
        check(s.bloomThreshold == 0.0f && s.ssaoSampleCount == 32u && s.exposure == 16.0f,
              "validate() must clamp out-of-range parameters safely");
        std::cout << "  -> PASSED: Defaults, EV exposure, and parameter clamping verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 2: Tone mapping (ACES, Reinhard) & soft-knee Bloom bright-pass math
    // -------------------------------------------------------------------------
    std::cout << "[Test 2] ACES/Reinhard tone mapping & soft-knee Bloom bright-pass...\n";
    {
        const glm::vec3 dark(0.2f, 0.2f, 0.2f);
        const glm::vec3 hdrBright(5.0f, 3.0f, 1.5f);

        const glm::vec3 acesDark   = PostProcessSettings::evalToneMapACES(dark);
        const glm::vec3 acesBright = PostProcessSettings::evalToneMapACES(hdrBright);
        check(acesBright.r > acesDark.r && acesBright.r <= 1.0f, "ACES must compress HDR > 1.0 into [0,1]");

        const glm::vec3 rein = PostProcessSettings::evalToneMapReinhard(glm::vec3(1.0f));
        check(std::abs(rein.r - 0.5f) < 1e-5f, "Reinhard(1.0) must equal 0.5");

        const glm::vec3 bpDark   = PostProcessSettings::evalBrightPass(glm::vec3(0.2f), 1.0f, 0.5f);
        const glm::vec3 bpBright = PostProcessSettings::evalBrightPass(glm::vec3(3.0f), 1.0f, 0.5f);
        check(bpDark.r < 1e-5f && bpBright.r > 1.5f, "Bright-pass must reject dark pixels and extract HDR > threshold");
        std::cout << "  -> PASSED: ACES, Reinhard, and soft-knee Bloom bright-pass verified.\n";
    }

    // -------------------------------------------------------------------------
    // Test 3: HDR format selection, pipeline resize, and multi-viewport independence
    // -------------------------------------------------------------------------
    std::cout << "[Test 3] HDR format selection, resize, and multi-viewport independence...\n";
    {
        check(PostProcessPipeline::selectBestHDRFormat(VK_NULL_HANDLE) == VK_FORMAT_R16G16B16A16_SFLOAT,
              "Preferred HDR format must be VK_FORMAT_R16G16B16A16_SFLOAT");

        PostProcessPipeline vp0, vp1;
        vp0.init(1920, 1080, nullptr);
        vp1.init(960, 540, nullptr);
        check(vp0.getStats().width == 1920u && vp0.getStats().bloomWidth == 960u, "VP0 half-res bloom/SSAO must be 960x540");
        check(vp1.getStats().width == 960u && vp1.getStats().bloomWidth == 480u, "VP1 half-res bloom/SSAO must be 480x270");

        vp0.resize(2560, 1440);
        check(vp0.getStats().width == 2560u && vp1.getStats().width == 960u,
              "Resizing VP0 must not affect VP1 resources");
        std::cout << "  -> PASSED: HDR format, resize, and multi-viewport independence verified.\n";
    }

    std::cout << "=================================================================\n";
    std::cout << "ALL 3 ADVANCED POST PROCESSING UNIT TESTS PASSED!\n";
    std::cout << "=================================================================\n";
    return 0;
}
