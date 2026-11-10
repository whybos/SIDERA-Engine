#pragma once

#include "renderer/BoundingVolume.h"

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cmath>

namespace sidera {

// Plane in standard form: A*x + B*y + C*z + D = 0, where (A, B, C) is a normalized normal vector.
struct FrustumPlane {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;

    [[nodiscard]] constexpr float signedDistance(const glm::vec3& point) const noexcept {
        return glm::dot(normal, point) + distance;
    }

    [[nodiscard]] glm::vec4 toVec4() const noexcept {
        return glm::vec4(normal, distance);
    }
};

// Camera view frustum containing the 6 bounding planes (Left, Right, Bottom, Top, Near, Far)
struct Frustum {
    enum PlaneIndex : size_t {
        Left = 0,
        Right = 1,
        Bottom = 2,
        Top = 3,
        Near = 4,
        Far = 5
    };

    std::array<FrustumPlane, 6> planes{};

    // Extracts frustum planes from a view-projection matrix (Gribb-Hartmann algorithm)
    // configured for Vulkan's depth range [0, 1].
    static Frustum fromViewProj(const glm::mat4& viewProj) noexcept { return fromViewProjection(viewProj); }
    static Frustum fromViewProjection(const glm::mat4& viewProj) noexcept {
        Frustum f;

        // In GLM, m[col][row]. Extract matrix rows:
        const glm::vec4 r0 = glm::vec4(viewProj[0][0], viewProj[1][0], viewProj[2][0], viewProj[3][0]);
        const glm::vec4 r1 = glm::vec4(viewProj[0][1], viewProj[1][1], viewProj[2][1], viewProj[3][1]);
        const glm::vec4 r2 = glm::vec4(viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]);
        const glm::vec4 r3 = glm::vec4(viewProj[0][3], viewProj[1][3], viewProj[2][3], viewProj[3][3]);

        const auto normalizePlane = [](const glm::vec4& p) -> FrustumPlane {
            const glm::vec3 n(p.x, p.y, p.z);
            const float len = glm::length(n);
            if (len > 0.00001f) {
                return FrustumPlane{
                    .normal = n / len,
                    .distance = p.w / len
                };
            }
            return FrustumPlane{};
        };

        // Vulkan clip space: -w <= x <= w, -w <= y <= w, 0 <= z <= w
        f.planes[Left]   = normalizePlane(r3 + r0);
        f.planes[Right]  = normalizePlane(r3 - r0);
        f.planes[Bottom] = normalizePlane(r3 + r1);
        f.planes[Top]    = normalizePlane(r3 - r1);
        f.planes[Near]   = normalizePlane(r2);       // Vulkan near plane is at z_clip = 0
        f.planes[Far]    = normalizePlane(r3 - r2);  // Vulkan far plane is at z_clip = w

        return f;
    }

    // Conservative sphere test: returns false if sphere is completely outside any plane.
    [[nodiscard]] bool intersectsSphere(const glm::vec3& center, float radius) const noexcept {
        for (const auto& plane : planes) {
            if (plane.signedDistance(center) < -radius) {
                return false;
            }
        }
        return true;
    }

    // Conservative AABB test: returns false if AABB is completely outside any plane.
    [[nodiscard]] bool intersectsAABB(const glm::vec3& min, const glm::vec3& max) const noexcept {
        for (const auto& plane : planes) {
            // Find positive vertex (vertex furthest in direction of plane normal)
            const glm::vec3 pVertex{
                (plane.normal.x >= 0.0f) ? max.x : min.x,
                (plane.normal.y >= 0.0f) ? max.y : min.y,
                (plane.normal.z >= 0.0f) ? max.z : min.z
            };

            if (plane.signedDistance(pVertex) < 0.0f) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool intersects(const BoundingSphere& sphere, const AABB& aabb) const noexcept {
        // Fast sphere rejection first, then tighter AABB rejection
        if (!intersectsSphere(sphere.center, sphere.radius)) {
            return false;
        }
        return intersectsAABB(aabb.min, aabb.max);
    }
};

} // namespace sidera
