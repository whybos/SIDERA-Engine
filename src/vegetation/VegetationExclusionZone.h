#pragma once

#include <glm/vec2.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace sidera {

enum class ExclusionShape : uint8_t {
    Circle = 0,
    Box,
    WorldBounds
};

struct VegetationExclusionZone {
    uint32_t id = 0;
    std::string name = "ExclusionZone";
    ExclusionShape shape = ExclusionShape::Circle;

    glm::vec2 center{0.0f, 0.0f};
    float radius = 5.0f;            // Used for Circle
    glm::vec2 minBounds{0.0f, 0.0f}; // Used for Box / WorldBounds
    glm::vec2 maxBounds{0.0f, 0.0f};
    bool enabled = true;

    [[nodiscard]] bool contains(float worldX, float worldZ) const noexcept {
        if (!enabled) return false;
        if (shape == ExclusionShape::Circle) {
            const float dx = worldX - center.x;
            const float dz = worldZ - center.y;
            return (dx * dx + dz * dz) <= (radius * radius);
        }
        return worldX >= minBounds.x && worldX <= maxBounds.x &&
               worldZ >= minBounds.y && worldZ <= maxBounds.y;
    }

    [[nodiscard]] static VegetationExclusionZone makeCircle(uint32_t id, const std::string& name, glm::vec2 center, float radius) {
        VegetationExclusionZone z{};
        z.id = id;
        z.name = name;
        z.shape = ExclusionShape::Circle;
        z.center = center;
        z.radius = radius;
        return z;
    }

    [[nodiscard]] static VegetationExclusionZone makeBox(uint32_t id, const std::string& name, glm::vec2 minB, glm::vec2 maxB) {
        VegetationExclusionZone z{};
        z.id = id;
        z.name = name;
        z.shape = ExclusionShape::Box;
        z.minBounds = minB;
        z.maxBounds = maxB;
        return z;
    }
};

} // namespace sidera
