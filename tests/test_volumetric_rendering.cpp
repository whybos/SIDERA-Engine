#include "renderer/volumetric/VolumetricSettings.h"
#include "renderer/volumetric/VolumetricPipeline.h"
#include "renderer/Camera.h"

#include <cmath>
#include <iostream>

using namespace sidera;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { ++g_passed; } \
        else { ++g_failed; std::cerr << "[FAIL] " << msg << "\n"; } \
    } while (0)

static void testMathAndScattering() {
    // 1. Beer-Lambert transmittance: T(0) == 1, T(d) monotonically decreases
    const float t0 = evalBeerLambertTransmittance(0.5f, 0.0f);
    const float t1 = evalBeerLambertTransmittance(0.5f, 2.0f);
    const float t2 = evalBeerLambertTransmittance(0.5f, 4.0f);
    CHECK(std::abs(t0 - 1.0f) < 1e-5f, "Beer-Lambert T(0) == 1.0");
    CHECK(std::abs(t1 - std::exp(-1.0f)) < 1e-5f, "Beer-Lambert T(2, 0.5) == exp(-1)");
    CHECK(t2 < t1 && t2 > 0.0f, "Beer-Lambert monotonically decreases");

    // 2. Henyey-Greenstein phase function: isotropic (g=0) == 1/(4*pi), forward scattering peak for g>0
    const float hgIso = evalHenyeyGreenstein(0.5f, 0.0f);
    CHECK(std::abs(hgIso - (1.0f / (4.0f * 3.14159265359f))) < 1e-5f, "HG isotropic == 1/(4*pi)");
    const float hgFwd = evalHenyeyGreenstein(1.0f, 0.7f);
    const float hgBack = evalHenyeyGreenstein(-1.0f, 0.7f);
    CHECK(hgFwd > hgBack && hgFwd > hgIso, "HG forward scattering > isotropic > backscattering");

    // 3. Height fog density falloff & clamping
    const float dLow = evalHeightFogDensity(5.0f, 0.05f, 10.0f, 0.1f, 0.25f);
    const float dHigh = evalHeightFogDensity(30.0f, 0.05f, 10.0f, 0.1f, 0.25f);
    CHECK(std::abs(dLow - 0.05f) < 1e-5f, "Height fog below fogHeight equals baseDensity");
    CHECK(dHigh < dLow && dHigh > 0.0f, "Height fog decays above fogHeight");

    // 4. Logarithmic depth slice distribution
    const float z0 = evalFroxelSliceDepth(0, 64, 0.1f, 160.0f);
    const float zMid = evalFroxelSliceDepth(32, 64, 0.1f, 160.0f);
    const float zEnd = evalFroxelSliceDepth(64, 64, 0.1f, 160.0f);
    CHECK(std::abs(z0 - 0.1f) < 1e-4f, "Froxel slice 0 == nearPlane");
    CHECK(std::abs(zEnd - 160.0f) < 1e-3f, "Froxel slice N == farPlane");
    CHECK(zMid < 80.0f && zMid > 0.1f, "Logarithmic distribution concentrates slices near camera");

    // 5. Front-to-back integration step
    glm::vec3 accumS(0.0f);
    float accumT = 1.0f;
    evalFroxelIntegrationStep(accumS, accumT, glm::vec3(0.2f, 0.3f, 0.4f), 0.1f, 5.0f);
    CHECK(accumT < 1.0f && accumT > 0.0f, "Integration step reduces transmittance");
    CHECK(accumS.z > accumS.y && accumS.y > accumS.x && accumS.x > 0.0f, "Integration step accumulates scattering");
}

static void testSettingsAndQualityPresets() {
    VolumetricSettings s{};
    s.anisotropy = 2.5f;
    s.temporalBlend = 1.5f;
    s.froxelDepthSlices = 999u;
    s.validate();
    CHECK(s.anisotropy <= 0.95f, "Anisotropy clamped <= 0.95");
    CHECK(s.temporalBlend <= 0.98f, "Temporal blend clamped <= 0.98");
    CHECK(s.froxelDepthSlices <= 128u, "Froxel depth slices clamped <= 128");

    s.applyQualityPreset(VolumetricQualityPreset::Low);
    CHECK(s.froxelXYScale == 16u && s.froxelDepthSlices == 32u, "Low preset sets 16px/32 slices");
    s.applyQualityPreset(VolumetricQualityPreset::Ultra);
    CHECK(s.froxelXYScale == 8u && s.froxelDepthSlices == 128u, "Ultra preset sets 8px/128 slices");
}

static void testPipelineLifecycleAndMultiViewport() {
    VolumetricPipeline vp;
    CHECK(vp.init(1920, 1080, nullptr), "CPU-mode VolumetricPipeline::init succeeds");
    CHECK(vp.isInitialized(), "VolumetricPipeline reports initialized");
    CHECK(vp.getStats().froxelX == 120u && vp.getStats().froxelY == 68u && vp.getStats().froxelZ == 64u,
          "Medium preset 1920x1080 -> 120x68x64 froxels");
    CHECK(vp.getStats().gpuMemoryBytes > 0u, "GPU memory footprint computed");

    // Resize updates froxel grid and invalidates history
    CHECK(vp.resize(1280, 720), "VolumetricPipeline::resize succeeds");
    CHECK(vp.getStats().froxelX == 80u && vp.getStats().froxelY == 45u, "1280x720 -> 80x45 froxels");

    // Local fog volumes
    LocalFogVolume box{};
    box.positionType = glm::vec4(10.0f, 5.0f, -8.0f, 1.0f);
    const uint32_t idx = vp.addLocalVolume(box);
    CHECK(idx != 0xFFFFFFFFu && vp.getLocalVolumes().size() >= 2u, "Local fog volume added");

    // Independent multi-viewport camera-cut detection
    Camera camA;
    camA.setPosition(glm::vec3(0.0f, 5.0f, 0.0f));
    Camera camB;
    camB.setPosition(glm::vec3(100.0f, 5.0f, 0.0f));
    CHECK(!vp.checkAndUpdateViewportHistory(0u, camA), "Viewport 0 initial history invalid");
    CHECK(!vp.checkAndUpdateViewportHistory(1u, camB), "Viewport 1 initial history invalid");

    // Format fallback check
    CHECK(VolumetricPipeline::selectBestVolumeFormat(VK_NULL_HANDLE) == VK_FORMAT_R16G16B16A16_SFLOAT,
          "Null physicalDevice defaults to R16G16B16A16_SFLOAT");

    vp.destroy();
    CHECK(!vp.isInitialized(), "VolumetricPipeline::destroy clears initialized flag");
}

int main() {
    testMathAndScattering();
    testSettingsAndQualityPresets();
    testPipelineLifecycleAndMultiViewport();

    std::cout << "test_volumetric_rendering: " << g_passed << " passed, " << g_failed << " failed.\n";
    return (g_failed == 0) ? 0 : 1;
}
