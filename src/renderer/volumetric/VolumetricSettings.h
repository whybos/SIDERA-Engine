#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sidera {

enum class VolumetricQualityPreset : uint32_t {
    Low = 0,
    Medium = 1,
    High = 2,
    Ultra = 3
};

enum class VolumetricDebugMode : uint32_t {
    Final = 0,
    DensityExtinction = 1,
    IntegratedScattering = 2,
    Transmittance = 3,
    TemporalHistory = 4
};

// std430-aligned local volumetric fog volume (64 bytes)
struct alignas(16) LocalFogVolume {
    glm::vec4 positionType{0.0f, 0.0f, 0.0f, 0.0f};   // xyz = center, w = 0 Sphere / 1 AABB
    glm::vec4 extentsDensity{10.0f, 10.0f, 10.0f, 0.05f}; // xyz = halfExtents or radius in x, w = densityMultiplier
    glm::vec4 colorFalloff{0.85f, 0.90f, 1.0f, 1.5f}; // rgb = scatteringColor, a = edgeFalloff
    glm::vec4 params{1.0f, 0.0f, 0.0f, 0.0f};         // x = enabled, y = blendMode
};
static_assert(sizeof(LocalFogVolume) == 64, "LocalFogVolume must be 64 bytes (std430).");

struct VolumetricSettings {
    bool enabled = true;
    VolumetricQualityPreset qualityPreset = VolumetricQualityPreset::Medium;

    // Froxel grid configuration
    uint32_t froxelXYScale = 16;      // screen pixels per froxel (8 or 16)
    uint32_t froxelDepthSlices = 64;  // logarithmic depth slices (16..128)
    float maxDistance = 160.0f;       // maximum volumetric integration distance

    // Atmospheric & height fog
    float fogDensity = 0.015f;
    float fogHeight = 12.0f;
    float heightFalloff = 0.08f;
    float maxDensity = 0.25f;
    glm::vec3 fogColor{0.62f, 0.74f, 0.90f};
    glm::vec3 scatteringColor{1.0f, 0.96f, 0.88f};

    // Participating media coefficients
    float scatteringCoefficient = 0.85f;
    float extinctionCoefficient = 1.0f;
    float scatteringIntensity = 1.0f;
    float anisotropy = 0.65f; // Henyey-Greenstein g in [-0.95, 0.95]

    // Temporal reprojection
    bool temporalEnabled = true;
    float temporalBlend = 0.85f; // [0.0, 0.98]
    float cameraCutPositionThreshold = 15.0f;

    // Lighting & feature toggles
    bool enableDirectionalScattering = true;
    bool enablePointScattering = true;
    bool enableSpotScattering = true;
    bool enableVolumetricShadows = true;
    bool enableLightShafts = true;
    bool enableLocalVolumes = true;
    float shadowBias = 0.002f;

    VolumetricDebugMode debugMode = VolumetricDebugMode::Final;

    void applyQualityPreset(VolumetricQualityPreset preset) noexcept {
        qualityPreset = preset;
        switch (preset) {
            case VolumetricQualityPreset::Low:
                froxelXYScale = 16;
                froxelDepthSlices = 32;
                break;
            case VolumetricQualityPreset::Medium:
                froxelXYScale = 16;
                froxelDepthSlices = 64;
                break;
            case VolumetricQualityPreset::High:
                froxelXYScale = 8;
                froxelDepthSlices = 64;
                break;
            case VolumetricQualityPreset::Ultra:
                froxelXYScale = 8;
                froxelDepthSlices = 128;
                break;
        }
        validate();
    }

    void validate() noexcept {
        froxelXYScale = std::clamp(froxelXYScale, 4u, 32u);
        froxelDepthSlices = std::clamp(froxelDepthSlices, 16u, 128u);
        maxDistance = std::clamp(maxDistance, 10.0f, 2000.0f);
        fogDensity = std::clamp(fogDensity, 0.0f, 2.0f);
        heightFalloff = std::clamp(heightFalloff, 0.0f, 10.0f);
        maxDensity = std::clamp(maxDensity, 0.001f, 5.0f);
        scatteringCoefficient = std::clamp(scatteringCoefficient, 0.0f, 5.0f);
        extinctionCoefficient = std::clamp(extinctionCoefficient, 0.001f, 5.0f);
        scatteringIntensity = std::clamp(scatteringIntensity, 0.0f, 10.0f);
        anisotropy = std::clamp(anisotropy, -0.95f, 0.95f);
        temporalBlend = std::clamp(temporalBlend, 0.0f, 0.98f);
        cameraCutPositionThreshold = std::clamp(cameraCutPositionThreshold, 1.0f, 500.0f);
        shadowBias = std::clamp(shadowBias, 0.0f, 0.05f);
    }

    [[nodiscard]] uint32_t packFeatureFlags() const noexcept {
        uint32_t f = 0u;
        if (enableDirectionalScattering) f |= 1u;
        if (enablePointScattering)       f |= 2u;
        if (enableSpotScattering)        f |= 4u;
        if (enableVolumetricShadows && enableLightShafts) f |= 8u;
        if (enableLocalVolumes)          f |= 16u;
        return f;
    }
};

struct VolumetricStatistics {
    uint32_t froxelX = 0;
    uint32_t froxelY = 0;
    uint32_t froxelZ = 0;
    uint32_t totalFroxels = 0;
    uint64_t gpuMemoryBytes = 0;
    uint32_t activeLights = 0;
    uint32_t activeLocalVolumes = 0;
    uint32_t historyResetCount = 0;
    bool historyValid = false;
    bool active = false;
    float injectMs = 0.0f;
    float temporalMs = 0.0f;
    float integrateMs = 0.0f;
    float compositeMs = 0.0f;
};

// --- CPU Reference Mathematical Functions ---

inline float evalBeerLambertTransmittance(float extinction, float distance) noexcept {
    if (extinction <= 0.0f || distance <= 0.0f) return 1.0f;
    return std::exp(-extinction * distance);
}

inline float evalHenyeyGreenstein(float cosTheta, float g) noexcept {
    constexpr float kPi = 3.14159265359f;
    const float gc = std::clamp(g, -0.95f, 0.95f);
    const float g2 = gc * gc;
    const float denom = std::max(1.0f + g2 - 2.0f * gc * cosTheta, 1e-4f);
    return (1.0f - g2) / (4.0f * kPi * denom * std::sqrt(denom));
}

inline float evalHeightFogDensity(float worldY, float baseDensity, float fogHeight,
                                  float heightFalloff, float maxDensity) noexcept {
    const float h = std::max(worldY - fogHeight, 0.0f);
    const float d = std::max(baseDensity, 0.0f) * std::exp(-std::max(heightFalloff, 0.0f) * h);
    return std::clamp(d, 0.0f, std::max(maxDensity, 0.0f));
}

inline float evalFroxelSliceDepth(uint32_t slice, uint32_t sliceCount,
                                  float nearPlane, float farPlane) noexcept {
    const float n = std::max(nearPlane, 0.01f);
    const float f = std::max(farPlane, n + 1.0f);
    const float t = static_cast<float>(std::min(slice, sliceCount)) / static_cast<float>(std::max(sliceCount, 1u));
    return n * std::pow(f / n, t);
}

inline void evalFroxelIntegrationStep(glm::vec3& accumScattering, float& accumTransmittance,
                                      const glm::vec3& inScattering, float extinction,
                                      float stepLength) noexcept {
    const float d = std::max(stepLength, 0.0f);
    const float ext = std::max(extinction, 0.0f);
    const float sliceT = evalBeerLambertTransmittance(ext, d);
    const glm::vec3 sliceS = (ext > 1e-5f)
        ? inScattering * ((1.0f - sliceT) / ext)
        : inScattering * d;
    accumScattering += accumTransmittance * sliceS;
    accumTransmittance *= sliceT;
}

inline void computeFroxelDimensions(uint32_t screenWidth, uint32_t screenHeight,
                                    uint32_t xyScale, uint32_t depthSlices,
                                    uint32_t maxDim3D,
                                    uint32_t& outX, uint32_t& outY, uint32_t& outZ) noexcept {
    const uint32_t s = std::max(xyScale, 1u);
    const uint32_t limit = std::max(maxDim3D, 16u);
    outX = std::clamp((std::max(screenWidth, 1u) + s - 1u) / s, 1u, limit);
    outY = std::clamp((std::max(screenHeight, 1u) + s - 1u) / s, 1u, limit);
    outZ = std::clamp(depthSlices, 16u, limit);
}

} // namespace sidera
