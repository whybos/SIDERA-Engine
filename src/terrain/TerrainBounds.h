#pragma once

#include "renderer/BoundingVolume.h"
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace sidera {

struct TerrainBounds {
    AABB aabb{};
    glm::vec3 center{0.0f};
    glm::vec2 size{0.0f};       // width (X), length (Z)
    float minHeight = 0.0f;
    float maxHeight = 0.0f;

    [[nodiscard]] constexpr bool containsPoint(float x, float z) const noexcept {
        return x >= aabb.min.x && x <= aabb.max.x &&
               z >= aabb.min.z && z <= aabb.max.z;
    }

    [[nodiscard]] constexpr glm::vec2 getNormalizedCoordinates(float x, float z) const noexcept {
        const float u = (size.x > 0.0001f) ? (x - aabb.min.x) / size.x : 0.0f;
        const float v = (size.y > 0.0001f) ? (z - aabb.min.z) / size.y : 0.0f;
        return glm::vec2(u, v);
    }
};

} // namespace sidera
