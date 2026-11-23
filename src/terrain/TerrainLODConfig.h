#pragma once

#include <cstdint>
#include <string_view>

namespace sidera {

enum class TerrainLODStrategy : uint8_t {
    ScreenSpaceError = 0, // Default: project geometric error to screen-space pixels
    DistanceBased = 1     // Alternative: camera distance thresholds per LOD level
};

enum class TerrainQualityPreset : uint8_t {
    Low = 0,
    Medium = 1,
    High = 2,
    Ultra = 3
};

[[nodiscard]] constexpr std::string_view getTerrainQualityPresetName(TerrainQualityPreset preset) noexcept {
    switch (preset) {
        case TerrainQualityPreset::Low:    return "Low";
        case TerrainQualityPreset::Medium: return "Medium";
        case TerrainQualityPreset::High:   return "High";
        case TerrainQualityPreset::Ultra:  return "Ultra";
    }
    return "Custom";
}

[[nodiscard]] constexpr std::string_view getTerrainLODStrategyName(TerrainLODStrategy strategy) noexcept {
    switch (strategy) {
        case TerrainLODStrategy::ScreenSpaceError: return "Screen-Space Error";
        case TerrainLODStrategy::DistanceBased:    return "Distance-Based";
    }
    return "Unknown";
}

struct TerrainLODConfig {
    bool enabled = true;
    TerrainLODStrategy strategy = TerrainLODStrategy::ScreenSpaceError;

    // Quadtree depth bounds (depth 0 = root covering entire terrain, maxDepth = finest detail)
    uint32_t minDepth = 1;
    uint32_t maxDepth = 4;

    // Quads per patch edge (must be even and >= 4; e.g. 16 -> 17x17 verts, 32 -> 33x33 verts)
    uint32_t patchResolution = 16;

    // Screen-space geometric error thresholds (in pixels)
    float pixelErrorThreshold = 2.0f;
    float maxScreenError = 64.0f;

    // Hysteresis factor to prevent split/merge oscillation (merge threshold = splitThreshold * hysteresis)
    float lodHysteresis = 0.75f;

    // Distance-based fallback multiplier (split when dist < patchSize * distanceSplitFactor)
    float distanceSplitFactor = 2.2f;

    // Geomorphing transition parameters
    bool geomorphingEnabled = true;
    float morphDuration = 0.25f; // seconds for smooth 0..1 transition

    // Neighbor balancing & stitching
    bool balanceQuadtree = true;
    bool stitchingEnabled = true;

    // Resource & triangle budgets
    uint32_t maxActivePatches = 512;
    uint64_t maxVisibleTriangles = 2000000;

    // Shadow pass LOD bias (positive = coarser LOD for shadows, e.g. 1.5x error threshold)
    float shadowLodBias = 1.5f;

    // Debug visualization
    bool debugVisualization = false;
    uint32_t debugOverlayMode = 0; // 0 = off, 1 = LOD colors, 2 = morph factor, 3 = stitch edges

    static TerrainLODConfig fromPreset(TerrainQualityPreset preset) noexcept {
        TerrainLODConfig cfg{};
        switch (preset) {
            case TerrainQualityPreset::Low:
                cfg.minDepth = 1;
                cfg.maxDepth = 3;
                cfg.patchResolution = 16;
                cfg.pixelErrorThreshold = 4.0f;
                cfg.maxActivePatches = 128;
                cfg.maxVisibleTriangles = 250000;
                cfg.shadowLodBias = 2.0f;
                break;
            case TerrainQualityPreset::Medium:
                cfg.minDepth = 1;
                cfg.maxDepth = 4;
                cfg.patchResolution = 16;
                cfg.pixelErrorThreshold = 2.5f;
                cfg.maxActivePatches = 256;
                cfg.maxVisibleTriangles = 750000;
                cfg.shadowLodBias = 1.75f;
                break;
            case TerrainQualityPreset::High:
                cfg.minDepth = 1;
                cfg.maxDepth = 4;
                cfg.patchResolution = 16;
                cfg.pixelErrorThreshold = 2.0f;
                cfg.maxActivePatches = 512;
                cfg.maxVisibleTriangles = 2000000;
                cfg.shadowLodBias = 1.5f;
                break;
            case TerrainQualityPreset::Ultra:
                cfg.minDepth = 2;
                cfg.maxDepth = 5;
                cfg.patchResolution = 32;
                cfg.pixelErrorThreshold = 1.25f;
                cfg.maxActivePatches = 1024;
                cfg.maxVisibleTriangles = 4000000;
                cfg.shadowLodBias = 1.25f;
                break;
        }
        return cfg;
    }

    void validate() noexcept {
        if (minDepth > maxDepth) {
            minDepth = maxDepth;
        }
        if (maxDepth > 7) {
            maxDepth = 7;
        }
        if (patchResolution < 4) {
            patchResolution = 4;
        }
        if ((patchResolution & 1u) != 0u) {
            patchResolution += 1u; // Must be even for exact parent-child morphing and stitching
        }
        if (pixelErrorThreshold < 0.1f) {
            pixelErrorThreshold = 0.1f;
        }
        if (lodHysteresis < 0.1f) {
            lodHysteresis = 0.1f;
        } else if (lodHysteresis > 0.98f) {
            lodHysteresis = 0.98f;
        }
        if (morphDuration < 0.0f) {
            morphDuration = 0.0f;
        }
        if (shadowLodBias < 0.5f) {
            shadowLodBias = 0.5f;
        }
    }
};

} // namespace sidera
