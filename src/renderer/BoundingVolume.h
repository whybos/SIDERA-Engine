#pragma once

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sidera {

// Axis-Aligned Bounding Box (AABB)
struct AABB {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }

    [[nodiscard]] constexpr glm::vec3 getCenter() const noexcept {
        return (min + max) * 0.5f;
    }

    [[nodiscard]] constexpr glm::vec3 getExtents() const noexcept {
        return (max - min) * 0.5f;
    }

    void expand(const glm::vec3& point) noexcept {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    // Transforms the AABB by an arbitrary 4x4 affine model matrix (translation, rotation, non-uniform scale)
    [[nodiscard]] AABB transform(const glm::mat4& m) const noexcept {
        const glm::vec3 center = getCenter();
        const glm::vec3 extents = getExtents();

        const glm::vec3 newCenter = glm::vec3(m * glm::vec4(center, 1.0f));

        // Exact transformed half-extents for non-uniform scale + rotation
        const glm::vec3 newExtents{
            std::abs(m[0][0]) * extents.x + std::abs(m[1][0]) * extents.y + std::abs(m[2][0]) * extents.z,
            std::abs(m[0][1]) * extents.x + std::abs(m[1][1]) * extents.y + std::abs(m[2][1]) * extents.z,
            std::abs(m[0][2]) * extents.x + std::abs(m[1][2]) * extents.y + std::abs(m[2][2]) * extents.z
        };

        return AABB{
            .min = newCenter - newExtents,
            .max = newCenter + newExtents
        };
    }
};

// Bounding Sphere
struct BoundingSphere {
    glm::vec3 center{0.0f};
    float radius = 0.0f;

    // Transforms the bounding sphere by a 4x4 matrix, accounting for maximum scale
    [[nodiscard]] BoundingSphere transform(const glm::mat4& m) const noexcept {
        const glm::vec3 newCenter = glm::vec3(m * glm::vec4(center, 1.0f));

        // Extract scale from each matrix column
        const float scaleX = glm::length(glm::vec3(m[0]));
        const float scaleY = glm::length(glm::vec3(m[1]));
        const float scaleZ = glm::length(glm::vec3(m[2]));
        const float maxScale = std::max({scaleX, scaleY, scaleZ});

        return BoundingSphere{
            .center = newCenter,
            .radius = radius * maxScale
        };
    }
};

} // namespace sidera
