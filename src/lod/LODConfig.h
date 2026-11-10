#pragma once

#include <cstdint>
#include <string_view>

namespace sidera {

// Selection metric for automatic LOD determination
enum class LODSelectionMode : uint32_t {
    Distance = 0,       // Based on Euclidean camera-to-object distance
    ScreenSpace = 1     // Based on projected screen pixel coverage
};

[[nodiscard]] constexpr std::string_view getLODSelectionModeName(LODSelectionMode mode) noexcept {
    switch (mode) {
        case LODSelectionMode::Distance:    return "DISTANCE";
        case LODSelectionMode::ScreenSpace: return "SCREEN-SPACE";
    }
    return "UNKNOWN";
}

// Forced LOD override for debugging and quality comparisons
enum class ForcedLODMode : int32_t {
    Auto = -1,          // Automatic dynamic selection
    ForceLOD0 = 0,      // Force highest detail (LOD 0)
    ForceLOD1 = 1,      // Force medium detail (LOD 1)
    ForceLOD2 = 2,      // Force low detail (LOD 2)
    ForceLOD3 = 3       // Force ultra-low detail (LOD 3)
};

[[nodiscard]] constexpr std::string_view getForcedLODModeName(ForcedLODMode mode) noexcept {
    switch (mode) {
        case ForcedLODMode::Auto:      return "AUTO";
        case ForcedLODMode::ForceLOD0: return "FORCE LOD 0";
        case ForcedLODMode::ForceLOD1: return "FORCE LOD 1";
        case ForcedLODMode::ForceLOD2: return "FORCE LOD 2";
        case ForcedLODMode::ForceLOD3: return "FORCE LOD 3";
    }
    return "UNKNOWN";
}

// Global and per-group LOD configuration parameters
struct LODConfig {
    LODSelectionMode selectionMode = LODSelectionMode::Distance;
    ForcedLODMode forcedLOD = ForcedLODMode::Auto;
    float distanceHysteresis = 2.5f;     // Distance deadband margin (units) to eliminate transition popping
    float screenHysteresis = 8.0f;       // Screen pixel deadband margin (px) to eliminate transition popping
    int32_t shadowLodBias = 1;           // LOD offset applied during shadow map rendering (+1 = 1 tier lower)
    bool enabled = true;                 // Master LOD toggle (false = always use LOD 0)
};

} // namespace sidera
