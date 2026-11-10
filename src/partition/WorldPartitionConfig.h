#pragma once

#include <glm/vec3.hpp>
#include <algorithm>
#include <cstdint>

namespace sidera {

// Configuration for World Partition spatial indexing, hierarchy, and streaming integration
struct WorldPartitionConfig {
    bool enabled = true;
    bool debugVisualization = false; // Toggleable via hotkey 'G'

    // Spatial Grid Parameters
    float cellSize = 30.0f;          // Width and length in meters per partition cell
    glm::vec3 worldOrigin{0.0f};     // Global reference origin

    // Macro Region Hierarchy (aggregates macroCellFactor x macroCellFactor cells)
    uint32_t macroCellFactor = 2;    // Default: 2x2 cells = 4 cells per macro region

    // Distance Hysteresis for Cell Activation / Streaming Integration
    float loadRadius = 65.0f;
    float unloadRadius = 115.0f;
    float hysteresisMargin = 25.0f;

    // Viewport / Editor Camera Policy
    bool enableEditorCameraStreaming = true;
    float editorCameraPriorityMultiplier = 0.5f;

    // Memory budget & limits
    uint32_t maxActiveCells = 64;
    uint64_t cellMemoryBudgetBytes = 256 * 1024 * 1024; // 256 MB

    void validate() noexcept {
        cellSize = std::max(5.0f, cellSize);
        macroCellFactor = std::max(1u, macroCellFactor);
        if (unloadRadius <= loadRadius + hysteresisMargin) {
            unloadRadius = loadRadius + std::max(10.0f, hysteresisMargin);
        }
        maxActiveCells = std::max(1u, maxActiveCells);
    }
};

} // namespace sidera
