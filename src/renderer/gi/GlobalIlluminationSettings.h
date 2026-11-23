#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sidera {

enum class GIQualityPreset : uint32_t {
    Low = 0,
    Medium = 1,
    High = 2,
    Ultra = 3
};

enum class GIDebugMode : uint32_t {
    Final = 0,
    RawGI = 1,
    TemporalHistory = 2,
    DenoisedGI = 3,
    Confidence = 4,
    IndirectDiffuse = 5
};

// Queried Vulkan physical device ray-tracing capability flags (SSGI does not require HW RT)
struct GICapabilities {
    bool hasAccelerationStructure = false;
    bool hasRayTracingPipeline = false;
    bool hasRayQuery = false;
    bool hasBufferDeviceAddress = false;
    bool ssgiSupported = true;
};

struct GlobalIlluminationSettings {
    bool enabled = true;
    bool ssgiEnabled = true;
    GIQualityPreset qualityPreset = GIQualityPreset::Medium;

    // Resolution & ray marching
    float resolutionScale = 0.5f;   // 0.25 (quarter) .. 1.0 (full)
    uint32_t rayCount = 4;          // stochastic hemisphere rays per pixel
    uint32_t maxSteps = 16;         // screen-space / Hi-Z march steps
    float maxRayDistance = 24.0f;   // world-space ray length
    float rayThickness = 0.75f;     // depth thickness tolerance

    // Radiance & PBR integration
    float giIntensity = 0.85f;
    float indirectDiffuseIntensity = 1.0f;
    float iblIntensity = 1.0f;
    float emissiveContribution = 1.0f;
    float ssaoModulation = 0.5f;

    // Temporal accumulation
    bool temporalEnabled = true;
    float temporalBlend = 0.88f;
    float depthRejectionThreshold = 0.05f;
    float normalRejectionThreshold = 0.85f;
    float cameraCutThreshold = 15.0f;

    // Edge-aware denoising & upsampling
    bool denoiseEnabled = true;
    uint32_t denoiseRadius = 2; // 1..3
    bool upsampleEnabled = true;

    GIDebugMode debugMode = GIDebugMode::Final;

    void applyQualityPreset(GIQualityPreset preset) noexcept {
        qualityPreset = preset;
        switch (preset) {
            case GIQualityPreset::Low:
                resolutionScale = 0.25f;
                rayCount = 2;
                maxSteps = 12;
                denoiseRadius = 1;
                break;
            case GIQualityPreset::Medium:
                resolutionScale = 0.5f;
                rayCount = 4;
                maxSteps = 16;
                denoiseRadius = 2;
                break;
            case GIQualityPreset::High:
                resolutionScale = 0.5f;
                rayCount = 6;
                maxSteps = 24;
                denoiseRadius = 2;
                break;
            case GIQualityPreset::Ultra:
                resolutionScale = 0.75f;
                rayCount = 8;
                maxSteps = 32;
                denoiseRadius = 3;
                break;
        }
        validate();
    }

    void validate() noexcept {
        resolutionScale = std::clamp(resolutionScale, 0.25f, 1.0f);
        rayCount = std::clamp(rayCount, 1u, 16u);
        maxSteps = std::clamp(maxSteps, 4u, 64u);
        maxRayDistance = std::clamp(maxRayDistance, 1.0f, 200.0f);
        rayThickness = std::clamp(rayThickness, 0.05f, 10.0f);
        giIntensity = std::clamp(giIntensity, 0.0f, 5.0f);
        indirectDiffuseIntensity = std::clamp(indirectDiffuseIntensity, 0.0f, 5.0f);
        iblIntensity = std::clamp(iblIntensity, 0.0f, 5.0f);
        emissiveContribution = std::clamp(emissiveContribution, 0.0f, 5.0f);
        ssaoModulation = std::clamp(ssaoModulation, 0.0f, 1.0f);
        temporalBlend = std::clamp(temporalBlend, 0.0f, 0.96f);
        depthRejectionThreshold = std::clamp(depthRejectionThreshold, 0.001f, 1.0f);
        normalRejectionThreshold = std::clamp(normalRejectionThreshold, 0.0f, 1.0f);
        cameraCutThreshold = std::clamp(cameraCutThreshold, 1.0f, 500.0f);
        denoiseRadius = std::clamp(denoiseRadius, 1u, 3u);
    }
};

struct GlobalIlluminationStatistics {
    uint32_t giWidth = 0;
    uint32_t giHeight = 0;
    uint32_t fullWidth = 0;
    uint32_t fullHeight = 0;
    uint32_t hiZMipLevels = 0;
    uint32_t rayCount = 0;
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

inline glm::vec3 evalCosineHemisphereSample(float u1, float u2, const glm::vec3& normal) noexcept {
    constexpr float kPi = 3.14159265359f;
    const float c1 = std::clamp(u1, 0.0f, 1.0f);
    const float r = std::sqrt(c1);
    const float phi = 2.0f * kPi * u2;
    const glm::vec3 local(r * std::cos(phi), r * std::sin(phi), std::sqrt(std::max(1.0f - c1, 0.0f)));
    const glm::vec3 N = glm::normalize(normal);
    const glm::vec3 up = (std::abs(N.z) < 0.999f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 T = glm::normalize(glm::cross(up, N));
    const glm::vec3 B = glm::cross(N, T);
    return glm::normalize(T * local.x + B * local.y + N * local.z);
}

// Energy-conserving Lambertian diffuse indirect contribution (zero for pure metals)
inline glm::vec3 evalDiffuseIndirectContribution(const glm::vec3& incomingRadiance,
                                                 const glm::vec3& albedo,
                                                 float metallic,
                                                 float intensity) noexcept {
    constexpr float kInvPi = 1.0f / 3.14159265359f;
    const float diffuseWeight = 1.0f - std::clamp(metallic, 0.0f, 1.0f);
    const glm::vec3 rad = glm::max(incomingRadiance, glm::vec3(0.0f));
    const glm::vec3 alb = glm::clamp(albedo, glm::vec3(0.0f), glm::vec3(1.0f));
    return rad * alb * diffuseWeight * std::max(intensity, 0.0f) * kInvPi;
}

inline bool evalTemporalDepthRejection(float currDepth, float prevDepth, float threshold) noexcept {
    const float relDiff = std::abs(currDepth - prevDepth) / std::max(currDepth, 1e-4f);
    return relDiff > std::max(threshold, 0.0f);
}

inline bool evalTemporalNormalRejection(const glm::vec3& currNormal, const glm::vec3& prevNormal, float threshold) noexcept {
    const float d = glm::dot(glm::normalize(currNormal), glm::normalize(prevNormal));
    return d < std::clamp(threshold, -1.0f, 1.0f);
}

inline uint32_t computeHiZMipLevels(uint32_t width, uint32_t height) noexcept {
    const uint32_t maxDim = std::max(std::max(width, height), 1u);
    return static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(maxDim)))) + 1u;
}

} // namespace sidera
