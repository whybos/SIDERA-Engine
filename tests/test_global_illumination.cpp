#include "renderer/gi/GlobalIlluminationSettings.h"
#include "renderer/gi/GlobalIlluminationPipeline.h"
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

static void testHemisphereAndMaterialMath() {
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    for (int i = 0; i < 8; ++i) {
        const float u1 = (static_cast<float>(i) + 0.5f) / 8.0f;
        const float u2 = std::fmod(u1 * 1.6180339f, 1.0f);
        const glm::vec3 dir = evalCosineHemisphereSample(u1, u2, up);
        CHECK(std::abs(glm::length(dir) - 1.0f) < 1e-4f, "Hemisphere sample is unit length");
        CHECK(glm::dot(dir, up) >= -1e-4f, "Hemisphere sample lies in upper hemisphere");
    }

    // Energy-conserving diffuse indirect response: dielectric vs pure metal
    const glm::vec3 incoming(2.0f, 1.0f, 0.5f);
    const glm::vec3 albedo(0.8f, 0.2f, 0.2f);
    const glm::vec3 dielectric = evalDiffuseIndirectContribution(incoming, albedo, 0.0f, 1.0f);
    const glm::vec3 metal = evalDiffuseIndirectContribution(incoming, albedo, 1.0f, 1.0f);
    CHECK(dielectric.r > dielectric.g && dielectric.r > 0.0f, "Dielectric receives color-bled indirect diffuse");
    CHECK(glm::length(metal) < 1e-6f, "Pure metallic surface receives zero diffuse GI");

    // Temporal depth & normal rejection
    CHECK(!evalTemporalDepthRejection(10.0f, 10.2f, 0.05f), "Small depth delta accepted");
    CHECK(evalTemporalDepthRejection(10.0f, 12.0f, 0.05f), "Large depth delta rejected");
    CHECK(!evalTemporalNormalRejection(glm::vec3(0, 1, 0), glm::vec3(0.1f, 0.99f, 0), 0.85f), "Aligned normal accepted");
    CHECK(evalTemporalNormalRejection(glm::vec3(0, 1, 0), glm::vec3(1, 0, 0), 0.85f), "Perpendicular normal rejected");
}

static void testSettingsAndPresets() {
    GlobalIlluminationSettings s{};
    s.rayCount = 999u;
    s.temporalBlend = 2.0f;
    s.resolutionScale = 0.01f;
    s.validate();
    CHECK(s.rayCount <= 16u, "Ray count clamped <= 16");
    CHECK(s.temporalBlend <= 0.96f, "Temporal blend clamped <= 0.96");
    CHECK(s.resolutionScale >= 0.25f, "Resolution scale clamped >= 0.25");

    s.applyQualityPreset(GIQualityPreset::Low);
    CHECK(s.resolutionScale == 0.25f && s.rayCount == 2u, "Low preset sets quarter-res / 2 rays");
    s.applyQualityPreset(GIQualityPreset::Ultra);
    CHECK(s.resolutionScale == 0.75f && s.rayCount == 8u && s.maxSteps == 32u, "Ultra preset sets 0.75x / 8 rays / 32 steps");
}

static void testPipelineLifecycleAndMultiViewport() {
    GlobalIlluminationPipeline gi;
    CHECK(gi.init(1920, 1080, nullptr), "CPU-mode GlobalIlluminationPipeline::init succeeds");
    CHECK(gi.isInitialized(), "GI pipeline reports initialized");
    CHECK(gi.getStats().giWidth == 960u && gi.getStats().giHeight == 540u, "Medium preset 1920x1080 -> 960x540");
    CHECK(gi.getStats().hiZMipLevels == computeHiZMipLevels(1920, 1080), "Hi-Z mip count matches");
    CHECK(gi.getStats().gpuMemoryBytes > 0u, "GI GPU memory footprint computed");
    CHECK(gi.getCapabilities().ssgiSupported, "SSGI supported without requiring HW ray tracing");

    CHECK(gi.resize(1280, 720), "GI resize succeeds");
    CHECK(gi.getStats().giWidth == 640u && gi.getStats().giHeight == 360u, "1280x720 -> 640x360");

    // Multi-viewport independent history
    Camera camA;
    camA.setPosition(glm::vec3(0.0f, 5.0f, 0.0f));
    Camera camB;
    camB.setPosition(glm::vec3(200.0f, 5.0f, 0.0f));
    CHECK(!gi.checkAndUpdateViewportHistory(0u, camA), "Viewport 0 initial history invalid");
    CHECK(!gi.checkAndUpdateViewportHistory(1u, camB), "Viewport 1 initial history invalid");

    gi.destroy();
    CHECK(!gi.isInitialized(), "GI destroy clears initialized flag");
}

int main() {
    testHemisphereAndMaterialMath();
    testSettingsAndPresets();
    testPipelineLifecycleAndMultiViewport();

    std::cout << "test_global_illumination: " << g_passed << " passed, " << g_failed << " failed.\n";
    return (g_failed == 0) ? 0 : 1;
}
