#include "renderer/reflection/ReflectionSettings.h"
#include "renderer/reflection/ReflectionPipeline.h"
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

static void testReflectionMath() {
    // 1. Reflection vector: 45 deg incident -> 45 deg reflected
    const glm::vec3 V = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
    const glm::vec3 N(0.0f, 1.0f, 0.0f);
    const glm::vec3 R = evalReflectionVector(V, N);
    CHECK(std::abs(R.x + V.x) < 1e-4f && std::abs(R.y - V.y) < 1e-4f, "Reflection vector mirrors across normal");

    // 2. Fresnel-Schlick: cosTheta=1 -> F0, cosTheta=0 -> 1.0
    const glm::vec3 f0(0.04f);
    const glm::vec3 fNormal = evalFresnelSchlick(1.0f, f0);
    const glm::vec3 fGrazing = evalFresnelSchlick(0.0f, f0);
    CHECK(std::abs(fNormal.r - 0.04f) < 1e-4f, "Fresnel at normal incidence == F0");
    CHECK(std::abs(fGrazing.r - 1.0f) < 1e-4f, "Fresnel at grazing angle == 1.0");

    // 3. Roughness to mip level
    CHECK(std::abs(evalRoughnessToMip(0.0f, 5u) - 0.0f) < 1e-4f, "Roughness 0 -> mip 0");
    CHECK(std::abs(evalRoughnessToMip(1.0f, 5u) - 4.0f) < 1e-4f, "Roughness 1 -> max mip");

    // 4. Box projection & probe influence
    ReflectionProbe probe{};
    probe.positionRadius = glm::vec4(0.0f, 5.0f, 0.0f, 30.0f);
    probe.boxMinPriority = glm::vec4(-20.0f, 0.0f, -20.0f, 1.0f);
    probe.boxMaxBlend = glm::vec4(20.0f, 20.0f, 20.0f, 10.0f);
    probe.params = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);

    const glm::vec3 bpDir = evalBoxProjectedDirection(glm::vec3(5.0f, 2.0f, 0.0f), glm::vec3(1.0f, 0.5f, 0.0f),
                                                      glm::vec3(probe.positionRadius),
                                                      glm::vec3(probe.boxMinPriority),
                                                      glm::vec3(probe.boxMaxBlend));
    CHECK(std::abs(glm::length(bpDir) - 1.0f) < 1e-4f, "Box-projected direction is unit length");
    CHECK(evalProbeInfluenceWeight(glm::vec3(0.0f, 5.0f, 0.0f), probe) == 1.0f, "Probe center weight == 1.0");
    CHECK(evalProbeInfluenceWeight(glm::vec3(50.0f, 5.0f, 0.0f), probe) == 0.0f, "Outside probe radius weight == 0.0");

    // 5. SSR confidence: center sharp hit > edge hit; rough >= maxRoughness -> 0
    const float cCenter = evalSSRConfidence(glm::vec2(0.5f, 0.5f), 5.0f, 48.0f, 0.1f, 0.65f);
    const float cEdge = evalSSRConfidence(glm::vec2(0.02f, 0.5f), 5.0f, 48.0f, 0.1f, 0.65f);
    const float cRough = evalSSRConfidence(glm::vec2(0.5f, 0.5f), 5.0f, 48.0f, 0.8f, 0.65f);
    CHECK(cCenter > cEdge && cEdge > 0.0f, "SSR confidence fades smoothly at screen edges");
    CHECK(cRough == 0.0f, "SSR confidence == 0 above maxRoughness");
}

static void testSettingsAndPresets() {
    ReflectionSettings s{};
    s.maxSteps = 999u;
    s.binarySearchSteps = 99u;
    s.temporalBlend = 1.5f;
    s.validate();
    CHECK(s.maxSteps <= 64u, "maxSteps clamped <= 64");
    CHECK(s.binarySearchSteps <= 8u, "binarySearchSteps clamped <= 8");
    CHECK(s.temporalBlend <= 0.95f, "temporalBlend clamped <= 0.95");

    s.applyQualityPreset(ReflectionQualityPreset::Low);
    CHECK(s.resolutionScale == 0.25f && s.maxSteps == 16u, "Low preset sets 0.25x / 16 steps");
    s.applyQualityPreset(ReflectionQualityPreset::Ultra);
    CHECK(s.resolutionScale == 1.0f && s.maxSteps == 48u && s.binarySearchSteps == 8u, "Ultra preset sets 1.0x / 48 steps / 8 binary");
}

static void testPipelineLifecycleAndMultiViewport() {
    ReflectionPipeline rp;
    CHECK(rp.init(1920, 1080, nullptr), "CPU-mode ReflectionPipeline::init succeeds");
    CHECK(rp.isInitialized(), "ReflectionPipeline reports initialized");
    CHECK(rp.getStats().ssrWidth == 960u && rp.getStats().ssrHeight == 540u, "Medium preset 1920x1080 -> 960x540");
    CHECK(rp.getStats().gpuMemoryBytes > 0u, "Reflection GPU memory footprint computed");

    // Probe registration & removal
    ReflectionProbe p{};
    p.positionRadius = glm::vec4(25.0f, 5.0f, -15.0f, 30.0f);
    const uint32_t idx = rp.registerProbe(p);
    CHECK(idx != 0xFFFFFFFFu && rp.getProbes().size() >= 2u, "Probe registered");
    rp.removeProbe(idx);
    CHECK(rp.getProbes().size() == 1u, "Probe removed");

    // Resize & multi-viewport independence
    CHECK(rp.resize(1280, 720), "ReflectionPipeline::resize succeeds");
    CHECK(rp.getStats().ssrWidth == 640u && rp.getStats().ssrHeight == 360u, "1280x720 -> 640x360");

    Camera camA, camB;
    camA.setPosition(glm::vec3(0.0f, 5.0f, 0.0f));
    camB.setPosition(glm::vec3(150.0f, 5.0f, 0.0f));
    CHECK(!rp.checkAndUpdateViewportHistory(0u, camA), "Viewport 0 initial history invalid");
    CHECK(!rp.checkAndUpdateViewportHistory(1u, camB), "Viewport 1 initial history invalid");

    rp.destroy();
    CHECK(!rp.isInitialized(), "ReflectionPipeline::destroy clears initialized flag");
}

int main() {
    testReflectionMath();
    testSettingsAndPresets();
    testPipelineLifecycleAndMultiViewport();

    std::cout << "test_reflection_system: " << g_passed << " passed, " << g_failed << " failed.\n";
    return (g_failed == 0) ? 0 : 1;
}
