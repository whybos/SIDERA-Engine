#pragma once

#include <cstdint>
#include <string>

namespace sidera {

class Mesh;

// A single level of detail within a LODGroup
struct LODLevel {
    const Mesh* mesh = nullptr;          // Pointer to GPU mesh geometry for this level
    float maxDistance = 0.0f;            // Maximum camera distance threshold (distance-based mode)
    float minScreenSize = 0.0f;          // Minimum projected pixel diameter threshold (screen-space mode)
    float qualityReduction = 1.0f;       // Approximate geometry fraction relative to base mesh (1.0 = 100%, 0.5 = 50%, etc.)
    uint32_t triangleCount = 0;          // Cached triangle count (indexCount / 3)
    uint32_t vertexCount = 0;            // Cached vertex count
};

} // namespace sidera
