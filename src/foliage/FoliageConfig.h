#pragma once

#include <glm/vec2.hpp>
#include <cstdint>

namespace sidera {

// Wind parameters for procedural vertex shader swaying and fluttering
struct WindSettings {
    glm::vec2 direction{1.0f, 0.35f};   // Horizontal wind vector in XZ world plane (normalized in shader)
    float strength = 0.45f;             // Master displacement amplitude (meters at unit height)
    float speed = 1.25f;                // Temporal wave propagation speed (radians/sec)
    float frequency = 0.85f;            // Spatial wavelength factor (radians/meter)
    float turbulence = 0.35f;           // High-frequency secondary flutter intensity
};

// Global foliage system configuration
struct FoliageConfiguration {
    bool enabled = true;                // Master foliage toggle (F2/hotkey)
    bool windEnabled = true;            // Master wind animation toggle
    WindSettings wind{};                // Active wind parameters
    bool shadowsEnabled = true;         // Foliage shadow casting in CSM passes
    float maxDrawDistance = 160.0f;     // Foliage culling distance cutoff (meters)
    float grassDensity = 1.0f;          // Density scaling for grass instances
    float treeDensity = 1.0f;           // Density scaling for tree instances
    float bushDensity = 1.0f;           // Density scaling for bush instances
};

} // namespace sidera
