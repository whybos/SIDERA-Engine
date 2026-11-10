#pragma once

#include "renderer/BoundingVolume.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <cstdint>

namespace sidera {

// Concrete instance placed on the terrain surface
struct FoliageInstance {
    uint32_t instanceId = 0;                    // Unique identifier
    uint32_t typeId = 0;                        // ID of originating FoliageType
    uint32_t randomSeed = 0;                    // Deterministic seed used for placement & phase

    // Positional and transform state
    glm::vec3 position{0.0f};                   // World space coordinate (placed directly on terrain)
    glm::vec3 rotation{0.0f};                   // Euler angles (yaw, pitch, roll in radians)
    glm::vec3 scale{1.0f};                      // 3D scale factors
    glm::mat4 transform{1.0f};                  // Precomputed 4x4 affine model matrix

    // Terrain sampling context
    float terrainHeight = 0.0f;                 // Elevation sampled from terrain at (X, Z)
    glm::vec3 surfaceNormal{0.0f, 1.0f, 0.0f};  // Terrain surface normal vector

    // Precomputed world bounding volumes for culling & screen-space LOD
    AABB worldBounds{};
    BoundingSphere worldSphere{};

    // Active detail and visibility state
    uint32_t currentLod = 0;                    // Active LOD tier (0..3)
    bool isVisible = true;                      // Frustum & distance visibility flag
};

} // namespace sidera
