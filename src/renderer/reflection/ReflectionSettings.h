#pragma once

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sidera {

enum class ReflectionQualityPreset : uint32_t {
    Low = 0,
    Medium = 1,
    High = 2,
    Ultra = 3
};

enum class ReflectionDebugMode : uint32_t {
    Final = 0,
    RawSSR = 1,
    SSRConfidence = 2,
    ProbeContribution = 3,
    EnvFallback = 4,
    DenoisedSSR = 5,
    HybridReflection = 6
};

// std430-aligned GPU reflection probe (64 bytes)
struct alignas(16) ReflectionProbe {
    glm::vec4 positionRadius{0.0f, 5.0f, 0.0f, 40.0f};     // xyz = center, w = radius
    glm::vec4 boxMinPriority{-30.0f, 0.0f, -30.0f, 0.0f};  // xyz = boxMin, w = priority
    glm::vec4 boxMaxBlend{30.0f, 25.0f, 30.0f, 10.0f};     // xyz = boxMax, w = blendDistance
    glm::vec4 params{1.0f, 1.0f, 1.0f, 0.0f};              // x = intensity, y = enabled, z = boxProjected, w = probeId
};
static_assert(sizeof(ReflectionProbe) == 64, "ReflectionProbe must be 64 bytes (std430).");

struct ReflectionSettings {
    bool enabled = true;
    bool ssrEnabled = true;
    bool probesEnabled = true;
    bool envFallbackEnabled = true;
    ReflectionQualityPreset qualityPreset = ReflectionQualityPreset::Medium;

    // SSR configuration
    float resolutionScale = 0.5f;
    uint32_t maxSteps = 24;
    uint32_t binarySearchSteps = 4;
    float maxRayDistance = 48.0f;
    float rayThickness = 0.65f;
    float maxRoughness = 0.65f;
    float confidenceThreshold = 0.1f;

    // Temporal & denoising
    bool temporalEnabled = true;
    float temporalBlend = 0.85f;
    float cameraCutThreshold = 15.0f;
    bool denoiseEnabled = true;
    uint32_t denoiseRadius = 2;

    // Blending & probes
    float reflectionIntensity = 1.0f;
    float probeIntensity = 1.0f;
    uint32_t probeUpdateBudget = 1; // max probes updated per frame

    ReflectionDebugMode debugMode = ReflectionDebugMode::Final;

    void applyQualityPreset(ReflectionQualityPreset preset) noexcept {
        qualityPreset = preset;
        switch (preset) {
            case ReflectionQualityPreset::Low:
                resolutionScale = 0.25f;
                maxSteps = 16;
                binarySearchSteps = 2;
                denoiseRadius = 1;
                break;
            case ReflectionQualityPreset::Medium:
                resolutionScale = 0.5f;
                maxSteps = 24;
                binarySearchSteps = 4;
                denoiseRadius = 2;
                break;
            case ReflectionQualityPreset::High:
                resolutionScale = 0.5f;
                maxSteps = 32;
                binarySearchSteps = 6;
                denoiseRadius = 2;
                break;
            case ReflectionQualityPreset::Ultra:
                resolutionScale = 1.0f;
                maxSteps = 48;
                binarySearchSteps = 8;
                denoiseRadius = 3;
                break;
        }
        validate();
    }

    void validate() noexcept {
        resolutionScale = std::clamp(resolutionScale, 0.25f, 1.0f);
        maxSteps = std::clamp(maxSteps, 4u, 64u);
        binarySearchSteps = std::clamp(binarySearchSteps, 1u, 8u);
        maxRayDistance = std::clamp(maxRayDistance, 2.0f, 300.0f);
        rayThickness = std::clamp(rayThickness, 0.05f, 10.0f);
        maxRoughness = std::clamp(maxRoughness, 0.05f, 1.0f);
        confidenceThreshold = std::clamp(confidenceThreshold, 0.0f, 1.0f);
        temporalBlend = std::clamp(temporalBlend, 0.0f, 0.95f);
        cameraCutThreshold = std::clamp(cameraCutThreshold, 1.0f, 500.0f);
        denoiseRadius = std::clamp(denoiseRadius, 1u, 3u);
        reflectionIntensity = std::clamp(reflectionIntensity, 0.0f, 5.0f);
        probeIntensity = std::clamp(probeIntensity, 0.0f, 5.0f);
        probeUpdateBudget = std::clamp(probeUpdateBudget, 1u, 8u);
    }
};

struct ReflectionStatistics {
    uint32_t ssrWidth = 0;
    uint32_t ssrHeight = 0;
    uint32_t activeProbes = 0;
    uint64_t gpuMemoryBytes = 0;
    uint32_t historyResetCount = 0;
    bool historyValid = false;
    bool active = false;
    float traceMs = 0.0f;
    float temporalMs = 0.0f;
    float denoiseMs = 0.0f;
    float compositeMs = 0.0f;
};

// --- CPU Reference Mathematical Functions ---

inline glm::vec3 evalReflectionVector(const glm::vec3& viewDir, const glm::vec3& normal) noexcept {
    return glm::normalize(glm::reflect(-glm::normalize(viewDir), glm::normalize(normal)));
}

inline glm::vec3 evalFresnelSchlick(float cosTheta, const glm::vec3& f0) noexcept {
    const float c = std::clamp(1.0f - std::clamp(cosTheta, 0.0f, 1.0f), 0.0f, 1.0f);
    const float c5 = c * c * c * c * c;
    return f0 + (glm::vec3(1.0f) - f0) * c5;
}

inline float evalRoughnessToMip(float roughness, uint32_t maxMipLevels) noexcept {
    if (maxMipLevels <= 1u) return 0.0f;
    return std::clamp(roughness, 0.0f, 1.0f) * static_cast<float>(maxMipLevels - 1u);
}

inline glm::vec3 evalBoxProjectedDirection(const glm::vec3& worldPos, const glm::vec3& reflDir,
                                           const glm::vec3& probePos, const glm::vec3& boxMin,
                                           const glm::vec3& boxMax) noexcept {
    const glm::vec3 r = glm::normalize(reflDir);
    const glm::vec3 safeR(
        std::abs(r.x) < 1e-4f ? 1e-4f : r.x,
        std::abs(r.y) < 1e-4f ? 1e-4f : r.y,
        std::abs(r.z) < 1e-4f ? 1e-4f : r.z);
    const glm::vec3 tMax = (boxMax - worldPos) / safeR;
    const glm::vec3 tMin = (boxMin - worldPos) / safeR;
    const glm::vec3 tFar = glm::max(tMax, tMin);
    const float tHit = std::min(tFar.x, std::min(tFar.y, tFar.z));
    const glm::vec3 hitPos = worldPos + r * std::max(tHit, 0.0f);
    return glm::normalize(hitPos - probePos);
}

inline float evalProbeInfluenceWeight(const glm::vec3& worldPos, const ReflectionProbe& probe) noexcept {
    if (probe.params.y < 0.5f) return 0.0f;
    const float dist = glm::length(worldPos - glm::vec3(probe.positionRadius));
    const float radius = std::max(probe.positionRadius.w, 0.1f);
    if (dist >= radius) return 0.0f;
    const float blend = std::max(probe.boxMaxBlend.w, 0.1f);
    return std::clamp((radius - dist) / blend, 0.0f, 1.0f);
}

inline float evalSSRConfidence(const glm::vec2& hitUV, float hitDist, float maxDist,
                               float roughness, float maxRoughness) noexcept {
    if (hitUV.x <= 0.0f || hitUV.x >= 1.0f || hitUV.y <= 0.0f || hitUV.y >= 1.0f) return 0.0f;
    if (roughness >= maxRoughness) return 0.0f;
    const float ex = std::min(hitUV.x, 1.0f - hitUV.x) / 0.12f;
    const float ey = std::min(hitUV.y, 1.0f - hitUV.y) / 0.12f;
    const float edge = std::clamp(ex, 0.0f, 1.0f) * std::clamp(ey, 0.0f, 1.0f);
    const float distFade = std::clamp(1.0f - hitDist / std::max(maxDist, 1.0f), 0.0f, 1.0f);
    const float roughFade = std::clamp(1.0f - roughness / std::max(maxRoughness, 0.05f), 0.0f, 1.0f);
    return std::clamp(edge * distFade * roughFade, 0.0f, 1.0f);
}

} // namespace sidera
